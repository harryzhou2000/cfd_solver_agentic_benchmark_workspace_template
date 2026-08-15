#include "mesh.hpp"
#include <cgnslib.h>
#include <metis.h>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace fv {

static void cg_ok(int ierr, const char* what) {
    if (ierr != CG_OK)
        throw std::runtime_error(std::string("CGNS error in ") + what + ": " + cg_get_error());
}

struct EdgeKey {
    int a, b; // sorted
    bool operator==(const EdgeKey& o) const { return a == o.a && b == o.b; }
};
struct EdgeKeyHash {
    size_t operator()(const EdgeKey& k) const {
        return (size_t)k.a * 0x9E3779B97F4A7C15ull ^ (size_t)k.b;
    }
};

// ---------------- CGNS global mesh read (rank 0) ----------------

GlobalMesh read_global_mesh(const CaseConfig& cfg) {
    int fn;
    cg_ok(cg_open(cfg.mesh_file.c_str(), CG_MODE_READ, &fn), "cg_open");

    int nbases;
    cg_ok(cg_nbases(fn, &nbases), "cg_nbases");
    if (nbases != 1) { cg_close(fn); throw std::runtime_error("expected exactly one CGNS base"); }
    int B = 1;
    char basename[256];
    int cellDim, physDim;
    cg_ok(cg_base_read(fn, B, basename, &cellDim, &physDim), "cg_base_read");
    if (physDim != 2 && physDim != 3) { cg_close(fn); throw std::runtime_error("expected 2-D physical space"); }

    int nzones;
    cg_ok(cg_nzones(fn, B, &nzones), "cg_nzones");

    // First pass: zone sizes and names
    std::vector<std::string> zoneNames(nzones);
    std::vector<long> zoneNodes(nzones), nodeOffset(nzones);
    long totalNodes = 0;
    for (int Z = 1; Z <= nzones; ++Z) {
        char zname[256];
        cgsize_t size[3];
        cg_ok(cg_zone_read(fn, B, Z, zname, size), "cg_zone_read");
        zoneNames[Z - 1] = zname;
        zoneNodes[Z - 1] = (long)size[0];
        nodeOffset[Z - 1] = totalNodes;
        totalNodes += (long)size[0];
    }

    // Read coordinates of every zone into global arrays
    GlobalMesh gm;
    std::vector<double> gx(totalNodes), gy(totalNodes);
    for (int Z = 1; Z <= nzones; ++Z) {
        cgsize_t rmin = 1, rmax = (cgsize_t)zoneNodes[Z - 1];
        cg_ok(cg_coord_read(fn, B, Z, "CoordinateX", RealDouble, &rmin, &rmax, gx.data() + nodeOffset[Z - 1]), "cg_coord_read X");
        cg_ok(cg_coord_read(fn, B, Z, "CoordinateY", RealDouble, &rmin, &rmax, gy.data() + nodeOffset[Z - 1]), "cg_coord_read Y");
    }

    // 1-to-1 connections: build union-find over global node ids
    std::vector<int> uf(totalNodes);
    for (long i = 0; i < totalNodes; ++i) uf[i] = (int)i;
    auto find = [&](int a) { while (uf[a] != a) { uf[a] = uf[uf[a]]; a = uf[a]; } return a; };
    auto unite = [&](int a, int b) { a = find(a); b = find(b); if (a != b) uf[std::max(a,b)] = std::min(a,b); };
    auto zoneIndex = [&](const std::string& nm) -> int {
        for (int i = 0; i < nzones; ++i) if (zoneNames[i] == nm) return i;
        return -1;
    };

    for (int Z = 1; Z <= nzones; ++Z) {
        int nconn;
        cg_ok(cg_n1to1(fn, B, Z, &nconn), "cg_n1to1");
        for (int C = 1; C <= nconn; ++C) {
            char cname[256], dname[256];
            cgsize_t range[2], drange[2];
            int transform[2];
            cg_ok(cg_1to1_read(fn, B, Z, C, cname, dname, range, drange, transform), "cg_1to1_read");
            int dz = zoneIndex(dname);
            if (dz < 0) { cg_close(fn); throw std::runtime_error(std::string("1-to-1 donor zone not found: ") + dname); }
            long npts = (long)(range[1] - range[0]) + 1;
            if ((long)(drange[1] - drange[0]) + 1 != npts) { cg_close(fn); throw std::runtime_error("1-to-1 range size mismatch"); }
            int t = transform[0]; // +-1 for 1-D index spaces
            if (std::abs(t) != 1) { cg_close(fn); throw std::runtime_error("unsupported 1-to-1 transform"); }
            for (long k = 0; k < npts; ++k) {
                long pa = nodeOffset[Z - 1] + (long)range[0] - 1 + k;
                long pb = nodeOffset[dz] + (long)drange[0] - 1 + (t > 0 ? k : (npts - 1 - k));
                unite((int)pa, (int)pb);
            }
        }
        // general (Abutting1to1 with point lists) grid connectivity
        int ngcon;
        cg_ok(cg_nconns(fn, B, Z, &ngcon), "cg_nconns");
        for (int C = 1; C <= ngcon; ++C) {
            char cname[256], dname[256];
            GridLocation_t location;
            GridConnectivityType_t gctype;
            PointSetType_t ptset, donor_ptset;
            ZoneType_t donor_ztype;
            DataType_t donor_dtype;
            cgsize_t npnts = 0, ndonor = 0;
            cg_ok(cg_conn_info(fn, B, Z, C, cname, &location, &gctype, &ptset, &npnts,
                               dname, &donor_ztype, &donor_ptset, &donor_dtype, &ndonor), "cg_conn_info");
            int dz = zoneIndex(dname);
            if (dz < 0) { cg_close(fn); throw std::runtime_error(std::string("grid-connectivity donor zone not found: ") + dname); }
            if (npnts != ndonor) { cg_close(fn); throw std::runtime_error("grid-connectivity point list size mismatch"); }
            std::vector<cgsize_t> pnts(npnts), dpnts(ndonor);
            cg_ok(cg_conn_read(fn, B, Z, C, pnts.data(), donor_dtype, dpnts.data()), "cg_conn_read");
            for (long k = 0; k < (long)npnts; ++k) {
                long pa = nodeOffset[Z - 1] + (long)pnts[k] - 1;
                long pb = nodeOffset[dz] + (long)dpnts[k] - 1;
                unite((int)pa, (int)pb);
            }
        }
    }

    // Compact merged nodes
    std::vector<int> compact(totalNodes, -1);
    long nNodes = 0;
    for (long i = 0; i < totalNodes; ++i) {
        int root = find((int)i);
        if (root == (int)i) {
            compact[i] = (int)nNodes;
            gm.x.push_back(gx[i]);
            gm.y.push_back(gy[i]);
            ++nNodes;
        }
    }
    gm.nNodes = nNodes;
    auto gmap = [&](long g) { return compact[find((int)g)]; };

    // BC family lookup from case
    auto bcForName = [&](const std::string& nm) -> int {
        for (size_t i = 0; i < cfg.bc_map.size(); ++i)
            if (cfg.bc_map[i].first == nm) return (int)i;
        return -1;
    };
    for (auto& [nm, t] : cfg.bc_map) gm.tagNames.push_back(nm);
    std::set<int> matchedTags;

    // Second pass: elements
    for (int Z = 1; Z <= nzones; ++Z) {
        int nsections;
        cg_ok(cg_nsections(fn, B, Z, &nsections), "cg_nsections");
        for (int S = 1; S <= nsections; ++S) {
            char sname[256];
            ElementType_t etype;
            cgsize_t start, end;
            int nbndry, pflag;
            cg_ok(cg_section_read(fn, B, Z, S, sname, &etype, &start, &end, &nbndry, &pflag), "cg_section_read");
            long nelem = (long)(end - start) + 1;
            if (etype == TRI_3 || etype == QUAD_4) {
                int npe = (etype == TRI_3) ? 3 : 4;
                std::vector<cgsize_t> conn(nelem * npe);
                cg_ok(cg_elements_read(fn, B, Z, S, conn.data(), nullptr), "cg_elements_read");
                for (long e = 0; e < nelem; ++e) {
                    std::array<int,4> cn;
                    for (int k = 0; k < npe; ++k)
                        cn[k] = gmap(nodeOffset[Z - 1] + (long)conn[e * npe + k] - 1);
                    // ensure CCW orientation
                    double a2 = 0.0;
                    for (int k = 0; k < npe; ++k) {
                        int k2 = (k + 1) % npe;
                        a2 += gm.x[cn[k]] * gm.y[cn[k2]] - gm.x[cn[k2]] * gm.y[cn[k]];
                    }
                    gm.cellNodeOff.push_back((int)gm.cellNodes.size());
                    if (a2 < 0.0) { // reverse to make CCW
                        for (int k = npe - 1; k >= 0; --k) gm.cellNodes.push_back(cn[k]);
                    } else {
                        for (int k = 0; k < npe; ++k) gm.cellNodes.push_back(cn[k]);
                    }
                    ++gm.nCells;
                }
            } else if (etype == BAR_2) {
                int tag = bcForName(sname);
                if (tag < 0) continue; // interface edge sections (e.g. con-*) are skipped
                matchedTags.insert(tag);
                std::vector<cgsize_t> conn(nelem * 2);
                cg_ok(cg_elements_read(fn, B, Z, S, conn.data(), nullptr), "cg_elements_read bc");
                for (long e = 0; e < nelem; ++e) {
                    int a = gmap(nodeOffset[Z - 1] + (long)conn[e * 2 + 0] - 1);
                    int b = gmap(nodeOffset[Z - 1] + (long)conn[e * 2 + 1] - 1);
                    gm.bedges.push_back({a, b, tag});
                }
            } else {
                cg_close(fn);
                throw std::runtime_error(std::string("unsupported CGNS element type in section ") + sname);
            }
        }
    }
    cg_close(fn);

    gm.cellNodeOff.push_back((int)gm.cellNodes.size());

    // integrity check: every non-BC edge must be shared by exactly two cells
    {
        std::unordered_map<EdgeKey, int, EdgeKeyHash> ecount;
        for (long c = 0; c < gm.nCells; ++c) {
            int nn = gm.cellNodeOff[c + 1] - gm.cellNodeOff[c];
            for (int k = 0; k < nn; ++k) {
                int a = gm.cellNodes[gm.cellNodeOff[c] + k];
                int b = gm.cellNodes[gm.cellNodeOff[c] + (k + 1) % nn];
                ecount[{std::min(a, b), std::max(a, b)}]++;
            }
        }
        std::unordered_set<long> bset;
        for (auto& be : gm.bedges) {
            int a = std::min(be.a, be.b), b = std::max(be.a, be.b);
            bset.insert(((long)a << 32) | (unsigned)b);
        }
        long unmatched = 0;
        for (auto& kv : ecount) {
            long key = ((long)kv.first.a << 32) | (unsigned)kv.first.b;
            if (kv.second == 1 && !bset.count(key)) ++unmatched;
        }
        if (unmatched > 0)
            throw std::runtime_error("mesh has " + std::to_string(unmatched) +
                                     " unmatched non-boundary edges (zone merge failure)");
    }

    for (size_t i = 0; i < cfg.bc_map.size(); ++i) {
        if (!matchedTags.count((int)i)) {
            std::string avail;
            throw std::runtime_error("boundary family '" + cfg.bc_map[i].first +
                "' from case file not found as a BAR_2 section in mesh");
        }
    }
    if (gm.nCells <= 0) throw std::runtime_error("mesh has no volume cells");
    if (gm.bedges.empty()) throw std::runtime_error("mesh has no boundary edges");
    return gm;
}

