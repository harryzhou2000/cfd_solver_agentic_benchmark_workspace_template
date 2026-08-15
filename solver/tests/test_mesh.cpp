#include "cfd/Mesh.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: test_mesh mesh.cgns\n";
    return 2;
  }
  try {
    const cfd::GlobalMesh mesh = cfd::GlobalMesh::read_cgns(argv[1]);
    if (mesh.nodes.empty() || mesh.cells.empty() || mesh.faces.empty()) return 3;
    std::size_t boundary_faces = 0;
    for (const auto& face : mesh.faces) boundary_faces += face.right < 0;
    std::cout << "nodes=" << mesh.nodes.size() << " cells=" << mesh.cells.size()
              << " faces=" << mesh.faces.size() << " boundary_faces=" << boundary_faces
              << " adjacency_entries=" << mesh.adjacency.size() << '\n';
    return boundary_faces == 0 ? 4 : 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
