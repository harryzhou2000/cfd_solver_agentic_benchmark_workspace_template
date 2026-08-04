#include "cfd/config.hpp"
#include "cfd/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

double magnitude(const cfd::Vec2 value) { return std::hypot(value.x, value.y); }

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: mesh_diagnostics <case.json>\n";
    return 2;
  }
  try {
    const cfd::CaseConfig config = cfd::load_case_config(argv[1]);
    const cfd::Mesh mesh = cfd::read_cgns_mesh(config.mesh_file.string(), config.boundary_conditions);
    double largest_normal_closure = 0.0;
    double smallest_area = std::numeric_limits<double>::infinity();
    int unmapped_boundary_faces = 0;
    for (int cell_id = 0; cell_id < static_cast<int>(mesh.cells.size()); ++cell_id) {
      const cfd::Cell& cell = mesh.cells[static_cast<std::size_t>(cell_id)];
      cfd::Vec2 normal_sum{};
      for (const int face_id : cell.faces) {
        const cfd::Face& face = mesh.faces[static_cast<std::size_t>(face_id)];
        const double sign = face.left_cell == cell_id ? 1.0 : -1.0;
        normal_sum.x += sign * face.normal.x;
        normal_sum.y += sign * face.normal.y;
      }
      largest_normal_closure = std::max(largest_normal_closure, magnitude(normal_sum));
      smallest_area = std::min(smallest_area, cell.area);
    }
    for (const cfd::Face& face : mesh.faces) {
      if (face.right_cell < 0 && face.boundary_type == cfd::BoundaryType::unspecified) {
        ++unmapped_boundary_faces;
      }
    }
    std::cout << "cells=" << mesh.cells.size() << " faces=" << mesh.faces.size()
              << " min_area=" << smallest_area
              << " max_cell_normal_closure=" << largest_normal_closure
              << " unmapped_boundary_faces=" << unmapped_boundary_faces << '\n';
    if (smallest_area <= 0.0 || largest_normal_closure > 1.0e-10 || unmapped_boundary_faces != 0) {
      return 1;
    }
  } catch (const std::exception& error) {
    std::cerr << "mesh diagnostic error: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