// ---------------- Partition build (rank 0) ----------------

static void write_partition_info(const std::string& dir, const PartitionInfo& pi) {
    nlohmann::json j;
    j["num_cells_global"] = pi.nCellsGlobal;
    j["num_faces_global"] = pi.nFacesGlobal;
    j["edge_cut"] = pi.edgeCut;
    j["owned_per_rank"] = pi.ownedPerRank;
    j["ghost_per_rank"] = pi.ghostPerRank;
    j["boundary_faces_per_rank"] = pi.bdryPerRank;
    j["send_cells_per_rank"] = pi.sendPerRank;
    j["recv_cells_per_rank"] = pi.recvPerRank;
    j["neighbors_per_rank"] = pi.neighborsPerRank;
    std::ofstream(dir + "/partition_info.json") << j.dump(2) << "\n";
}

PartitionInfo read_partition_info(const std::string& dir) {
    std::ifstream in(dir + "/partition_info.json");
    if (!in) throw std::runtime_error("missing partition_info.json in " + dir);
    nlohmann::json j;
    in >> j;
    PartitionInfo pi;
    pi.nCellsGlobal = j.at("num_cells_global").get<long>();
    pi.nFacesGlobal = j.at("num_faces_global").get<long>();
    pi.edgeCut = j.at("edge_cut").get<long>();
    pi.ownedPerRank = j.at("owned_per_rank").get<std::vector<int>>();
    pi.ghostPerRank = j.at("ghost_per_rank").get<std::vector<int>>();
    pi.bdryPerRank = j.at("boundary_faces_per_rank").get<std::vector<int>>();
    pi.sendPerRank = j.at("send_cells_per_rank").get<std::vector<long>>();
    pi.recvPerRank = j.at("recv_cells_per_rank").get<std::vector<long>>();
    pi.neighborsPerRank = j.at("neighbors_per_rank").get<std::vector<int>>();
    return pi;
}

