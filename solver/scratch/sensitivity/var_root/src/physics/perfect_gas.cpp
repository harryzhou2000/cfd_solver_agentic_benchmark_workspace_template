#include "physics/perfect_gas.h"

#include <cmath>

#include "core/exceptions.h"
#include "core/logging.h"

namespace cns2d {

FlowContext makeFlowContext(const CaseInput &input) {
  FlowContext ctx;
  ctx.gas = PerfectGas(input.gas);

  const Real rho = input.freestream.rho;
  const Real p = input.freestream.pressure;
  const Real speed = input.freestream.velocity_magnitude;
  const Real a = ctx.gas.soundSpeed(rho, p);
  const Real mach_from_state = speed / a;

  // The benchmark case files fix rho, |U| and p, and separately state the Mach
  // number.  Check them against each other so an inconsistent case file is
  // reported instead of silently producing the wrong flow.
  if (std::abs(mach_from_state - input.freestream.mach) > 1.0e-6 * std::max(1.0, input.freestream.mach)) {
    throw CnsError("inconsistent freestream in case '" + input.case_id + "': stated mach " +
                   std::to_string(input.freestream.mach) + " but rho/p/|U| imply " +
                   std::to_string(mach_from_state));
  }

  const Real aoa = input.freestream.aoa_degrees * M_PI / 180.0;
  ctx.flow_direction = {std::cos(aoa), std::sin(aoa)};
  const Real u = speed * ctx.flow_direction.x;
  const Real v = speed * ctx.flow_direction.y;

  ctx.freestream_prim = {rho, u, v, p};
  ctx.freestream_cons = ctx.gas.consFromPrim(ctx.freestream_prim);
  ctx.freestream_mach = input.freestream.mach;
  ctx.freestream_speed = speed;
  ctx.freestream_sound_speed = a;
  ctx.dynamic_pressure = 0.5 * rho * speed * speed;

  ctx.ref_area = input.reference.area;
  ctx.ref_length = input.reference.length;
  ctx.moment_center = input.reference.moment_center;

  if (input.physics.mode == PhysicsMode::kLaminar) {
    ctx.viscous = true;
    if (input.physics.viscosity_model == ViscosityModel::kConstant) {
      ctx.transport = TransportModel::makeConstant(rho, speed, input.reference.reynolds_length,
                                                   input.physics.reynolds);
      logInfo(formatString(
          "physics: laminar, Re = %.6g, constant mu = %.6e (rho_inf*U_inf*L_ref/Re), Pr = %.4g",
          input.physics.reynolds, ctx.transport.referenceViscosity(), ctx.gas.prandtl()));
    } else {
      const Real t_inf = ctx.gas.temperatureFromRhoP(rho, p);
      const Real mu_inf = rho * speed * input.reference.reynolds_length / input.physics.reynolds;
      // 110.4 K / 288.15 K is the standard-air ratio; expressed relative to the
      // freestream temperature so the model stays nondimensional.
      ctx.transport = TransportModel::makeSutherland(mu_inf, t_inf, 110.4 / 288.15);
      logInfo(formatString("physics: laminar, Re = %.6g, Sutherland mu_inf = %.6e, T_inf = %.6g",
                           input.physics.reynolds, mu_inf, t_inf));
    }
  } else {
    ctx.viscous = false;
    ctx.transport = TransportModel::makeInviscid();
    logInfo("physics: inviscid (viscosity and heat conduction identically zero)");
  }

  return ctx;
}

}  // namespace cns2d
