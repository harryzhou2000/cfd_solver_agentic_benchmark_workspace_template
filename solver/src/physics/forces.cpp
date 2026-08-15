#include "physics/forces.hpp"

#include <cmath>

#include "mesh/mesh.hpp"
#include "physics/gas_model.hpp"

namespace cfd {

ForceResult compute_forces(const std::vector<double>& U,
                           const DistributedMesh& dmesh,
                           const CaseConfig& cfg, double mu) {
  ForceResult F;

  const double gamma = cfg.gas.gamma;
  const double q_inf =
      0.5 * cfg.freestream.rho * cfg.freestream.velocity_magnitude *
      cfg.freestream.velocity_magnitude;
  if (!(q_inf > 0.0))
    throw std::runtime_error("compute_forces: non-positive freestream dynamic "
                             "pressure");
  const double ref_area = cfg.reference.area;
  const double ref_length = cfg.reference.length;
  const Vector3 moment_center(cfg.reference.moment_center.at(0),
                              cfg.reference.moment_center.at(1), 0.0);

  double P_drag = 0.0, P_lift = 0.0, V_drag = 0.0, V_lift = 0.0, M = 0.0;

  for (const Face2D& f : dmesh.boundary_faces) {
    if (f.bc_type != BCType::SlipWall &&
        f.bc_type != BCType::NoSlipAdiabaticWall)
      continue;

    const int owner = f.left_cell >= 0 ? f.left_cell : f.right_cell;
    const double* u = U.data() + static_cast<std::size_t>(owner) * NVARS;
    const PrimitiveState prim = cons_to_prim(u, gamma, cfg.gas.R);
    const Cell2D& cell = dmesh.cells[owner];

    // --- pressure force -------------------------------------------------
    const double p_wall = prim.p;
    const double Fpx = p_wall * f.normal.x * f.area;
    const double Fpy = p_wall * f.normal.y * f.area;
    P_drag += Fpx;
    P_lift += Fpy;
    const Vector3 r = f.center - moment_center;
    M += r.x * Fpy - r.y * Fpx;

    // --- viscous force (no-slip walls only) -----------------------------
    if (f.bc_type == BCType::NoSlipAdiabaticWall && mu > 0.0) {
      const double tx = -f.normal.y;
      const double ty = f.normal.x;
      const double V_t = prim.u * tx + prim.v * ty;
      const double d = (f.center - cell.cell_center).norm();
      if (d > 0.0) {
        // Force on the WALL is the reaction of the shear on the fluid:
        // F_wall = -tau_fluid * t * area = +mu*V_t/d * t * area.
        const double Fvx = (mu * V_t / d) * tx * f.area;
        const double Fvy = (mu * V_t / d) * ty * f.area;
        V_drag += Fvx;
        V_lift += Fvy;
        M += r.x * Fvy - r.y * Fvx;
      }
    }
  }

  const double qA = q_inf * ref_area;
  F.pressure_drag = P_drag / qA;
  F.viscous_drag = V_drag / qA;
  F.pressure_lift = P_lift / qA;
  F.viscous_lift = V_lift / qA;
  F.moment_z = M / (qA * ref_length);
  return F;
}

}  // namespace cfd
