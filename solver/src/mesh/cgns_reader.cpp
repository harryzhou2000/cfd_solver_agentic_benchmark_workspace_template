#include "mesh/cgns_reader.hpp"
#include "cgnslib.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cfd {

// Sentinel for boundary face (no right cell) lives in types.hpp as INVALID_INDEX

// --- CGNS error handling ---
static void check_cg_error(int ier, const char* msg) {
    if (ier != CG_OK) {
        std::string errmsg(msg);
        const char* cgns_msg = cg_get_error();
        if (cgns_msg != nullptr && cgns_msg[0] != '\0') {
            errmsg += ": ";
            errmsg += cgns_msg;
        }
        throw std::runtime_error(errmsg);
    }
}

// --- Helper: cross product in 2D (z-component of 3D cross) ---
// (polygon_centroid / polygon_area live in mesh.cpp next to Mesh::finalize)

// --- Determine element type from CGNS section type ---
enum class ElemType { Tri, Quad, Unknown };

static ElemType get_elem_type(ElementType_t cgns_type) {
    switch (cgns_type) {
        case TRI_3:  return ElemType::Tri;
        case QUAD_4: return ElemType::Quad;
        default:     return ElemType::Unknown;
    }
}

static Int nodes_per_elem(ElemType t) {
    switch (t) {
        case ElemType::Tri:  return 3;
        case ElemType::Quad: return 4;
        default: return 0;
    }
}

// --- Face key for deduplication ---
// We use a sorted pair of vertex indices. For boundary faces we include
// a marker to keep them separate from internal faces.
struct FaceKey {
    Int v0, v1;
    bool operator==(const FaceKey& o) const {
        return (v0 == o.v0 && v1 == o.v1) || (v0 == o.v1 && v1 == o.v0);
    }
};

struct FaceKeyHash {
    std::size_t operator()(const FaceKey& k) const {
        // Order-independent hash: hash sorted pair
        Int a = std::min(k.v0, k.v1);
        Int b = std::max(k.v0, k.v1);
        return std::hash<Int>()(a) ^ (std::hash<Int>()(b) << 1);
    }
};

// --- Read a single CGNS zone ---
struct RawZone {
    std::string name;
    std::vector<Vec2> coords;    // vertex coordinates
    // Volume elements: cell_id -> vertex indices (zone-local, 0-based)
    std::vector<std::vector<Int>> cell_verts;
    // CGNS element id (1-based, zone-global) -> zone-local vertex pair,
    // for all BAR_2 boundary face elements in the zone.
    std::map<cgsize_t, std::pair<Int, Int>> elem_to_bc_face;
    // ZoneBC entries: family name -> list of zone-local vertex pairs
    struct BcEntry {
        std::string family_name;
        std::vector<std::pair<Int, Int>> faces;
    };
    std::vector<BcEntry> bcs;
    // 1-to-1 connectivity: conn_name -> { donor_zone, donor_pts, receiver_pts }
    struct ConnInfo {
        std::string donor_zone;
        std::vector<Int> donor_pts;    // vertex indices in donor zone
        std::vector<Int> receiver_pts; // vertex indices in this zone
    };
    std::map<std::string, ConnInfo> connections;
};

