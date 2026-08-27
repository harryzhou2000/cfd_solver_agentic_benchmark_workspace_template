#include "Partitioner.hpp"
#include <metis.h>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <cassert>
#include <cmath>

LocalMesh buildLocalMesh(const GlobalMesh& gm, MPI_Comm comm) {
    int rank, n_ranks;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &n_ranks);

    int n_cells = gm.n_cells;
    std::vector<int> partition(n_cells, 0);
    int edge_cut = 0;

    if (n_ranks == 1) {
        // No partitioning needed
        std::fill(partition.begin(), partition.end(), 0);
        edge_cut = 0;
    } else if (rank == 0) {
        // Build cell adjacency graph for METIS
        // METIS uses CSR format: xadj[i+1]-xadj[i] = number of neighbors of cell i
        std::vector<idx_t> xadj(n_cells+1, 0);
        // Count neighbors
        for (int c = 0; c < n_cells; c++)
            xadj[c+1] = (idx_t)gm.cell_neighbors[c].size();
        for (int c = 0; c < n_cells; c++)
            xadj[c+1] += xadj[c];

        idx_t total_adj = xadj[n_cells];
        std::vector<idx_t> adjncy(total_adj);
        int pos = 0;
        for (int c = 0; c < n_cells; c++) {
            for (int nb : gm.cell_neighbors[c]) {
                adjncy[pos++] = (nb >= 0) ? (idx_t)nb : -1;
            }
        }
        // Remove boundary (-1) neighbors from adjacency
        // METIS adjacency should only have valid cell neighbors
        std::vector<idx_t> xadj2(n_cells+1, 0);
        std::vector<idx_t> adjncy2;
        for (int c = 0; c < n_cells; c++) {
            xadj2[c] = (idx_t)adjncy2.size();
            for (int nb : gm.cell_neighbors[c]) {
                if (nb >= 0) adjncy2.push_back((idx_t)nb);
            }
        }
        xadj2[n_cells] = (idx_t)adjncy2.size();

        idx_t nvtxs = (idx_t)n_cells;
        idx_t ncon = 1;
        idx_t nparts = (idx_t)n_ranks;
        idx_t objval = 0;
        std::vector<idx_t> part(n_cells, 0);

        idx_t options[METIS_NOPTIONS];
        METIS_SetDefaultOptions(options);
        options[METIS_OPTION_SEED] = 42;
        options[METIS_OPTION_NUMBERING] = 0;

        int ret = METIS_PartGraphKway(
            &nvtxs, &ncon, xadj2.data(), adjncy2.data(),
            nullptr, nullptr, nullptr, &nparts, nullptr, nullptr,
            options, &objval, part.data());

        if (ret != METIS_OK)
            throw std::runtime_error("METIS_PartGraphKway failed");

        edge_cut = (int)objval;
        for (int i = 0; i < n_cells; i++) partition[i] = (int)part[i];

        std::cout << "[METIS] Partition edge cut: " << edge_cut << std::endl;
    }

    // Broadcast partition to all ranks
    MPI_Bcast(partition.data(), n_cells, MPI_INT, 0, comm);
    MPI_Bcast(&edge_cut, 1, MPI_INT, 0, comm);

    // Build local mesh for this rank
    LocalMesh lm;
    lm.rank    = rank;
    lm.n_ranks = n_ranks;
    lm.partition_edge_cut = edge_cut;

    // Owned cells: all cells assigned to this rank
    std::vector<int> owned_cells;
    for (int c = 0; c < n_cells; c++)
        if (partition[c] == rank) owned_cells.push_back(c);
    lm.n_owned = (int)owned_cells.size();

    // Build global->local mapping for owned cells
    std::unordered_map<int,int> global_to_local;
    for (int i = 0; i < lm.n_owned; i++)
        global_to_local[owned_cells[i]] = i;

    // Find ghost cells: neighbors of owned cells that belong to other ranks
    std::vector<int> ghost_cells;
    std::set<int> ghost_set;
    for (int c : owned_cells) {
        for (int nb : gm.cell_neighbors[c]) {
            if (nb >= 0 && partition[nb] != rank && ghost_set.find(nb) == ghost_set.end()) {
                ghost_set.insert(nb);
                ghost_cells.push_back(nb);
            }
        }
    }
    lm.n_ghost = (int)ghost_cells.size();

    // Assign local ids to ghost cells
    int ghost_start = lm.n_owned;
    for (int i = 0; i < lm.n_ghost; i++)
        global_to_local[ghost_cells[i]] = ghost_start + i;

    int n_local = lm.n_owned + lm.n_ghost;

    // Store global cell ids
    lm.global_cell_id.resize(n_local);
    for (int i = 0; i < lm.n_owned; i++) lm.global_cell_id[i] = owned_cells[i];
    for (int i = 0; i < lm.n_ghost; i++) lm.global_cell_id[lm.n_owned+i] = ghost_cells[i];

    // Build local node mapping
    std::unordered_map<int,int> node_global_to_local;
    std::vector<int> local_nodes;
    auto getOrAddNode = [&](int gn) {
        auto it = node_global_to_local.find(gn);
        if (it != node_global_to_local.end()) return it->second;
        int ln = (int)local_nodes.size();
        local_nodes.push_back(gn);
        node_global_to_local[gn] = ln;
        return ln;
    };

    // Add all nodes from local cells
    for (int i = 0; i < n_local; i++) {
        int gc = lm.global_cell_id[i];
        for (int gn : gm.cell_nodes[gc]) getOrAddNode(gn);
    }

    lm.n_nodes_local = (int)local_nodes.size();
    lm.x.resize(lm.n_nodes_local);
    lm.y.resize(lm.n_nodes_local);
    for (int i = 0; i < lm.n_nodes_local; i++) {
        lm.x[i] = gm.x[local_nodes[i]];
        lm.y[i] = gm.y[local_nodes[i]];
    }

    // Cell node lists (local ids)
    lm.cell_nodes_local.resize(n_local);
    for (int i = 0; i < n_local; i++) {
        int gc = lm.global_cell_id[i];
        lm.cell_nodes_local[i].clear();
        for (int gn : gm.cell_nodes[gc])
            lm.cell_nodes_local[i].push_back(node_global_to_local[gn]);
    }

    // Cell geometry
    lm.cell_cx.resize(n_local); lm.cell_cy.resize(n_local); lm.cell_vol.resize(n_local);
    for (int i = 0; i < n_local; i++) {
        int gc = lm.global_cell_id[i];
        lm.cell_cx[i]  = gm.cell_cx[gc];
        lm.cell_cy[i]  = gm.cell_cy[gc];
        lm.cell_vol[i] = gm.cell_vol[gc];
    }

    // Build local faces: faces connecting at least one owned cell
    std::vector<bool> face_added(gm.n_faces, false);
    for (int f = 0; f < gm.n_interior_faces; f++) {
        int L = gm.face_left[f], R = gm.face_right[f];
        bool L_local = global_to_local.count(L) > 0;
        bool R_local = global_to_local.count(R) > 0;
        bool L_owned = L_local && global_to_local[L] < lm.n_owned;
        bool R_owned = R_local && global_to_local[R] < lm.n_owned;
        if (!L_local && !R_local) continue;
        if (!L_owned && !R_owned) continue;
        // Include face if at least one side is owned
        if (!L_local || !R_local) continue;  // skip if either side not in local mesh

        int fid_local = (int)lm.face_left_local.size();
        face_added[f] = true;
        int lL = global_to_local[L], lR = global_to_local[R];
        lm.face_left_local.push_back(lL);
        lm.face_right_local.push_back(lR);
        lm.face_nx.push_back(gm.face_nx[f]);
        lm.face_ny.push_back(gm.face_ny[f]);
        lm.face_area.push_back(gm.face_area[f]);
        lm.face_cx.push_back(gm.face_cx[f]);
        lm.face_cy.push_back(gm.face_cy[f]);
        // Local face nodes
        std::array<int,2> fn_loc;
        fn_loc[0] = node_global_to_local.count(gm.face_nodes[f][0]) ?
                    node_global_to_local[gm.face_nodes[f][0]] : 0;
        fn_loc[1] = node_global_to_local.count(gm.face_nodes[f][1]) ?
                    node_global_to_local[gm.face_nodes[f][1]] : 0;
        lm.face_nodes_local.push_back(fn_loc);
    }
    lm.n_faces_local = (int)lm.face_left_local.size();

    // Boundary faces
    for (int bf = 0; bf < gm.n_boundary_faces; bf++) {
        int f = gm.bface_face_id[bf];
        int cell = gm.bface_cell[bf];
        auto gl_it = global_to_local.find(cell);
        if (gl_it == global_to_local.end()) continue;
        int lc = gl_it->second;
        if (lc >= lm.n_owned) continue;  // ghost cell boundary - skip

        // Add boundary face to local mesh
        int bfid_local = (int)lm.face_left_local.size();
        lm.face_left_local.push_back(lc);
        lm.face_right_local.push_back(-1);
        lm.face_nx.push_back(gm.face_nx[f]);
        lm.face_ny.push_back(gm.face_ny[f]);
        lm.face_area.push_back(gm.face_area[f]);
        lm.face_cx.push_back(gm.face_cx[f]);
        lm.face_cy.push_back(gm.face_cy[f]);
        std::array<int,2> fn_loc;
        fn_loc[0] = node_global_to_local.count(gm.face_nodes[f][0]) ?
                    node_global_to_local[gm.face_nodes[f][0]] : 0;
        fn_loc[1] = node_global_to_local.count(gm.face_nodes[f][1]) ?
                    node_global_to_local[gm.face_nodes[f][1]] : 0;
        lm.face_nodes_local.push_back(fn_loc);

        lm.bface_face_id.push_back(bfid_local);
        lm.bface_type.push_back(gm.bface_type[bf]);
        lm.bface_family.push_back(gm.bface_family[bf]);
    }
    lm.n_bfaces = (int)lm.bface_family.size();

    // Build MPI halo exchange info
    // For each other rank: cells this rank needs to send (owned that are ghost on other rank)
    // and cells this rank needs to receive (owned on other rank, ghost here)
    std::map<int, std::set<int>> send_sets, recv_sets;
    for (int i = 0; i < lm.n_ghost; i++) {
        int gc = ghost_cells[i];
        int other = partition[gc];
        recv_sets[other].insert(lm.n_owned + i);
    }
    // For each owned cell, check which ranks have it as a ghost
    // We need to figure out what other ranks need from us
    // Simple approach: for each ghost cell, the owner rank needs
    // the cells adjacent to that ghost (which are owned here)
    for (int i = 0; i < lm.n_owned; i++) {
        int gc = owned_cells[i];
        for (int nb : gm.cell_neighbors[gc]) {
            if (nb >= 0 && partition[nb] != rank) {
                send_sets[partition[nb]].insert(i);
            }
        }
    }

    // Build HaloInfo structs
    std::set<int> neighbor_rank_set;
    for (auto& [r, _] : send_sets) neighbor_rank_set.insert(r);
    for (auto& [r, _] : recv_sets) neighbor_rank_set.insert(r);

    for (int nr : neighbor_rank_set) {
        LocalMesh::HaloInfo h;
        h.neighbor_rank = nr;
        if (send_sets.count(nr))
        {
            // Sort send cells by GLOBAL cell id for consistent ordering across ranks
            std::vector<int> sends(send_sets[nr].begin(), send_sets[nr].end());
            std::sort(sends.begin(), sends.end(), [&](int a, int b) {
                return owned_cells[a] < owned_cells[b];
            });
            h.send_cells = sends;
        }
        if (recv_sets.count(nr))
        {
            // Sort recv (ghost) cells by GLOBAL cell id to match sender's ordering
            std::vector<int> recvs(recv_sets[nr].begin(), recv_sets[nr].end());
            std::sort(recvs.begin(), recvs.end(), [&](int a, int b) {
                int ia = a - lm.n_owned, ib = b - lm.n_owned;
                return ghost_cells[ia] < ghost_cells[ib];
            });
            h.recv_cells = recvs;
        }
        lm.halos.push_back(h);
        lm.neighbor_ranks.push_back(nr);
    }

    // Build per-cell face lists for efficient neighbor traversal
    int n_total_local = lm.n_owned + lm.n_ghost;
    int total_faces_local = (int)lm.face_left_local.size();
    lm.cell_face_ids_local.assign(n_total_local, {});
    lm.cell_nbrs_local_int.assign(n_total_local, {});
    for (int f = 0; f < total_faces_local; f++) {
        int L = lm.face_left_local[f], R = lm.face_right_local[f];
        if (L >= 0 && L < n_total_local) lm.cell_face_ids_local[L].push_back(f);
        if (R >= 0 && R < n_total_local) lm.cell_face_ids_local[R].push_back(f);
        if (R >= 0 && f < lm.n_faces_local) {  // interior face
            if (L < n_total_local) lm.cell_nbrs_local_int[L].push_back(R);
            if (R < n_total_local) lm.cell_nbrs_local_int[R].push_back(L);
        }
    }

    std::cout << "[Rank " << rank << "] Local mesh: " << lm.n_owned
              << " owned, " << lm.n_ghost << " ghost, "
              << lm.n_faces_local << " interior faces, "
              << lm.n_bfaces << " boundary faces" << std::endl;

    return lm;
}
