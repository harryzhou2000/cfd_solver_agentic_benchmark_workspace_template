#include "solver/mesh_reader.hpp"

#include <cgnslib.h>
#include <fmt/core.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace solver {

namespace {

// Check a CGNS return code and throw with the library error message on failure.
void check_cgns(int ierr, const std::string& context) {
    if (ierr != CG_OK) {
        throw std::runtime_error("CGNS error in " + context + ": " +
                                 cg_get_error());
    }
}

// Map a CGNS boundary condition type to the solver's BC family type string.
// Note: many benchmark meshes mark their bocos as "FamilySpecified" with a
// Null family BC, in which case the authoritative type comes from the case
// configuration's boundary_conditions map.
std::string bc_type_string(BCType_t type) {
    switch (type) {
        case BCFarfield:
            return "farfield";
        case BCWall:
        case BCWallInviscid:
        case BCSymmetryPlane:
            return "slip_wall";
        case BCWallViscous:
        case BCWallViscousHeatFlux:
            return "no_slip_adiabatic_wall";
        case BCWallViscousIsothermal:
            return "isothermal_wall";
        default:
            return "unknown";
    }
}

// Lowercase copy of a string, used for coordinate-name matching.
std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

// Identify the names of the x and y coordinate arrays in the zone.
void find_coordinate_names(int fn, std::string& xname, std::string& yname) {
    int ncoords = 0;
    check_cgns(cg_ncoords(fn, 1, 1, &ncoords), "cg_ncoords");
    for (int c = 1; c <= ncoords; ++c) {
        DataType_t dtype;
        char coordname[33];
        check_cgns(cg_coord_info(fn, 1, 1, c, &dtype, coordname),
                   "cg_coord_info");
        std::string lower = to_lower(coordname);
        if (lower == "x" || lower == "coordinatex") {
            xname = coordname;
        } else if (lower == "y" || lower == "coordinatey") {
            yname = coordname;
        } else if (lower.find('x') != std::string::npos ||
                   lower.find("coordx") != std::string::npos) {
            if (xname.empty()) xname = coordname;
        } else if (lower.find('y') != std::string::npos ||
                   lower.find("coordy") != std::string::npos) {
            if (yname.empty()) yname = coordname;
        }
    }
    // Fallback: assign by position (first unassigned = x, second = y).
    if (xname.empty() || yname.empty()) {
        for (int c = 1; c <= ncoords; ++c) {
            DataType_t dtype;
            char coordname[33];
            check_cgns(cg_coord_info(fn, 1, 1, c, &dtype, coordname),
                       "cg_coord_info");
            if (xname.empty()) {
                xname = coordname;
            } else if (coordname != xname && yname.empty()) {
                yname = coordname;
            }
        }
    }
    if (xname.empty() || yname.empty()) {
        throw std::runtime_error(
            "read_cgns_mesh: could not identify 2D coordinate arrays");
    }
}

struct EdgeKey {
    int a;  // lower node id
    int b;  // higher node id
    bool operator<(const EdgeKey& other) const {
        return a < other.a || (a == other.a && b < other.b);
    }
};

// A boundary edge element from a BAR_2 section.
struct BoundaryEdge {
    int n1;  // node ids (0-based)
    int n2;
};

// Expand a boco point set into a list of 1-based element ids.
// PointRange/ElementRange store [start, end]; PointList/ElementList store
// individual ids.
std::vector<cgsize_t> expand_point_set(PointSetType_t ptset_type,
                                       const cgsize_t* pnts, cgsize_t npnts) {
    std::vector<cgsize_t> ids;
    if (ptset_type == PointRange || ptset_type == ElementRange) {
        if (npnts >= 1) {
            for (cgsize_t e = pnts[0]; e <= pnts[1]; ++e) ids.push_back(e);
        }
    } else if (ptset_type == PointList || ptset_type == ElementList) {
        ids.assign(pnts, pnts + npnts);
    }
    return ids;
}

// Build cells and faces from per-cell node lists plus boundary edge
// elements. Interior faces are shared between two cells; boundary faces are
// either cell edges not shared by another cell or BAR_2 edge elements.
// Returns the face id of each boundary edge element (-1 if orphaned).
std::vector<int> build_topology(Mesh& mesh,
                                std::vector<std::vector<int>>& cell_nodes,
                                const std::vector<BoundaryEdge>& boundary_edges) {
    const int num_cells = static_cast<int>(cell_nodes.size());
    mesh.cells.resize(num_cells);

    // Map from sorted node pair to face index.
    std::map<EdgeKey, int> edge_to_face;

    // Pre-create faces for the boundary edge elements (BAR_2 sections).
    for (const BoundaryEdge& be : boundary_edges) {
        EdgeKey key{std::min(be.n1, be.n2), std::max(be.n1, be.n2)};
        Face face;
        face.cell_ids = {-1, -1};  // attached to its cell later
        face.nodes = {mesh.nodes[be.n1], mesh.nodes[be.n2]};
        face.centroid = Vec2::Zero();
        face.normal = Vec2::Zero();
        face.area = 0.0;
        face.bc_type = -1;
        const int fid = static_cast<int>(mesh.faces.size());
        mesh.faces.push_back(std::move(face));
        edge_to_face[key] = fid;
    }

    for (int c = 0; c < num_cells; ++c) {
        const std::vector<int>& nodes = cell_nodes[c];
        if (nodes.size() < 3) {
            throw std::runtime_error("read_cgns_mesh: cell " +
                                     std::to_string(c) +
                                     " has fewer than 3 nodes");
        }
        const int nn = static_cast<int>(nodes.size());
        for (int i = 0; i < nn; ++i) {
            const int n1 = nodes[i];
            const int n2 = nodes[(i + 1) % nn];
            EdgeKey key{std::min(n1, n2), std::max(n1, n2)};

            auto it = edge_to_face.find(key);
            if (it == edge_to_face.end()) {
                // New face: first cell that uses this edge.
                Face face;
                face.cell_ids = {c, -1};
                face.nodes = {mesh.nodes[n1], mesh.nodes[n2]};
                face.centroid = Vec2::Zero();
                face.normal = Vec2::Zero();
                face.area = 0.0;
                face.bc_type = -1;
                const int fid = static_cast<int>(mesh.faces.size());
                mesh.faces.push_back(std::move(face));
                edge_to_face[key] = fid;
                mesh.cells[c].face_ids.push_back(fid);
            } else {
                // Existing edge: attach this cell to the face.
                const int fid = it->second;
                Face& face = mesh.faces[fid];
                if (face.cell_ids[0] == -1) {
                    face.cell_ids[0] = c;  // BAR_2-created boundary face
                } else if (face.cell_ids[1] == -1) {
                    face.cell_ids[1] = c;  // shared interior face
                } else {
                    throw std::runtime_error(
                        "read_cgns_mesh: edge used by more than two cells "
                        "(non-manifold mesh)");
                }
                mesh.cells[c].face_ids.push_back(fid);
            }
        }
    }

    // Warn about boundary edge elements that matched no cell edge, and
    // record the face id of each boundary edge element.
    std::vector<int> edge_face(boundary_edges.size(), -1);
    for (size_t i = 0; i < boundary_edges.size(); ++i) {
        const BoundaryEdge& be = boundary_edges[i];
        EdgeKey key{std::min(be.n1, be.n2), std::max(be.n1, be.n2)};
        Face& face = mesh.faces[edge_to_face[key]];
        if (face.cell_ids[0] == -1) {
            fmt::print(stderr,
                       "read_cgns_mesh: warning: boundary edge element "
                       "({}, {}) matches no cell edge; dropped\n",
                       be.n1 + 1, be.n2 + 1);
            face.cell_ids = {-1, -2};  // mark as dropped
        } else {
            edge_face[i] = edge_to_face[key];
        }
    }

    mesh.num_cells = num_cells;
    mesh.num_faces = static_cast<int>(mesh.faces.size());
    mesh.num_bnd_faces = 0;
    for (const Face& f : mesh.faces) {
        if (f.cell_ids[1] == -1) mesh.num_bnd_faces++;
    }
    return edge_face;
}

// Read all boundary conditions into BCFamily entries, tagging the faces.
void read_boundary_conditions(int fn, const CaseConfig& config, Mesh& mesh,
                              const std::map<cgsize_t, int>& cell_of_eid,
                              const std::map<cgsize_t, int>& face_of_eid) {
    // 1. Read families (name -> BC type) so that faces can be associated
    //    through the family mechanism.
    std::map<std::string, std::string> family_bc_types;
    int nfamilies = 0;
    check_cgns(cg_nfamilies(fn, 1, &nfamilies), "cg_nfamilies");
    for (int f = 1; f <= nfamilies; ++f) {
        char family_name[33];
        int nfambc = 0;
        int ngeo = 0;
        check_cgns(cg_family_read(fn, 1, f, family_name, &nfambc, &ngeo),
                   "cg_family_read");
        std::string bc_type;
        if (nfambc > 0) {
            char bcname[33];
            BCType_t bctype;
            check_cgns(cg_fambc_read(fn, 1, f, 1, bcname, &bctype),
                       "cg_fambc_read");
            bc_type = bc_type_string(bctype);
        }
        family_bc_types[family_name] = bc_type;
    }

    // 2. Read the zone's boundary conditions (bocos). Each boco carries a
    //    point set of element ids and, optionally, a family name.
    int nbocos = 0;
    check_cgns(cg_nbocos(fn, 1, 1, &nbocos), "cg_nbocos");

    // Family registry: name -> index into mesh.bc_families.
    std::map<std::string, int> family_index;

    for (int b = 1; b <= nbocos; ++b) {
        char boconame[33];
        BCType_t botype;
        PointSetType_t ptset_type;
        cgsize_t npnts = 0;
        int normal_index = 0;
        cgsize_t normal_list_size = 0;
        DataType_t normal_data_type;
        int ndataset = 0;
        check_cgns(cg_boco_info(fn, 1, 1, b, boconame, &botype, &ptset_type,
                                &npnts, &normal_index, &normal_list_size,
                                &normal_data_type, &ndataset),
                   "cg_boco_info");

        // Read the point set. Ranges store [start, end]; lists store ids.
        const cgsize_t alloc = std::max<cgsize_t>(npnts, 2);
        std::vector<cgsize_t> pnts(alloc);
        check_cgns(cg_boco_read(fn, 1, 1, b, pnts.data(), nullptr),
                   "cg_boco_read");
        std::vector<cgsize_t> elements =
            expand_point_set(ptset_type, pnts.data(), npnts);

        // Determine the family name for this boco, if any.
        std::string family_name;
        char famname[33];
        if (cg_goto(fn, 1, "Zone_t", 1, "ZoneBC_t", 1, "BC_t", b, "end") ==
            CG_OK) {
            famname[0] = '\0';
            if (cg_famname_read(famname) == CG_OK && famname[0] != '\0') {
                family_name = famname;
            }
        }
        cg_goto(fn, 1, "Zone_t", 1, "end");  // reset to zone node
        if (family_name.empty()) {
            family_name = boconame;
        }

        // Resolve the BC type: the case configuration is authoritative when
        // it names this family; otherwise fall back to the CGNS family BC
        // and then the boco type.
        std::string bc_type;
        auto cit = config.boundary_conditions.find(family_name);
        if (cit != config.boundary_conditions.end()) {
            bc_type = cit->second;
        } else {
            auto fit = family_bc_types.find(family_name);
            if (fit != family_bc_types.end() && !fit->second.empty() &&
                fit->second != "unknown") {
                bc_type = fit->second;
            } else {
                bc_type = bc_type_string(botype);
            }
        }

        // Register (or fetch) the BCFamily entry.
        int fidx = -1;
        auto it = family_index.find(family_name);
        if (it == family_index.end()) {
            BCFamily fam;
            fam.name = family_name;
            fam.bc_type = bc_type;
            fidx = static_cast<int>(mesh.bc_families.size());
            mesh.bc_families.push_back(std::move(fam));
            family_index[family_name] = fidx;
        } else {
            fidx = it->second;
        }

        // Tag all boundary faces belonging to the listed elements. The
        // element ids may reference boundary edge elements (BAR_2) or
        // volume elements.
        int tagged = 0;
        for (cgsize_t eid : elements) {
            auto feit = face_of_eid.find(eid);
            if (feit != face_of_eid.end()) {
                Face& face = mesh.faces[feit->second];
                if (face.cell_ids[1] == -1 && face.bc_type == -1) {
                    face.bc_type = fidx;
                    mesh.bc_families[fidx].face_ids.push_back(feit->second);
                    tagged++;
                }
                continue;
            }
            auto ceit = cell_of_eid.find(eid);
            if (ceit != cell_of_eid.end()) {
                const int cell = ceit->second;
                for (int fid : mesh.cells[cell].face_ids) {
                    Face& face = mesh.faces[fid];
                    if (face.cell_ids[1] == -1 && face.bc_type == -1) {
                        face.bc_type = fidx;
                        mesh.bc_families[fidx].face_ids.push_back(fid);
                        tagged++;
                    }
                }
            }
        }
        fmt::print("read_cgns_mesh: boco '{}' (family '{}', type '{}'): {} "
                   "faces tagged\n",
                   boconame, family_name, bc_type, tagged);
    }

    // Drop families that ended up with no faces (e.g. unused families).
    mesh.bc_families.erase(
        std::remove_if(mesh.bc_families.begin(), mesh.bc_families.end(),
                       [](const BCFamily& fam) { return fam.face_ids.empty(); }),
        mesh.bc_families.end());

    // Remap face.bc_type indices after pruning.
    std::map<std::string, int> remap;
    for (int i = 0; i < static_cast<int>(mesh.bc_families.size()); ++i) {
        remap[mesh.bc_families[i].name] = i;
    }
    for (Face& face : mesh.faces) {
        if (face.bc_type >= 0 &&
            face.bc_type < static_cast<int>(mesh.bc_families.size())) {
            const std::string& name = mesh.bc_families[face.bc_type].name;
            auto rit = remap.find(name);
            face.bc_type = (rit != remap.end()) ? rit->second : -1;
        }
    }
}

} // namespace

