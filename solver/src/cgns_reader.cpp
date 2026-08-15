/// @file cgns_reader.cpp
/// Implementation of the CGNS mesh reader with multi-zone merging.
///
/// Reads every zone of the first base of a 2-D unstructured CGNS mesh and
/// merges them into a single conformal `cfd::Mesh` by connecting zone
/// interfaces declared as vertex-based Abutting1to1 connectivity
/// (GridConnectivity_t nodes with PointList/PointListDonor).
///
/// NOTE on the connectivity API: the benchmark meshes (e.g. CylinderB1.cgns)
/// store their interfaces as *generic* GridConnectivity_t nodes, which are
/// NOT counted by cg_n1to1() (it returns 0 for these files). They are read
/// with cg_nconns()/cg_conn_info()/cg_conn_read() instead. The receiver and
/// donor point lists are matched element-by-element (the donor list is
/// typically traversed in the opposite direction, which the element-by-element
/// pairing absorbs), so no transform handling is required.

#include "cgns_reader.hpp"

#include <cgnslib.h>

#include "logging.hpp"

#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

namespace cfd {

namespace {

/// Standard CGNS name buffer length (not exported by this CGNS version).
constexpr std::size_t kMaxNameLength = 32;

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

[[noreturn]] void cgns_fail(const std::string& what) {
    // cg_get_error() returns the message of the most recent CGNS error.
    const char* msg = cg_get_error();
    throw std::runtime_error(what + (msg && msg[0] ? std::string(": ") + msg : ""));
}

void check_cgns(int err, const std::string& what) {
    if (err != CG_OK) cgns_fail(what);
}

bool is_cell_section(ElementType_t type) {
    switch (type) {
        case CGNS_ENUMV(TRI_3):
        case CGNS_ENUMV(QUAD_4):
        case CGNS_ENUMV(NGON_n):
            return true;
        default:
            return false;
    }
}

/// One element section of a zone, with its connectivity already read.
struct SectionData {
    int index = 0;              ///< CGNS section number (1-based)
    std::string name;
    ElementType_t type = CGNS_ENUMV(MIXED);
    cgsize_t start = 0;         ///< first global element index (1-based)
    cgsize_t end = 0;           ///< last global element index (1-based)
    std::vector<cgsize_t> elements;   ///< raw connectivity (per node, 1-based)
    std::vector<cgsize_t> offsets;    ///< per-element start offsets (NGON_n only)
    bool is_cell = false;       ///< elements are cells (not faces)
};

/// Read connectivity of one section into `sd`.
void read_section_connectivity(int fn, int B, int Z, SectionData& sd) {
    int nbndry = 0;
    int parent_flag = 0;
    // NOTE: cg_section_read writes *nbndry and *parent_flag unconditionally.
    check_cgns(cg_section_read(fn, B, Z, sd.index, sd.name.data(), &sd.type, &sd.start,
                               &sd.end, &nbndry, &parent_flag),
               "cg_section_read");

    cgsize_t data_size = 0;
    check_cgns(cg_ElementDataSize(fn, B, Z, sd.index, &data_size), "cg_ElementDataSize");

    if (sd.type == CGNS_ENUMV(NGON_n)) {
        const cgsize_t n_elem = sd.end - sd.start + 1;
        sd.offsets.resize(static_cast<std::size_t>(n_elem) + 1);
        sd.elements.resize(static_cast<std::size_t>(data_size));
        // cg_poly_elements_read reads the connectivity and the per-element
        // start offsets (0-based cumulative); parent data is not needed.
        check_cgns(cg_poly_elements_read(fn, B, Z, sd.index, sd.elements.data(),
                                         sd.offsets.data(), nullptr),
                   "cg_poly_elements_read");
    } else {
        sd.elements.resize(static_cast<std::size_t>(data_size));
        check_cgns(cg_elements_read(fn, B, Z, sd.index, sd.elements.data(), nullptr),
                   "cg_elements_read");
    }
}

/// Look up the family name of a BC: the FamilyName_t child node of the BC_t
/// node; falls back to the BC (section) name when absent.
std::string read_bc_family_name(int fn, int B, int Z, int bc, const std::string& bc_name) {
    // Navigate to the BC_t node under ZoneBC_t.
    const int err = cg_goto(fn, B, "Zone_t", Z, "ZoneBC_t", 1, "BC_t", bc, nullptr);
    if (err == CG_OK) {
        char family[kMaxNameLength + 1] = "";
        if (cg_famname_read(family) == CG_OK && family[0] != '\0') {
            return std::string(family);
        }
    }
    return bc_name;  // no FamilyName node: use the BC name
}

/// Expand a point set (PointRange/PointRangeDonor or PointList/PointListDonor)
/// into a flat index list. Returns 1-based indices.
std::vector<cgsize_t> expand_point_set(PointSetType_t ptset_type, const cgsize_t* pnts,
                                       cgsize_t npnts) {
    std::vector<cgsize_t> out;
    if (ptset_type == CGNS_ENUMV(PointRange) || ptset_type == CGNS_ENUMV(PointRangeDonor)) {
        if (npnts >= 2) {
            const cgsize_t first = pnts[0];
            const cgsize_t last = pnts[1];
            if (last < first) {
                throw std::runtime_error("Point range is inverted: [" +
                                         std::to_string(first) + ", " + std::to_string(last) + "]");
            }
            out.reserve(static_cast<std::size_t>(last - first + 1));
            for (cgsize_t e = first; e <= last; ++e) out.push_back(e);
        }
    } else if (ptset_type == CGNS_ENUMV(PointList) ||
               ptset_type == CGNS_ENUMV(PointListDonor)) {
        out.assign(pnts, pnts + npnts);
    } else {
        throw std::runtime_error("Unsupported point-set type (only PointRange/PointList)");
    }
    return out;
}

// ---------------------------------------------------------------------------
// Per-zone raw data (before merging)
// ---------------------------------------------------------------------------

struct ZoneData {
    std::string name;
    std::vector<Vec2> coords;                 ///< zone-local, 0-based
    std::vector<std::vector<std::size_t>> cells;  ///< zone-local node ids, 0-based