bool build_partitions(const CaseConfig& cfg, const GlobalMesh& gm, int nparts,
                      const std::string& dir, long& edgeCutOut) {
    if (fs::exists(dir + "/partition_info.json")) {
        PartitionInfo pi = read_partition_info(dir);
        if (pi.nCellsGlobal == gm.nCells && (int)pi.ownedPerRank.size() == nparts) {
            edgeCutOut = pi.edgeCut;
            return false; // cache hit
        }
    }
    fs::create_directories(dir);

    // cell-to-cell adjacency through shared edges
    std::unordered_map<EdgeKey, int, EdgeKeyHash> edgeCell;
    edgeCell.reserve(gm.nCells * 2);
    std::vector<std::vector<int>> adj(gm.nCells);
    std::vector<int> cellEdgeCount(gm.nCells, 0);
    for (long c = 0; c < gm.nCells; ++c) {
        int nn = gm.cellNodeOff[c + 1] - gm.cellNodeOff[c];
        cellEdgeCount[c] = nn;
        for (int k = 0; k < nn; ++k) {
            int a = gm.cellNodes[gm.cellNodeOff[c] + k];
            int b = gm.cellNodes[gm.cellNodeOff[c] + (k + 1) % nn];
            EdgeKey key{std::min(a, b), std::max(a, b)};
            auto it = edgeCell.find(key);
            if (it == edgeCell.end()) {
                edgeCell[key] = (int)c;
            } else {
                int other = it->second;
                adj[c].push_back(other);
                adj[other].push_back((int)c);
            }
        }
    }

    // METIS k-way partitioning of the dual graph
    std::vector<idx_t> xadj(gm.nCells + 1, 0), adjncy;
    for (long c = 0; c < gm.nCells; ++c) {
        xadj[c + 1] = xadj[c] + (idx_t)adj[c].size();
        for (int nb : adj[c]) adjncy.push_back((idx_t)nb);
    }
    std::vector<idx_t> part(gm.nCells, 0);
    idx_t edgeCut = 0;
    if (nparts > 1) {
        idx_t nvtxs = (idx_t)gm.nCells, ncon = 1, np = nparts;
        std::vector<idx_t> opts(METIS_NOPTIONS);
        METIS_SetDefaultOptions(opts.data());
        opts[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
        int rc = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                     nullptr, nullptr, nullptr, &np,
                                     nullptr, nullptr, opts.data(), &edgeCut, part.data());
        if (rc != METIS_OK) throw std::runtime_error("METIS_PartGraphKway failed");
    }
    edgeCutOut = edgeCut;

    // per-rank assembly
    PartitionInfo pi;
    pi.nCellsGlobal = gm.nCells;
    pi.edgeCut = edgeCut;
    pi.ownedPerRank.assign(nparts, 0);
    pi.ghostPerRank.assign(nparts, 0);
    pi.bdryPerRank.assign(nparts, 0);
    pi.sendPerRank.assign(nparts, 0);
    pi.recvPerRank.assign(nparts, 0);
    pi.neighborsPerRank.assign(nparts, 0);

    // boundary edge lookup: sorted node pair -> tag
    std::unordered_map<EdgeKey, int, EdgeKeyHash> bedgeTag;
    for (auto& be : gm.bedges)
        bedgeTag[{std::min(be.a, be.b), std::max(be.a, be.b)}] = be.tag;

    long totalFaces = 0; // count once
    {
        // interior faces = edges shared by 2 cells; boundary faces = bedges
        long interior = 0;
        std::unordered_map<EdgeKey, int, EdgeKeyHash> cnt;
        for (long c = 0; c < gm.nCells; ++c) {
            int nn = gm.cellNodeOff[c + 1] - gm.cellNodeOff[c];
            for (int k = 0; k < nn; ++k) {
                int a = gm.cellNodes[gm.cellNodeOff[c] + k];
                int b = gm.cellNodes[gm.cellNodeOff[c] + (k + 1) % nn];
                cnt[{std::min(a, b), std::max(a, b)}]++;
            }
        }
        for (auto& kv : cnt) if (kv.second == 2) ++interior;
        totalFaces = interior + (long)gm.bedges.size();
    }
    pi.nFacesGlobal = totalFaces;

    for (int r = 0; r < nparts; ++r) {
        // owned cells (sorted by global id = natural order)
        std::vector<int> owned;
        for (long c = 0; c < gm.nCells; ++c)
            if (part[c] == r) owned.push_back((int)c);
        std::vector<char> isOwned(gm.nCells, 0);
        for (int c : owned) isOwned[c] = 1;

        // ghost set
        std::set<int> ghostSet;
        std::unordered_map<int, std::set<int>> sendTo; // rank -> owned cell gids to send
        for (int c : owned) {
            for (int nb : adj[c]) {
                if (!isOwned[nb]) {
                    ghostSet.insert(nb);
                    sendTo[part[nb]].insert(c);
                }
            }
        }
        std::vector<int> ghosts(ghostSet.begin(), ghostSet.end()); // sorted by gid

        std::unordered_map<int, int> localCell; // gid -> local idx
        int idx = 0;
        for (int c : owned) localCell[c] = idx++;
        for (int c : ghosts) localCell[c] = idx++;

        // local nodes
        std::unordered_map<int, int> localNode;
        std::vector<int> lnodes;
        auto nodeLocal = [&](int g) {
            auto it = localNode.find(g);
            if (it != localNode.end()) return it->second;
            int li = (int)lnodes.size();
            localNode[g] = li;
            lnodes.push_back(g);
            return li;
        };

        std::vector<int> cellOff, cellNodes;
        std::vector<long> cellGid;
        std::vector<int> ghostOwner;
        for (int c : owned) {
            cellOff.push_back((int)cellNodes.size());
            cellGid.push_back(c);
            int nn = gm.cellNodeOff[c + 1] - gm.cellNodeOff[c];
            for (int k = 0; k < nn; ++k)
                cellNodes.push_back(nodeLocal(gm.cellNodes[gm.cellNodeOff[c] + k]));
        }
        for (int c : ghosts) {
            cellOff.push_back((int)cellNodes.size());
            cellGid.push_back(c);
            ghostOwner.push_back(part[c]);
            int nn = gm.cellNodeOff[c + 1] - gm.cellNodeOff[c];
            for (int k = 0; k < nn; ++k)
                cellNodes.push_back(nodeLocal(gm.cellNodes[gm.cellNodeOff[c] + k]));
        }
        cellOff.push_back((int)cellNodes.size());

        // boundary faces of owned cells
        std::vector<std::array<int,3>> bfaces; // localCell, edgeIdx, tag
        for (int c : owned) {
            int nn = gm.cellNodeOff[c + 1] - gm.cellNodeOff[c];
            for (int k = 0; k < nn; ++k) {
                int a = gm.cellNodes[gm.cellNodeOff[c] + k];
                int b = gm.cellNodes[gm.cellNodeOff[c] + (k + 1) % nn];
                auto it = bedgeTag.find({std::min(a, b), std::max(a, b)});
                if (it != bedgeTag.end())
                    bfaces.push_back({localCell[c], k, it->second});
            }
        }

        // recv plan: ghosts grouped by owner rank, sorted by gid within group
        std::map<int, std::vector<int>> recvFrom; // rank -> ghost local idx sorted by gid
        for (size_t gi = 0; gi < ghosts.size(); ++gi)
            recvFrom[part[ghosts[gi]]].push_back((int)(owned.size() + gi));

        // write binary partition file
        std::string path = dir + "/part_" + std::to_string(r) + ".bin";
        std::ofstream out(path, std::ios::binary);
        if (!out) throw std::runtime_error("cannot write " + path);
        auto w32 = [&](int v) { out.write(reinterpret_cast<const char*>(&v), 4); };
        auto w64 = [&](long v) { out.write(reinterpret_cast<const char*>(&v), 8); };
        auto wd = [&](double v) { out.write(reinterpret_cast<const char*>(&v), 8); };
        out.write("FVP1", 4);
        w32((int)owned.size()); w32((int)ghosts.size()); w32((int)lnodes.size());
        w32((int)bfaces.size()); w32((int)sendTo.size()); w32((int)recvFrom.size());
        w32((int)gm.tagNames.size());
        w64(gm.nCells); w64(totalFaces); w64(edgeCut); w64((long)nparts);
        for (int g : lnodes) { wd(gm.x[g]); wd(gm.y[g]); w64(g); }
        for (int v : cellOff) w32(v);
        for (int v : cellNodes) w32(v);
        for (long v : cellGid) w64(v);
        for (int v : ghostOwner) w32(v);
        for (auto& bf : bfaces) { w32(bf[0]); w32(bf[1]); w32(bf[2]); }
        for (auto& [rank, cells] : sendTo) {
            w32(rank); w32((int)cells.size());
            for (int c : cells) w32(localCell[c]);
        }
        for (auto& [rank, cells] : recvFrom) {
            w32(rank); w32((int)cells.size());
            for (int c : cells) w32(c);
        }
        for (auto& tn : gm.tagNames) { w32((int)tn.size()); out.write(tn.data(), tn.size()); }
        out.close();

        pi.ownedPerRank[r] = (int)owned.size();
        pi.ghostPerRank[r] = (int)ghosts.size();
        pi.bdryPerRank[r] = (int)bfaces.size();
        pi.sendPerRank[r] = 0;
        for (auto& [rank, cells] : sendTo) pi.sendPerRank[r] += (long)cells.size();
        for (auto& [rank, cells] : recvFrom) pi.recvPerRank[r] += (long)cells.size();
        pi.neighborsPerRank[r] = (int)sendTo.size();
    }

    write_partition_info(dir, pi);
    return true;
}