static RawZone read_single_zone(int fn, int B, int Z) {
    RawZone zone;

    // Zone type and name
    ZoneType_t zone_type;
    char zone_name[128];
    cgsize_t sizes[9];
    check_cg_error(cg_zone_type(fn, B, Z, &zone_type),
                   "cg_zone_type");
    check_cg_error(cg_zone_read(fn, B, Z, zone_name, sizes),
                   "cg_zone_read");

    zone.name = zone_name;
    Int n_verts = static_cast<Int>(sizes[0]);  // VertexSize
    // sizes[1] is the CellSize; for unstructured zones the real cell count
    // comes from the element sections.

    std::cout << "  Zone '" << zone.name << "': " << n_verts << " vertices"
              << std::endl;

    // Read coordinates (stored as RealDouble / R8 in the repo meshes)
    int n_coords;
    check_cg_error(cg_ncoords(fn, B, Z, &n_coords), "cg_ncoords");

    zone.coords.resize(n_verts, Vec2{0, 0});
    for (int c = 1; c <= n_coords; ++c) {
        DataType_t dt;
        char coord_name[128];
        check_cg_error(cg_coord_info(fn, B, Z, c, &dt, coord_name),
                       "cg_coord_info");

        std::vector<double> buf(n_verts);
        cgsize_t rmin = 1, rmax = static_cast<cgsize_t>(n_verts);
        check_cg_error(cg_coord_read(fn, B, Z, coord_name, RealDouble,
                                      &rmin, &rmax, buf.data()),
                       "cg_coord_read");

        if (std::strcmp(coord_name, "CoordinateX") == 0) {
            for (Int i = 0; i < n_verts; ++i)
                zone.coords[i][0] = static_cast<Real>(buf[i]);
        } else if (std::strcmp(coord_name, "CoordinateY") == 0) {
            for (Int i = 0; i < n_verts; ++i)
                zone.coords[i][1] = static_cast<Real>(buf[i]);
        }
        // CoordinateZ is ignored (2D case)
    }

    // Read sections (element sets)
    int n_sections;
    check_cg_error(cg_nsections(fn, B, Z, &n_sections), "cg_nsections");

    struct SectionInfo {
        int section_idx;
        std::string name;
        ElementType_t etype;
        cgsize_t start, end;
        int nbndry;
        int parent_flag;
        bool is_volume;
    };
    std::vector<SectionInfo> sections;

    for (int S = 1; S <= n_sections; ++S) {
        char sname[128];
        ElementType_t etype;
        cgsize_t start, end;
        int nbndry, parent_flag;
        check_cg_error(cg_section_read(fn, B, Z, S, sname, &etype,
                                        &start, &end, &nbndry, &parent_flag),
                       "cg_section_read");

        SectionInfo si;
        si.section_idx = S;
        si.name = sname;
        si.etype = etype;
        si.start = start;
        si.end = end;
        si.nbndry = nbndry;
        si.parent_flag = parent_flag;

        // Volume elements: TRI_3, QUAD_4. Boundary faces: BAR_2 line elements.
        ElemType et = get_elem_type(etype);
        si.is_volume = (et == ElemType::Tri || et == ElemType::Quad);
        sections.push_back(si);
    }

    // Read volume elements
    Int total_cells = 0;
    for (auto& si : sections) {
        if (si.is_volume) {
            total_cells += static_cast<Int>(si.end - si.start + 1);
        }
    }

    // Pre-allocate cell_verts
    zone.cell_verts.reserve(total_cells);

    for (auto& si : sections) {
        if (!si.is_volume) continue;

        ElemType et = get_elem_type(si.etype);
        Int npe = nodes_per_elem(et);
        cgsize_t count = si.end - si.start + 1;

        // Read element connectivity for this section (CGNS 4.x API:
        // per-section calls with a section index)
        cgsize_t data_size;
        check_cg_error(cg_ElementDataSize(fn, B, Z, si.section_idx, &data_size),
                       "cg_ElementDataSize");
        assert(data_size == count * static_cast<cgsize_t>(npe));

        std::vector<cgsize_t> conn(data_size);
        check_cg_error(cg_elements_read(fn, B, Z, si.section_idx,
                                         conn.data(), nullptr),
                       "cg_elements_read");

        // Convert from 1-based CGNS to 0-based internal indexing
        for (cgsize_t i = 0; i < count; ++i) {
            std::vector<Int> verts(npe);
            for (Int j = 0; j < npe; ++j) {
                verts[j] = static_cast<Int>(conn[i * npe + j]) - 1;
            }
            zone.cell_verts.push_back(std::move(verts));
        }
    }

    std::cout << "    Volume cells: " << zone.cell_verts.size() << std::endl;

    // Read boundary face sections (BAR_2 elements). Each BAR_2 element is a
    // boundary face; remember the mapping CGNS element id -> vertex pair so
    // that ZoneBC point sets (which reference these element ids) can be
    // resolved to edges later.
    for (auto& si : sections) {
        if (si.is_volume) continue;

        if (si.etype != BAR_2) {
            std::cerr << "  Warning: unexpected boundary element type in section '"
                      << si.name << "'" << std::endl;
            continue;
        }

        cgsize_t count = si.end - si.start + 1;
        cgsize_t data_size;
        check_cg_error(cg_ElementDataSize(fn, B, Z, si.section_idx, &data_size),
                       "cg_ElementDataSize");
        std::vector<cgsize_t> conn(std::max(data_size, count * 2));
        check_cg_error(cg_elements_read(fn, B, Z, si.section_idx,
                                         conn.data(), nullptr),
                       "cg_elements_read");

        for (cgsize_t i = 0; i < count; ++i) {
            Int v0 = static_cast<Int>(conn[i * 2 + 0]) - 1;
            Int v1 = static_cast<Int>(conn[i * 2 + 1]) - 1;
            zone.elem_to_bc_face[si.start + i] = std::make_pair(v0, v1);
        }
        std::cout << "    Boundary section '" << si.name << "': "
                  << count << " faces" << std::endl;
    }

    // Read boundary conditions (ZoneBC). The point set of each BC_t node
    // holds the element ids of the BAR_2 faces it covers; the family name is
    // read after navigating to the BC_t node with cg_goto.
    int n_bocos;
    check_cg_error(cg_nbocos(fn, B, Z, &n_bocos), "cg_nbocos");

    for (int bc = 1; bc <= n_bocos; ++bc) {
        char bc_name[128];
        BCType_t bc_type;
        PointSetType_t pt_set;
        cgsize_t npts;
        int normal_index;
        cgsize_t normal_list_size;
        DataType_t normal_dt;
        int ndataset;
        check_cg_error(cg_boco_info(fn, B, Z, bc, bc_name, &bc_type,
                                     &pt_set, &npts, &normal_index,
                                     &normal_list_size, &normal_dt,
                                     &ndataset),
                       "cg_boco_info");

        // Boundary face ELEMENT ids covered by this BC (1-based)
        std::vector<cgsize_t> pnts(std::max<cgsize_t>(npts, 2));
        check_cg_error(cg_boco_read(fn, B, Z, bc, pnts.data(), nullptr),
                       "cg_boco_read");

        // Family name: navigate to the BC_t node, then cg_famname_read.
        // There is no cg_fambc_read(file, base, zone, bc, ...) API.
        check_cg_error(cg_goto(fn, B, "Zone_t", Z, "ZoneBC_t", 1,
                               "BC_t", bc, "end"),
                       "cg_goto (BC_t)");
        char family_name[128] = "";
        int ier = cg_famname_read(family_name);
        if (ier != CG_OK && ier != CG_NODE_NOT_FOUND) {
            check_cg_error(ier, "cg_famname_read");
        }

        RawZone::BcEntry entry;
        entry.family_name = family_name;

        auto add_element = [&](cgsize_t eid) {
            auto it = zone.elem_to_bc_face.find(eid);
            if (it != zone.elem_to_bc_face.end()) {
                entry.faces.push_back(it->second);
            } else {
                std::cerr << "  Warning: BC '" << bc_name
                          << "' references element " << eid
                          << " which is not a BAR_2 boundary face"
                          << std::endl;
            }
        };

        // For unstructured zones the BC point set holds the element ids of
        // the boundary faces (as returned by cg_boco_read). Files in the
        // wild use either the element-* or the point-* set types for this.
        if (pt_set == ElementRange || pt_set == PointRange) {
            // pnts = {first_element, last_element}
            for (cgsize_t eid = pnts[0]; eid <= pnts[1]; ++eid) {
                add_element(eid);
            }
        } else if (pt_set == ElementList || pt_set == PointList) {
            for (cgsize_t i = 0; i < npts; ++i) {
                add_element(pnts[i]);
            }
        } else {
            std::cerr << "  Warning: BC '" << bc_name
                      << "' has unsupported point-set type "
                      << static_cast<int>(pt_set) << std::endl;
        }

        if (!entry.faces.empty()) {
            std::cout << "    BC '" << bc_name << "' -> family '"
                      << family_name << "', " << entry.faces.size()
                      << " faces" << std::endl;
            zone.bcs.push_back(std::move(entry));
        }
    }

    // Read 1-to-1 zone connectivity (for multi-zone meshes like CylinderB1)
    int n_conns;
    check_cg_error(cg_nconns(fn, B, Z, &n_conns), "cg_nconns");

    for (int c = 1; c <= n_conns; ++c) {
        char conn_name[128];
        GridLocation_t loc;
        GridConnectivityType_t conn_type;
        PointSetType_t pt_set;
        cgsize_t npts;
        char donor_name[128];
        ZoneType_t donor_zonetype;
        PointSetType_t donor_ptset_type;
        DataType_t donor_dt;
        cgsize_t ndata_donor;

        // Full CGNS 4.x signature
        int ier = cg_conn_info(fn, B, Z, c, conn_name, &loc,
                                &conn_type, &pt_set, &npts,
                                donor_name, &donor_zonetype,
                                &donor_ptset_type, &donor_dt,
                                &ndata_donor);
        if (ier != CG_OK) {
            std::cerr << "  Warning: cannot read connection " << c
                      << std::endl;
            continue;   // optional, skip if not available
        }

        if (conn_type != Abutting1to1) continue;

        // Read receiver point list and donor point list
        std::vector<cgsize_t> pnts(std::max<cgsize_t>(npts, 1));
        std::vector<cgsize_t> donor_pts(
            std::max<cgsize_t>(ndata_donor, npts));
        check_cg_error(cg_conn_read(fn, B, Z, c, pnts.data(), donor_dt,
                                     donor_pts.data()),
                       "cg_conn_read");

        RawZone::ConnInfo ci;

        // Donor zone name may carry a path prefix in some files
        std::string donor_zone(donor_name);
        auto slash = donor_zone.find_last_of('/');
        if (slash != std::string::npos) {
            donor_zone = donor_zone.substr(slash + 1);
        }
        ci.donor_zone = donor_zone;

        if (pt_set == PointList) {
            for (cgsize_t i = 0; i < npts; ++i) {
                ci.receiver_pts.push_back(static_cast<Int>(pnts[i]) - 1);
            }
        } else if (pt_set == PointRange) {
            for (cgsize_t v = pnts[0]; v <= pnts[1]; ++v) {
                ci.receiver_pts.push_back(static_cast<Int>(v) - 1);
            }
        } else {
            std::cerr << "  Warning: connection '" << conn_name
                      << "' has unsupported receiver point-set type "
                      << static_cast<int>(pt_set) << std::endl;
            continue;
        }

        if (donor_ptset_type == PointListDonor) {
            for (cgsize_t i = 0; i < ndata_donor; ++i) {
                ci.donor_pts.push_back(static_cast<Int>(donor_pts[i]) - 1);
            }
        } else if (donor_ptset_type == PointRangeDonor) {
            for (cgsize_t v = donor_pts[0]; v <= donor_pts[1]; ++v) {
                ci.donor_pts.push_back(static_cast<Int>(v) - 1);
            }
        } else {
            std::cerr << "  Warning: connection '" << conn_name
                      << "' has unsupported donor point-set type "
                      << static_cast<int>(donor_ptset_type) << std::endl;
            continue;
        }

        if (ci.receiver_pts.size() != ci.donor_pts.size()) {
            std::cerr << "  Warning: connection '" << conn_name
                      << "' receiver/donor point count mismatch"
                      << std::endl;
            continue;
        }

        std::cout << "    Connection '" << conn_name << "' -> donor zone '"
                  << ci.donor_zone << "', " << ci.receiver_pts.size()
                  << " points" << std::endl;
        zone.connections[conn_name] = std::move(ci);
    }

    return zone;
}

