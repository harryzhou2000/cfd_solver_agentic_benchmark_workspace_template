/// @file partition.cpp
/// METIS-based domain decomposition implementation.

#include "partition.hpp"
#include "logging.hpp"

#include <metis.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace cfd {

namespace {

/// Sorted node-pair key identifying an undirected edge.
struct EdgeKey {
    std::size_t lo;
    std::size_t hi;

    bool operator==(const EdgeKey& other) const {
        return lo == other.lo && hi == other.hi;
    }
};

struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey& k) const {
        std::size_t h = k.lo + 0x9e3779b97f4a7c15ULL;
        h ^= (k.hi + 0x9e3779b97f4a7c15ULL) + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace

std::vector<Mesh> partition_mesh(const Mesh& full_mesh, int nparts, MPI_Comm /*comm*/) {
    const std::size_t ncells = full_mesh.n_cells();

    // --- 1. Build CSR adjacency for METIS -------------------------------------
    // xadj[i] = start index in adjncy for cell i
    // adjncy  = flattened neighbour indices (1-indexed per METIS convention)
    // METIS expects a symmetric graph: for each edge (i,j) we need both
    // directions.  We also deduplicate so each pair appears only once per
    // direction.
    std::vector<std::set<idx_t>> adj_sets(ncells);
    for (std::size_t i = 0; i < ncells; ++i) {
        for (auto nb : full_mesh.cells[i].neighbors) {
            adj_sets[i].insert(static_cast<idx_t>(nb + 1));  // 1-indexed
        }
    }

    // Ensure each cell has at least one outgoing edge for METIS.
    // If a cell truly has no neighbors (should not happen in a closed mesh),
    // add a self-loop so METIS doesn't choke.
    std::size_t isolated_count = 0;
    for (std::size_t i = 0; i < ncells; ++i) {
        if (adj_sets[i].empty()) {
            adj_sets[i].insert(static_cast<idx_t>(i + 1));  // self-loop
            ++isolated_count;
        }
    }
    if (isolated_count > 0) {
        LOG_WARN("{} cells had no neighbours — added self-loops for METIS", isolated_count);
    }

    std::vector<idx_t> xadj(ncells + 1);
    std::vector<idx_t> adjncy;

    xadj[0] = 0;
    for (std::size_t i = 0; i < ncells; ++i) {
        for (auto nb : adj_sets[i]) {
            adjncy.push_back(nb);
        }
        xadj[i + 1] = static_cast<idx_t>(adjncy.size());
    }

    LOG_INFO("METIS graph: {} vertices, {} edges (avg {:.1f} edges/vertex)",
             ncells, adjncy.size(),
             static_cast<double>(adjncy.size()) / static_cast<double>(ncells));

    // --- 2. Call METIS --------------------------------------------------------
    idx_t nvtxs   = static_cast<idx_t>(ncells);
    idx_t ncon    = 1;
    idx_t nparts_m = static_cast<idx_t>(nparts);
    idx_t objval  = 0;
    std::vector<idx_t> part(ncells);

    // Suppress METIS debug output to stderr
    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_DBGLVL] = 0;

    int ret = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                   nullptr, nullptr, nullptr,
                                   &nparts_m, nullptr, nullptr,
                                   options, &objval, part.data());
    if (ret != METIS_OK) {
        throw std::runtime_error("METIS_PartGraphKway failed with code " + std::to_string(ret));
    }

    LOG_INFO("METIS partitioning complete: {} cells -> {} parts, edge cut = {}",
             ncells, nparts, objval);

    // --- 3. Build per-rank sub-meshes -----------------------------------------
    std::vector<Mesh> sub_meshes(static_cast<std::size_t>(nparts));

    for (int r = 0; r < nparts; ++r) {
        Mesh& local = sub_meshes[static_cast<std::size_t>(r)];
        local.zone_name = full_mesh.zone_name;
        local.cell_dimension = full_mesh.cell_dimension;

        // --- 3a. Collect owned cell indices (global) for this rank ------------
        std::vector<std::size_t> owned_global;
        for (std::size_t gi = 0; gi < ncells; ++gi) {
            if (part[gi] == static_cast<idx_t>(r)) {
                owned_global.push_back(gi);
            }
        }

        // --- 3b. Identify ghost cells (one layer) ------------------------------
        // ghost = neighbour of an owned cell that belongs to another rank
        std::set<std::size_t> ghost_set;
        for (auto gi : owned_global) {
            for (auto nb : full_mesh.cells[gi].neighbors) {
                if (part[nb] != static_cast<idx_t>(r)) {
                    ghost_set.insert(nb);
                }
            }
        }

        // Order: owned first, then ghosts
        std::vector<std::size_t> local_cells_global = owned_global;
        local_cells_global.insert(local_cells_global.end(),
                                  ghost_set.begin(), ghost_set.end());

        const std::size_t n_owned = owned_global.size();
        local.n_owned_cells = n_owned;

        // --- 3c. Global -> local cell index map --------------------------------
        std::unordered_map<std::size_t, std::size_t> global_to_local;
        for (std::size_t li = 0; li < local_cells_global.size(); ++li) {
            global_to_local[local_cells_global[li]] = li;
        }

        // --- 3d. Collect nodes used by local cells -----------------------------
        std::set<std::size_t> local_node_set;
        for (auto gi : local_cells_global) {
            for (auto nid : full_mesh.cells[gi].nodes) {
                local_node_set.insert(nid);
            }
        }

        // Renumber nodes
        std::unordered_map<std::size_t, std::size_t> global_to_local_node;
        {
            std::size_t new_idx = 0;
            for (auto gn : local_node_set) {
                global_to_local_node[gn] = new_idx++;
            }
        }

        // Copy local nodes
        local.nodes.resize(local_node_set.size());
        for (auto gn : local_node_set) {
            std::size_t ln = global_to_local_node[gn];
            local.nodes[ln].coord = full_mesh.nodes[gn].coord;
        }

        // --- 3e. Build local cells (geometry + remapped nodes) -----------------
        local.cells.resize(local_cells_global.size());
        for (std::size_t li = 0; li < local_cells_global.size(); ++li) {
            std::size_t gi = local_cells_global[li];
            const Cell& gc = full_mesh.cells[gi];
            Cell& lc = local.cells[li];

            lc.centroid = gc.centroid;
            lc.volume = gc.volume;
            lc.global_id = gi;

            // Remap nodes
            lc.nodes.resize(gc.nodes.size());
            for (std::size_t j = 0; j < gc.nodes.size(); ++j) {
                lc.nodes[j] = global_to_local_node[gc.nodes[j]];
            }

            // Ghost metadata
            if (li >= n_owned) {
                lc.is_ghost = true;
                lc.owner_rank = static_cast<int>(part[gi]);
            } else {
                lc.is_ghost = false;
                lc.owner_rank = r;
            }
        }

        // --- 3f. Build local faces --------------------------------------------
        // Include:
        //   - Internal faces where BOTH left_cell and right_cell are in local set
        //   - Boundary faces whose interior cell is OWNED by this rank
        // We also need to handle faces connecting owned<->ghost cells.

        // First, build a set of global cell pairs that define internal faces
        // and boundary faces we need.
        std::unordered_map<EdgeKey, std::size_t, EdgeKeyHash> edge_to_local_face;

        // Build internal faces from global faces
        for (std::size_t gfi = 0; gfi < full_mesh.n_faces(); ++gfi) {
            const Face& gf = full_mesh.faces[gfi];

            if (gf.is_boundary) {
                // Only keep if interior cell is owned
                std::size_t ci = (gf.left_cell != Face::INVALID)
                                     ? gf.left_cell : gf.right_cell;
                if (ci == Face::INVALID) continue;
                auto it = global_to_local.find(ci);
                if (it == global_to_local.end()) continue;  // not local
                std::size_t li = it->second;
                if (li >= n_owned) continue;  // not owned

                // Create boundary face
                Face lf;
                lf.nodes = {
                    global_to_local_node[gf.nodes[0]],
                    global_to_local_node[gf.nodes[1]]
                };
                lf.centroid = gf.centroid;
                lf.normal = gf.normal;
                lf.area = gf.area;
                lf.left_cell = li;
                lf.right_cell = Face::INVALID;
                lf.is_boundary = true;
                lf.bc_tag = gf.bc_tag;
                lf.bc_family = gf.bc_family;

                std::size_t lfi = local.faces.size();
                local.faces.push_back(std::move(lf));

                // Register in boundary_faces
                local.boundary_faces[lf.bc_tag].push_back(lfi);

            } else {
                // Internal face: include if both cells are local
                std::size_t gci = gf.left_cell;
                std::size_t gcj = gf.right_cell;
                auto it_i = global_to_local.find(gci);
                auto it_j = global_to_local.find(gcj);
                if (it_i == global_to_local.end() || it_j == global_to_local.end()) {
                    continue;
                }

                std::size_t li = it_i->second;
                std::size_t lj = it_j->second;

                Face lf;
                lf.nodes = {
                    global_to_local_node[gf.nodes[0]],
                    global_to_local_node[gf.nodes[1]]
                };
                lf.centroid = gf.centroid;
                lf.normal = gf.normal;
                lf.area = gf.area;
                lf.left_cell = li;
                lf.right_cell = lj;
                lf.is_boundary = false;
                lf.bc_tag = 0;

                local.faces.push_back(std::move(lf));
            }
        }

        // --- 3g. Build cell->face and cell->neighbor adjacency ----------------
        for (auto& cell : local.cells) {
            cell.faces.clear();
            cell.neighbors.clear();
        }
        for (std::size_t lfi = 0; lfi < local.faces.size(); ++lfi) {
            const Face& lf = local.faces[lfi];
            if (lf.left_cell != Face::INVALID) {
                local.cells[lf.left_cell].faces.push_back(lfi);
            }
            if (lf.right_cell != Face::INVALID) {
                local.cells[lf.right_cell].faces.push_back(lfi);
                local.cells[lf.right_cell].neighbors.push_back(lf.left_cell);
                local.cells[lf.left_cell].neighbors.push_back(lf.right_cell);
            }
        }

        // --- 3h. Compute bounding box -----------------------------------------
        if (!local.nodes.empty()) {
            local.min_coord = local.nodes[0].coord;
            local.max_coord = local.nodes[0].coord;
            for (const auto& node : local.nodes) {
                local.min_coord = local.min_coord.cwiseMin(node.coord);
                local.max_coord = local.max_coord.cwiseMax(node.coord);
            }
        }

        LOG_INFO("  Rank {}: {} owned, {} ghost, {} local faces",
                 r, n_owned, local.cells.size() - n_owned, local.faces.size());
    }

    return sub_meshes;
}

