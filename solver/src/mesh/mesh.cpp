#include "mesh/mesh.hpp"

#include <stdexcept>

namespace cfd {

const char* bc_type_name(BCType t) {
  switch (t) {
    case BCType::Interior: return "interior";
    case BCType::Farfield: return "farfield";
    case BCType::SlipWall: return "slip_wall";
    case BCType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
  }
  return "unknown";
}

BCType bc_type_from_string(const std::string& name) {
  if (name == "farfield") return BCType::Farfield;
  if (name == "slip_wall") return BCType::SlipWall;
  if (name == "no_slip_adiabatic_wall") return BCType::NoSlipAdiabaticWall;
  if (name == "interior") return BCType::Interior;
  throw std::invalid_argument("unknown boundary condition type '" + name +
                              "' (expected farfield, slip_wall, "
                              "no_slip_adiabatic_wall)");
}

void Mesh2D::finalize() {
  num_cells = static_cast<long long>(cells.size());
  num_faces = static_cast<long long>(faces.size());
  num_boundary_faces = 0;
  for (const Face2D& f : faces) {
    const bool has_l = f.left_cell >= 0;
    const bool has_r = f.right_cell >= 0;
    if (has_l != has_r) ++num_boundary_faces;
  }
}

}  // namespace cfd
