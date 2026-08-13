// Phase 3: boundary conditions implementation (see boundary.h).

#include "boundary.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "physics.h"

namespace cfd {

ConsState farfield_state(const ConsState& U_int, const Freestream& fs,
                         const Vec2& normal, const GasConfig& gas) {
  const double area = normal.norm();
  const Vec2 n_hat = normal / area;
  const Vec2 t_hat{-n_hat.y, n_hat.x};

  const PrimState P_int = cons_to_prim(U_int, gas);
  const PrimState P_fs = freestream_to_prim(fs, gas);

  const double vn_int = P_int.u * n_hat.x + P_int.v * n_hat.y;
  const double vn_fs = P_fs.u * n_hat.x + P_fs.v * n_hat.y;
  const double vt_int = P_int.u * t_hat.x + P_int.v * t_hat.y;
  const double vt_fs = P_fs.u * t_hat.x + P_fs.v * t_hat.y;

  const double gamma = gas.gamma;
  const double gm1 = gamma - 1.0;

  PrimState P_b;

  if (vn_int >= P_int.a) {
    // Supersonic outflow: everything from the interior.
    P_b = P_int;
  } else if (vn_int <= -P_int.a) {
    // Supersonic inflow: everything from the freestream.
    P_b = P_fs;
  } else {
    // Subsonic: Riemann invariants.
    // R+ is carried out of the domain by the interior, R- is carried in
    // from the freestream.
    const double Rplus = vn_int + 2.0 * P_int.a / gm1;
    const double Rminus = vn_fs - 2.0 * P_fs.a / gm1;
    const double vn_b = 0.5 * (Rplus + Rminus);
    double a_b = 0.25 * gm1 * (Rplus - Rminus);
    if (!(a_b > 0.0) || !std::isfinite(a_b)) {
      // Safety fallback for a degenerate state (should not occur with
      // physical states; prevents a NaN from propagating). Log the first few
      // occurrences for diagnostics.
      static int fallback_count = 0;
      if (fallback_count < 3) {
        std::fprintf(stderr,
                     "cfd_solver: farfield_state: degenerate invariant "
                     "reconstruction (a_b = %e); using fallback sound speed "
                     "[occurrence %d]\n",
                     a_b, fallback_count + 1);
      }
      ++fallback_count;
      a_b = 1e-3 * std::max(P_int.a, P_fs.a);
    }

    if (vn_int > 0.0) {
      // Outflow: entropy and tangential velocity from the interior.
      P_b.rho = P_int.rho;
      P_b.u = vn_b * n_hat.x + vt_int * t_hat.x;
      P_b.v = vn_b * n_hat.y + vt_int * t_hat.y;
    } else {
      // Inflow: entropy and tangential velocity from the freestream.
      P_b.rho = P_fs.rho;
      P_b.u = vn_b * n_hat.x + vt_fs * t_hat.x;
      P_b.v = vn_b * n_hat.y + vt_fs * t_hat.y;
    }
    // p = rho * a^2 / gamma from the invariant-reconstructed sound speed.
    P_b.p = P_b.rho * a_b * a_b / gamma;
  }

  return prim_to_cons(P_b, gas);
}

ConsState slip_wall_state(const ConsState& U_int, const Vec2& normal,
                          const GasConfig& gas) {
  const double area = normal.norm();
  const Vec2 n_hat = normal / area;
  const Vec2 t_hat{-n_hat.y, n_hat.x};

  const PrimState P = cons_to_prim(U_int, gas);
  const double vn = P.u * n_hat.x + P.v * n_hat.y;
  const double vt = P.u * t_hat.x + P.v * t_hat.y;

  // Mirror the normal velocity; keep the tangential component and the
  // interior density/pressure.
  PrimState Pb = P;
  Pb.u = -vn * n_hat.x + vt * t_hat.x;
  Pb.v = -vn * n_hat.y + vt * t_hat.y;
  return prim_to_cons(Pb, gas);
}

ConsState no_slip_wall_state(const ConsState& U_int, const GasConfig& gas) {
  const PrimState P = cons_to_prim(U_int, gas);
  PrimState Pb = P;
  Pb.u = 0.0;
  Pb.v = 0.0;
  return prim_to_cons(Pb, gas);
}

ConsState boundary_state(const ConsState& U_int, BCType bc,
                         const Vec2& normal, const Freestream& fs,
                         const GasConfig& gas) {
  switch (bc) {
    case BCType::Farfield:
      return farfield_state(U_int, fs, normal, gas);
    case BCType::SlipWall:
      return slip_wall_state(U_int, normal, gas);
    case BCType::NoSlipAdiabaticWall:
      return no_slip_wall_state(U_int, gas);
    case BCType::Interface:
      // 1-to-1 zone interface faces are internal faces in the serial mesh
      // and never reach the boundary path; fall back to a slip wall if one
      // does (conservative and safe).
      return slip_wall_state(U_int, normal, gas);
    case BCType::Invalid:
    default:
      throw std::runtime_error(
          "boundary_state: unsupported boundary condition type");
  }
}

}  // namespace cfd