// ============================================================================
// MPI mesh serialization helpers
// ============================================================================

namespace {

/// Pack a size_t vector into a flat buffer.
void pack_vec(const std::vector<std::size_t>& v, std::vector<char>& buf, std::size_t& offset) {
    std::size_t sz = v.size();
    std::memcpy(buf.data() + offset, &sz, sizeof(std::size_t)); offset += sizeof(std::size_t);
    if (sz > 0) {
        std::memcpy(buf.data() + offset, v.data(), sz * sizeof(std::size_t));
        offset += sz * sizeof(std::size_t);
    }
}

/// Unpack a size_t vector from a flat buffer.
void unpack_vec(const std::vector<char>& buf, std::size_t& offset, std::vector<std::size_t>& v) {
    std::size_t sz;
    std::memcpy(&sz, buf.data() + offset, sizeof(std::size_t)); offset += sizeof(std::size_t);
    v.resize(sz);
    if (sz > 0) {
        std::memcpy(v.data(), buf.data() + offset, sz * sizeof(std::size_t));
        offset += sz * sizeof(std::size_t);
    }
}

} // namespace

void send_mesh(const Mesh& mesh, int dest_rank, MPI_Comm comm) {
    // Compute total buffer size first
    std::size_t buf_size = 0;

    // Nodes
    buf_size += sizeof(std::size_t);  // count
    buf_size += mesh.nodes.size() * 2 * sizeof(Real);

    // Cells
    buf_size += sizeof(std::size_t);  // count
    for (const auto& c : mesh.cells) {
        buf_size += 2 * sizeof(Real);           // centroid
        buf_size += sizeof(Real);                // volume
        buf_size += sizeof(std::size_t);         // nodes count + data
        buf_size += c.nodes.size() * sizeof(std::size_t);
        buf_size += sizeof(std::size_t);         // faces count + data
        buf_size += c.faces.size() * sizeof(std::size_t);
        buf_size += sizeof(std::size_t);         // neighbors count + data
        buf_size += c.neighbors.size() * sizeof(std::size_t);
        buf_size += sizeof(std::size_t);         // global_id
        buf_size += sizeof(bool);                // is_ghost
        buf_size += sizeof(int);                 // owner_rank
    }

    // Faces
    buf_size += sizeof(std::size_t);  // count
    for (const auto& f : mesh.faces) {
        buf_size += 2 * sizeof(std::size_t);     // nodes
        buf_size += 2 * sizeof(Real);            // centroid
        buf_size += 2 * sizeof(Real);            // normal
        buf_size += sizeof(Real);                // area
        buf_size += sizeof(std::size_t);         // left_cell
        buf_size += sizeof(std::size_t);         // right_cell
        buf_size += sizeof(bool);                // is_boundary
        buf_size += sizeof(int);                 // bc_tag
        buf_size += sizeof(std::size_t);         // bc_family length
        buf_size += f.bc_family.size();
    }

    // Boundary faces map
    buf_size += sizeof(std::size_t);  // number of entries
    for (const auto& [tag, faces] : mesh.boundary_faces) {
        buf_size += sizeof(int);              // key
        buf_size += sizeof(std::size_t);      // face count
        buf_size += faces.size() * sizeof(std::size_t);
    }

    // zone_name
    buf_size += sizeof(std::size_t);  // length
    buf_size += mesh.zone_name.size();

    // cell_dimension, n_owned_cells, min_coord, max_coord
    buf_size += sizeof(int) + sizeof(std::size_t) + 4 * sizeof(Real);

    // --- Pack ---
    std::vector<char> buf(buf_size);
    std::size_t offset = 0;

    // Nodes
    std::size_t nnodes = mesh.nodes.size();
    std::memcpy(buf.data() + offset, &nnodes, sizeof(std::size_t)); offset += sizeof(std::size_t);
    for (const auto& n : mesh.nodes) {
        std::memcpy(buf.data() + offset, n.coord.data(), 2 * sizeof(Real));
        offset += 2 * sizeof(Real);
    }

    // Cells
    std::size_t ncells = mesh.cells.size();
    std::memcpy(buf.data() + offset, &ncells, sizeof(std::size_t)); offset += sizeof(std::size_t);
    for (const auto& c : mesh.cells) {
        std::memcpy(buf.data() + offset, c.centroid.data(), 2 * sizeof(Real)); offset += 2 * sizeof(Real);
        std::memcpy(buf.data() + offset, &c.volume, sizeof(Real)); offset += sizeof(Real);
        pack_vec(c.nodes, buf, offset);
        pack_vec(c.faces, buf, offset);
        pack_vec(c.neighbors, buf, offset);
        std::memcpy(buf.data() + offset, &c.global_id, sizeof(std::size_t)); offset += sizeof(std::size_t);
        std::memcpy(buf.data() + offset, &c.is_ghost, sizeof(bool)); offset += sizeof(bool);
        std::memcpy(buf.data() + offset, &c.owner_rank, sizeof(int)); offset += sizeof(int);
    }

    // Faces
    std::size_t nfaces = mesh.faces.size();
    std::memcpy(buf.data() + offset, &nfaces, sizeof(std::size_t)); offset += sizeof(std::size_t);
    for (const auto& f : mesh.faces) {
        std::memcpy(buf.data() + offset, f.nodes.data(), 2 * sizeof(std::size_t)); offset += 2 * sizeof(std::size_t);
        std::memcpy(buf.data() + offset, f.centroid.data(), 2 * sizeof(Real)); offset += 2 * sizeof(Real);
        std::memcpy(buf.data() + offset, f.normal.data(), 2 * sizeof(Real)); offset += 2 * sizeof(Real);
        std::memcpy(buf.data() + offset, &f.area, sizeof(Real)); offset += sizeof(Real);
        std::memcpy(buf.data() + offset, &f.left_cell, sizeof(std::size_t)); offset += sizeof(std::size_t);
        std::memcpy(buf.data() + offset, &f.right_cell, sizeof(std::size_t)); offset += sizeof(std::size_t);
        std::memcpy(buf.data() + offset, &f.is_boundary, sizeof(bool)); offset += sizeof(bool);
        std::memcpy(buf.data() + offset, &f.bc_tag, sizeof(int)); offset += sizeof(int);
        std::size_t fam_len = f.bc_family.size();
        std::memcpy(buf.data() + offset, &fam_len, sizeof(std::size_t)); offset += sizeof(std::size_t);
        if (fam_len > 0) {
            std::memcpy(buf.data() + offset, f.bc_family.data(), fam_len); offset += fam_len;
        }
    }

    // Boundary faces
    std::size_t nbf = mesh.boundary_faces.size();
    std::memcpy(buf.data() + offset, &nbf, sizeof(std::size_t)); offset += sizeof(std::size_t);
    for (const auto& [tag, faces] : mesh.boundary_faces) {
        std::memcpy(buf.data() + offset, &tag, sizeof(int)); offset += sizeof(int);
        pack_vec(faces, buf, offset);
    }

    // zone_name
    std::size_t zn_len = mesh.zone_name.size();
    std::memcpy(buf.data() + offset, &zn_len, sizeof(std::size_t)); offset += sizeof(std::size_t);
    if (zn_len > 0) {
        std::memcpy(buf.data() + offset, mesh.zone_name.data(), zn_len); offset += zn_len;
    }

    // Scalar fields
    std::memcpy(buf.data() + offset, &mesh.cell_dimension, sizeof(int)); offset += sizeof(int);
    std::memcpy(buf.data() + offset, &mesh.n_owned_cells, sizeof(std::size_t)); offset += sizeof(std::size_t);
    std::memcpy(buf.data() + offset, mesh.min_coord.data(), 2 * sizeof(Real)); offset += 2 * sizeof(Real);
    std::memcpy(buf.data() + offset, mesh.max_coord.data(), 2 * sizeof(Real)); offset += 2 * sizeof(Real);

    // Send buffer size + data
    std::size_t total_size = buf.size();
    MPI_Send(&total_size, 1, MPI_UNSIGNED_LONG, dest_rank, 0, comm);
    MPI_Send(buf.data(), static_cast<int>(total_size), MPI_CHAR, dest_rank, 1, comm);
}

