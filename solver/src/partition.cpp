#include "partition.hpp"
#include <metis.h>
#include <map>
#include <set>
#include <unordered_map>

namespace fv {

namespace {

// Simple byte-buffer serializer for rank-local mesh distribution.
struct Buffer {
  vector<char> data;
  size_t pos = 0;
  template <typename T>
  void put(const T& v) {
    const char* p = reinterpret_cast<const char*>(&v);
    data.insert(data.end(), p, p + sizeof(T));
  }
  void putBytes(const void* p, size_t n) {
    const char* c = reinterpret_cast<const char*>(p);
    data.insert(data.end(), c, c + n);
  }
  void putString(const string& s) {
    long n = (long)s.size();
    put(n);
    putBytes(s.data(), (size_t)n);
  }
  template <typename T>
  void putVec(const vector<T>& v) {
    long n = (long)v.size();
    put(n);
    if (n) putBytes(v.data(), sizeof(T) * (size_t)n);
  }
  template <typename T>
  T get() {
    T v;
    std::memcpy(&v, data.data() + pos, sizeof(T));
    pos += sizeof(T);
    return v;
  }
  void getBytes(void* p, size_t n) {
    std::memcpy(p, data.data() + pos, n);
    pos += n;
  }
  string getString() {
    long n = get<long>();
    string s(data.data() + pos, (size_t)n);
    pos += (size_t)n;
    return s;
  }
  template <typename T>
  vector<T> getVec() {
    long n = get<long>();
    vector<T> v((size_t)n);
    if (n) getBytes(v.data(), sizeof(T) * (size_t)n);
    return v;
  }
};

}  // namespace

LocalMesh partitionMesh(const GlobalMesh& gm,
                        const vector<std::pair<string, BCType>>& bcMap,
                        MPI_Comm comm) {
  int rank, nranks;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nranks);
  Buffer buf;
  if (rank == 0) {
    const int nc = gm.numCells();
    // ---- cell adjacency graph (CSR) from interior faces
    vector<int> xadj(nc + 1, 0);
    for (size_t f = 0; f < gm.face_c0.size(); ++f)
      if (gm.face_c1[f] >= 0) { xadj[gm.face_c0[f] + 1]++; xadj[gm.face_c1[f] + 1]++; }
    for (int c = 0; c < nc; ++c) xadj[c + 1] += xadj[c];
    vector<int> adjncy(xadj[nc]);
    {
      vector<int> pos(xadj.begin(), xadj.end() - 1);
      for (size_t f = 0; f < gm.face_c0.size(); ++f)
        if (gm.face_c1[f] >= 0) {
          adjncy[pos[gm.face_c0[f]]++] = gm.face_c1[f];
          adjncy[pos[gm.face_c1[f]]++] = gm.face_c0[f];
        }
    }
    // ---- METIS k-way partitioning
    vector<idx_t> part(nc, 0);
    idx_t edgecut = 0;
    if (nranks > 1) {
      idx_t nvtxs = nc, ncon = 1, nparts = nranks;
      vector<idx_t> mxadj(xadj.begin(), xadj.end()), madj(adjncy.begin(), adjncy.end());
      idx_t options[METIS_NOPTIONS];
      METIS_SetDefaultOptions(options);
      options[METIS_OPTION_SEED] = 7;
      int rc = METIS_PartGraphKway(&nvtxs, &ncon, mxadj.data(), madj.data(), nullptr, nullptr,
                                   nullptr, &nparts, nullptr, nullptr, options, &edgecut, part.data());
      check(rc == METIS_OK, "METIS_PartGraphKway failed");
    }
    // ---- per-rank owned/ghost sets
    vector<vector<int>> owned(nranks);
    for (int c = 0; c < nc; ++c) owned[part[c]].push_back(c);
    vector<vector<int>> ghosts(nranks);
    {
      vector<vector<char>> isGhost(nranks);
      for (int r = 0; r < nranks; ++r) isGhost[r].assign(nc, 0);
      for (size_t f = 0; f < gm.face_c0.size(); ++f) {
        int c0 = gm.face_c0[f], c1 = gm.face_c1[f];
        if (c1 < 0) continue;
        int p0 = part[c0], p1 = part[c1];
        if (p0 == p1) continue;
        if (!isGhost[p0][c1]) { isGhost[p0][c1] = 1; ghosts[p0].push_back(c1); }
        if (!isGhost[p1][c0]) { isGhost[p1][c0] = 1; ghosts[p1].push_back(c0); }
      }
    }
    // sort ghosts by global id for deterministic halo ordering
    for (int r = 0; r < nranks; ++r) std::sort(ghosts[r].begin(), ghosts[r].end());
    // family -> bc type lookup
    std::map<string, BCType> famBc;
    for (auto& kv : bcMap) famBc[kv.first] = kv.second;
    for (auto& fam : gm.families)
      check(famBc.count(fam), "mesh boundary family has no mapping in boundary_conditions: " + fam);
    // ---- halo send/recv lists (global ids, ascending)
    vector<std::map<int, vector<int>>> recvList(nranks), sendList(nranks);
    for (int r = 0; r < nranks; ++r)
      for (int gcell : ghosts[r]) recvList[r][part[gcell]].push_back(gcell);
    for (int r = 0; r < nranks; ++r)
      for (auto& kv : recvList[r])
        for (int gcell : kv.second) sendList[kv.first][r].push_back(gcell);
    for (int r = 0; r < nranks; ++r)
      for (auto& kv : sendList[r]) std::sort(kv.second.begin(), kv.second.end());
    long totalFaces = (long)gm.face_c0.size();
    for (int r = 0; r < nranks; ++r) {
      Buffer b;
      // local cell indexing: owned first (in global id order), then ghosts
      std::unordered_map<int, int> loc;
      vector<int> localCells;
      for (int c : owned[r]) { loc[c] = (int)localCells.size(); localCells.push_back(c); }
      int nOwn = (int)localCells.size();
      for (int c : ghosts[r]) { loc[c] = (int)localCells.size(); localCells.push_back(c); }
      int nAll = (int)localCells.size();
      // local nodes
      std::unordered_map<int, int> nloc;
      vector<int> localNodes;
      for (int c : localCells)
        for (int k = 0; k < gm.cell_nnodes[c]; ++k) {
          int n = gm.cell_nodes[c][k];
          if (!nloc.count(n)) { nloc[n] = (int)localNodes.size(); localNodes.push_back(n); }
        }
      // local faces: every global face touching an owned cell
      struct LF { int c0, c1, n0, n1, bc, fam; long gid; };
      vector<LF> lfaces;
      for (size_t f = 0; f < gm.face_c0.size(); ++f) {
        int c0 = gm.face_c0[f], c1 = gm.face_c1[f];
        bool c0own = part[c0] == r;
        bool c1own = (c1 >= 0 && part[c1] == r);
        if (c1 >= 0) {
          if (!c0own && !c1own) continue;
          LF lf;
          lf.c0 = loc[c0];
          lf.c1 = loc[c1];
          lf.n0 = nloc[gm.face_nodes[f][0]];
          lf.n1 = nloc[gm.face_nodes[f][1]];
          lf.bc = 0;
          lf.fam = -1;
          lf.gid = (long)f;
          lfaces.push_back(lf);
        } else {
          if (!c0own) continue;
          LF lf;
          lf.c0 = loc[c0];
          lf.c1 = -1;
          lf.n0 = nloc[gm.face_nodes[f][0]];
          lf.n1 = nloc[gm.face_nodes[f][1]];
          lf.fam = gm.face_bc_family[f];
          lf.bc = (int)famBc[gm.families[lf.fam]];
          lf.gid = (long)f;
          lfaces.push_back(lf);
        }
      }
      std::set<int> nbrRanks;
      for (auto& kv : recvList[r]) nbrRanks.insert(kv.first);
      for (auto& kv : sendList[r]) nbrRanks.insert(kv.first);
      // ---- serialize
      b.put<long>(nc);
      b.put<long>(totalFaces);
      b.put<long>((long)gm.numNodes());
      b.put<long>((long)edgecut);
      b.put<int>(nOwn);
      b.put<int>(nAll - nOwn);
      b.put<int>((int)localNodes.size());
      b.put<int>((int)lfaces.size());
      b.put<long>((long)gm.families.size());
      for (auto& fam : gm.families) b.putString(fam);
      {
        vector<long> gcid(nAll);
        vector<int> owner(nAll);
        vector<int> cnn(nAll);
        vector<int> cnodes((size_t)nAll * 4);
        for (int i = 0; i < nAll; ++i) {
          int c = localCells[i];
          gcid[i] = c;
          owner[i] = part[c];
          cnn[i] = gm.cell_nnodes[c];
          for (int k = 0; k < 4; ++k)
            cnodes[(size_t)i * 4 + k] = (k < cnn[i]) ? nloc[gm.cell_nodes[c][k]] : -1;
        }
        b.putVec(gcid);
        b.putVec(owner);
        b.putVec(cnn);
        b.putVec(cnodes);
      }
      {
        vector<double> nxv(localNodes.size()), nyv(localNodes.size());
        vector<long> gnid(localNodes.size());
        for (size_t i = 0; i < localNodes.size(); ++i) {
          nxv[i] = gm.x[localNodes[i]];
          nyv[i] = gm.y[localNodes[i]];
          gnid[i] = localNodes[i];
        }
        b.putVec(nxv);
        b.putVec(nyv);
        b.putVec(gnid);
      }
      {
        int nf = (int)lfaces.size();
        vector<int> c0(nf), c1(nf), n0(nf), n1(nf);
        vector<int> bc(nf), fam(nf);
        vector<long> gid(nf);
        for (int i = 0; i < nf; ++i) {
          c0[i] = lfaces[i].c0; c1[i] = lfaces[i].c1;
          n0[i] = lfaces[i].n0; n1[i] = lfaces[i].n1;
          bc[i] = lfaces[i].bc; fam[i] = lfaces[i].fam; gid[i] = lfaces[i].gid;
        }
        b.putVec(c0); b.putVec(c1); b.putVec(n0); b.putVec(n1);
        b.putVec(bc); b.putVec(fam); b.putVec(gid);
      }
      b.put<int>((int)nbrRanks.size());
      for (int q : nbrRanks) {
        b.put<int>(q);
        vector<int> sendIdx, recvIdx;
        if (sendList[r].count(q))
          for (int gcell : sendList[r][q]) sendIdx.push_back(loc[gcell]);
        if (recvList[r].count(q))
          for (int gcell : recvList[r][q]) recvIdx.push_back(loc[gcell]);
        b.putVec(sendIdx);
        b.putVec(recvIdx);
      }
      if (r == 0) {
        buf = std::move(b);
      } else {
        long sz = (long)b.data.size();
        MPI_Send(&sz, 1, MPI_LONG, r, 100, comm);
        MPI_Send(b.data.data(), (int)b.data.size(), MPI_BYTE, r, 101, comm);
      }
    }
  } else {
    long sz;
    MPI_Recv(&sz, 1, MPI_LONG, 0, 100, comm, MPI_STATUS_IGNORE);
    buf.data.resize((size_t)sz);
    MPI_Recv(buf.data.data(), (int)sz, MPI_BYTE, 0, 101, comm, MPI_STATUS_IGNORE);
  }
  // ---- deserialize
  LocalMesh m;
  m.nCellsGlobal = buf.get<long>();
  m.nFacesGlobal = buf.get<long>();
  m.nNodesGlobal = buf.get<long>();
  m.partitionEdgeCut = buf.get<long>();
  m.nOwn = buf.get<int>();
  m.nGhost = buf.get<int>();
  m.nAll = m.nOwn + m.nGhost;
  m.nNodes = buf.get<int>();
  int nFaces = buf.get<int>();
  {
    long nfam = buf.get<long>();
    for (long i = 0; i < nfam; ++i) m.familyNames.push_back(buf.getString());
  }
  m.globalCellId = buf.getVec<long>();
  m.cellOwner = buf.getVec<int>();
  m.cellNNodes = buf.getVec<int>();
  {
    vector<int> cn = buf.getVec<int>();
    m.cellNodes.resize(m.nAll);
    for (int i = 0; i < m.nAll; ++i)
      for (int k = 0; k < 4; ++k) m.cellNodes[i][k] = cn[(size_t)i * 4 + k];
  }
  m.nodeX = buf.getVec<double>();
  m.nodeY = buf.getVec<double>();
  m.globalNodeId = buf.getVec<long>();
  {
    vector<int> c0 = buf.getVec<int>(), c1 = buf.getVec<int>();
    vector<int> n0 = buf.getVec<int>(), n1 = buf.getVec<int>();
    vector<int> bc = buf.getVec<int>(), fam = buf.getVec<int>();
    vector<long> gid = buf.getVec<long>();
    m.faces.resize(nFaces);
    for (int i = 0; i < nFaces; ++i) {
      LocalFace& f = m.faces[i];
      f.c0 = c0[i]; f.c1 = c1[i]; f.n0 = n0[i]; f.n1 = n1[i];
      f.bc = bc[i]; f.family = fam[i]; f.globalId = gid[i];
    }
  }
  {
    int nnbr = buf.get<int>();
    m.neighbors.resize(nnbr);
    for (int i = 0; i < nnbr; ++i) {
      m.neighbors[i].rank = buf.get<int>();
      m.neighbors[i].sendIdx = buf.getVec<int>();
      m.neighbors[i].recvIdx = buf.getVec<int>();
    }
  }
  m.computeGeometry();
  m.buildCellFaces();
  m.buildLsq();
  for (int fi = 0; fi < nFaces; ++fi) {
    if (m.faces[fi].c1 < 0 && m.faces[fi].c0 < m.nOwn) {
      m.boundaryFaces.push_back(fi);
      if (m.faces[fi].bc == (int)BCType::SlipWall || m.faces[fi].bc == (int)BCType::NoSlipAdiabaticWall)
        m.wallFaces.push_back(fi);
    }
  }
  return m;
}

}  // namespace fv