// ---------------- Partition load (all ranks) ----------------

LocalMesh load_partition(const std::string& dir, int rank) {
    std::string path = dir + "/part_" + std::to_string(rank) + ".bin";
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open partition file " + path);
    char magic[4];
    in.read(magic, 4);
    if (std::memcmp(magic, "FVP1", 4) != 0) throw std::runtime_error("bad partition file magic");
    auto r32 = [&]() { int v; in.read(reinterpret_cast<char*>(&v), 4); return v; };
    auto r64 = [&]() { long v; in.read(reinterpret_cast<char*>(&v), 8); return v; };
    auto rd = [&]() { double v; in.read(reinterpret_cast<char*>(&v), 8); return v; };

    LocalMesh m;
    int nOwned = r32(), nGhost = r32(), nNodes = r32();
    int nB = r32(), nSend = r32(), nRecv = r32(), nTags = r32();
    m.nOwned = nOwned; m.nGhost = nGhost; m.nNodes = nNodes;
    m.nCellsGlobal = r64(); m.nFacesGlobal = r64(); m.edgeCut = r64();
    r64(); // nparts (not needed here)

    for (int i = 0; i < nNodes; ++i) {
        double x = rd(), y = rd(); long g = r64();
        m.nodeX.push_back(x); m.nodeY.push_back(y); m.nodeGid.push_back(g);
    }
    int nCells = nOwned + nGhost;
    for (int i = 0; i <= nCells; ++i) m.cellNodeOff.push_back(r32());
    for (int i = 0; i < m.cellNodeOff[nCells]; ++i) m.cellNodes.push_back(r32());
    for (int i = 0; i < nCells; ++i) m.cellGid.push_back(r64());
    for (int i = 0; i < nGhost; ++i) m.ghostOwner.push_back(r32());

    struct BF { int cell, edge, tag; };
    std::vector<BF> bfs(nB);
    for (auto& b : bfs) { b.cell = r32(); b.edge = r32(); b.tag = r32(); }

    m.sendOff.push_back(0);
    for (int i = 0; i < nSend; ++i) {
        int rank2 = r32(), cnt = r32();
        m.sendRanks.push_back(rank2);
        m.sendOff.push_back(m.sendOff.back() + cnt);
        for (int k = 0; k < cnt; ++k) m.sendCells.push_back(r32());
    }
    m.recvOff.push_back(0);
    for (int i = 0; i < nRecv; ++i) {
        int rank2 = r32(), cnt = r32();
        m.recvRanks.push_back(rank2);
        m.recvOff.push_back(m.recvOff.back() + cnt);
        for (int k = 0; k < cnt; ++k) m.recvCells.push_back(r32());
    }
    for (int i = 0; i < nTags; ++i) {
        int len = r32();
        std::string s(len, ' ');
        in.read(s.data(), len);
        m.tagNames.push_back(s);
    }
    in.close();

    // store boundary faces in a side array used by finalize_local_mesh
    // (encoded via a member-less approach: stash into bfaces after face build;
    //  here we keep them in the mesh via a temporary static map keyed by (cell,edge))
    // Simpler: attach as extra members.
    m.bndCell.reserve(nB);
    m.bndEdge.reserve(nB);
    m.bndTag.reserve(nB);
    for (auto& b : bfs) { m.bndCell.push_back(b.cell); m.bndEdge.push_back(b.edge); m.bndTag.push_back(b.tag); }

    return m;
}