Mesh recv_mesh(int src_rank, MPI_Comm comm) {
    // Receive buffer size
    std::size_t total_size = 0;
    MPI_Recv(&total_size, 1, MPI_UNSIGNED_LONG, src_rank, 0, comm, MPI_STATUS_IGNORE);

    std::vector<char> buf(total_size);
    MPI_Recv(buf.data(), static_cast<int>(total_size), MPI_CHAR, src_rank, 1, comm, MPI_STATUS_IGNORE);

    Mesh mesh;
    std::size_t offset = 0;

    // Nodes
    std::size_t nnodes;
    std::memcpy(&nnodes, buf.data() + offset, sizeof(std::size_t)); offset += sizeof(std::size_t);
    mesh.nodes.resize(nnodes);
    for (auto& n : mesh.nodes) {
        std::memcpy(n.coord.data(), buf.data() + offset, 2 * sizeof(Real));
        offset += 2 * sizeof(Real);
    }

    // Cells
    std::size_t ncells;
    std::memcpy(&ncells, buf.data() + offset, sizeof(std::size_t)); offset += sizeof(std::size_t);
    mesh.cells.resize(ncells);
    for (auto& c : mesh.cells) {
        std::memcpy(c.centroid.data(), buf.data() + offset, 2 * sizeof(Real)); offset += 2 * sizeof(Real);
        std::memcpy(&c.volume, buf.data() + offset, sizeof(Real)); offset += sizeof(Real);
        unpack_vec(buf, offset, c.nodes);
        unpack_vec(buf, offset, c.faces);
        unpack_vec(buf, offset, c.neighbors);
        std::memcpy(&c.global_id, buf.data() + offset, sizeof(std::size_t)); offset += sizeof(std::size_t);
        std::memcpy(&c.is_ghost, buf.data() + offset, sizeof(bool)); offset += sizeof(bool);
        std::memcpy(&c.owner_rank, buf.data() + offset, sizeof(int)); offset += sizeof(int);
    }

    // Faces
    std::size_t nfaces;
    std::memcpy(&nfaces, buf.data() + offset, sizeof(std::size_t)); offset += sizeof(std::size_t);
    mesh.faces.resize(nfaces);
    for (auto& f : mesh.faces) {
        std::memcpy(f.nodes.data(), buf.data() + offset, 2 * sizeof(std::size_t)); offset += 2 * sizeof(std::size_t);
        std::memcpy(f.centroid.data(), buf.data() + offset, 2 * sizeof(Real)); offset += 2 * sizeof(Real);
        std::memcpy(f.normal.data(), buf.data() + offset, 2 * sizeof(Real)); offset += 2 * sizeof(Real);
        std::memcpy(&f.area, buf.data() + offset, sizeof(Real)); offset += sizeof(Real);
        std::memcpy(&f.left_cell, buf.data() + offset, sizeof(std::size_t)); offset += sizeof(std::size_t);
        std::memcpy(&f.right_cell, buf.data() + offset, sizeof(std::size_t)); offset += sizeof(std::size_t);
        std::memcpy(&f.is_boundary, buf.data() + offset, sizeof(bool)); offset += sizeof(bool);
        std::memcpy(&f.bc_tag, buf.data() + offset, sizeof(int)); offset += sizeof(int);
        std::size_t fam_len;
        std::memcpy(&fam_len, buf.data() + offset, sizeof(std::size_t)); offset += sizeof(std::size_t);
        if (fam_len > 0) {
            f.bc_family.assign(buf.data() + offset, buf.data() + offset + fam_len);
            offset += fam_len;
        }
    }

    // Boundary faces
    std::size_t nbf;
    std::memcpy(&nbf, buf.data() + offset, sizeof(std::size_t)); offset += sizeof(std::size_t);
    for (std::size_t i = 0; i < nbf; ++i) {
        int tag;
        std::memcpy(&tag, buf.data() + offset, sizeof(int)); offset += sizeof(int);
        std::vector<std::size_t> faces;
        unpack_vec(buf, offset, faces);
        mesh.boundary_faces[tag] = std::move(faces);
    }

    // zone_name
    std::size_t zn_len;
    std::memcpy(&zn_len, buf.data() + offset, sizeof(std::size_t)); offset += sizeof(std::size_t);
    if (zn_len > 0) {
        mesh.zone_name.assign(buf.data() + offset, buf.data() + offset + zn_len);
        offset += zn_len;
    }

    // Scalar fields
    std::memcpy(&mesh.cell_dimension, buf.data() + offset, sizeof(int)); offset += sizeof(int);
    std::memcpy(&mesh.n_owned_cells, buf.data() + offset, sizeof(std::size_t)); offset += sizeof(std::size_t);
    std::memcpy(mesh.min_coord.data(), buf.data() + offset, 2 * sizeof(Real)); offset += 2 * sizeof(Real);
    std::memcpy(mesh.max_coord.data(), buf.data() + offset, 2 * sizeof(Real)); offset += 2 * sizeof(Real);

    return mesh;
}

} // namespace cfd
