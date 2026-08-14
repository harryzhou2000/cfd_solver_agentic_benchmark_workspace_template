#include "flux.hpp"

#include <algorithm>
#include <cmath>

namespace cfd2d {

BcType bcTypeFromString(const std::string& s) {
  if (s == "farfield") return BcType::Farfield;
  if (s == "slip_wall") return BcType::SlipWall;
  if (s == "no_slip_adiabatic_wall") return BcType::NoSlipAdiabaticWall;
  throw FatalError("unsupported boundary condition: " + s);
}

std::string bcTypeToString(BcType t) {
  switch (t) {
    case BcType::Farfield: return "farfield";
    case BcType::SlipWall: return "slip_wall";
    case BcType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
  }
  return "unknown";
}

Vec4 inviscidPhysicalFlux(const Gas& g, const Vec4& W, double nx, double ny) {
  const double rho = W[0], u = W[1], v = W[2], p = W[3];
  const double un = u * nx + v * ny;
  const double E = p / (g.gamma - 1.0) + 0.5 * rho * (u * u + v * v);
  return {rho * un, rho * u * un + p * nx, rho * v * un + p * ny,
          un * (E + p)};
}

double convectiveSpectralRadius(const Gas& g, const Vec4& W, double nx,
                                double ny, double len) {
  const double a = soundSpeed(g, std::max(W[0], 1e-12), std::max(W[3], 1e-12));
  return (std::abs(W[1] * nx + W[2] * ny) + a) * len;
}

Vec4 rusanovFlux(const FluxContext& ctx, const Vec4& WL, const Vec4& WR,
                 const Vec4& UL, const Vec4& UR, double nx, double ny,
                 double len, double* lambdaOut) {
  const Vec4 FL = inviscidPhysicalFlux(ctx.gas, WL, nx, ny);
  const Vec4 FR = inviscidPhysicalFlux(ctx.gas, WR, nx, ny);
  const double lam = ctx.rusanovScale *
                     std::max(convectiveSpectralRadius(ctx.gas, WL, nx, ny, 1.0),
                              convectiveSpectralRadius(ctx.gas, WR, nx, ny, 1.0));
  if (lambdaOut) *lambdaOut = lam * len;
  Vec4 F{};
  for (int q = 0; q < 4; ++q) F[q] = (0.5 * (FL[q] + FR[q]) - 0.5 * lam * (UR[q] - UL[q])) * len;
  return F;
}

Vec4 viscousFlux(const FluxContext& ctx, double dudx, double dudy, double dvdx,
                 double dvdy, double dTdx, double dTdy, double uf, double vf,
                 double nx, double ny, double len) {
  if (!ctx.viscous || ctx.mu <= 0.0) return Vec4{0., 0., 0., 0.};
  const double div = dudx + dvdy;
  const double tau_xx = 2.0 * ctx.mu * dudx - (2.0 / 3.0) * ctx.mu * div;
  const double tau_yy = 2.0 * ctx.mu * dvdy - (2.0 / 3.0) * ctx.mu * div;
  const double tau_xy = ctx.mu * (dudy + dvdx);
  // Fourier heat flux q = -k grad T. The conservative viscous flux on the RHS
  // is [0, tau.n, tau.n, u(tau.n)_x+v(tau.n)_y-q.n].
  const double qn = -ctx.kCond * (dTdx * nx + dTdy * ny);
  const double tx = tau_xx * nx + tau_xy * ny;
  const double ty = tau_xy * nx + tau_yy * ny;
  return {0.0, tx * len, ty * len, (uf * tx + vf * ty - qn) * len};
}

void eulerFluxJacobian(const Gas& g, const Vec4& W, double nx, double ny,
                       double A[16]) {
  // F_n(U) = [rho un, rho u un + p nx, rho v un + p ny, un (rho E + p)].
  const double rho = std::max(W[0], 1e-12);
  const double u = W[1], v = W[2], p = W[3];
  const double gm = g.gamma - 1.0;
  const double un = u * nx + v * ny;
  const double q2 = u * u + v * v;
  const double phi = gm * 0.5 * q2;                    // (gamma-1)(u^2+v^2)/2
  const double H = g.gamma * p / (rho * gm) + 0.5 * q2;  // a^2/(gamma-1)+q2/2
  std::fill(A, A + 16, 0.0);
  // Row 0: d(rho un)/dU = [0, nx, ny, 0]
  A[0] = 0.0;         A[1] = nx;              A[2] = ny;              A[3] = 0.0;
  // Row 1: d(rho u un + p nx)/dU
  A[4] = -u * un + phi * nx;
  A[5] = un + (2.0 - g.gamma) * u * nx;
  A[6] = u * ny - gm * v * nx;
  A[7] = gm * nx;
  // Row 2: d(rho v un + p ny)/dU
  A[8] = -v * un + phi * ny;
  A[9] = v * nx - gm * u * ny;
  A[10] = un + (2.0 - g.gamma) * v * ny;
  A[11] = gm * ny;
  // Row 3: d(un (rhoE + p))/dU
  A[12] = un * (phi - H);
  A[13] = H * nx - gm * u * un;
  A[14] = H * ny - gm * v * un;
  A[15] = g.gamma * un;
}

Vec4 boundaryGhostState(BcType bc, const FluxContext& ctx,
                        const FreeStream& fs, const Vec4& Wi, double nx,
                        double ny) {
  Vec4 W = Wi;
  if (bc == BcType::Farfield) {
    // Locally one-dimensional characteristic (Riemann-invariant) farfield.
    const double gamma = ctx.gas.gamma;
    const double unI = Wi[1] * nx + Wi[2] * ny;
    const double aI = soundSpeed(ctx.gas, Wi[0], Wi[3]);
    const double unF = fs.u * nx + fs.v * ny;
    const double aF = fs.soundSpeed;
    if (unI >= aI) {
      // Supersonic outflow: everything comes from inside.
      W = Wi;
    } else if (unI <= -aI) {
      // Supersonic inflow: everything imposed from the freestream.
      W[0] = fs.rho;
      W[1] = fs.u;
      W[2] = fs.v;
      W[3] = fs.pressure;
    } else {
      // Subsonic: combine interior R+ with freestream R-.
      const double Rplus = unI + 2.0 * aI / (gamma - 1.0);
      const double Rminus = unF - 2.0 * aF / (gamma - 1.0);
      const double unB = 0.5 * (Rplus + Rminus);
      const double aB = 0.25 * (gamma - 1.0) * (Rplus - Rminus);
      double sB, utx, uty;
      if (unI >= 0.0) {
        // Subsonic outflow: entropy and tangential velocity from inside.
        sB = Wi[3] / std::pow(Wi[0], gamma);
        utx = Wi[1] - unI * nx;
        uty = Wi[2] - unI * ny;
      } else {
        // Subsonic inflow: entropy and tangential velocity from freestream.
        sB = fs.pressure / std::pow(fs.rho, gamma);
        utx = fs.u - unF * nx;
        uty = fs.v - unF * ny;
      }
      const double rhoB = std::pow(aB * aB / (gamma * sB), 1.0 / (gamma - 1.0));
      W[0] = rhoB;
      W[1] = utx + unB * nx;
      W[2] = uty + unB * ny;
      W[3] = rhoB * aB * aB / gamma;
    }
  } else if (bc == BcType::SlipWall) {
    // Mirror only the normal velocity; density and pressure remain continuous.
    const double un = Wi[1] * nx + Wi[2] * ny;
    W[1] = Wi[1] - 2.0 * un * nx;
    W[2] = Wi[2] - 2.0 * un * ny;
  } else if (bc == BcType::NoSlipAdiabaticWall) {
    // Mirror velocity around zero. Keep density/pressure, which gives an
    // adiabatic wall and a zero reported wall velocity.
    W[1] = -Wi[1];
    W[2] = -Wi[2];
  }
  (void)ctx;
  return W;
}

}  // namespace cfd2d
