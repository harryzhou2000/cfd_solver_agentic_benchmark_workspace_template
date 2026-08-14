#pragma once
#include "partition_types.hpp"
#include "case_config.hpp"
#include "mesh_types.hpp"
#include <vector>

namespace solver {

// Convert conservative state Vec4 to primitive [rho, u, v, p]
inline Vec4 conservative_to_primitive(const Vec4& U, double gamma) {
    double rho = U(0), u = U(1)/rho, v = U(2)/rho, Ee = U(3)/rho;
    double e = Ee - 0.5*(u*u + v*v);
    double p = (gamma - 1.0) * rho * e;
    return Vec4(rho, u, v, p);
}
inline Vec4 primitive_to_conservative(const Vec4& W, double gamma) {
    double rho = W(0), u = W(1), v = W(2), p = W(3);
    double Ee = p/((gamma-1.0)*rho) + 0.5*(u*u + v*v);
    return Vec4(rho, rho*u, rho*v, rho*Ee);
}
inline double speed_of_sound_inline(double rho, double p, double gamma) {
    return std::sqrt(gamma * p / rho);
}

// Set boundary face right state UR for a given BC type
// UL_face: reconstructed left state at this face
// normal: outward-pointing face unit normal
// Returns UR_face
Vec4 apply_boundary_condition(const Vec4& UL, const Vec2& normal,
                               const std::string& bc_type,
                               const CaseConfig& config);

// Initialize all cells to freestream state
void set_freestream_state(const DistributedMesh& mesh,
                           std::vector<double>& state,
                           const CaseConfig& config);

} // namespace solver
