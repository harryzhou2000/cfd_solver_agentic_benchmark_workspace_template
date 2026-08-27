#include "mesh/cgns_reader.h"

#include <algorithm>
#include <sstream>

#include <cgnslib.h>

#include "core/exceptions.h"
#include "core/logging.h"

namespace cns2d {
namespace {

// CGNS name fields are at most 32 characters plus a terminator.
constexpr int kCgnsNameBuffer = 64;

// CGNS integer data type matching the library's cgsize_t width.
constexpr DataType_t kCgSizeDataType = (sizeof(cgsize_t) == 8) ? LongInteger : Integer;

// Wrap a CGNS call and convert a failure into CnsError with the library message.
void cgnsCheck(int ier, const std::string &what) {
  if (ier != CG_OK) {
    throw CnsError("CGNS error while " + what + ": " + std::string(cg_get_error()));
  }
}

// RAII handle so an exception cannot leak an open CGNS file.
class CgnsFile {
 public:
  explicit CgnsFile(const std::string &path) {
    if (cg_open(path.c_str(), CG_MODE_READ, &index_) != CG_OK) {
      throw CnsError("cannot open CGNS file '" + path + "': " + std::string(cg_get_error()));
    }
  }
  ~CgnsFile() {
    if (index_ > 0) cg_close(index_);
  }
  CgnsFile(const CgnsFile &) = delete;
  CgnsFile &operator=(const CgnsFile &) = delete;
  int index() const { return index_; }