    struct BocoFace {
        std::size_t a = 0;    ///< first node (zone-local, 0-based)
        std::size_t b = 0;    ///< second node (zone-local, 0-based)
        int tag = 0;          ///< bc_tag (BoundaryType int)
        std::string family;   ///< boundary family name
    };
    std::vector<BocoFace> boco_faces;
};

/// Read one zone: coordinates, cell connectivity, and boundary faces.
///
/// Only elements referenced by a BC (via the ZoneBC point sets) become
/// boundary faces. BAR_2 sections that are not referenced by any BC
/// (e.g. "con-*" zone-interface sections) are read but never instantiated
/// as faces — they become internal faces after zone merging.
ZoneData read_zone(int fn, int B, int Z,
                   const std::unordered_map<std::string, std::string>& bc_map) {
    ZoneData zd;

    char zone_name[kMaxNameLength + 1] = "";
    cgsize_t zone_size[3] = {0, 0, 0};
    check_cgns(cg_zone_read(fn, B, Z, zone_name, zone_size), "cg_zone_read");
    zd.name = zone_name;

    ZoneType_t zt = CGNS_ENUMV(Unstructured);
    check_cgns(cg_zone_type(fn, B, Z, &zt), "cg_zone_type");
    if (zt != CGNS_ENUMV(Unstructured)) {
        throw std::runtime_error("Zone '" + zd.name + "' is not unstructured");
    }

    const cgsize_t n_nodes = zone_size[0];

    // --- Coordinates -----------------------------------------------------
    {
        std::vector<Real> x(static_cast<std::size_t>(n_nodes));
        std::vector<Real> y(static_cast<std::size_t>(n_nodes));
        const cgsize_t rmin[1] = {1};
        const cgsize_t rmax[1] = {n_nodes};
        check_cgns(cg_coord_read(fn, B, Z, "CoordinateX", CGNS_ENUMV(RealDouble), rmin, rmax,
                                 x.data()),
                   "cg_coord_read(CoordinateX)");
        check_cgns(cg_coord_read(fn, B, Z, "CoordinateY", CGNS_ENUMV(RealDouble), rmin, rmax,
                                 y.data()),
                   "cg_coord_read(CoordinateY)");

        zd.coords.resize(static_cast<std::size_t>(n_nodes));
        for (cgsize_t i = 0; i < n_nodes; ++i) {
            zd.coords[static_cast<std::size_t>(i)] << x[static_cast<std::size_t>(i)],
                y[static_cast<std::size_t>(i)];
        }
    }

    // --- Sections ---------------------------------------------------------
    int nsections = 0;
    check_cgns(cg_nsections(fn, B, Z, &nsections), "cg_nsections");

    std::vector<SectionData> sections;
    sections.reserve(static_cast<std::size_t>(nsections));
    cgsize_t max_elem_index = 0;

    for (int s = 1; s <= nsections; ++s) {
        SectionData sd;
        sd.index = s;
        sd.name.resize(kMaxNameLength + 1);
        read_section_connectivity(fn, B, Z, sd);
        sd.name.resize(std::strlen(sd.name.c_str()));
        sd.is_cell = is_cell_section(sd.type);

        if (sd.is_cell || sd.type == CGNS_ENUMV(BAR_2)) {
            sections.push_back(std::move(sd));
            max_elem_index = std::max(max_elem_index, sd.end);
        } else {
            LOG_WARN("Skipping unsupported element section '{}' (type {})", sd.name,
                     static_cast<int>(sd.type));
        }
    }

    // Map global (1-based) element index -> section entry.
    std::vector<int> elem_section(static_cast<std::size_t>(max_elem_index) + 1, -1);
    for (std::size_t si = 0; si < sections.size(); ++si) {
        const auto& sd = sections[si];
        for (cgsize_t e = sd.start; e <= sd.end; ++e) {
            elem_section[static_cast<std::size_t>(e)] = static_cast<int>(si);
        }
    }

    // --- Cells --------------------------------------------------------------
    for (const auto& sd : sections) {
        if (!sd.is_cell) continue;

        const cgsize_t n_elem = sd.end - sd.start + 1;
        for (cgsize_t e = 0; e < n_elem; ++e) {
            std::vector<std::size_t> cell_nodes;
            const std::size_t base = static_cast<std::size_t>(e);

            if (sd.type == CGNS_ENUMV(NGON_n)) {
                const cgsize_t off0 = sd.offsets[base];
                const cgsize_t off1 = sd.offsets[base + 1];
                cell_nodes.reserve(static_cast<std::size_t>(off1 - off0));
                for (cgsize_t k = off0; k < off1; ++k) {
                    cell_nodes.push_back(
                        static_cast<std::size_t>(sd.elements[static_cast<std::size_t>(k)] - 1));
                }
            } else {
                const int npe = (sd.type == CGNS_ENUMV(TRI_3)) ? 3 : 4;
                cell_nodes.reserve(static_cast<std::size_t>(npe));
                for (int k = 0; k < npe; ++k) {
                    cell_nodes.push_back(
                        static_cast<std::size_t>(sd.elements[base * static_cast<std::size_t>(npe) +
                                                             static_cast<std::size_t>(k)] -
                                                 1));
                }
            }
            zd.cells.push_back(std::move(cell_nodes));
        }
    }

    // --- Boundary conditions -------------------------------------------------
    int nbocos = 0;
    check_cgns(cg_nbocos(fn, B, Z, &nbocos), "cg_nbocos");

    for (int bc = 1; bc <= nbocos; ++bc) {
        char bc_name[kMaxNameLength + 1] = "";
        BCType_t bc_type = CGNS_ENUMV(BCTypeNull);
        PointSetType_t ptset_type = CGNS_ENUMV(PointList);
        cgsize_t npnts = 0;
        cgsize_t normal_list_size = 0;
        DataType_t normal_data_type = CGNS_ENUMV(DataTypeNull);
        int ndataset = 0;

        check_cgns(cg_boco_info(fn, B, Z, bc, bc_name, &bc_type, &ptset_type, &npnts,
                                nullptr, &normal_list_size, &normal_data_type, &ndataset),
                   "cg_boco_info");

        std::vector<cgsize_t> pnts(static_cast<std::size_t>(npnts));
        if (npnts > 0) {
            check_cgns(cg_boco_read(fn, B, Z, bc, pnts.data(), nullptr), "cg_boco_read");
        }

        const std::string family = read_bc_family_name(fn, B, Z, bc, bc_name);
        const std::vector<cgsize_t> elem_indices =
            expand_point_set(ptset_type, pnts.data(), npnts);

        // Map the family name through the case's boundary_conditions map to a
        // tag (0 = farfield, 1 = slip_wall, 2 = no_slip_adiabatic_wall).
        int bc_tag = boundary_type_to_tag(BoundaryType::Unknown);
        auto it = bc_map.find(family);
        if (it != bc_map.end()) {
            bc_tag = boundary_type_to_tag(boundary_type_from_string(it->second));
        } else {
            LOG_WARN("Boundary family '{}' (BC '{}') in zone '{}' is not present in the case's "
                     "boundary_conditions map; tagging as Unknown",
                     family, bc_name, zd.name);
        }

        std::size_t n_created = 0;
        for (cgsize_t elem : elem_indices) {
            if (elem <= 0 || static_cast<std::size_t>(elem) >= elem_section.size()) {
                LOG_WARN("BC '{}' in zone '{}' references element {} outside the zone; skipping",
                         bc_name, zd.name, elem);
                continue;
            }
            const int si = elem_section[static_cast<std::size_t>(elem)];
            if (si < 0) {
                LOG_WARN("BC '{}' in zone '{}' references element {} in an unsupported section; "
                         "skipping",
                         bc_name, zd.name, elem);
                continue;
            }
            const SectionData& sd = sections[static_cast<std::size_t>(si)];
            if (sd.is_cell) {
                LOG_WARN("BC '{}' in zone '{}' references element {} which is a cell, not a face; "
                         "skipping",
                         bc_name, zd.name, elem);
                continue;
            }
            const std::size_t local =
                static_cast<std::size_t>(elem - sd.start) * 2;  // BAR_2: 2 nodes per face
            if (local + 1 >= sd.elements.size()) {
                LOG_WARN("BC '{}' in zone '{}' references element {} with unexpected "
                         "connectivity; skipping",
                         bc_name, zd.name, elem);
                continue;
            }

            ZoneData::BocoFace bf;
            bf.a = static_cast<std::size_t>(sd.elements[local] - 1);
            bf.b = static_cast<std::size_t>(sd.elements[local + 1] - 1);
            bf.tag = bc_tag;
            bf.family = family;
            zd.boco_faces.push_back(bf);
            ++n_created;
        }

        LOG_INFO("Zone '{}' BC '{}' (family '{}', tag {}): {} boundary faces", zd.name, bc_name,
                 family, bc_tag, n_created);
    }

    return zd;
}

/// Build the vertex remap table of `zone Z` (zone-local 0-based vertex ->
/// merged mesh node index, or Face::INVALID if the vertex is not on any
/// interface) from the zone's Abutting1to1 connections to already-merged
/// donor zones.
///
/// `zone_index_by_name` maps zone names to CGNS zone numbers; `zone_node_merged`
/// holds, for each already-merged zone, the merged index of every one of its
/// vertices.
std::vector<std::size_t> build_vertex_remap(
    int fn, int B, int Z, std::size_t n_zone_nodes,
    const std::map<std::string, int>& zone_index_by_name,
    const std::vector<std::vector<std::size_t>>& zone_node_merged) {
    std::vector<std::size_t> remap(n_zone_nodes, Face::INVALID);

    int nconns = 0;
    check_cgns(cg_nconns(fn, B, Z, &nconns), "cg_nconns");
    if (nconns == 0) {
        LOG_WARN("Zone {} has no zone-interface connectivity", Z);
        return remap;
    }

    for (int i = 1; i <= nconns; ++i) {
        char conn_name[kMaxNameLength + 1] = "";
        char donor_name[kMaxNameLength + 1] = "";
        GridLocation_t location = CGNS_ENUMV(Vertex);
        GridConnectivityType_t ctype = CGNS_ENUMV(Abutting1to1);
        PointSetType_t ptset_type = CGNS_ENUMV(PointList);
        PointSetType_t donor_ptset_type = CGNS_ENUMV(PointList);
        cgsize_t npnts = 0;
        cgsize_t ndata_donor = 0;
        DataType_t donor_datatype = CGNS_ENUMV(DataTypeNull);
        ZoneType_t donor_zonetype = CGNS_ENUMV(Unstructured);

        check_cgns(cg_conn_info(fn, B, Z, i, conn_name, &location, &ctype, &ptset_type,
                                &npnts, donor_name, &donor_zonetype, &donor_ptset_type,
                                &donor_datatype, &ndata_donor),
                   "cg_conn_info");

        if (ctype != CGNS_ENUMV(Abutting1to1)) {
            LOG_WARN("Connection '{}' in zone {} is not Abutting1to1; skipping", conn_name, Z);
            continue;
        }
        if (location != CGNS_ENUMV(Vertex)) {
            LOG_WARN("Connection '{}' in zone {} is not vertex-based; skipping", conn_name, Z);
            continue;
        }

        std::vector<cgsize_t> pnts(static_cast<std::size_t>(npnts));
        std::vector<cgsize_t> donor_pnts(static_cast<std::size_t>(ndata_donor));
        check_cgns(cg_conn_read(fn, B, Z, i, pnts.data(), donor_datatype, donor_pnts.data()),
                   "cg_conn_read");

        const std::vector<cgsize_t> recv = expand_point_set(ptset_type, pnts.data(), npnts);
        const std::vector<cgsize_t> donor =
            expand_point_set(donor_ptset_type, donor_pnts.data(), ndata_donor);
        if (recv.size() != donor.size()) {
            throw std::runtime_error("Connection '" + std::string(conn_name) +
                                     "': receiver and donor point lists have different sizes (" +
                                     std::to_string(recv.size()) + " vs " +
                                     std::to_string(donor.size()) + ")");
        }

        auto dit = zone_index_by_name.find(donor_name);
        if (dit == zone_index_by_name.end()) {
            throw std::runtime_error("Connection '" + std::string(conn_name) +
                                     "' references unknown donor zone '" + donor_name + "'");
        }
        const int donor_zone = dit->second;
        if (donor_zone >= Z) {
            LOG_WARN("Connection '{}' in zone {} donates to zone {} which is not yet merged; "
                     "skipping (the donor zone's own connection list covers this interface)",
                     conn_name, Z, donor_zone);
            continue;
        }
        const std::vector<std::size_t>& donor_merged = zone_node_merged[donor_zone];

        for (std::size_t k = 0; k < recv.size(); ++k) {
            const std::size_t rv = static_cast<std::size_t>(recv[k]) - 1;  // zone-local
            const std::size_t dv = static_cast<std::size_t>(donor[k]) - 1; // donor-local
            if (rv >= n_zone_nodes) {
                throw std::runtime_error("Connection '" + std::string(conn_name) +
                                         "' references receiver vertex " + std::to_string(rv) +
                                         " outside the zone");
            }
            if (dv >= donor_merged.size()) {
                throw std::runtime_error("Connection '" + std::string(conn_name) +
                                         "' references donor vertex " + std::to_string(dv) +
                                         " outside donor zone '" + donor_name + "'");
            }
            const std::size_t merged = donor_merged[dv];
            if (remap[rv] != Face::INVALID && remap[rv] != merged) {
                throw std::runtime_error("Inconsistent zone-interface mapping: vertex " +
                                         std::to_string(rv + 1) + " of zone " + std::to_string(Z) +
                                         " maps to both " + std::to_string(remap[rv]) + " and " +
                                         std::to_string(merged));
            }
            remap[rv] = merged;
        }

        LOG_INFO("Connection '{}' in zone {}: {} interface vertices matched to zone '{}'",
                 conn_name, Z, recv.size(), donor_name);
    }

    return remap;
}

} // namespace

// ---------------------------------------------------------------------------
// Mesh reader
// ---------------------------------------------------------------------------

Mesh read_cgns_mesh(const std::string& mesh_file_path,
                    const std::unordered_map<std::string, std::string>& bc_map) {
    Mesh mesh;

    // --- Open the file -------------------------------------------------------
    int fn = 0;
    if (cg_open(mesh_file_path.c_str(), CG_MODE_READ, &fn) != CG_OK) {
        cgns_fail("Failed to open CGNS file '" + mesh_file_path + "'");
    }
    struct CgnsFileGuard {
        int fn;
        ~CgnsFileGuard() { cg_close(fn); }
    } guard{fn};

    // --- Base -----------------------------------------------------------------
    int nbases = 0;
    check_cgns(cg_nbases(fn, &nbases), "cg_nbases");
    if (nbases < 1) throw std::runtime_error("CGNS file contains no base: " + mesh_file_path);

    char base_name[kMaxNameLength + 1] = "";
    int phys_dim = 0;
    // NOTE: cg_base_read writes *cell_dim and *phys_dim unconditionally;
    // neither pointer may be null.
    check_cgns(cg_base_read(fn, 1, base_name, &mesh.cell_dimension, &phys_dim), "cg_base_read");
    if (mesh.cell_dimension != 2) {
        throw std::runtime_error("Expected a 2-D mesh (cell dimension 2), got " +
                                 std::to_string(mesh.cell_dimension) + " in '" + mesh_file_path + "'");
    }

    // --- Zones ----------------------------------------------------------------
    int nzones = 0;
    check_cgns(cg_nzones(fn, 1, &nzones), "cg_nzones");
    if (nzones < 1) throw std::runtime_error("CGNS base contains no zones: " + mesh_file_path);

    // Zone name -> CGNS zone index.
    std::map<std::string, int> zone_index_by_name;
    for (int z = 1; z <= nzones; ++z) {
        char zn[kMaxNameLength + 1] = "";
        cgsize_t zs[3] = {0, 0, 0};
        check_cgns(cg_zone_read(fn, 1, z, zn, zs), "cg_zone_read");
        zone_index_by_name[zn] = z;
    }

    // zone_node_merged[z] = merged mesh index of every vertex of zone z
    // (filled as zones are merged, in zone order).
    std::vector<std::vector<std::size_t>> zone_node_merged(nzones + 1);

    for (int Z = 1; Z <= nzones; ++Z) {
        ZoneData zd = read_zone(fn, 1, Z, bc_map);
        LOG_INFO("Read zone '{}': {} nodes, {} cells, {} boundary faces", zd.name,
                 zd.coords.size(), zd.cells.size(), zd.boco_faces.size());

        // Vertex remap for this zone: interface vertices resolve to the merged
        // node indices of their donor-zone counterparts; everything else is
        // appended as a new node. The first zone keeps its own numbering.
        std::vector<std::size_t> remap;
        if (Z == 1) {
            remap.assign(zd.coords.size(), Face::INVALID);
        } else {
            remap = build_vertex_remap(fn, 1, Z, zd.coords.size(), zone_index_by_name,
                                       zone_node_merged);
        }

        // --- Merge nodes -----------------------------------------------------
        std::vector<std::size_t> merged_of(zd.coords.size());
        for (std::size_t v = 0; v < zd.coords.size(); ++v) {
            if (remap[v] != Face::INVALID) {
                merged_of[v] = remap[v];
            } else {
                merged_of[v] = mesh.nodes.size();
                Node n;
                n.coord = zd.coords[v];
                mesh.nodes.push_back(n);
            }
        }

        // --- Merge cells (with node remapping) --------------------------------
        for (const auto& cell_nodes : zd.cells) {
            Cell cell;
            cell.nodes.reserve(cell_nodes.size());
            for (std::size_t v : cell_nodes) {
                cell.nodes.push_back(merged_of[v]);
            }
            cell.global_id = mesh.cells.size();
            mesh.cells.push_back(std::move(cell));
        }

        // --- Merge boundary faces (with node remapping) ------------------------
        for (const auto& bf : zd.boco_faces) {
            Face face;
            face.nodes = {merged_of[bf.a], merged_of[bf.b]};
            face.left_cell = Face::INVALID;
            face.right_cell = Face::INVALID;
            face.is_boundary = true;
            face.bc_tag = bf.tag;
            face.bc_family = bf.family;

            mesh.boundary_faces[bf.tag].push_back(mesh.faces.size());
            mesh.faces.push_back(face);
        }

        zone_node_merged[Z] = std::move(merged_of);

        if (Z == 1) mesh.zone_name = zd.name;
    }

    std::size_t n_boundary_faces = 0;
    for (const auto& [tag, faces] : mesh.boundary_faces) {
        n_boundary_faces += faces.size();
    }
    LOG_INFO("Merged {} zones from '{}': {} nodes, {} cells, {} boundary faces", nzones,
             mesh_file_path, mesh.n_nodes(), mesh.n_cells(), n_boundary_faces);

    return mesh;
}

} // namespace cfd