// --- Build global mesh from raw zones ---
Mesh read_cgns_mesh(const std::string& filename,
                    const BcMappings& bc_map) {
    int fn;
    check_cg_error(cg_open(filename.c_str(), CG_MODE_READ, &fn),
                   "cg_open");

    // Read all bases
    int n_bases;
    check_cg_error(cg_nbases(fn, &n_bases), "cg_nbases");

    std::vector<RawZone> raw_zones;
    std::map<std::string, int> zone_name_to_idx;   // zone name -> raw_zones index

    for (int B = 1; B <= n_bases; ++B) {
        char base_name[128];
        int cell_dim, phys_dim;
        check_cg_error(cg_base_read(fn, B, base_name, &cell_dim, &phys_dim),
                       "cg_base_read");
        std::cout << "Base '" << base_name << "': cell_dim=" << cell_dim
                  << ", phys_dim=" << phys_dim << std::endl;

        // The reader only understands 2D meshes (TRI_3 / QUAD_4 cells with
        // BAR_2 boundary faces); a 3D file would silently produce garbage.
        // phys_dim may be 2 or 3 (2D extrusion stored in 3D coordinates).
        if (cell_dim != 2) {
            throw std::runtime_error(
                "unsupported mesh dimensionality: cell_dim=" +
                std::to_string(cell_dim) +
                " (only 2D meshes with cell_dim == 2 are supported; "
                "phys_dim may be 2 or 3 for 2D extrusions)");
        }

        int n_zones;
        check_cg_error(cg_nzones(fn, B, &n_zones), "cg_nzones");

        for (int Z = 1; Z <= n_zones; ++Z) {
            RawZone rz = read_single_zone(fn, B, Z);
            zone_name_to_idx[rz.name] = static_cast<int>(raw_zones.size());
            raw_zones.push_back(std::move(rz));
        }
    }

    cg_close(fn);

    // --- Build BC mapping: family name -> BcType ---
    std::unordered_map<std::string, BcType> fam_to_bc;
    for (auto& bm : bc_map) {
        fam_to_bc[bm.family_name] = bm.bc_type;
    }

    // --- Merge all zones into one Mesh ---
    // Strategy:
    // 1. Concatenate all vertices with offset
    // 2. Build per-zone cell -> global cell map
    // 3. Hash every cell edge (sorted vertex pair) -> owning cells
    // 4. Edges with 2 owners become internal faces; edges with 1 owner are
    //    boundary faces, unless they lie on a 1-to-1 zone interface (both
    //    endpoints are connected vertices AND the corresponding donor edge
    //    is also owned by a single cell), in which case the two zone-local
    //    edges collapse into one internal face across the zones.
    // 5. Match boundary faces to BC sections via the BAR_2 element ids read
    //    from the ZoneBC point sets, and assign bc_tag via family->bc_map.

    Int total_verts = 0;
    Int total_cells = 0;
    for (auto& rz : raw_zones) {
        total_verts += rz.coords.size();
        total_cells += rz.cell_verts.size();
    }

    Mesh mesh;
    mesh.vertices.reserve(total_verts);
    mesh.cells.reserve(total_cells);

    // Vertex offset per zone
    std::vector<Int> zone_vert_offset(raw_zones.size(), 0);
    // Cell offset per zone
    std::vector<Int> zone_cell_offset(raw_zones.size(), 0);

    // Append vertices
    for (Int zi = 0; zi < raw_zones.size(); ++zi) {
        zone_vert_offset[zi] = mesh.vertices.size();
        auto& rz = raw_zones[zi];
        for (auto& v : rz.coords) {
            mesh.vertices.push_back(v);
        }
    }

    // Append cells (vertex_ids stores vertex indices; face_ids is reserved
    // for face indices and is filled in finalize())
    for (Int zi = 0; zi < raw_zones.size(); ++zi) {
        zone_cell_offset[zi] = mesh.cells.size();
        auto& rz = raw_zones[zi];
        for (auto& cv : rz.cell_verts) {
            Cell cell;
            for (auto vid : cv) {
                // Remap to global vertex index
                cell.vertex_ids.push_back(vid + zone_vert_offset[zi]);
            }
            mesh.cells.push_back(std::move(cell));
        }
    }

    // --- Build faces from cell edges ---
    // Edge -> (cell_idx, local_edge_idx) map
    struct EdgeInfo {
        Int cell_idx;
        Int local_edge;  // 0,1,2 for tri; 0,1,2,3 for quad
    };

    std::unordered_map<FaceKey, std::vector<EdgeInfo>, FaceKeyHash> edge_map;
    edge_map.reserve(total_cells * 4);

    for (Int ci = 0; ci < mesh.cells.size(); ++ci) {
        auto& verts = mesh.cells[ci].vertex_ids;
        Int nv = verts.size();
        for (Int e = 0; e < nv; ++e) {
            FaceKey key{verts[e], verts[(e + 1) % nv]};
            edge_map[key].push_back({ci, e});
        }
    }

    // --- Zone connectivity: vertex -> donor vertex pairing ---
    // Two vertices that are paired by a 1-to-1 connection are the same
    // physical point; edges between paired vertices are zone interfaces.
    std::unordered_map<Int, Int> vertex_donor;
    std::unordered_set<Int> connected_verts;
    for (Int zi = 0; zi < raw_zones.size(); ++zi) {
        auto& rz = raw_zones[zi];
        for (auto& [cname, ci] : rz.connections) {
            auto dit = zone_name_to_idx.find(ci.donor_zone);
            if (dit == zone_name_to_idx.end()) {
                std::cerr << "  Warning: connection '" << cname
                          << "' donor zone '" << ci.donor_zone
                          << "' not found" << std::endl;
                continue;
            }
            Int dzi = static_cast<Int>(dit->second);

            for (Int i = 0; i < ci.receiver_pts.size(); ++i) {
                Int v_recv = ci.receiver_pts[i] + zone_vert_offset[zi];
                Int v_don  = ci.donor_pts[i]  + zone_vert_offset[dzi];
                vertex_donor[v_recv] = v_don;
                vertex_donor[v_don] = v_recv;
                connected_verts.insert(v_recv);
                connected_verts.insert(v_don);
            }
        }
    }

    // Faces that were already merged across a zone interface
    std::unordered_set<FaceKey, FaceKeyHash> consumed;
    // Vertex pair -> boundary face index (single-owner, non-interface edges)
    std::unordered_map<FaceKey, Int, FaceKeyHash> boundary_face_index;

    mesh.faces.reserve(edge_map.size());

    for (auto& [key, edges] : edge_map) {
        if (consumed.count(key) > 0) continue;

        if (edges.size() == 2) {
            // Internal face
            Face face;
            face.centroid = 0.5 * (mesh.vertices[key.v0] + mesh.vertices[key.v1]);
            Vec2 edge_vec = mesh.vertices[key.v1] - mesh.vertices[key.v0];
            Real len = edge_vec.norm();
            face.normal.area = len;
            // Raw normal candidate from the edge vector (v0 -> v1). The
            // orientation is arbitrary here; finalize() verifies unit length
            // and flips it so it points from left cell to right cell using
            // (cell_right.centroid - cell_left.centroid).dot(normal).
            if (len > 0) {
                face.normal.nx = -edge_vec[1] / len;
                face.normal.ny =  edge_vec[0] / len;
            }

            face.left_cell = edges[0].cell_idx;
            face.right_cell = edges[1].cell_idx;
            face.is_boundary = false;
            face.bc_tag = -1;

            mesh.faces.push_back(std::move(face));
        } else if (edges.size() == 1) {
            // Single-owner edge: zone interface or true boundary
            bool made_interface_face = false;

            auto it0 = vertex_donor.find(key.v0);
            auto it1 = vertex_donor.find(key.v1);
            bool both_connected =
                connected_verts.count(key.v0) > 0 &&
                connected_verts.count(key.v1) > 0 &&
                it0 != vertex_donor.end() && it1 != vertex_donor.end() &&
                !(it0->second == key.v0 && it1->second == key.v1);

            if (both_connected) {
                FaceKey donor_key{it0->second, it1->second};
                auto dit = edge_map.find(donor_key);
                if (dit != edge_map.end() && dit->second.size() == 1 &&
                    consumed.count(donor_key) == 0) {
                    // The corresponding donor edge is also a single-owner
                    // edge: the two zone-local edges are the two sides of
                    // the same interface -> one internal face across zones.
                    Face face;
                    face.centroid =
                        0.5 * (mesh.vertices[key.v0] + mesh.vertices[key.v1]);
                    Vec2 edge_vec =
                        mesh.vertices[key.v1] - mesh.vertices[key.v0];
                    Real len = edge_vec.norm();
                    face.normal.area = len;
                    // Raw normal candidate; orientation fixed in finalize()
                    // to point from the left cell to the right cell.
                    if (len > 0) {
                        face.normal.nx = -edge_vec[1] / len;
                        face.normal.ny =  edge_vec[0] / len;
                    }

                    face.left_cell = edges[0].cell_idx;
                    face.right_cell = dit->second[0].cell_idx;
                    face.is_boundary = false;
                    face.bc_tag = -1;

                    mesh.faces.push_back(std::move(face));
                    consumed.insert(key);
                    consumed.insert(donor_key);
                    made_interface_face = true;
                }
            }

            if (!made_interface_face) {
                // Boundary face
                Face face;
                face.centroid =
                    0.5 * (mesh.vertices[key.v0] + mesh.vertices[key.v1]);

                // Outward normal: use the edge direction in the owning
                // cell's traversal order. The raw normal assumes CCW winding
                // (interior to the left of the traversal); finalize()
                // re-validates the orientation geometrically, so the winding
                // assumption no longer matters.
                Int ci = edges[0].cell_idx;
                auto& verts = mesh.cells[ci].vertex_ids;
                Int nv = verts.size();
                Int e = edges[0].local_edge;
                Int v0 = verts[e];
                Int v1 = verts[(e + 1) % nv];
                Real dx = mesh.vertices[v1][0] - mesh.vertices[v0][0];
                Real dy = mesh.vertices[v1][1] - mesh.vertices[v0][1];
                Real len = std::sqrt(dx * dx + dy * dy);

                if (len > 0) {
                    face.normal.nx =  dy / len;
                    face.normal.ny = -dx / len;
                }
                face.normal.area = len;

                face.left_cell = ci;
                face.right_cell = INVALID_INDEX;
                face.is_boundary = true;
                face.bc_tag = -1;

                Int fi = mesh.faces.size();
                boundary_face_index[key] = fi;
                mesh.faces.push_back(std::move(face));
            }
        } else {
            // > 2 cells sharing one edge: non-manifold mesh
            throw std::runtime_error(
                "non-manifold mesh: edge shared by more than two cells");
        }
    }

    // --- Map boundary faces to BC patches ---
    // Each BC's point set references BAR_2 boundary face elements; resolve
    // those element ids to vertex pairs and tag the matching boundary faces.
    for (Int zi = 0; zi < raw_zones.size(); ++zi) {
        auto& rz = raw_zones[zi];
        Int v_off = zone_vert_offset[zi];

        for (auto& bc : rz.bcs) {
            // Find BcType for this family
            BcType bct = BcType::Unsupported;
            auto fit = fam_to_bc.find(bc.family_name);
            if (fit != fam_to_bc.end()) {
                bct = fit->second;
            } else {
                std::cerr << "  Warning: no bc_map entry for family '"
                          << bc.family_name << "'" << std::endl;
            }

            // Create or find boundary patch
            Int patch_idx = INVALID_INDEX;
            for (Int pi = 0; pi < mesh.boundary_patches.size(); ++pi) {
                if (mesh.boundary_patches[pi].family_name == bc.family_name) {
                    patch_idx = pi;
                    break;
                }
            }
            if (patch_idx == INVALID_INDEX) {
                BoundaryPatch patch;
                patch.family_name = bc.family_name;
                patch.bc_type = bct;
                patch_idx = mesh.boundary_patches.size();
                mesh.boundary_patches.push_back(std::move(patch));
            }

            // Tag each boundary face referenced by this BC
            for (auto& [v0, v1] : bc.faces) {
                FaceKey key{v0 + v_off, v1 + v_off};
                auto bit = boundary_face_index.find(key);
                if (bit == boundary_face_index.end()) {
                    // Not a boundary face (e.g. a zone interface edge that
                    // was merged into an internal face) - skip
                    continue;
                }
                Int fi = bit->second;
                mesh.faces[fi].bc_tag = patch_idx;
                mesh.boundary_patches[patch_idx].face_ids.push_back(fi);
            }
        }
    }

    // --- Validate BC tagging ---
    // Every boundary face must carry a BC tag. Untagged boundary faces are
    // silent holes in the mesh (they would become "no flux" walls in the
    // solver); they also show up when a 1-to-1 zone interface edge failed to
    // merge and was left as a boundary face. Both are hard errors.
    Int tagged_count = 0;
    Int boundary_count = 0;
    for (Int fi = 0; fi < mesh.faces.size(); ++fi) {
        const auto& f = mesh.faces[fi];
        if (!f.is_boundary) continue;
        boundary_count++;
        if (f.bc_tag == -1) {
            throw std::runtime_error(
                "boundary face " + std::to_string(fi) +
                " has no BC tag: the mesh contains boundary faces that no "
                "ZoneBC entry covers (missing family mapping, or a zone "
                "interface edge that failed to merge)");
        }
        tagged_count++;
    }
    if (tagged_count != boundary_count) {
        throw std::runtime_error(
            "BC tagging mismatch: " + std::to_string(tagged_count) +
            " tagged boundary faces but " + std::to_string(boundary_count) +
            " boundary faces total");
    }

    // Finalize mesh (compute centroids, fix normals, build adjacency)
    mesh.finalize();

    return mesh;
}

} // namespace cfd