 private:
  int index_{0};
};

std::string trimmed(const char *raw) {
  std::string s(raw);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
  return s;
}

std::string safeName(const char *raw) { return raw != nullptr ? trimmed(raw) : std::string(); }

// Read the FamilyName_t attached to the current cg_goto location, if any.
std::string readFamilyNameAtCurrentNode() {
  char family[kCgnsNameBuffer] = {0};
  if (cg_famname_read(family) == CG_OK) {
    return trimmed(family);
  }
  return std::string();
}

}  // namespace

ElementTopology topologyForShape(ElementShape shape) {
  switch (shape) {
    case ElementShape::kBar2:
      return {ElementShape::kBar2, 2, 1, "BAR_2"};
    case ElementShape::kTri3:
      return {ElementShape::kTri3, 3, 2, "TRI_3"};
    case ElementShape::kQuad4:
      return {ElementShape::kQuad4, 4, 2, "QUAD_4"};
    case ElementShape::kUnsupported:
      break;
  }
  return {ElementShape::kUnsupported, 0, 0, "unsupported"};
}

const char *elementShapeName(ElementShape shape) { return topologyForShape(shape).name; }

ElementShape shapeFromCgnsElementType(int cgns_element_type) {
  switch (static_cast<ElementType_t>(cgns_element_type)) {
    case BAR_2:
      return ElementShape::kBar2;
    case TRI_3:
      return ElementShape::kTri3;
    case QUAD_4:
      return ElementShape::kQuad4;
    default:
      return ElementShape::kUnsupported;
  }
}

RawMesh readCgnsMesh(const std::string &path) {
  CgnsFile file(path);
  const int fn = file.index();

  int num_bases = 0;
  cgnsCheck(cg_nbases(fn, &num_bases), "counting bases in " + path);
  if (num_bases < 1) {
    throw CnsError("CGNS file '" + path + "' contains no base");
  }
  if (num_bases > 1) {
    logWarn("CGNS file contains " + std::to_string(num_bases) +
            " bases; only the first base is used");
  }

  RawMesh mesh;
  mesh.file_path = path;

  const int base = 1;
  {
    char base_name[kCgnsNameBuffer] = {0};
    int cell_dim = 0;
    int phys_dim = 0;
    cgnsCheck(cg_base_read(fn, base, base_name, &cell_dim, &phys_dim), "reading base 1");
    mesh.base_name = trimmed(base_name);
    mesh.cell_dimension = cell_dim;
    mesh.physical_dimension = phys_dim;
    if (cell_dim != 2) {
      throw CnsError("CGNS base '" + mesh.base_name + "' has cell dimension " +
                     std::to_string(cell_dim) + "; this solver build requires a 2-D mesh");
    }
  }

  // Base-level families.
  {
    int num_families = 0;
    if (cg_nfamilies(fn, base, &num_families) == CG_OK) {
      for (int fi = 1; fi <= num_families; ++fi) {
        char fam_name[kCgnsNameBuffer] = {0};
        int nboco = 0;
        int ngeo = 0;
        if (cg_family_read(fn, base, fi, fam_name, &nboco, &ngeo) == CG_OK) {
          mesh.family_names.push_back(trimmed(fam_name));
        }
      }
    }
  }

  int num_zones = 0;
  cgnsCheck(cg_nzones(fn, base, &num_zones), "counting zones");
  if (num_zones < 1) {
    throw CnsError("CGNS base '" + mesh.base_name + "' contains no zone");
  }

  for (int zi = 1; zi <= num_zones; ++zi) {
    RawZone zone;

    ZoneType_t zone_type = ZoneTypeNull;
    cgnsCheck(cg_zone_type(fn, base, zi, &zone_type), "reading zone type");
    if (zone_type != Unstructured) {
      throw CnsError("zone " + std::to_string(zi) + " of '" + path +
                     "' is not Unstructured; structured zones are not supported");
    }

    cgsize_t zone_size[9] = {0};
    {
      char zone_name[kCgnsNameBuffer] = {0};
      cgnsCheck(cg_zone_read(fn, base, zi, zone_name, zone_size), "reading zone header");
      zone.name = trimmed(zone_name);
    }
    zone.num_nodes = static_cast<Index>(zone_size[0]);
    zone.num_cells = static_cast<Index>(zone_size[1]);
    if (zone.num_nodes <= 0 || zone.num_cells <= 0) {
      throw CnsError("zone '" + zone.name + "' reports non-positive node or cell count");
    }

    // Zone family name (optional).
    if (cg_goto(fn, base, "Zone_t", zi, "end") == CG_OK) {
      zone.family_name = readFamilyNameAtCurrentNode();
    }

    // --- coordinates ------------------------------------------------------
    {
      int num_coords = 0;
      cgnsCheck(cg_ncoords(fn, base, zi, &num_coords), "counting coordinates in zone " + zone.name);
      if (num_coords < 2) {
        throw CnsError("zone '" + zone.name + "' has fewer than 2 coordinate arrays");
      }
      zone.x.assign(static_cast<std::size_t>(zone.num_nodes), 0.0);
      zone.y.assign(static_cast<std::size_t>(zone.num_nodes), 0.0);
      const cgsize_t rmin = 1;
      const cgsize_t rmax = static_cast<cgsize_t>(zone.num_nodes);
      bool have_x = false;
      bool have_y = false;
      for (int ci = 1; ci <= num_coords; ++ci) {
        char coord_name[kCgnsNameBuffer] = {0};
        DataType_t dtype = DataTypeNull;
        cgnsCheck(cg_coord_info(fn, base, zi, ci, &dtype, coord_name), "reading coordinate info");
        const std::string cname = trimmed(coord_name);
        if (cname == "CoordinateX") {
          cgnsCheck(cg_coord_read(fn, base, zi, "CoordinateX", RealDouble, &rmin, &rmax, zone.x.data()),
                    "reading CoordinateX of zone " + zone.name);
          have_x = true;
        } else if (cname == "CoordinateY") {
          cgnsCheck(cg_coord_read(fn, base, zi, "CoordinateY", RealDouble, &rmin, &rmax, zone.y.data()),
                    "reading CoordinateY of zone " + zone.name);
          have_y = true;
        }
        // CoordinateZ, if present in a 2-D mesh, is ignored on purpose.
      }
      if (!have_x || !have_y) {
        throw CnsError("zone '" + zone.name + "' is missing CoordinateX or CoordinateY");
      }
    }

    // --- element sections -------------------------------------------------
    {
      int num_sections = 0;
      cgnsCheck(cg_nsections(fn, base, zi, &num_sections), "counting sections in zone " + zone.name);
      for (int si = 1; si <= num_sections; ++si) {
        char sec_name[kCgnsNameBuffer] = {0};
        ElementType_t etype = ElementTypeNull;
        cgsize_t start = 0;
        cgsize_t end = 0;
        int nbndry = 0;
        int parent_flag = 0;
        cgnsCheck(cg_section_read(fn, base, zi, si, sec_name, &etype, &start, &end, &nbndry, &parent_flag),
                  "reading section header");

        RawSection section;
        section.name = trimmed(sec_name);
        section.cgns_element_type = static_cast<int>(etype);
        section.shape = shapeFromCgnsElementType(section.cgns_element_type);
        section.first_element = static_cast<GlobalIndex>(start);
        section.last_element = static_cast<GlobalIndex>(end);
        section.num_elements = static_cast<Index>(end - start + 1);

        if (etype == MIXED || etype == NGON_n || etype == NFACE_n) {
          throw CnsError("section '" + section.name + "' of zone '" + zone.name +
                         "' uses a polymorphic element type (MIXED/NGON/NFACE) which this "
                         "reader does not support");
        }
        if (section.shape == ElementShape::kUnsupported) {
          const char *type_name = cg_ElementTypeName(etype);
          throw CnsError("section '" + section.name + "' of zone '" + zone.name +
                         "' uses unsupported CGNS element type id " +
                         std::to_string(section.cgns_element_type) + " (" + safeName(type_name) + ")" +
                         " (supported: BAR_2, TRI_3, QUAD_4)");
        }

        const ElementTopology topo = topologyForShape(section.shape);
        section.num_nodes_per_element = topo.num_nodes;

        cgsize_t data_size = 0;
        cgnsCheck(cg_ElementDataSize(fn, base, zi, si, &data_size),
                  "querying element data size of section " + section.name);
        const cgsize_t expected =
            static_cast<cgsize_t>(section.num_elements) * static_cast<cgsize_t>(topo.num_nodes);
        if (data_size != expected) {
          throw CnsError("section '" + section.name + "' of zone '" + zone.name +
                         "' has inconsistent connectivity size (" + std::to_string(data_size) +
                         " vs expected " + std::to_string(expected) + ")");
        }

        std::vector<cgsize_t> raw(static_cast<std::size_t>(data_size));
        cgnsCheck(cg_elements_read(fn, base, zi, si, raw.data(), nullptr),
                  "reading connectivity of section " + section.name);

        section.connectivity.resize(raw.size());
        for (std::size_t k = 0; k < raw.size(); ++k) {
          const cgsize_t node_1based = raw[k];
          if (node_1based < 1 || node_1based > static_cast<cgsize_t>(zone.num_nodes)) {
            throw CnsError("section '" + section.name + "' of zone '" + zone.name +
                           "' references out-of-range node index " + std::to_string(node_1based));
          }
          section.connectivity[k] = static_cast<Index>(node_1based - 1);
        }
        zone.sections.push_back(std::move(section));
      }
    }

    // --- boundary conditions ---------------------------------------------
    {
      int num_bocos = 0;
      cgnsCheck(cg_nbocos(fn, base, zi, &num_bocos), "counting BCs in zone " + zone.name);
      for (int bi = 1; bi <= num_bocos; ++bi) {
        char boco_name[kCgnsNameBuffer] = {0};
        BCType_t bctype = BCTypeNull;
        PointSetType_t pset = PointSetTypeNull;
        cgsize_t npnts = 0;
        int normal_index[3] = {0, 0, 0};
        cgsize_t normal_list_size = 0;
        DataType_t normal_data_type = DataTypeNull;
        int ndataset = 0;
        cgnsCheck(cg_boco_info(fn, base, zi, bi, boco_name, &bctype, &pset, &npnts, normal_index,
                               &normal_list_size, &normal_data_type, &ndataset),
                  "reading BC info in zone " + zone.name);

        RawBoco boco;
        boco.name = trimmed(boco_name);
        boco.point_set_type = safeName(cg_PointSetTypeName(pset));

        GridLocation_t location = GridLocationNull;
        if (cg_boco_gridlocation_read(fn, base, zi, bi, &location) == CG_OK) {
          boco.grid_location = safeName(cg_GridLocationName(location));
        }

        std::vector<cgsize_t> pnts(static_cast<std::size_t>(std::max<cgsize_t>(npnts, 1)));
        cgnsCheck(cg_boco_read(fn, base, zi, bi, pnts.data(), nullptr),
                  "reading BC point set of " + boco.name);

        if (pset == PointRange && npnts == 2) {
          boco.has_element_range = true;
          boco.first_element = static_cast<GlobalIndex>(pnts[0]);
          boco.last_element = static_cast<GlobalIndex>(pnts[1]);
        } else {
          boco.point_list.reserve(static_cast<std::size_t>(npnts));
          for (cgsize_t k = 0; k < npnts; ++k) {
            boco.point_list.push_back(static_cast<GlobalIndex>(pnts[static_cast<std::size_t>(k)]));
          }
        }

        if (cg_goto(fn, base, "Zone_t", zi, "ZoneBC_t", 1, "BC_t", bi, "end") == CG_OK) {
          boco.family_name = readFamilyNameAtCurrentNode();
        }
        if (boco.family_name.empty()) boco.family_name = boco.name;

        zone.bocos.push_back(std::move(boco));
      }
    }

    // --- zone-to-zone connectivity ---------------------------------------
    // The general GridConnectivity_t API (cg_nconns) is used rather than the
    // structured-oriented cg_n1to1 API: point-matched unstructured interfaces
    // are commonly written as GridConnectivity_t with
    // GridConnectivityType=Abutting1to1 and explicit PointList/PointListDonor
    // vertex lists, in which case cg_n1to1 reports zero patches.  Both node
    // flavours are handled here.
    {
      int num_conns = 0;
      if (cg_nconns(fn, base, zi, &num_conns) == CG_OK) {
        for (int ci = 1; ci <= num_conns; ++ci) {
          char conn_name[kCgnsNameBuffer] = {0};
          char donor_name[kCgnsNameBuffer] = {0};
          GridLocation_t location = GridLocationNull;
          GridConnectivityType_t conn_type = GridConnectivityTypeNull;
          PointSetType_t pset = PointSetTypeNull;
          cgsize_t npnts = 0;
          ZoneType_t donor_zone_type = ZoneTypeNull;
          PointSetType_t donor_pset = PointSetTypeNull;
          DataType_t donor_dtype = DataTypeNull;
          cgsize_t ndata_donor = 0;
          if (cg_conn_info(fn, base, zi, ci, conn_name, &location, &conn_type, &pset, &npnts,
                           donor_name, &donor_zone_type, &donor_pset, &donor_dtype,
                           &ndata_donor) != CG_OK) {
            continue;
          }

          RawConnectivity conn;
          conn.name = trimmed(conn_name);
          conn.donor_zone_name = trimmed(donor_name);
          conn.connectivity_type = safeName(cg_GridConnectivityTypeName(conn_type));
          conn.grid_location = safeName(cg_GridLocationName(location));

          if (conn_type != Abutting1to1) {
            throw CnsError("zone connectivity '" + conn.name + "' of zone '" + zone.name +
                           "' has GridConnectivityType '" + conn.connectivity_type +
                           "'; only point-matched Abutting1to1 interfaces are supported");
          }
          if (location != Vertex && location != GridLocationNull) {
            throw CnsError("zone connectivity '" + conn.name + "' of zone '" + zone.name +
                           "' uses GridLocation '" + conn.grid_location +
                           "'; only vertex-matched interfaces are supported");
          }
          if (npnts <= 0 || ndata_donor != npnts) {
            throw CnsError("zone connectivity '" + conn.name + "' of zone '" + zone.name +
                           "' has inconsistent PointList/PointListDonor sizes (" +
                           std::to_string(npnts) + " vs " + std::to_string(ndata_donor) + ")");
          }

          std::vector<cgsize_t> pnts(static_cast<std::size_t>(npnts));
          std::vector<cgsize_t> donor(static_cast<std::size_t>(ndata_donor));
          cgnsCheck(cg_conn_read(fn, base, zi, ci, pnts.data(), kCgSizeDataType, donor.data()),
                    "reading zone connectivity " + conn.name + " of zone " + zone.name);

          conn.point_list.assign(pnts.begin(), pnts.end());
          conn.point_list_donor.assign(donor.begin(), donor.end());
          zone.connectivities.push_back(std::move(conn));
        }
      }
    }

    mesh.zones.push_back(std::move(zone));
  }

  return mesh;
}

std::string describeRawMesh(const RawMesh &mesh) {
  std::ostringstream os;
  os << "CGNS mesh '" << mesh.file_path << "': base '" << mesh.base_name << "' (cell dim "
     << mesh.cell_dimension << ", phys dim " << mesh.physical_dimension << "), " << mesh.zones.size()
     << " zone(s)";
  for (const RawZone &z : mesh.zones) {
    os << "\n  zone '" << z.name << "': " << z.num_nodes << " nodes, " << z.num_cells << " cells";
    for (const RawSection &s : z.sections) {
      os << "\n    section '" << s.name << "': " << elementShapeName(s.shape) << " x "
         << s.num_elements << " (elements " << s.first_element << ".." << s.last_element << ")";
    }
    for (const RawBoco &b : z.bocos) {
      os << "\n    BC '" << b.name << "' family '" << b.family_name << "' location '"
         << b.grid_location << "' pset '" << b.point_set_type << "'";
      if (b.has_element_range) {
        os << " range " << b.first_element << ".." << b.last_element;
      } else {
        os << " list size " << b.point_list.size();
      }
    }
    for (const RawConnectivity &c : z.connectivities) {
      os << "\n    connectivity '" << c.name << "' (" << c.connectivity_type << ", "
         << c.grid_location << ") -> zone '" << c.donor_zone_name << "', " << c.point_list.size()
         << " matched nodes";
    }
  }
  return os.str();
}

}  // namespace cns2d