Mesh read_cgns_mesh(const std::string& filepath, const CaseConfig& config) {
    if (!config.mesh.format.empty() &&
        to_lower(config.mesh.format) != "cgns") {
        throw std::runtime_error(
            "read_cgns_mesh: unsupported mesh format '" + config.mesh.format +
            "' (only 'CGNS' is supported)");
    }

    Mesh mesh;
    int fn = 0;

    try {
        // 1. Open the file.
        int ierr = cg_open(filepath.c_str(), CG_MODE_READ, &fn);
        check_cgns(ierr, "cg_open(" + filepath + ")");

        // 2. Read the base.
        int nbases = 0;
        check_cgns(cg_nbases(fn, &nbases), "cg_nbases");
        if (nbases < 1) {
            throw std::runtime_error("read_cgns_mesh: file '" + filepath +
                                     "' contains no bases");
        }
        char basename[33];
        int cell_dim = 0;
        int phys_dim = 0;
        check_cgns(cg_base_read(fn, 1, basename, &cell_dim, &phys_dim),
                   "cg_base_read");
        if (cell_dim != 2) {
            throw std::runtime_error(
                "read_cgns_mesh: base '" + std::string(basename) +
                "' has cell dimension " + std::to_string(cell_dim) +
                "; only 2D meshes are supported");
        }
        if (nbases > 1) {
            fmt::print(stderr,
                       "read_cgns_mesh: warning: file contains {} bases; "
                       "reading base 1 ('{}')\n",
                       nbases, basename);
        }

        // 3. Read the zone (Phase 1: single-zone support; zone 1 only).
        int nzones = 0;
        check_cgns(cg_nzones(fn, 1, &nzones), "cg_nzones");
        if (nzones < 1) {
            throw std::runtime_error("read_cgns_mesh: base '" +
                                     std::string(basename) +
                                     "' contains no zones");
        }
        char zonename[33];
        cgsize_t zone_size[3] = {0, 0, 0};
        check_cgns(cg_zone_read(fn, 1, 1, zonename, zone_size), "cg_zone_read");
        if (nzones > 1) {
            fmt::print(stderr,
                       "read_cgns_mesh: warning: base contains {} zones; "
                       "multi-zone meshes are not supported yet, reading "
                       "zone 1 ('{}')\n",
                       nzones, zonename);
        }

        const cgsize_t num_verts = zone_size[0];
        const cgsize_t num_cells = zone_size[1];

        // 4. Read coordinates.
        std::string xname, yname;
        find_coordinate_names(fn, xname, yname);

        cgsize_t rmin[3] = {1, 1, 1};
        cgsize_t rmax[3] = {num_verts, 1, 1};
        std::vector<double> xs(num_verts);
        std::vector<double> ys(num_verts);
        check_cgns(cg_coord_read(fn, 1, 1, xname.c_str(), RealDouble, rmin,
                                 rmax, xs.data()),
                   "cg_coord_read(" + xname + ")");
        check_cgns(cg_coord_read(fn, 1, 1, yname.c_str(), RealDouble, rmin,
                                 rmax, ys.data()),
                   "cg_coord_read(" + yname + ")");

        mesh.nodes.resize(num_verts);
        for (cgsize_t i = 0; i < num_verts; ++i) {
            mesh.nodes[i] = Vec2(xs[i], ys[i]);
        }

        // 5. Read element sections. Volume elements (TRI_3, QUAD_4, and
        //    3/4-node entries of MIXED sections) become cells; BAR_2
        //    elements are boundary edges.
        int nsections = 0;
        check_cgns(cg_nsections(fn, 1, 1, &nsections), "cg_nsections");
        if (nsections < 1) {
            throw std::runtime_error("read_cgns_mesh: zone '" +
                                     std::string(zonename) +
                                     "' contains no element sections");
        }

        std::vector<std::vector<int>> cell_nodes;
        cell_nodes.reserve(num_cells);
        std::vector<BoundaryEdge> boundary_edges;
        // Global (1-based) element id of each boundary edge element.
        std::vector<cgsize_t> bar_eid;
        // Global element id -> cell index (volume elements only).
        std::map<cgsize_t, int> cell_of_eid;

        for (int s = 1; s <= nsections; ++s) {
            char secname[33];
            ElementType_t elem_type;
            cgsize_t start = 0;
            cgsize_t end = 0;
            int nbndry = 0;
            int parent_flag = 0;
            check_cgns(cg_section_read(fn, 1, 1, s, secname, &elem_type,
                                       &start, &end, &nbndry, &parent_flag),
                       "cg_section_read");

            const cgsize_t nelem = end - start + 1;
            cgsize_t data_size = 0;
            if (elem_type == MIXED) {
                check_cgns(
                    cg_ElementDataSize(fn, 1, 1, s, &data_size),
                    "cg_ElementDataSize");
            } else {
                int npe = 0;
                check_cgns(cg_npe(elem_type, &npe), "cg_npe");
                data_size = nelem * npe;
            }

            std::vector<cgsize_t> elem_data(data_size);
            check_cgns(cg_elements_read(fn, 1, 1, s, elem_data.data(),
                                        nullptr),
                       "cg_elements_read");

            if (elem_type != TRI_3 && elem_type != QUAD_4 &&
                elem_type != BAR_2 && elem_type != MIXED) {
                fmt::print(stderr,
                           "read_cgns_mesh: warning: skipping section '{}' "
                           "with unsupported element type {}\n",
                           secname, cg_ElementTypeName(elem_type));
                continue;
            }

            cgsize_t idx = 0;
            for (cgsize_t e = 0; e < nelem; ++e) {
                const cgsize_t global_eid = start + e;
                ElementType_t et = elem_type;
                if (elem_type == MIXED) {
                    if (idx >= data_size) {
                        throw std::runtime_error(
                            "read_cgns_mesh: truncated MIXED section '" +
                            std::string(secname) + "'");
                    }
                    et = static_cast<ElementType_t>(elem_data[idx++]);
                }
                int npe = 0;
                check_cgns(cg_npe(et, &npe), "cg_npe");
                if (idx + npe > data_size) {
                    throw std::runtime_error(
                        "read_cgns_mesh: truncated element section '" +
                        std::string(secname) + "'");
                }

                if (et == BAR_2 && npe == 2) {
                    BoundaryEdge be;
                    be.n1 = static_cast<int>(elem_data[idx] - 1);
                    be.n2 = static_cast<int>(elem_data[idx + 1] - 1);
                    idx += 2;
                    boundary_edges.push_back(be);
                    bar_eid.push_back(global_eid);
                    continue;
                }
                if (npe != 3 && npe != 4) {
                    throw std::runtime_error(
                        "read_cgns_mesh: unsupported 2D element type " +
                        std::string(cg_ElementTypeName(et)) +
                        " in section '" + std::string(secname) + "'");
                }
                std::vector<int> nodes(npe);
                for (int k = 0; k < npe; ++k) {
                    nodes[k] = static_cast<int>(elem_data[idx++] - 1);  // 1-based
                }
                Cell cell;
                cell.centroid = Vec2::Zero();
                cell.volume = 0.0;
                const int cell_index = static_cast<int>(mesh.cells.size());
                mesh.cells.push_back(std::move(cell));
                cell_nodes.push_back(std::move(nodes));
                cell_of_eid[global_eid] = cell_index;
            }
        }

        if (mesh.cells.size() != static_cast<size_t>(num_cells)) {
            throw std::runtime_error(
                "read_cgns_mesh: zone declares " +
                std::to_string(num_cells) + " cells but sections contain " +
                std::to_string(mesh.cells.size()));
        }

        // 6. Build faces from cell edges and boundary edge elements.
        const std::vector<int> edge_face =
            build_topology(mesh, cell_nodes, boundary_edges);

        // Map boundary edge elements (by global element id) to faces.
        std::map<cgsize_t, int> face_of_eid;
        for (size_t i = 0; i < boundary_edges.size(); ++i) {
            if (edge_face[i] >= 0) face_of_eid[bar_eid[i]] = edge_face[i];
        }

        // 7. Attach boundary conditions.
        read_boundary_conditions(fn, config, mesh, cell_of_eid, face_of_eid);

        // 8. Close the file.
        cg_close(fn);

    } catch (...) {
        if (fn != 0) cg_close(fn);
        throw;
    }

    return mesh;
}

} // namespace solver
