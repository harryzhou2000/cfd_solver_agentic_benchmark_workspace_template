// Small CGNS topology/boundary inspector used to verify benchmark meshes before
// constructing the finite-volume mesh.  It deliberately uses the CGNS API
// rather than HDF5 implementation details, so it also works for ADF-backed
// CGNS files.
#include <cgnslib.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

[[noreturn]] void fail(const std::string& context) {
  throw std::runtime_error(context + ": " + cg_get_error());
}

void check(int code, const std::string& context) {
  if (code != CG_OK) fail(context);
}

const char* element_name(ElementType_t type) {
  const char* name = cg_ElementTypeName(type);
  return name == nullptr ? "<unknown>" : name;
}

const char* bc_name(BCType_t type) {
  const char* name = cg_BCTypeName(type);
  return name == nullptr ? "<unknown>" : name;
}

const char* point_set_name(PointSetType_t type) {
  const char* name = cg_PointSetTypeName(type);
  return name == nullptr ? "<unknown>" : name;
}

const char* grid_location_name(GridLocation_t location) {
  const char* name = cg_GridLocationName(location);
  return name == nullptr ? "<unknown>" : name;
}

void inspect_zone(int file, int base, int zone) {
  char zone_name[33]{};  // CGNS names are limited to 32 characters.
  cgsize_t size[9]{};
  check(cg_zone_read(file, base, zone, zone_name, size), "cg_zone_read");
  ZoneType_t zone_type{};
  check(cg_zone_type(file, base, zone, &zone_type), "cg_zone_type");
  std::cout << "  zone " << zone << " name=" << zone_name
            << " type=" << (zone_type == Unstructured ? "Unstructured" : "Structured")
            << " vertices=" << size[0] << " cells=" << size[1]
            << " boundary_vertices=" << size[2] << '\n';

  int coordinates = 0;
  check(cg_ncoords(file, base, zone, &coordinates), "cg_ncoords");
  std::cout << "    coordinates (" << coordinates << "):";
  for (int c = 1; c <= coordinates; ++c) {
    DataType_t data_type{};
    char coordinate_name[33]{};
    check(cg_coord_info(file, base, zone, c, &data_type, coordinate_name), "cg_coord_info");
    std::cout << ' ' << coordinate_name;
  }
  std::cout << '\n';

  int sections = 0;
  check(cg_nsections(file, base, zone, &sections), "cg_nsections");
  std::cout << "    sections (" << sections << "):\n";
  for (int section = 1; section <= sections; ++section) {
    char section_name[33]{};
    ElementType_t element_type{};
    cgsize_t start = 0, end = 0;
    int boundary_count = 0, parent_flag = 0;
    check(cg_section_read(file, base, zone, section, section_name, &element_type, &start, &end,
                          &boundary_count, &parent_flag),
          "cg_section_read");
    cgsize_t data_size = 0;
    check(cg_ElementDataSize(file, base, zone, section, &data_size), "cg_ElementDataSize");
    std::cout << "      " << section_name << ": " << element_name(element_type)
              << " ids=[" << start << ',' << end << "] count=" << (end - start + 1)
              << " connectivity_entries=" << data_size << " boundary_count=" << boundary_count
              << " parent_flag=" << parent_flag << '\n';
  }

  int bocos = 0;
  check(cg_nbocos(file, base, zone, &bocos), "cg_nbocos");
  std::cout << "    boundary conditions (" << bocos << "):\n";
  for (int boco = 1; boco <= bocos; ++boco) {
    char name[33]{};
    BCType_t bc_type{};
    PointSetType_t point_set{};
    cgsize_t point_count = 0;
    int normal_index[3]{};
    cgsize_t normal_list_size = 0;
    DataType_t normal_data_type{};
    int dataset_count = 0;
    check(cg_boco_info(file, base, zone, boco, name, &bc_type, &point_set, &point_count,
                       normal_index, &normal_list_size, &normal_data_type, &dataset_count),
          "cg_boco_info");
    GridLocation_t location = Vertex;
    check(cg_boco_gridlocation_read(file, base, zone, boco, &location),
          "cg_boco_gridlocation_read");
    std::vector<cgsize_t> points(static_cast<size_t>(point_count));
    check(cg_boco_read(file, base, zone, boco, points.data(), nullptr), "cg_boco_read");
    std::cout << "      " << name << ": type=" << bc_name(bc_type)
              << " point_set=" << point_set_name(point_set) << " count=" << point_count
              << " location=" << grid_location_name(location);
    if (point_set == PointRange && points.size() == 2) {
      std::cout << " range=[" << points[0] << ',' << points[1] << ']';
    }
    std::cout
              << " datasets=" << dataset_count << '\n';
  }

  int one_to_one_count = 0;
  check(cg_n1to1(file, base, zone, &one_to_one_count), "cg_n1to1");
  std::cout << "    one-to-one interfaces (" << one_to_one_count << "):\n";
  for (int connection = 1; connection <= one_to_one_count; ++connection) {
    char connection_name[33]{}, donor_name[33]{};
    // CGNS stores IndexRange_t as 3 x 2 even for a 2-D base.
    cgsize_t receiver_range[6]{}, donor_range[6]{};
    int transform[3]{};
    check(cg_1to1_read(file, base, zone, connection, connection_name, donor_name,
                       receiver_range, donor_range, transform),
          "cg_1to1_read");
    std::cout << "      " << connection_name << " -> " << donor_name << " receiver=["
              << receiver_range[0] << ',' << receiver_range[1] << "] donor=["
              << donor_range[0] << ',' << donor_range[1] << "] transform=" << transform[0]
              << '\n';
  }
}

void inspect_file(const char* path) {
  int file = 0;
  check(cg_open(path, CG_MODE_READ, &file), "cg_open(" + std::string(path) + ")");
  try {
    int bases = 0;
    check(cg_nbases(file, &bases), "cg_nbases");
    std::cout << "mesh=" << path << " bases=" << bases << '\n';
    for (int base = 1; base <= bases; ++base) {
      char base_name[33]{};
      int cell_dim = 0, physical_dim = 0;
      check(cg_base_read(file, base, base_name, &cell_dim, &physical_dim), "cg_base_read");
      int zones = 0;
      check(cg_nzones(file, base, &zones), "cg_nzones");
      std::cout << "base " << base << " name=" << base_name << " cell_dim=" << cell_dim
                << " physical_dim=" << physical_dim << " zones=" << zones << '\n';
      for (int zone = 1; zone <= zones; ++zone) inspect_zone(file, base, zone);
    }
    check(cg_close(file), "cg_close");
  } catch (...) {
    cg_close(file);
    throw;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: cgns_mesh_inspect MESH.cgns [MESH.cgns ...]\n";
    return EXIT_FAILURE;
  }
  try {
    for (int i = 1; i < argc; ++i) inspect_file(argv[i]);
  } catch (const std::exception& error) {
    std::cerr << "CGNS inspection failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
