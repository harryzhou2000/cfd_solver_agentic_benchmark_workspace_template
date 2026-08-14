#include "mpi_distributor.hpp"
#include <iostream>
#include <algorithm>
#include <set>
#include <map>
#include <numeric>

void distribute_mesh(const Mesh& global_mesh,
                     const std::vector<int>& cell_rank,
                     int nparts,
                     RankMesh& rm,
                     MPI_Comm comm) {
    int rank, size;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    // Identify owned cells for this rank
    std::vector<Index> owned_cells;
    for (Index ic = 0; ic < global_mesh.num_cells; ic++) {
        if (cell_rank[ic] == rank) {
            owned_cells.push_back(ic);
        }
    }

    // Local-to-global mapping
    std::map<Index, Index> global_to_local;
    for (Index i = 0; i < (Index)owned_cells.size(); i++) {
        global_to_local[owned_cells[i]] = i;
    }

    // Copy owned cells
    rm.owned_cells.clear();
    rm.owned_state.resize(owned_cells.size(), StateVector::Zero());
    rm.owned_residual.resize(owned_cells.size(), StateVector::Zero());

    for (Index i = 0; i < (Index)owned_cells.size(); i++) {
        Index gc = owned_cells[i];
        Cell cell = global_mesh.cells[gc];
        // Remap face references to local indices (deferred for now; faces are global)
        rm.owned_cells.push_back(cell);
    }

    // Build face connectivity for owned cells
    // For each face of an owned cell, identify if it's internal (both sides owned,
    // one side ghost, or one boundary) or ghost-boundary
    std::set<FaceID> ghost_faces;
    std::set<FaceID> owned_boundary_faces;

    for (Index ic : owned_cells) {
        for (FaceID fid : global_mesh.cells[ic].faces) {
            const Face& f = global_mesh.faces[fid];
            if (f.right >= 0) {
                // Internal face
                if (cell_rank[f.left] == rank && cell_rank[f.right] == rank) {
                    // Both sides owned → internal
                } else if (cell_rank[f.left] == rank || cell_rank[f.right] == rank) {
                    // One side owned, other side ghost
                    ghost_faces.insert(fid);
                }
            } else {
                // Boundary face
                if (cell_rank[f.left] == rank) {
                    owned_boundary_faces.insert(fid);
                }
            }
        }
    }

    // Identify ghost cells needed
    std::map<FaceID, Index> ghost_map; // global ghost cell -> local ghost index
    std::vector<Index> ghost_global_ids;

    for (FaceID fid : ghost_faces) {
        const Face& f = global_mesh.faces[fid];
        Index gc = (cell_rank[f.left] == rank) ? f.right : f.left;
        if (gc >= 0 && ghost_map.find(gc) == ghost_map.end()) {
            Index li = (Index)ghost_global_ids.size();
            ghost_map[gc] = li;
            ghost_global_ids.push_back(gc);
        }
    }

    // Copy ghost cells
    rm.ghost_cells.reserve(ghost_global_ids.size());
    rm.ghost_state.resize(ghost_global_ids.size(), StateVector::Zero());
    rm.ghost_owner_rank.resize(ghost_global_ids.size(), -1);

    for (Index i = 0; i < (Index)ghost_global_ids.size(); i++) {
        Index gc = ghost_global_ids[i];
        rm.ghost_cells.push_back(global_mesh.cells[gc]);
        rm.ghost_owner_rank[i] = cell_rank[gc];
    }

    // Build face_conn: for each global face that involves owned cells
    // Remap to local indices
    rm.face_conn.clear();
    std::map<FaceID, RankMesh::FaceConn> face_conn_map;

    for (Index ic : owned_cells) {
        Index lc = global_to_local[ic];
        for (FaceID fid : global_mesh.cells[ic].faces) {
            const Face& f = global_mesh.faces[fid];

            RankMesh::FaceConn fc;
            fc.left = -1;
            fc.right = -1;

            if (f.left >= 0) {
                auto it = global_to_local.find(f.left);
                if (it != global_to_local.end()) {
                    fc.left = it->second;
                } else {
                    auto git = ghost_map.find(f.left);
                    if (git != ghost_map.end()) fc.left = -(git->second + 1); // negative = ghost index + 1
                }
            }
            if (f.right >= 0) {
                auto it = global_to_local.find(f.right);
                if (it != global_to_local.end()) {
                    fc.right = it->second;
                } else {
                    auto git = ghost_map.find(f.right);
                    if (git != ghost_map.end()) fc.right = -(git->second + 1);
                }
            }

            // Store only once per face
            face_conn_map[fid] = fc;
        }
    }

    // Build internal_faces array for this rank
    rm.internal_faces.clear();
    for (FaceID fid : ghost_faces) {
        const Face& f = global_mesh.faces[fid];
        rm.internal_faces.push_back(f);
    }
    // Also add owned-owned internal faces
    for (FaceID fid = 0; fid < global_mesh.num_internal_faces; fid++) {
        const Face& f = global_mesh.faces[fid];
        if (cell_rank[f.left] == rank && cell_rank[f.right] == rank) {
            rm.internal_faces.push_back(f);
        }
    }

    // Boundary faces owned by this rank
    rm.internal_boundary_faces.clear();
    rm.wall_face_ids.clear();
    for (FaceID fid : owned_boundary_faces) {
        auto it = global_mesh.boundary_faces.find(fid);
        if (it != global_mesh.boundary_faces.end()) {
            rm.internal_boundary_faces.push_back(it->second);
            if (it->second.bc_type == BCType::SlipWall ||
                it->second.bc_type == BCType::NoSlipAdiabaticWall) {
                rm.wall_face_ids.push_back(fid);
            }
        }
    }

    // Store pointer to global nodes
    rm.global_nodes = &global_mesh.nodes;

    if (rank == 0) {
        std::cout << "Rank " << rank << ": owned=" << rm.owned_cells.size()
                  << " ghost=" << rm.ghost_cells.size()
                  << " internal_faces=" << rm.internal_faces.size()
                  << " boundary_faces=" << rm.internal_boundary_faces.size()
                  << std::endl;
    }
}

