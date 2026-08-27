#include "partition.hpp"
#include <metis.h>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

PartitionInfo partition_mesh(const Mesh& mesh, int nparts) {
    PartitionInfo pinfo;
    int ncells = (int)mesh.cells.size();
    pinfo.cell_partition.resize(ncells, 0);

    if (nparts <= 1) return pinfo;

    std::vector<idx_t> xadj(ncells + 1, 0);
    std::vector<idx_t> adjncy;

    std::map<std::pair<int,int>, std::vector<int>> edge_cells;
    for (int ci = 0; ci < ncells; ci++) {
        auto& cell = mesh.cells[ci];
        int nn = (int)cell.nodes.size();
        for (int k = 0; k < nn; k++) {
            int n0 = cell.nodes[k];
            int n1 = cell.nodes[(k + 1) % nn];
            auto ek = std::make_pair(std::min(n0, n1), std::max(n0, n1));
            edge_cells[ek].push_back(ci);
        }
    }

    std::vector<std::set<int>> neighbors(ncells);
    for (auto& [ek, cells] : edge_cells) {
        for (int i = 0; i < (int)cells.size(); i++) {
            for (int j = i + 1; j < (int)cells.size(); j++) {
                neighbors[cells[i]].insert(cells[j]);
                neighbors[cells[j]].insert(cells[i]);
            }
        }
    }

    for (int ci = 0; ci < ncells; ci++) {
        xadj[ci + 1] = xadj[ci] + (idx_t)neighbors[ci].size();
        for (int nb : neighbors[ci]) adjncy.push_back(nb);
    }

    idx_t nvtxs = ncells;
    idx_t ncon = 1;
    idx_t np = nparts;
    idx_t objval;
    std::vector<idx_t> part(ncells);

    int ret = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                  nullptr, nullptr, nullptr, &np, nullptr,
                                  nullptr, nullptr, &objval, part.data());
    if (ret != METIS_OK) throw std::runtime_error("METIS_PartGraphKway failed");

    for (int i = 0; i < ncells; i++) pinfo.cell_partition[i] = (int)part[i];
    pinfo.edge_cut = (int)objval;
    return pinfo;
}

