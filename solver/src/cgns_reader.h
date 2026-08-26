#pragma once
#include "types.h"
#include <cgnslib.h>
#include <iostream>
#include <algorithm>
#include <map>
#include <set>
#include <cstring>

struct MergedMesh {
    std::vector<double> x, y;
    std::vector<std::vector<int>> tri_conn;
    std::vector<std::vector<int>> quad_conn;
    struct BCSectionInfo {
        std::string family_name;
        std::vector<std::array<int,2>> bar_conn;
    };
    std::vector<BCSectionInfo> bc_sections;
};

inline MergedMesh read_cgns_mesh(const std::string& filename) {
    int file_id;
    if (cg_open(filename.c_str(), CG_MODE_READ, &file_id) != CG_OK) {
        throw std::runtime_error("Cannot open CGNS file: " + filename);
    }

    int nzones;
    cg_nzones(file_id, 1, &nzones);

    // Accumulate all zone data, merging coincident nodes across zones
    std::vector<double> all_x, all_y;
    std::vector<std::vector<int>> all_tris, all_quads;
    std::vector<MergedMesh::BCSectionInfo> all_bcs;

    // First pass: read BC family names from ZoneBC nodes
    // Build a mapping: for each zone, section_name -> family_name from the ZoneBC
    struct ZoneData {
        int nnodes, ncells;
        std::vector<double> x, y;
        std::map<std::string, std::string> bc_name_to_family;
    };
    std::vector<ZoneData> zones(nzones);

    for (int z = 1; z <= nzones; z++) {
        auto& zd = zones[z-1];
        char zone_name[33];
        cgsize_t sizes[3];
        cg_zone_read(file_id, 1, z, zone_name, sizes);
        zd.nnodes = (int)sizes[0];
        zd.ncells = (int)sizes[1];

        zd.x.resize(zd.nnodes);
        zd.y.resize(zd.nnodes);
        cgsize_t rmin = 1, rmax = zd.nnodes;
        cg_coord_read(file_id, 1, z, "CoordinateX", CGNS_ENUMV(RealDouble), &rmin, &rmax, zd.x.data());
        cg_coord_read(file_id, 1, z, "CoordinateY", CGNS_ENUMV(RealDouble), &rmin, &rmax, zd.y.data());

        // Read BCs to get name -> family mapping
        int nbc = 0;
        cg_nbocos(file_id, 1, z, &nbc);
        for (int b = 1; b <= nbc; b++) {
            char bname[33];
            CGNS_ENUMT(BCType_t) btype;
            CGNS_ENUMT(PointSetType_t) pset_type;
            cgsize_t npnts;
            int norm_idx[3];
            cgsize_t norm_list_size;
            CGNS_ENUMT(DataType_t) norm_dt;
            int ndatasets;
            cg_boco_info(file_id, 1, z, b, bname, &btype, &pset_type, &npnts,
                        norm_idx, &norm_list_size, &norm_dt, &ndatasets);

            // Read family name from BC node
            char fam[33] = {};
            if (cg_goto(file_id, 1, "Zone_t", z, "ZoneBC_t", 1, "BC_t", b, "end") == CG_OK) {
                if (cg_famname_read(fam) != CG_OK) {
                    std::strncpy(fam, bname, 32);
                }
            } else {
                std::strncpy(fam, bname, 32);
            }
            zd.bc_name_to_family[std::string(bname)] = std::string(fam);
        }
    }

    // Second pass: read elements and build connectivity
    int global_node_offset = 0;
    for (int z = 1; z <= nzones; z++) {
        auto& zd = zones[z-1];
        int node_off = global_node_offset;

        // Add nodes
        all_x.insert(all_x.end(), zd.x.begin(), zd.x.end());
        all_y.insert(all_y.end(), zd.y.begin(), zd.y.end());

        // Read element sections
        int nsections;
        cg_nsections(file_id, 1, z, &nsections);

        for (int s = 1; s <= nsections; s++) {
            char sec_name[33];
            CGNS_ENUMT(ElementType_t) etype;
            cgsize_t start, end;
            int nbnd, pflag;
            cg_section_read(file_id, 1, z, s, sec_name, &etype, &start, &end, &nbnd, &pflag);
            int nelem = (int)(end - start + 1);

            if (etype == CGNS_ENUMV(TRI_3)) {
                cgsize_t conn_size;
                cg_ElementDataSize(file_id, 1, z, s, &conn_size);
                std::vector<cgsize_t> conn(conn_size);
                cg_elements_read(file_id, 1, z, s, conn.data(), nullptr);
                for (int i = 0; i < nelem; i++) {
                    all_tris.push_back({
                        (int)conn[i*3+0] - 1 + node_off,
                        (int)conn[i*3+1] - 1 + node_off,
                        (int)conn[i*3+2] - 1 + node_off
                    });
                }
            } else if (etype == CGNS_ENUMV(QUAD_4)) {
                cgsize_t conn_size;
                cg_ElementDataSize(file_id, 1, z, s, &conn_size);
                std::vector<cgsize_t> conn(conn_size);
                cg_elements_read(file_id, 1, z, s, conn.data(), nullptr);
                for (int i = 0; i < nelem; i++) {
                    all_quads.push_back({
                        (int)conn[i*4+0] - 1 + node_off,
                        (int)conn[i*4+1] - 1 + node_off,
                        (int)conn[i*4+2] - 1 + node_off,
                        (int)conn[i*4+3] - 1 + node_off
                    });
                }
            } else if (etype == CGNS_ENUMV(BAR_2)) {
                // Check if this section name matches a BC name
                std::string sname(sec_name);
                auto it = zd.bc_name_to_family.find(sname);
                if (it == zd.bc_name_to_family.end()) {
                    continue; // connectivity section, not a BC
                }

                cgsize_t conn_size;
                cg_ElementDataSize(file_id, 1, z, s, &conn_size);
                std::vector<cgsize_t> conn(conn_size);
                cg_elements_read(file_id, 1, z, s, conn.data(), nullptr);

                MergedMesh::BCSectionInfo bci;
                bci.family_name = it->second;
                for (int i = 0; i < nelem; i++) {
                    bci.bar_conn.push_back({
                        (int)conn[i*2+0] - 1 + node_off,
                        (int)conn[i*2+1] - 1 + node_off
                    });
                }
                all_bcs.push_back(bci);
            }
        }

        global_node_offset += zd.nnodes;
    }

    cg_close(file_id);

    MergedMesh mm;

    if (nzones == 1) {
        mm.x = all_x;
        mm.y = all_y;
        mm.tri_conn = all_tris;
        mm.quad_conn = all_quads;
        mm.bc_sections = all_bcs;
    } else {
        // Merge coincident nodes between zones
        int total_nodes = (int)all_x.size();
        std::vector<int> node_map(total_nodes);
        for (int i = 0; i < total_nodes; i++) node_map[i] = i;

        double tol = 1e-10;

        // Simple O(n) merge using spatial hash
        struct PairHash {
            size_t operator()(std::pair<long long, long long> p) const {
                return std::hash<long long>()(p.first) ^ (std::hash<long long>()(p.second) << 32);
            }
        };
        std::unordered_map<std::pair<long long, long long>, std::vector<int>, PairHash> spatial;
        double inv_tol = 1.0 / (tol * 100);

        for (int i = 0; i < total_nodes; i++) {
            long long ix = (long long)(all_x[i] * inv_tol);
            long long iy = (long long)(all_y[i] * inv_tol);
            bool found = false;
            for (long long dx = -1; dx <= 1 && !found; dx++) {
                for (long long dy = -1; dy <= 1 && !found; dy++) {
                    auto key = std::make_pair(ix + dx, iy + dy);
                    auto it = spatial.find(key);
                    if (it != spatial.end()) {
                        for (int j : it->second) {
                            if (node_map[j] == j) {
                                double dist2 = (all_x[i]-all_x[j])*(all_x[i]-all_x[j]) +
                                               (all_y[i]-all_y[j])*(all_y[i]-all_y[j]);
                                if (dist2 < tol*tol && i != j) {
                                    node_map[i] = j;
                                    found = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            spatial[{ix, iy}].push_back(i);
        }

        // Compact
        std::vector<int> compact(total_nodes, -1);
        int new_count = 0;
        for (int i = 0; i < total_nodes; i++) {
            int root = i;
            while (node_map[root] != root) root = node_map[root];
            node_map[i] = root;
            if (compact[root] == -1) compact[root] = new_count++;
            compact[i] = compact[root];
        }

        mm.x.resize(new_count);
        mm.y.resize(new_count);
        for (int i = 0; i < total_nodes; i++) {
            mm.x[compact[i]] = all_x[i];
            mm.y[compact[i]] = all_y[i];
        }

        for (auto& t : all_tris)
            mm.tri_conn.push_back({compact[t[0]], compact[t[1]], compact[t[2]]});
        for (auto& q : all_quads)
            mm.quad_conn.push_back({compact[q[0]], compact[q[1]], compact[q[2]], compact[q[3]]});
        for (auto& bc : all_bcs) {
            MergedMesh::BCSectionInfo bci;
            bci.family_name = bc.family_name;
            for (auto& bar : bc.bar_conn)
                bci.bar_conn.push_back({compact[bar[0]], compact[bar[1]]});
            mm.bc_sections.push_back(bci);
        }
    }

    return mm;
}