void build_neighbor_lists(RankMesh& rm, const Mesh& global_mesh,
                          const std::vector<int>& cell_rank,
                          MPI_Comm comm) {
    int rank, size;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    // Group ghost cells by owner rank
    std::map<int, std::vector<Index>> ghost_by_rank;
    for (Index i = 0; i < (Index)rm.ghost_owner_rank.size(); i++) {
        int owner = rm.ghost_owner_rank[i];
        ghost_by_rank[owner].push_back(i);
    }

    // Determine which of our owned cells are ghosts on other ranks
    std::map<int, std::vector<Index>> send_by_rank;

    // For each neighbor, determine the cells to send
    // Use a communication round to exchange send information
    for (auto& [neighbor_rank, ghost_indices] : ghost_by_rank) {
        // Tell neighbor which global cells we need as ghosts
        std::vector<Index> needed_global(ghost_indices.size());
        for (size_t k = 0; k < ghost_indices.size(); k++) {
            // The ghost cell at local index ghost_indices[k] corresponds to
            // the global cell stored in ghost_cells
            // We need to track the global cell ID
            needed_global[k] = ghost_indices[k]; // placeholder, we store the ghost cell index
        }

        RankMesh::Neighbor neigh;
        neigh.rank = neighbor_rank;
        neigh.ghost_cell_ids = ghost_indices;
        rm.neighbors.push_back(neigh);
    }

    // Build send lists by exchanging ghost requirements
    // For simplicity, send all of our owned cells that the neighbor needs
    // This is done via explicit MPI exchange in halo_exchange

    if (rank == 0) {
        std::cout << "Neighbor lists built: " << rm.neighbors.size()
                  << " neighbors" << std::endl;
    }
}
