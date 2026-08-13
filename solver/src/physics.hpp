#pragma once

#include <string>

#include "common.hpp"
#include "distributed_mesh.hpp"

namespace cfd {

struct Numerics {
    GasModel gas;
    FluxScheme flux_scheme = FluxScheme::Roe;
    double rusanov_dissipation_scale = 1.0;
    double entropy_fix_delta0 = 0.1;
    double limiter_k = 0.3;  // Venkatakrishnan K parameter
    double viscosity = 0.0;  // 0 => inviscid
    double thermal_conductivity = 0.0;
};

struct FaceFluxResult {
    VecN inviscid{};
    VecN viscous{};
};

// Least-squares gradients of [rho, u, v, p] for every *owned* cell. Ghost
// gradient entries are left untouched (they are filled by halo exchange).
void compute_lsq_gradients(const DistributedMesh& mesh,
                           const std::vector<double>& U,
                           std::vector<double>& grad, const GasModel& gas);

// Limited reconstruction of the conservative states on both sides of one
// face. Returns true if the second-order reconstruction was physically
// admissible; false forces a first-order fallback for this face.
bool reconstruct_face(const DistributedMesh& mesh, const LocalFace& face,
                      const std::vector<double>& U,
                      const std::vector<double>& grad,
                      const std::vector<double>& limiter, const Numerics& num,
                      VecN& UL, VecN& UR);

// Compute the Venkatakrishnan limiter factor per owned cell (one scalar used
// for all four primitive variables, the common conservative choice).
void compute_limiters(const DistributedMesh& mesh,
                      const std::vector<double>& U,
                      const std::vector<double>& grad,
                      std::vector<double>& limiter, const Numerics& num);

// Inviscid flux across a face from left/right conservative states.
VecN inviscid_face_flux(const VecN& UL, const VecN& UR, const Vec2& n,
                        const Numerics& num);

// Viscous flux across an interior face using averaged/corrected face
// gradients of velocity and temperature.
VecN viscous_face_flux(const DistributedMesh& mesh, const LocalFace& face,
                       const VecN& UL, const VecN& UR,
                       const std::vector<double>& grad, const Numerics& num);

// Boundary flux contribution for farfield/slip/no-slip faces. Also returns
// per-face wall data used for forces and surface.csv.
struct WallFaceData {
    double pressure = 0.0;
    Vec2 tau_n{};  // viscous traction tau . n_b (n_b = body outward normal)
    Vec2 n_b{};    // unit normal pointing out of the body into the fluid
    Vec2 t_b{};    // tangential unit vector (n_b rotated to align +x on top)
    double rho_wall = 0.0;
    Vec2 u_wall{};  // boundary-state velocity (zero for no-slip)
};

FaceFluxResult boundary_face_flux(const DistributedMesh& mesh,
                                  const LocalFace& face,
                                  const std::vector<double>& U,
                                  const std::vector<double>& grad,
                                  const Numerics& num,
                                  const Primitive& freestream,
                                  WallFaceData* wall_data);

// Build the global residual of the conservative state per owned cell.
// R = sum_over_faces (F_i - F_v) * area (with the face normal pointing out of
// the cell).
void assemble_residual(const DistributedMesh& mesh,
                       const std::vector<double>& U,
                       const std::vector<double>& grad,
                       const std::vector<double>& limiter,
                       const Numerics& num, const Primitive& freestream,
                       std::vector<double>& residual);

// Wall force accumulation on this rank: returns local sums.
struct ForceSum {
    double pressure_drag = 0.0;
    double viscous_drag = 0.0;
    double pressure_lift = 0.0;
    double viscous_lift = 0.0;
    double moment_z = 0.0;
};

ForceSum accumulate_wall_forces(const DistributedMesh& mesh,
                                const std::vector<double>& U,
                                const std::vector<double>& grad,
                                const Numerics& num,
                                const Primitive& freestream, double q_inf,
                                double ref_area, double ref_length,
                                const Vec2& moment_center);

}  // namespace cfd
