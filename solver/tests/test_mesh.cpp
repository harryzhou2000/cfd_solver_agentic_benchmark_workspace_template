#include "mesh.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <set>
#include <string>

using namespace aerofv;

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: test_mesh mesh.cgns\n";
    return 2;
  }
  const GlobalMesh mesh = read_cgns_unstructured_2d(argv[1]);
  mesh.validate_topology();
  assert(!mesh.vertices.empty());
  assert(!mesh.cells.empty());
  assert(!mesh.faces.empty());

  std::set<std::string> boundary_families;
  std::size_t boundary_faces = 0;
  double total_area = 0.0;
  for (const Cell &cell : mesh.cells) {
    assert(cell.vertices.size() == 3 || cell.vertices.size() == 4);
    assert(cell.faces.size() == cell.vertices.size());
    assert(cell.area > 0.0 && std::isfinite(cell.area));
    total_area += cell.area;
  }
  for (const Face &face : mesh.faces) {
    assert(face.left_cell >= 0);
    assert(face.length > 0.0);
    assert(std::abs(norm(face.normal) - 1.0) < 1.0e-10);
    if (face.right_cell < 0) {
      ++boundary_faces;
      assert(!face.boundary_family.empty());
      boundary_families.insert(face.boundary_family);
    }
  }
  assert(total_area > 0.0);
  assert(boundary_faces > 0);
  assert(boundary_families.size() >= 2);
  std::cout << argv[1] << ": vertices=" << mesh.vertices.size()
            << " cells=" << mesh.cells.size() << " faces=" << mesh.faces.size()
            << " physical-boundary-faces=" << boundary_faces << '\n';
}
