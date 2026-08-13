#include "mesh.hpp"
#include <metis.h>
#include <mpi.h>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

namespace cfd {

PartitionInfo partition_mesh_metis(const Mesh& mesh, idx_t n_parts) {
    PartitionInfo info;
    info.cell_part.resize(mesh.n_cells);
    info.part_ncells.resize(n_parts, 0);
    
    // If only one partition, assign everything to rank 0
    if (n_parts <= 1) {
        for (idx_t i = 0; i < mesh.n_cells; i++) {
            info.cell_part[i] = 0;
        }
        info.part_ncells[0] = mesh.n_cells;
        info.edge_cut = 0;
        return info;
    }
    
    // METIS uses 32-bit integers (::idx_t from metis.h)
    using midx_t = ::idx_t;
    
    idx_t n = mesh.n_cells;
    if (n > static_cast<idx_t>(std::numeric_limits<midx_t>::max())) {
        throw std::runtime_error("Mesh too large for 32-bit METIS");
    }
    
    // Build CSR graph for METIS
    std::vector<midx_t> xadj(n + 1, 0);
    std::vector<midx_t> adjncy;
    
    for (idx_t i = 0; i < n; i++) {
        std::set<idx_t> neighbor_set;
        for (idx_t fid : mesh.cells[i].face_ids) {
            const auto& face = mesh.faces[fid];
            idx_t nb = -1;
            if (face.left_cell == i && face.right_cell >= 0) nb = face.right_cell;
            if (face.right_cell == i && face.left_cell >= 0) nb = face.left_cell;
            if (nb >= 0) neighbor_set.insert(nb);
        }
        for (auto nb : neighbor_set) adjncy.push_back(static_cast<midx_t>(nb));
        xadj[i+1] = static_cast<midx_t>(adjncy.size());
    }
    
    midx_t nvtxs = static_cast<midx_t>(n);
    midx_t ncon = 1;
    midx_t nparts = static_cast<midx_t>(n_parts);
    midx_t objval = 0;
    
    std::vector<midx_t> part(n, 0);
    
    // Try first without options
    int result = METIS_PartGraphKway(
        &nvtxs, &ncon, xadj.data(), adjncy.data(),
        nullptr, nullptr, nullptr,
        &nparts, nullptr, nullptr, nullptr,
        &objval, part.data()
    );
    
    if (result != METIS_OK) {
        throw std::runtime_error("METIS partitioning failed with code " + std::to_string(result));
    }
    
    info.edge_cut = static_cast<idx_t>(objval);
    for (idx_t i = 0; i < n; i++) {
        info.cell_part[i] = static_cast<idx_t>(part[i]);
        info.part_ncells[part[i]]++;
    }
    
    return info;
}

void build_local_mesh(const Mesh& global_mesh, const PartitionInfo& part,
                      idx_t rank, LocalMesh& local) {
    
    idx_t n_global = global_mesh.n_cells;
    
    // Identify owned cells
    std::vector<idx_t> owned_cells;
    for (idx_t i = 0; i < n_global; i++) {
        if (part.cell_part[i] == rank) {
            owned_cells.push_back(i);
        }
    }
    
    local.n_owned = owned_cells.size();
    local.owned_to_global = owned_cells;
    local.global_to_local.assign(n_global, -1);
    for (idx_t i = 0; i < local.n_owned; i++) {
        local.global_to_local[owned_cells[i]] = i;
    }
    
    // Copy owned cells
    local.owned_cells.reserve(local.n_owned);
    for (idx_t gid : owned_cells) {
        local.owned_cells.push_back(global_mesh.cells[gid]);
    }
    
    // Identify ghost cells
    std::map<idx_t, idx_t> global_to_ghost;
    std::set<idx_t> ghost_set;
    
    for (idx_t gid : owned_cells) {
        for (idx_t fid : global_mesh.cells[gid].face_ids) {
            const auto& face = global_mesh.faces[fid];
            idx_t nb = -1;
            if (face.left_cell == gid && face.right_cell >= 0) nb = face.right_cell;
            if (face.right_cell == gid && face.left_cell >= 0) nb = face.left_cell;
            if (nb >= 0 && part.cell_part[nb] != rank) {
                ghost_set.insert(nb);
            }
        }
    }
    
    local.n_ghost = ghost_set.size();
    local.ghost_to_global.assign(ghost_set.begin(), ghost_set.end());
    local.ghost_cells.reserve(local.n_ghost);
    
    for (idx_t i = 0; i < local.n_ghost; i++) {
        idx_t gid = local.ghost_to_global[i];
        global_to_ghost[gid] = i;
        local.ghost_cells.push_back(global_mesh.cells[gid]);
    }
    
    local.n_total = local.n_owned + local.n_ghost;
    
    // Build neighbor communication info
    std::map<idx_t, std::vector<idx_t>> rank_to_send_global;
    std::map<idx_t, std::vector<idx_t>> rank_to_recv_global;
    
    for (idx_t i = 0; i < local.n_ghost; i++) {
        idx_t gid = local.ghost_to_global[i];
        idx_t owner = part.cell_part[gid];
        rank_to_recv_global[owner].push_back(gid);
    }
    
    for (idx_t gid : owned_cells) {
        for (idx_t fid : global_mesh.cells[gid].face_ids) {
            const auto& face = global_mesh.faces[fid];
            idx_t nb = -1;
            if (face.left_cell == gid && face.right_cell >= 0) nb = face.right_cell;
            if (face.right_cell == gid && face.left_cell >= 0) nb = face.left_cell;
            if (nb >= 0 && part.cell_part[nb] != rank) {
                idx_t remote_rank = part.cell_part[nb];
                rank_to_send_global[remote_rank].push_back(gid);
            }
        }
    }
    
    // Build neighbor list
    std::set<idx_t> neighbor_ranks_set;
    for (auto& [r, _] : rank_to_send_global) neighbor_ranks_set.insert(r);
    for (auto& [r, _] : rank_to_recv_global) neighbor_ranks_set.insert(r);
    
    local.neighbors.clear();
    for (idx_t r : neighbor_ranks_set) {
        LocalMesh::NeighborInfo ni;
        ni.rank = r;
        
        auto& sg = rank_to_send_global[r];
        auto& rg = rank_to_recv_global[r];
        
        // Remove duplicates from send list (same cell may touch multiple faces
        // on the same remote rank)
        std::sort(sg.begin(), sg.end());
        sg.erase(std::unique(sg.begin(), sg.end()), sg.end());
        
        ni.send_global = sg;
        ni.recv_global = rg;
        
        for (idx_t gid : sg) {
            idx_t lid = local.global_to_local[gid];
            if (lid >= 0) ni.send_cells.push_back(lid);
        }
        for (idx_t gid : rg) {
            auto it = global_to_ghost.find(gid);
            if (it != global_to_ghost.end()) {
                // Ghost cells are stored after owned cells in the local array
                ni.recv_cells.push_back(local.n_owned + it->second);
            }
        }
        
        local.neighbors.push_back(ni);
    }
    
    // Build local face list
    local.faces.clear();
    local.n_boundary_faces = 0;
    
    for (idx_t i = 0; i < local.n_owned; i++) {
        idx_t gid = owned_cells[i];
        for (idx_t fid : global_mesh.cells[gid].face_ids) {
            const auto& gface = global_mesh.faces[fid];
            // Include face only when processing its left cell
            if (gface.left_cell == gid) {
                local.faces.push_back(gface);
                if (gface.bc_tag != 0) {
                    local.n_boundary_faces++;
                }
            } else if (gface.right_cell == gid && gface.left_cell >= 0 &&
                       part.cell_part[gface.left_cell] != rank) {
                // Partition-interface face: include an inverted copy so both
                // ranks compute the flux across the interface and conserve it.
                Face inverted = gface;
                inverted.left_cell = gface.right_cell;
                inverted.right_cell = gface.left_cell;
                inverted.normal = -gface.normal;
                local.faces.push_back(inverted);
            }
        }
    }
    
    // Remap face connectivity to local indices
    for (auto& face : local.faces) {
        if (face.left_cell >= 0) {
            face.left_cell = local.global_to_local[face.left_cell];
        }
        if (face.right_cell >= 0) {
            idx_t lid = local.global_to_local[face.right_cell];
            if (lid < 0) {
                auto it = global_to_ghost.find(face.right_cell);
                if (it != global_to_ghost.end()) {
                    lid = it->second + local.n_owned;
                }
            }
            face.right_cell = lid;
        }
    }
    
    // Rebuild per-cell face/neighbor references
    for (auto& cell : local.owned_cells) {
        cell.face_ids.clear();
        cell.neighbor_ids.clear();
    }
    
    for (idx_t lfid = 0; lfid < static_cast<idx_t>(local.faces.size()); lfid++) {
        auto& face = local.faces[lfid];
        face.id = lfid;
        
        if (face.left_cell >= 0 && face.left_cell < local.n_owned) {
            local.owned_cells[face.left_cell].face_ids.push_back(lfid);
            if (face.right_cell >= 0 && face.right_cell < local.n_total) {
                local.owned_cells[face.left_cell].neighbor_ids.push_back(face.right_cell);
            }
        }
        if (face.right_cell >= 0 && face.right_cell < local.n_owned) {
            local.owned_cells[face.right_cell].face_ids.push_back(lfid);
            if (face.left_cell >= 0 && face.left_cell < local.n_total) {
                local.owned_cells[face.right_cell].neighbor_ids.push_back(face.left_cell);
            }
        }
    }
}

} // namespace cfd