LocalMesh build_local_mesh(const Mesh& global_mesh,
                           const PartitionInfo& pinfo,
                           int rank, int nranks) {
    LocalMesh lm;
    lm.rank = rank;
    lm.nranks = nranks;
    lm.edge_cut = pinfo.edge_cut;

    int ncells_global = (int)global_mesh.cells.size();

    std::vector<int> owned_cells;
    for (int ci = 0; ci < ncells_global; ci++) {
        if (pinfo.cell_partition[ci] == rank) owned_cells.push_back(ci);
    }

    std::set<int> ghost_global_set;
    for (int ci : owned_cells) {
        for (int fi : global_mesh.cells[ci].faces) {
            auto& f = global_mesh.faces[fi];
            int other = (f.left_cell == ci) ? f.right_cell : f.left_cell;
            if (other >= 0 && pinfo.cell_partition[other] != rank) {
                ghost_global_set.insert(other);
            }
        }
    }
    std::vector<int> ghost_cells(ghost_global_set.begin(), ghost_global_set.end());

    std::map<int, int> global_to_local_cell;
    lm.num_owned = (int)owned_cells.size();
    lm.num_ghost = (int)ghost_cells.size();

    for (int i = 0; i < (int)owned_cells.size(); i++) {
        global_to_local_cell[owned_cells[i]] = i;
        lm.local_to_global_cell.push_back(owned_cells[i]);
    }
    for (int i = 0; i < (int)ghost_cells.size(); i++) {
        global_to_local_cell[ghost_cells[i]] = lm.num_owned + i;
        lm.local_to_global_cell.push_back(ghost_cells[i]);
    }

    std::set<int> node_set;
    for (int gi : lm.local_to_global_cell) {
        for (int n : global_mesh.cells[gi].nodes) node_set.insert(n);
    }
    std::map<int, int> global_to_local_node;
    for (int gn : node_set) {
        int ln = (int)lm.local_to_global_node.size();
        global_to_local_node[gn] = ln;
        lm.local_to_global_node.push_back(gn);
        lm.mesh.nodes.push_back(global_mesh.nodes[gn]);
    }

    int total_local = lm.num_owned + lm.num_ghost;
    lm.mesh.cells.resize(total_local);
    for (int li = 0; li < total_local; li++) {
        int gi = lm.local_to_global_cell[li];
        auto& gc = global_mesh.cells[gi];
        auto& lc = lm.mesh.cells[li];
        lc.global_id = gi;
        lc.is_ghost = (li >= lm.num_owned);
        lc.owner_rank = pinfo.cell_partition[gi];
        lc.nodes.resize(gc.nodes.size());
        for (int k = 0; k < (int)gc.nodes.size(); k++) {
            lc.nodes[k] = global_to_local_node[gc.nodes[k]];
        }
        lc.centroid = gc.centroid;
        lc.volume = gc.volume;
    }

    for (auto& bg : global_mesh.boundary_groups) {
        BoundaryGroup lbg;
        lbg.family_name = bg.family_name;
        lbg.type = bg.type;
        lm.mesh.boundary_groups.push_back(lbg);
    }

    for (int fi = 0; fi < (int)global_mesh.faces.size(); fi++) {
        auto& gf = global_mesh.faces[fi];
        int gl = gf.left_cell, gr = gf.right_cell;
        bool left_local = (gl >= 0 && global_to_local_cell.count(gl));
        bool right_local = (gr >= 0 && global_to_local_cell.count(gr));

        if (!left_local && !right_local) continue;

        Face lf;
        lf.nodes = {global_to_local_node[gf.nodes[0]], global_to_local_node[gf.nodes[1]]};
        lf.midpoint = gf.midpoint;
        lf.normal = gf.normal;
        lf.area = gf.area;
        lf.is_boundary = gf.is_boundary;
        lf.bc_id = gf.bc_id;

        if (left_local) lf.left_cell = global_to_local_cell[gl];
        if (right_local) lf.right_cell = global_to_local_cell.count(gr) ? global_to_local_cell[gr] : -1;

        if (!lf.is_boundary && lf.left_cell >= 0 && lf.right_cell < 0 && !right_local) {
            continue;
        }
        if (!lf.is_boundary && lf.right_cell >= 0 && lf.left_cell < 0 && !left_local) {
            lf.left_cell = lf.right_cell;
            lf.right_cell = -1;
            lf.normal = -lf.normal;
        }

        if (lf.left_cell >= 0) {
            Vec2 c = lm.mesh.cells[lf.left_cell].centroid;
            if ((lf.midpoint - c).dot(lf.normal) < 0) {
                lf.normal = -lf.normal;
            }
        }

        int lfi = (int)lm.mesh.faces.size();
        lm.mesh.faces.push_back(lf);
        if (lf.left_cell >= 0) lm.mesh.cells[lf.left_cell].faces.push_back(lfi);
        if (lf.right_cell >= 0) lm.mesh.cells[lf.right_cell].faces.push_back(lfi);

        if (lf.is_boundary && lf.bc_id >= 0 && lf.bc_id < (int)lm.mesh.boundary_groups.size()) {
            lm.mesh.boundary_groups[lf.bc_id].face_ids.push_back(lfi);
        }
    }

    lm.mesh.num_cells_global = global_mesh.num_cells_global;
    lm.mesh.num_faces_global = global_mesh.num_faces_global;
    lm.mesh.num_owned = lm.num_owned;
    lm.mesh.num_ghost = lm.num_ghost;

    std::map<int, std::pair<std::vector<int>, std::vector<int>>> rank_halos;
    for (int li = 0; li < lm.num_owned; li++) {
        int gi = lm.local_to_global_cell[li];
        for (int gfi : global_mesh.cells[gi].faces) {
            auto& gf = global_mesh.faces[gfi];
            int other_g = (gf.left_cell == gi) ? gf.right_cell : gf.left_cell;
            if (other_g >= 0 && pinfo.cell_partition[other_g] != rank) {
                int other_rank = pinfo.cell_partition[other_g];
                rank_halos[other_rank].first.push_back(li);
            }
        }
    }
    for (int li = lm.num_owned; li < total_local; li++) {
        int gi = lm.local_to_global_cell[li];
        int ghost_rank = pinfo.cell_partition[gi];
        rank_halos[ghost_rank].second.push_back(li);
    }

    for (auto& [nr, sr] : rank_halos) {
        LocalMesh::HaloExchange he;
        he.neighbor_rank = nr;

        auto& send = sr.first;
        std::sort(send.begin(), send.end());
        send.erase(std::unique(send.begin(), send.end()), send.end());
        he.send_cells = send;

        auto& recv = sr.second;
        std::sort(recv.begin(), recv.end());
        recv.erase(std::unique(recv.begin(), recv.end()), recv.end());
        he.recv_cells = recv;

        lm.halos.push_back(he);
        lm.neighbor_ranks.push_back(nr);
    }

    return lm;
}
