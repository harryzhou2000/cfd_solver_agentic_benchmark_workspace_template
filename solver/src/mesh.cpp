#include "mesh.hpp"
#include "metis.h"
#include <cgnslib.h>
#include <cstring>
#include <unordered_map>
#include <queue>
#include <cmath>
#include <stdexcept>

namespace cfd {

// ---------- vertex deduplication by coordinate ----------
struct CoordHash {
    long long kx, ky;
    bool operator==(const CoordHash& o) const { return kx == o.kx && ky == o.ky; }
};
struct CoordHasher {
    size_t operator()(const CoordHash& c) const { return (size_t)c.kx * 1000003ULL + (size_t)c.ky; }
};
static const double DEDUP_SCALE = 1.0e7; // ~1e-7 resolution
static CoordHash makeKey(double x, double y) {
    return { (long long)std::llround(x * DEDUP_SCALE), (long long)std::llround(y * DEDUP_SCALE) };
}

// ---------- CGNS reader ----------
void readCGNS(const std::string& path, GlobalMesh& gm,
              std::vector<std::string>& bcNamesFound) {
    int fn = 0;
    if (cg_open(path.c_str(), CG_MODE_READ, &fn) != CG_OK)
        throw std::runtime_error("cg_open failed for " + path);

    std::unordered_map<CoordHash, Int, CoordHasher> vmap;
    auto addVertex = [&](double x, double y) -> Int {
        CoordHash k = makeKey(x, y);
        auto it = vmap.find(k);
        if (it != vmap.end()) return it->second;
        Int id = (Int)gm.vertices.size();
        gm.vertices.push_back({x, y});
        vmap[k] = id;
        return id;
    };

    int nbases = 0;
    cg_nbases(fn, &nbases);
    for (int B = 1; B <= nbases; ++B) {
        char basename[128]; int celldim = 0, physdim = 0;
        cg_base_read(fn, B, basename, &celldim, &physdim);
        int nzones = 0;
        cg_nzones(fn, B, &nzones);
        for (int Z = 1; Z <= nzones; ++Z) {
            char zonename[128]; cgsize_t zsize[9];
            cg_zone_read(fn, B, Z, zonename, zsize);
            cgsize_t nvert = zsize[0];
            // read coordinates
            std::vector<double> xs(nvert), ys(nvert);
            cgsize_t rmin = 1, rmax = nvert;
            cg_coord_read(fn, B, Z, "CoordinateX", RealDouble, &rmin, &rmax, xs.data());
            cg_coord_read(fn, B, Z, "CoordinateY", RealDouble, &rmin, &rmax, ys.data());
            // zone-local vertex id (1-based) -> global merged id
            std::vector<Int> vgid(nvert + 1, -1);
            for (cgsize_t i = 0; i < nvert; ++i)
                vgid[i + 1] = addVertex(xs[i], ys[i]);

            // read sections
            int nsections = 0;
            cg_nsections(fn, B, Z, &nsections);
            for (int S = 1; S <= nsections; ++S) {
                char secname[128]; ElementType_t etype; cgsize_t start, end; int nbndry = 0, parent = 0;
                cg_section_read(fn, B, Z, S, secname, &etype, &start, &end, &nbndry, &parent);
                cgsize_t nelem = end - start + 1;
                if (etype == TRI_3 || etype == QUAD_4) {
                    int npn = (etype == TRI_3) ? 3 : 4;
                    std::vector<cgsize_t> conn((size_t)npn * nelem);
                    cg_elements_read(fn, B, Z, S, conn.data(), nullptr);
                    for (cgsize_t e = 0; e < nelem; ++e) {
                        std::vector<Int> cell(npn);
                        for (int j = 0; j < npn; ++j)
                            cell[j] = vgid[conn[(size_t)e * npn + j]];
                        gm.cells.push_back(std::move(cell));
                        gm.cellNbr.push_back(npn);
                    }
                } else if (etype == BAR_2) {
                    // boundary/interface edge section; store node pairs with family name
                    std::vector<cgsize_t> conn((size_t)2 * nelem);
                    cg_elements_read(fn, B, Z, S, conn.data(), nullptr);
                    std::string fam(secname);
                    auto& vec = gm.bcFaces[fam];
                    for (cgsize_t e = 0; e < nelem; ++e) {
                        Int a = vgid[conn[(size_t)e * 2]];
                        Int b = vgid[conn[(size_t)e * 2 + 1]];
                        vec.push_back({a, b});
                    }
                }
            }
        }
    }
    cg_close(fn);

    // Record all boundary family names found
    for (auto& kv : gm.bcFaces) bcNamesFound.push_back(kv.first);
}

// ---------- geometry helpers ----------
static Real signedArea(const std::vector<Int>& cell, const std::vector<Vec2>& verts) {
    Real a = 0.0;
    int n = (int)cell.size();
    for (int i = 0; i < n; ++i) {
        const Vec2& p0 = verts[cell[i]];
        const Vec2& p1 = verts[cell[(i + 1) % n]];
        a += 0.5 * (p0.x * p1.y - p1.x * p0.y);
    }
    return a;
}

// ---------- global connectivity (face builder) ----------
void buildGlobalConnectivity(GlobalMesh& gm, GlobalConnectivity& gc) {
    // Ensure CCW orientation (positive signed area)
    for (Int c = 0; c < (Int)gm.cells.size(); ++c) {
        if (signedArea(gm.cells[c], gm.vertices) < 0.0)
            std::reverse(gm.cells[c].begin(), gm.cells[c].end());
    }
    // Build face map: sorted node pair -> (cellL, localEdgeIdx)
    std::map<std::pair<Int,Int>, std::pair<Int,Int>> fmap;
    for (Int c = 0; c < (Int)gm.cells.size(); ++c) {
        int n = (int)gm.cells[c].size();
        for (int e = 0; e < n; ++e) {
            Int n0 = gm.cells[c][e];
            Int n1 = gm.cells[c][(e + 1) % n];
            auto key = std::make_pair(std::min(n0, n1), std::max(n0, n1));
            auto it = fmap.find(key);
            if (it == fmap.end()) {
                fmap[key] = {c, e};
            } else {
                // second cell; create interior face
                Int cL = it->second.first;
                int eL = it->second.second;
                Int fn0 = gm.cells[cL][eL];
                Int fn1 = gm.cells[cL][(eL + 1) % (int)gm.cells[cL].size()];
                gc.fNode0.push_back(fn0);
                gc.fNode1.push_back(fn1);
                gc.fCellL.push_back(cL);
                gc.fCellR.push_back(c);
                gc.fBCFam.push_back(-1);
                fmap.erase(it);
            }
        }
    }
    // Remaining faces are boundaries; match to family via node pair
    std::map<std::pair<Int,Int>, std::string> bcPairToFam;
    for (auto& kv : gm.bcFaces) {
        const std::string& fam = kv.first;
        for (auto& pr : kv.second) {
            auto key = std::make_pair(std::min(pr[0], pr[1]), std::max(pr[0], pr[1]));
            bcPairToFam[key] = fam;
        }
    }
    gc.bcFamilyNames.clear();
    std::map<std::string, Int> famIdx;
    for (auto& kv : gm.bcFaces) {
        famIdx[kv.first] = (Int)gc.bcFamilyNames.size();
        gc.bcFamilyNames.push_back(kv.first);
    }
    Int unmatched = 0;
    for (auto& kv : fmap) {
        Int cL = kv.second.first;
        int eL = kv.second.second;
        Int fn0 = gm.cells[cL][eL];
        Int fn1 = gm.cells[cL][(eL + 1) % (int)gm.cells[cL].size()];
        auto key = std::make_pair(std::min(fn0, fn1), std::max(fn0, fn1));
        auto fit = bcPairToFam.find(key);
        Int bf = -1;
        if (fit != bcPairToFam.end()) bf = famIdx[fit->second];
        else unmatched++;
        gc.fNode0.push_back(fn0);
        gc.fNode1.push_back(fn1);
        gc.fCellL.push_back(cL);
        gc.fCellR.push_back(-1);
        gc.fBCFam.push_back(bf);
    }
    if (unmatched > 0) {
        // Some boundary edges did not match a named family (e.g. unresolved
        // zone interface). Treat them as farfield-like boundaries is unsafe;
        // report but keep going with a synthetic family.
        fprintf(stderr, "WARNING: %lld global boundary faces unmatched to a family "
                "(possible zone-interface dedup issue)\n", (long long)unmatched);
    }
}

// ---------- METIS partitioning + local mesh ----------
static void computeFaceGeom(Face& f, const Vec2& p0, const Vec2& p1) {
    f.node0 = -1; f.node1 = -1; // set later with local ids
    f.length = norm(p1 - p0);
    Vec2 d = p1 - p0;
    // outward normal from cellL (CCW): (dy, -dx)/L
    Real L = std::max(f.length, 1e-30);
    f.normal = {d.y / L, -d.x / L};
    f.center = {(p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5};
}

void partitionAndBuildLocal(GlobalMesh& gm, GlobalConnectivity& gc,
                            const std::map<std::string, BCType>& bcMap,
                            LocalMesh& lm, MPI_Comm comm) {
    MPI_Comm_rank(comm, &lm.rank);
    MPI_Comm_size(comm, &lm.nranks);
    Int ncell = gm.numCells();
    lm.numCellsGlobal = ncell;
    lm.numFacesGlobal = (Int)gc.numFaces();

    // Build CSR adjacency for METIS
    std::vector<Int> xadj(ncell + 1, 0);
    for (Int f = 0; f < (Int)gc.numFaces(); ++f)
        if (gc.fCellR[f] >= 0) { xadj[gc.fCellL[f] + 1]++; xadj[gc.fCellR[f] + 1]++; }
    for (Int i = 0; i < ncell; ++i) xadj[i + 1] += xadj[i];
    std::vector<Int> adjncy(xadj[ncell]);
    std::vector<Int> pos(xadj.begin(), xadj.end() - 1);
    for (Int f = 0; f < (Int)gc.numFaces(); ++f) {
        if (gc.fCellR[f] >= 0) {
            adjncy[pos[gc.fCellL[f]]++] = gc.fCellR[f];
            adjncy[pos[gc.fCellR[f]]++] = gc.fCellL[f];
        }
    }

    std::vector<Int> part(ncell, 0);
    lm.edgeCut = 0;
    if (lm.nranks > 1) {
        idx_t nvtxs = ncell, ncon = 1, nparts = lm.nranks, objval = 0;
        std::vector<idx_t> ixadj(ncell + 1), iadjncy(xadj[ncell]);
        for (Int i = 0; i <= ncell; ++i) ixadj[i] = xadj[i];
        for (Int i = 0; i < xadj[ncell]; ++i) iadjncy[i] = adjncy[i];
        std::vector<idx_t> ipart(ncell);
        idx_t options[METIS_NOPTIONS];
        METIS_SetDefaultOptions(options);
        options[METIS_OPTION_CONTIG] = 1;
        options[METIS_OPTION_SEED] = 42;
        int r = METIS_PartGraphKway(&nvtxs, &ncon, ixadj.data(), iadjncy.data(),
            nullptr, nullptr, nullptr, &nparts, nullptr, nullptr, options, &objval, ipart.data());
        if (r != METIS_OK) throw std::runtime_error("METIS_PartGraphKway failed");
        for (Int i = 0; i < ncell; ++i) part[i] = (Int)ipart[i];
        lm.edgeCut = (Int)objval;
    }
    // broadcast partition (rank 0 computed it via METIS; all ranks have same input)
    // Since all ranks read the same mesh and call METIS with fixed seed, results match.

    // Determine owned cells
    std::vector<Int> ownedList, ghostList;
    std::vector<int> ghostRank;
    std::vector<Int> g2l(ncell, -1);
    for (Int c = 0; c < ncell; ++c)
        if (part[c] == lm.rank) { g2l[c] = (Int)ownedList.size(); ownedList.push_back(c); }
    lm.nOwned = (Int)ownedList.size();
    // find ghosts: neighbors of owned cells owned by other ranks
    std::map<Int, int> ghostOf; // global cell -> owner rank
    for (Int c = 0; c < ncell; ++c) {
        if (part[c] != lm.rank) continue;
        for (Int j = xadj[c]; j < xadj[c + 1]; ++j) {
            Int nb = adjncy[j];
            if (part[nb] != lm.rank) ghostOf[nb] = part[nb];
        }
    }
    for (auto& kv : ghostOf) {
        g2l[kv.first] = lm.nOwned + (Int)ghostList.size();
        ghostList.push_back(kv.first);
        ghostRank.push_back(kv.second);
    }
    lm.nGhost = (Int)ghostList.size();
    lm.nCells = lm.nOwned + lm.nGhost;

    lm.cellCenter.resize(lm.nCells);
    lm.cellArea.resize(lm.nCells);
    lm.cells.resize(lm.nCells);
    lm.cellFaces.resize(lm.nCells);
    lm.cellNeighbors.resize(lm.nCells);
    lm.ownedGlobalId = ownedList;
    lm.ghostGlobalId = ghostList;
    lm.ghostOwnerRank = ghostRank;

    // Local vertex renumbering
    std::unordered_map<Int, Int> vgid2local;
    auto addVert = [&](Int gv) -> Int {
        auto it = vgid2local.find(gv);
        if (it != vgid2local.end()) return it->second;
        Int id = (Int)lm.vertices.size();
        lm.vertices.push_back(gm.vertices[gv]);
        vgid2local[gv] = id;
        return id;
    };

    // Build local cell geometry + vertex lists
    auto buildCell = [&](Int local, Int global) {
        const auto& cell = gm.cells[global];
        int n = (int)cell.size();
        lm.cells[local].resize(n);
        Vec2 ctr{0, 0};
        for (int j = 0; j < n; ++j) {
            Int lv = addVert(cell[j]);
            lm.cells[local][j] = lv;
            ctr += lm.vertices[lv];
        }
        ctr = ctr * (1.0 / n);
        lm.cellCenter[local] = ctr;
        lm.cellArea[local] = std::abs(signedArea(cell, gm.vertices));
    };
    for (Int i = 0; i < lm.nOwned; ++i) buildCell(i, ownedList[i]);
    for (Int i = 0; i < lm.nGhost; ++i) buildCell(lm.nOwned + i, ghostList[i]);

    // Build local faces from global faces
    std::set<int> nbrSet;
    for (Int f = 0; f < (Int)gc.numFaces(); ++f) {
        Int cL = gc.fCellL[f], cR = gc.fCellR[f];
        bool ownL = (part[cL] == lm.rank);
        bool ownR = (cR >= 0 && part[cR] == lm.rank);
        if (!ownL && !ownR) continue;
        Face face;
        Int gL, gR;
        if (ownL) { gL = cL; gR = cR; }
        else { gL = cR; gR = cL; } // swap so cellL is owned
        Int lL = g2l[gL];
        Int lR = (gR >= 0) ? g2l[gR] : -1;
        // geometry: use global face nodes (cellL CCW edge order)
        Int fn0 = gc.fNode0[f], fn1 = gc.fNode1[f];
        // if we swapped, the outward normal must flip
        Vec2 p0 = gm.vertices[fn0], p1 = gm.vertices[fn1];
        computeFaceGeom(face, p0, p1);
        if (!ownL) { face.normal = { -face.normal.x, -face.normal.y }; } // swapped
        face.cellL = lL;
        face.cellR = lR;
        face.ghostR = (lR >= lm.nOwned);
        // local vertex ids for output
        face.node0 = addVert(fn0);
        face.node1 = addVert(fn1);
        if (cR < 0) {
            // boundary face
            face.cellR = -1;
            face.ghostR = false;
            Int bf = gc.fBCFam[f];
            if (bf >= 0 && bf < (Int)gc.bcFamilyNames.size()) {
                const std::string& fam = gc.bcFamilyNames[bf];
                auto it = bcMap.find(fam);
                if (it != bcMap.end()) face.bc = it->second;
                else face.bc = BCType::Farfield; // fallback
            } else {
                face.bc = BCType::Farfield;
            }
        } else {
            face.bc = BCType::Interior;
            // record neighbor stencil (skip boundary)
            lm.cellNeighbors[lL].push_back(lR);
            lm.cellNeighbors[lR].push_back(lL);
            if (face.ghostR) nbrSet.insert(lm.ghostOwnerRank[lR - lm.nOwned]);
        }
        Int fi = (Int)lm.faces.size();
        lm.faces.push_back(face);
        lm.cellFaces[lL].push_back(fi);
        if (lR >= 0 && lR < lm.nOwned) lm.cellFaces[lR].push_back(fi);
    }
    // dedup neighbor lists
    for (auto& nb : lm.cellNeighbors) {
        std::sort(nb.begin(), nb.end());
        nb.erase(std::unique(nb.begin(), nb.end()), nb.end());
    }

    lm.neighborRanks.assign(nbrSet.begin(), nbrSet.end());
    std::sort(lm.neighborRanks.begin(), lm.neighborRanks.end());

    // ---- Halo send/recv setup via point-to-point requests ----
    // Each rank tells each neighbor which global cell ids it needs (recv);
    // neighbors reply which cells they will send.
    int nnbr = (int)lm.neighborRanks.size();
    lm.sendCells.assign(nnbr, {});
    lm.recvCells.assign(nnbr, {});
    std::map<int, int> rank2idx;
    for (int i = 0; i < nnbr; ++i) rank2idx[lm.neighborRanks[i]] = i;
    // recv: list of global cell ids I need from each neighbor
    std::vector<std::vector<Int>> needGlobal(nnbr);
    for (Int i = 0; i < lm.nGhost; ++i) {
        int r = lm.ghostOwnerRank[i];
        needGlobal[rank2idx[r]].push_back(lm.ghostGlobalId[i]);
    }
    // exchange counts then ids with each neighbor
    std::vector<MPI_Request> reqs;
    std::vector<std::vector<Int>> sendGlobal(nnbr);
    for (int i = 0; i < nnbr; ++i) {
        int nbr = lm.neighborRanks[i];
        MPI_Request req;
        MPI_Isend(needGlobal[i].data(), (int)(needGlobal[i].size() * sizeof(Int)),
                  MPI_BYTE, nbr, 100, comm, &req); reqs.push_back(req);
    }
    std::vector<int> recvCounts(nnbr, 0);
    // first probe for each neighbor to learn sizes
    for (int i = 0; i < nnbr; ++i) {
        int nbr = lm.neighborRanks[i];
        MPI_Status st; int flag = 0;
        // Use MPI_Probe to get the message size
        MPI_Probe(nbr, 100, comm, &st);
        int cnt = 0;
        MPI_Get_count(&st, MPI_BYTE, &cnt);
        recvCounts[i] = cnt / (int)sizeof(Int);
        sendGlobal[i].resize(recvCounts[i]);
        MPI_Request req;
        MPI_Irecv(sendGlobal[i].data(), cnt, MPI_BYTE, nbr, 100, comm, &req);
        reqs.push_back(req);
    }
    MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
    // sendGlobal[i] = global cell ids that neighbor i needs from me
    // build sendCells (local owned indices) and recvCells (local ghost indices)
    for (int i = 0; i < nnbr; ++i) {
        // recvCells: for the global ids I requested, map to local ghost indices
        for (Int g : needGlobal[i]) {
            Int l = g2l[g];
            lm.recvCells[i].push_back(l);
        }
        // sendCells: for global ids neighbor needs, map to my local owned indices
        for (Int g : sendGlobal[i]) {
            Int l = g2l[g]; // must be owned
            lm.sendCells[i].push_back(l);
        }
    }
}

// ---------- halo exchange ----------
void exchangeHalo(const LocalMesh& lm, std::vector<Cons>& U, MPI_Comm comm) {
    int nnbr = (int)lm.neighborRanks.size();
    if (nnbr == 0) return;
    std::vector<MPI_Request> reqs;
    std::vector<std::vector<Real>> sbuf(nnbr), rbuf(nnbr);
    for (int i = 0; i < nnbr; ++i) {
        int nbr = lm.neighborRanks[i];
        sbuf[i].resize(lm.sendCells[i].size() * NEQ);
        for (size_t j = 0; j < lm.sendCells[i].size(); ++j) {
            const Cons& c = U[lm.sendCells[i][j]];
            for (int k = 0; k < NEQ; ++k) sbuf[i][j * NEQ + k] = c[k];
        }
        rbuf[i].resize(lm.recvCells[i].size() * NEQ);
        MPI_Request rs, rr;
        MPI_Isend(sbuf[i].data(), (int)sbuf[i].size(), MPI_DOUBLE, nbr, 200, comm, &rs);
        MPI_Irecv(rbuf[i].data(), (int)rbuf[i].size(), MPI_DOUBLE, nbr, 200, comm, &rr);
        reqs.push_back(rs); reqs.push_back(rr);
    }
    MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
    for (int i = 0; i < nnbr; ++i) {
        for (size_t j = 0; j < lm.recvCells[i].size(); ++j) {
            Cons& c = U[lm.recvCells[i][j]];
            for (int k = 0; k < NEQ; ++k) c[k] = rbuf[i][j * NEQ + k];
        }
    }
}

void exchangeHaloScalar(const LocalMesh& lm, std::vector<Real>& f, MPI_Comm comm) {
    int nnbr = (int)lm.neighborRanks.size();
    if (nnbr == 0) return;
    std::vector<MPI_Request> reqs;
    std::vector<std::vector<Real>> sbuf(nnbr), rbuf(nnbr);
    for (int i = 0; i < nnbr; ++i) {
        int nbr = lm.neighborRanks[i];
        sbuf[i].resize(lm.sendCells[i].size());
        for (size_t j = 0; j < lm.sendCells[i].size(); ++j) sbuf[i][j] = f[lm.sendCells[i][j]];
        rbuf[i].resize(lm.recvCells[i].size());
        MPI_Request rs, rr;
        MPI_Isend(sbuf[i].data(), (int)sbuf[i].size(), MPI_DOUBLE, nbr, 201, comm, &rs);
        MPI_Irecv(rbuf[i].data(), (int)rbuf[i].size(), MPI_DOUBLE, nbr, 201, comm, &rr);
        reqs.push_back(rs); reqs.push_back(rr);
    }
    MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
    for (int i = 0; i < nnbr; ++i)
        for (size_t j = 0; j < lm.recvCells[i].size(); ++j) f[lm.recvCells[i][j]] = rbuf[i][j];
}

} // namespace cfd
