#include "physics/boundary.hpp"

#include <cmath>

#include "physics/inviscid_flux.hpp"

namespace cfd {

void farfield_flux(const double* U_int, const double* U_inf,
                   const Vector3& normal, double area, double gamma,
                   double rusanov_scale, double* flux) {
  const PrimitiveState prim_int = cons_to_prim(U_int, gamma, 1.0);
  const PrimitiveState prim_inf = cons_to_prim(U_inf, gamma, 1.0);

  const double vn_int = prim_int.u * normal.x + prim_int.v * normal.y;
  const double vn_inf = prim_inf.u * normal.x + prim_inf.v * normal.y;

  double U_b[NVARS];
  if (vn_int > prim_int.a) {
    // Supersonic outflow: everything from the interior.
    for (int k = 0; k < NVARS; ++k) U_b[k] = U_int[k];
  } else if (vn_inf < -prim_inf.a) {
    // Supersonic inflow: everything from the freestream.
    for (int k = 0; k < NVARS; ++k) U_b[k] = U_inf[k];
  } else {
    // Subsonic: Riemann invariants.
    const double rp = vn_int + 2.0 * prim_int.a / (gamma - 1.0);
    const double rm = vn_inf - 2.0 * prim_inf.a / (gamma - 1.0);
    const double vn_b = 0.5 * (rp + rm);
    double a_b = 0.25 * (gamma - 1.0) * (rp - rm);
    if (!(a_b > 0.0)) a_b = 1e-12;  // degenerate guard, never hit physically

    // Entropy: interior for outflow, freestream for inflow.
    const double s_int = prim_int.p / std::pow(prim_int.rho, gamma);
    const double s_inf = prim_inf.p / std::pow(prim_inf.rho, gamma);
    const double s_b = (vn_b > 0.0) ? s_int : s_inf;

    const double rho_b =
        std::pow(a_b * a_b / (gamma * s_b), 1.0 / (gamma - 1.0));
    const double p_b = s_b * std::pow(rho_b, gamma);

    PrimitiveState prim_b;
    prim_b.rho = rho_b;
    prim_b.u = prim_int.u + (vn_b - vn_int) * normal.x;
    prim_b.v = prim_int.v + (vn_b - vn_int) * normal.y;
    prim_b.p = p_b;
    prim_b.a = a_b;
    prim_to_cons(prim_b, gamma, U_b);
  }

  rusanov_flux(U_int, U_b, normal, area, gamma, flux, rusanov_scale);
}

void slip_wall_flux(const double* U_int, const Vector3& normal, double area,
                    double gamma, double* flux) {
  const PrimitiveState prim = cons_to_prim(U_int, gamma, 1.0);
  // Zero normal velocity: F.n = [0, p*nx, p*ny, 0].
  flux[0] = 0.0;
  flux[1] = prim.p * normal.x * area;
  flux[2] = prim.p * normal.y * area;
  flux[3] = 0.0;
}

void noslip_adiabatic_wall_flux(const double* U_cell, const double* U_face,
                                const Vector3& cell_center,
                                const Vector3& face_center,
                                const Vector3& normal, double area,
                                double gamma, double R, double Pr, double mu,
                                double* inviscid_flux, double* viscous_flux) {
  const PrimitiveState prim_cell = cons_to_prim(U_cell, gamma, R);
  const PrimitiveState prim_face = cons_to_prim(U_face, gamma, R);

  // --- inviscid part: pressure-only wall flux (wall velocity is zero) -----
  // F.n = [0, p*nx, p*ny, 0], p = (reconstructed) face pressure.
  inviscid_flux[0] = 0.0;
  inviscid_flux[1] = prim_face.p * normal.x * area;
  inviscid_flux[2] = prim_face.p * normal.y * area;
  inviscid_flux[3] = 0.0;

  // --- viscous part: wall shear from the CELL-CENTER velocity -------------
  // The Newtonian wall shear is mu*dV_t/dn with V_t(wall) = 0 and the cell
  // gradient approximated by (0 - V_t_cell)/d — the reconstructed FACE
  // velocity must NOT be used here: linear extrapolation to the wall gives
  // ~2x the cell velocity and doubles the drag. d is the wall distance.
  const double tx = -normal.y;
  const double ty = normal.x;
  const double V_t = prim_cell.u * tx + prim_cell.v * ty;
  const double d = (face_center - cell_center).norm();
  const double tau_w = (d > 0.0 && mu > 0.0) ? -mu * V_t / d : 0.0;
  viscous_flux[0] = 0.0;
  viscous_flux[1] = tau_w * tx * area;
  viscous_flux[2] = tau_w * ty * area;
  viscous_flux[3] = tau_w * V_t * area;  // shear work (dissipative)
  (void)Pr;  // heat flux at an adiabatic wall is zero
}

}  // namespace cfd
