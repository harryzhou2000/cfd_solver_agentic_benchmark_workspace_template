#include "partition.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

#include <metis.h>
#include <mpi.h>

namespace cfd {

Partition partition_mesh(const GlobalMesh& mesh, Index n_parts) {
    Partition info;
    info.cell_part.resize(mesh.n_cells, 0);
    info.part_ncells.resize(n_parts, 0);

    if (n_parts <= 1) {
        info.part_ncells[0] = mesh.n_cells;
        info.edge_cut = 0;
        return info;
    }

    using midx = ::idx_t; // METIS 32-bit index
    const Index n = mesh.n_cells;
    if (n > (Index)std::numeric_limits<midx>::max()) {
        throw std::runtime_error("mesh too large for 32-bit METIS");
    }

    std::vector<midx> xadj(n + 1, 0);
    std::vector<midx> adjncy;
    adjncy.reserve(4 * n);
    for (Index i = 0; i < n; i++) {
        std::set<Index> ns;
        for (Index fid : mesh.cells[i].faces) {
            const Face& face = mesh.faces[fid];
            Index nb = -1;
            if (face.left == i && face.right >= 0) nb = face.right;
            if (face.right == i && face.left >= 0) nb = face.left;
            if (nb >= 0) ns.insert(nb);
        }
        for (Index nb : ns) adjncy.push_back((midx)nb);
        xadj[i + 1] = (midx)adjncy.size();
    }

    midx nvtxs = (midx)n;
    midx ncon = 1;
    midx nparts = (midx)n_parts;
    midx objval = 0;
    std::vector<midx> part(n, 0);
    const int result = METIS_PartGraphKway(
        &nvtxs, &ncon, xadj.data(), adjncy.data(), nullptr, nullptr, nullptr,
        &nparts, nullptr, nullptr, nullptr, &objval, part.data());
    if (result != METIS_OK) {
        throw std::runtime_error("METIS partitioning failed with code " +
                                 std::to_string(result));
    }

    info.edge_cut = objval;
    for (Index i = 0; i < n; i++) {
        info.cell_part[i] = part[i];
        info.part_ncells[part[i]]++;
    }
    return info;
}

LocalMesh build_local_mesh(const GlobalMesh& global, const Partition& part,
                           Index rank) {
    LocalMesh local;
    const Index n_global = global.n_cells;

    std::vector<Index> owned;
    for (Index i = 0; i < n_global; i++) {
        if (part.cell_part[i] == rank) owned.push_back(i);
    }
    local.n_owned = (Index)owned.size();
    local.owned_to_global = owned;
    local.global_to_local.assign(n_global, -1);
    for (Index i = 0; i < local.n_owned; i++) local.global_to_local[owned[i]] = i;

    local.owned.reserve(local.n_owned);
    for (Index gid : owned) local.owned.push_back(global.cells[gid]);

    // Ghost cells: owned neighbors owned by other ranks
    std::set<Index> ghost_set;
    std::map<Index, Index> global_to_ghost;
    for (Index gid : owned) {
        for (Index fid : global.cells[gid].faces) {
            const Face& face = global.faces[fid];
            Index nb = -1;
            if (face.left == gid && face.right >= 0) nb = face.right;
            if (face.right == gid && face.left >= 0) nb = face.left;
            if (nb >= 0 && part.cell_part[nb] != rank) ghost_set.insert(nb);
        }
    }
    local.n_ghost = (Index)ghost_set.size();
    local.ghost_to_global.assign(ghost_set.begin(), ghost_set.end());
    local.ghost.reserve(local.n_ghost);
    for (Index i = 0; i < local.n_ghost; i++) {
        const Index gid = local.ghost_to_global[i];
        global_to_ghost[gid] = i;
        local.ghost.push_back(global.cells[gid]);
    }
    local.n_total = local.n_owned + local.n_ghost;

    // Neighbor communication lists
    std::map<Index, std::vector<Index>> rank_send, rank_recv;
    for (Index i = 0; i < local.n_ghost; i++) {
        rank_recv[part.cell_part[local.ghost_to_global[i]]].push_back(local.ghost_to_global[i]);
    }
    for (Index gid : owned) {
        for (Index fid : global.cells[gid].faces) {
            const Face& face = global.faces[fid];
            Index nb = -1;
            if (face.left == gid && face.right >= 0) nb = face.right;
            if (face.right == gid && face.left >= 0) nb = face.left;
            if (nb >= 0 && part.cell_part[nb] != rank) {
                rank_send[part.cell_part[nb]].push_back(gid);
            }
        }
    }

    std::set<Index> nbr_set;
    for (const auto& [r, _] : rank_send) nbr_set.insert(r);
    for (const auto& [r, _] : rank_recv) nbr_set.insert(r);

    local.neighbors.clear();
    for (Index r : nbr_set) {
        LocalMesh::Neighbor nb;
        nb.rank = r;
        auto& sg = rank_send[r];
        auto& rg = rank_recv[r];
        std::sort(sg.begin(), sg.end());
        sg.erase(std::unique(sg.begin(), sg.end()), sg.end());
        nb.send_global = sg;
        nb.recv_global = rg;
        for (Index gid : sg) {
            const Index lid = local.global_to_local[gid];
            if (lid >= 0) nb.send_cells.push_back(lid);
        }
        for (Index gid : rg) {
            auto it = global_to_ghost.find(gid);
            if (it != global_to_ghost.end()) {
                nb.recv_cells.push_back(local.n_owned + it->second);
            }
        }
        local.neighbors.push_back(std::move(nb));
    }

    // Local faces: faces of owned cells, with right-cell local ids.
    local.faces.clear();
    local.n_boundary_faces = 0;
    for (Index i = 0; i < local.n_owned; i++) {
        const Index gid = owned[i];
        for (Index fid : global.cells[gid].faces) {
            const Face& gface = global.faces[fid];
            if (gface.left == gid) {
                local.faces.push_back(gface);
                if (gface.bc_tag != 0) local.n_boundary_faces++;
            } else if (gface.right == gid && gface.left >= 0 &&
                       part.cell_part[gface.left] != rank) {
                Face inverted = gface;
                inverted.left = gface.right;
                inverted.right = gface.left;
                inverted.normal = -gface.normal;
                local.faces.push_back(inverted);
            }
        }
    }

    for (auto& face : local.faces) {
        if (face.left >= 0) face.left = local.global_to_local[face.left];
        if (face.right >= 0) {
            Index lid = local.global_to_local[face.right];
            if (lid < 0) {
                auto it = global_to_ghost.find(face.right);
                if (it != global_to_ghost.end()) lid = local.n_owned + it->second;
            }
            face.right = lid;
        }
    }

    for (auto& cell : local.owned) {
        cell.faces.clear();
        cell.neighbors.clear();
    }
    for (Index lf = 0; lf < (Index)local.faces.size(); lf++) {
        Face& face = local.faces[lf];
        face.id = lf;
        if (face.left >= 0 && face.left < local.n_owned) {
            local.owned[face.left].faces.push_back(lf);
            if (face.right >= 0 && face.right < local.n_total) {
                local.owned[face.left].neighbors.push_back(face.right);
            }
        }
        if (face.right >= 0 && face.right < local.n_owned) {
            local.owned[face.right].faces.push_back(lf);
            if (face.left >= 0 && face.left < local.n_total) {
                local.owned[face.right].neighbors.push_back(face.left);
            }
        }
    }

    return local;
}

} // namespace cfd