// ---------------- Local geometry / faces / LSQ ----------------

void finalize_local_mesh(LocalMesh& m) {
    int nC = m.nCells();

    // cell centers and volumes (polygon centroid + area)
    m.cellCx.assign(nC, 0.0); m.cellCy.assign(nC, 0.0); m.cellVol.assign(nC, 0.0);
    for (int c = 0; c < nC; ++c) {
        int nn = m.cellNodeOff[c + 1] - m.cellNodeOff[c];
        double a = 0.0, cx = 0.0, cy = 0.0;
        for (int k = 0; k < nn; ++k) {
            int i0 = m.cellNodes[m.cellNodeOff[c] + k];
            int i1 = m.cellNodes[m.cellNodeOff[c] + (k + 1) % nn];
            double x0 = m.nodeX[i0], y0 = m.nodeY[i0];
            double x1 = m.nodeX[i1], y1 = m.nodeY[i1];
            double cross = x0 * y1 - x1 * y0;
            a += cross;
            cx += (x0 + x1) * cross;
            cy += (y0 + y1) * cross;
        }
        a *= 0.5;
        if (a <= 0.0) throw std::runtime_error("non-positive cell area (orientation bug)");
        m.cellVol[c] = a;
        m.cellCx[c] = cx / (6.0 * a);
        m.cellCy[c] = cy / (6.0 * a);
    }

    auto keyOf = [&](int a, int b) {
        int lo = std::min(a, b), hi = std::max(a, b);
        return ((long)lo << 32) | (unsigned)hi;
    };
    // boundary lookup: (cell, edge) -> tag
    std::map<std::pair<int,int>, int> btag;
    for (size_t i = 0; i < m.bndCell.size(); ++i)
        btag[{m.bndCell[i], m.bndEdge[i]}] = m.bndTag[i];

    // Build faces for owned cells (one face per unique owned-cell edge)
    std::map<long, int> edgeToFace; // edge key -> face id (for owned-cell edges)
    for (int c = 0; c < m.nOwned; ++c) {
        int nn = m.cellNodeOff[c + 1] - m.cellNodeOff[c];
        for (int k = 0; k < nn; ++k) {
            int a = m.cellNodes[m.cellNodeOff[c] + k];
            int b = m.cellNodes[m.cellNodeOff[c] + (k + 1) % nn];
            long key = keyOf(a, b);
            if (edgeToFace.count(key)) continue; // already created by neighbor owned cell
            // outward normal of cell c for edge a->b (CCW cells): n = (dy, -dx)
            double xa = m.nodeX[a], ya = m.nodeY[a];
            double xb = m.nodeX[b], yb = m.nodeY[b];
            double dx = xb - xa, dy = yb - ya;
            double len = std::hypot(dx, dy);
            double nx = dy / len, ny = -dx / len;
            double fcx = 0.5 * (xa + xb), fcy = 0.5 * (ya + yb);
            m.faceCl.push_back(c);
            m.faceNx.push_back(nx); m.faceNy.push_back(ny);
            m.faceS.push_back(len);
            m.faceCx.push_back(fcx); m.faceCy.push_back(fcy);
            m.faceBc.push_back(-1);
            m.faceCr.push_back(-1); // fixed in the next pass
            edgeToFace[key] = (int)m.faceCl.size() - 1;
        }
    }

    // Neighbor resolution: iterate all cells' edges; for each edge of an owned cell,
    // find another cell sharing the same edge.
    {
        // map edge -> up to 2 cells
        std::unordered_map<long, std::pair<int,int>, std::hash<long>> edgeCells;
        edgeCells.reserve(nC * 2);
        for (int c = 0; c < nC; ++c) {
            int nn = m.cellNodeOff[c + 1] - m.cellNodeOff[c];
            for (int k = 0; k < nn; ++k) {
                int a = m.cellNodes[m.cellNodeOff[c] + k];
                int b = m.cellNodes[m.cellNodeOff[c] + (k + 1) % nn];
                long key = keyOf(a, b);
                auto it = edgeCells.find(key);
                if (it == edgeCells.end()) edgeCells[key] = {c, -1};
                else it->second.second = c;
            }
        }
        for (auto& [key, fid] : edgeToFace) {
            auto pr = edgeCells[key];
            int c = m.faceCl[fid];
            int other = (pr.first == c) ? pr.second : pr.first;
            if (other >= 0) {
                m.faceCr[fid] = other;
                // orient normal from cl to cr: check the normal points toward cr center
                double vx = m.cellCx[other] - m.cellCx[c];
                double vy = m.cellCy[other] - m.cellCy[c];
                if (vx * m.faceNx[fid] + vy * m.faceNy[fid] < 0.0) {
                    m.faceNx[fid] = -m.faceNx[fid];
                    m.faceNy[fid] = -m.faceNy[fid];
                }
            } else {
                // boundary face: look up tag
                int tag = -1;
                // find (cell, edge) pair
                // (linear scan acceptable: boundary faces are few)
                for (auto& [ce, tg] : btag) {
                    if (ce.first == c) {
                        int nn = m.cellNodeOff[c + 1] - m.cellNodeOff[c];
                        int k = ce.second;
                        int a2 = m.cellNodes[m.cellNodeOff[c] + k];
                        int b2 = m.cellNodes[m.cellNodeOff[c] + (k + 1) % nn];
                        if (keyOf(a2, b2) == key) { tag = tg; break; }
                    }
                }
                if (tag < 0) throw std::runtime_error("owned-cell edge with no neighbor and no BC tag");
                m.faceBc[fid] = tag;
                m.bfaces.push_back(fid);
            }
        }
    }
    m.nFaces = (int)m.faceCl.size();

    // per-cell face list (faces where the owned cell is left or right)
    m.cellFaceOff.assign(m.nOwned + 1, 0);
    for (int f = 0; f < m.nFaces; ++f) {
        m.cellFaceOff[m.faceCl[f] + 1]++;
        if (m.faceCr[f] >= 0 && m.faceCr[f] < m.nOwned) m.cellFaceOff[m.faceCr[f] + 1]++;
    }
    for (int c = 0; c < m.nOwned; ++c) m.cellFaceOff[c + 1] += m.cellFaceOff[c];
    m.cellFace.resize(m.cellFaceOff[m.nOwned]);
    {
        std::vector<int> pos(m.cellFaceOff.begin(), m.cellFaceOff.end());
        for (int f = 0; f < m.nFaces; ++f) {
            m.cellFace[pos[m.faceCl[f]]++] = f;
            int cr = m.faceCr[f];
            if (cr >= 0 && cr < m.nOwned) m.cellFace[pos[cr]++] = f;
        }
    }

    // reconstruction vectors (cell center -> face center)
    m.faceRxL.resize(m.nFaces); m.faceRyL.resize(m.nFaces);
    m.faceRxR.resize(m.nFaces); m.faceRyR.resize(m.nFaces);
    for (int f = 0; f < m.nFaces; ++f) {
        int cl = m.faceCl[f], cr = m.faceCr[f];
        m.faceRxL[f] = m.faceCx[f] - m.cellCx[cl];
        m.faceRyL[f] = m.faceCy[f] - m.cellCy[cl];
        if (cr >= 0) {
            m.faceRxR[f] = m.faceCx[f] - m.cellCx[cr];
            m.faceRyR[f] = m.faceCy[f] - m.cellCy[cr];
        }
    }

    // LSQ weights per owned cell over its face stencil
    m.lsqLx.assign(m.nFaces, 0.0); m.lsqLy.assign(m.nFaces, 0.0);
    m.lsqRx.assign(m.nFaces, 0.0); m.lsqRy.assign(m.nFaces, 0.0);
    for (int c = 0; c < m.nOwned; ++c) {
        int nf = m.cellFaceOff[c + 1] - m.cellFaceOff[c];
        double sxx = 0, sxy = 0, syy = 0;
        for (int t = 0; t < nf; ++t) {
            int f = m.cellFace[m.cellFaceOff[c] + t];
            int nb = (m.faceCl[f] == c) ? m.faceCr[f] : m.faceCl[f];
            double dx, dy;
            if (nb >= 0) {
                dx = m.cellCx[nb] - m.cellCx[c];
                dy = m.cellCy[nb] - m.cellCy[c];
            } else { // boundary face: stencil point = face center
                dx = m.faceCx[f] - m.cellCx[c];
                dy = m.faceCy[f] - m.cellCy[c];
            }
            double w = 1.0 / std::max(dx * dx + dy * dy, 1e-300);
            sxx += w * dx * dx; sxy += w * dx * dy; syy += w * dy * dy;
        }
        double det = sxx * syy - sxy * sxy;
        if (std::abs(det) < 1e-24 * (sxx * syy + 1e-300)) {
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "near-singular LSQ system: cell %d gid %ld nf %d sxx %.3e syy %.3e sxy %.3e det %.3e center %.6g %.6g",
                     c, m.cellGid[c], nf, sxx, syy, sxy, det, m.cellCx[c], m.cellCy[c]);
            throw std::runtime_error(msg);
        }
        double ixx = syy / det, ixy = -sxy / det, iyy = sxx / det;
        for (int t = 0; t < nf; ++t) {
            int f = m.cellFace[m.cellFaceOff[c] + t];
            bool asLeft = (m.faceCl[f] == c);
            int nb = asLeft ? m.faceCr[f] : m.faceCl[f];
            double dx, dy;
            if (nb >= 0) {
                dx = m.cellCx[nb] - m.cellCx[c];
                dy = m.cellCy[nb] - m.cellCy[c];
            } else {
                dx = m.faceCx[f] - m.cellCx[c];
                dy = m.faceCy[f] - m.cellCy[c];
            }
            double w = 1.0 / std::max(dx * dx + dy * dy, 1e-300);
            double cx = w * (ixx * dx + ixy * dy);
            double cy = w * (ixy * dx + iyy * dy);
            if (asLeft) { m.lsqLx[f] = cx; m.lsqLy[f] = cy; }
            else { m.lsqRx[f] = cx; m.lsqRy[f] = cy; }
        }
    }
}

} // namespace fv
