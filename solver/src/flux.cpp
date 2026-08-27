#include "flux.hpp"

namespace fv {

State physFluxN(const Prim& w, double nx, double ny, const Gas& g) {
  double un = w.u * nx + w.v * ny;
  State F;
  F[0] = w.rho * un;
  F[1] = w.rho * w.u * un + w.p * nx;
  F[2] = w.rho * w.v * un + w.p * ny;
  double E = w.p / ((g.gamma - 1.0) * w.rho) + 0.5 * (w.u * w.u + w.v * w.v);
  F[3] = un * (w.rho * E + w.p);
  return F;
}

State rusanovFlux(const Prim& wl, const Prim& wr, double nx, double ny, const Gas& g, double scale) {
  State FL = physFluxN(wl, nx, ny, g);
  State FR = physFluxN(wr, nx, ny, g);
  double unL = wl.u * nx + wl.v * ny;
  double unR = wr.u * nx + wr.v * ny;
  double smax = std::max(std::fabs(unL) + wl.a(g), std::fabs(unR) + wr.a(g)) * scale;
  State UL = wl.toCons(g), UR = wr.toCons(g);
  State F;
  for (int i = 0; i < 4; ++i) F[i] = 0.5 * (FL[i] + FR[i]) - 0.5 * smax * (UR[i] - UL[i]);
  return F;
}

static inline double hartenFix(double lam, double delta) {
  double a = std::fabs(lam);
  if (a < delta) return (lam * lam + delta * delta) / (2.0 * delta);
  return a;
}

State roeFlux(const Prim& wl, const Prim& wr, double nx, double ny, const Gas& g) {
  // rotate velocities into (normal, tangential) frame; t = (-ny, nx)
  double unL = wl.u * nx + wl.v * ny, utL = -wl.u * ny + wl.v * nx;
  double unR = wr.u * nx + wr.v * ny, utR = -wr.u * ny + wr.v * nx;
  State FL = physFluxN(wl, nx, ny, g);
  State FR = physFluxN(wr, nx, ny, g);
  double HL = wl.a(g) * wl.a(g) / (g.gamma - 1.0) + 0.5 * (unL * unL + utL * utL);
  double HR = wr.a(g) * wr.a(g) / (g.gamma - 1.0) + 0.5 * (unR * unR + utR * utR);
  double sqL = std::sqrt(wl.rho), sqR = std::sqrt(wr.rho);
  double denom = sqL + sqR;
  double rhoRoe = sqL * sqR;
  double un = (sqL * unL + sqR * unR) / denom;
  double ut = (sqL * utL + sqR * utR) / denom;
  double H = (sqL * HL + sqR * HR) / denom;
  double q2 = un * un + ut * ut;
  double a2 = (g.gamma - 1.0) * (H - 0.5 * q2);
  a2 = std::max(a2, 1e-12);
  double a = std::sqrt(a2);
  // jumps
  double drho = wr.rho - wl.rho;
  double dp = wr.p - wl.p;
  double dun = unR - unL;
  double dut = utR - utL;
  // wave strengths
  double a1 = (dp - rhoRoe * a * dun) / (2.0 * a2);   // left acoustic
  double a2s = drho - dp / a2;                        // entropy/contact
  double a3 = rhoRoe * dut;                           // shear
  double a4 = (dp + rhoRoe * a * dun) / (2.0 * a2);   // right acoustic
  double lam1 = un - a, lam2 = un, lam4 = un + a;
  double delta = 0.1 * a;  // Harten entropy fix width
  double f1 = hartenFix(lam1, delta) * a1;
  double f2 = hartenFix(lam2, delta) * a2s;
  double f3 = hartenFix(lam2, delta) * a3;
  double f4 = hartenFix(lam4, delta) * a4;
  // dissipation vector in rotated conservative frame [rho, rho*un, rho*ut, rhoE]
  double d0 = f1 * 1.0 + f2 * 1.0 + f4 * 1.0;
  double d1 = f1 * lam1 + f2 * un + f4 * lam4;              // momentum-n (r3 has 0 n-comp)
  double d2 = f1 * ut + f2 * ut + f3 * 1.0 + f4 * ut;       // momentum-t
  double d3 = f1 * (H - un * a) + f2 * 0.5 * q2 + f3 * ut + f4 * (H + un * a);
  // rotate momentum dissipation back to (x, y)
  double dmx = d1 * nx - d2 * ny;
  double dmy = d1 * ny + d2 * nx;
  State F;
  F[0] = 0.5 * (FL[0] + FR[0]) - 0.5 * d0;
  F[1] = 0.5 * (FL[1] + FR[1]) - 0.5 * dmx;
  F[2] = 0.5 * (FL[2] + FR[2]) - 0.5 * dmy;
  F[3] = 0.5 * (FL[3] + FR[3]) - 0.5 * d3;
  return F;
}

Prim farfieldState(const Prim& wc, double nx, double ny, const Prim& wf, const Gas& g) {
  double unC = wc.u * nx + wc.v * ny;
  double utC = -wc.u * ny + wc.v * nx;
  double aC = wc.a(g);
  double unF = wf.u * nx + wf.v * ny;
  double utF = -wf.u * ny + wf.v * nx;
  double aF = wf.a(g);
  Prim wb;
  if (unC >= aC) {
    wb = wc;  // supersonic outflow
  } else if (unC <= -aC) {
    wb = wf;  // supersonic inflow
  } else {
    double Rp = unC + 2.0 * aC / (g.gamma - 1.0);
    double Rm = unF - 2.0 * aF / (g.gamma - 1.0);
    double unb = 0.5 * (Rp + Rm);
    double ab = 0.25 * (g.gamma - 1.0) * (Rp - Rm);
    ab = std::max(ab, 1e-10);
    double sb, utb;
    if (unb >= 0.0) {  // outflow: entropy and tangential velocity from inside
      sb = wc.p / std::pow(wc.rho, g.gamma);
      utb = utC;
    } else {           // inflow: entropy and tangential velocity from freestream
      sb = wf.p / std::pow(wf.rho, g.gamma);
      utb = utF;
    }
    wb.rho = std::pow(ab * ab / (g.gamma * sb), 1.0 / (g.gamma - 1.0));
    wb.p = wb.rho * ab * ab / g.gamma;
    wb.u = unb * nx - utb * ny;
    wb.v = unb * ny + utb * nx;
  }
  return wb;
}

Prim slipWallGhost(const Prim& wc, double nx, double ny) {
  Prim wg = wc;
  double un = wc.u * nx + wc.v * ny;
  wg.u = wc.u - 2.0 * un * nx;
  wg.v = wc.v - 2.0 * un * ny;
  return wg;
}

Prim noSlipGhost(const Prim& wc) {
  Prim wg = wc;
  wg.u = -wc.u;
  wg.v = -wc.v;
  return wg;
}

State wallPressureFlux(double pw, double nx, double ny) {
  State F = {0.0, 0.0, 0.0, 0.0};
  F[1] = pw * nx;
  F[2] = pw * ny;
  return F;
}

State viscousFluxN(double gux, double guy, double gvx, double gvy, double gTx, double gTy,
                   double uf, double vf, double mu, double k, double nx, double ny, const Gas& g) {
  double div = gux + gvy;
  double txx = 2.0 * mu * gux - (2.0 / 3.0) * mu * div;
  double tyy = 2.0 * mu * gvy - (2.0 / 3.0) * mu * div;
  double txy = mu * (guy + gvx);
  State F = {0.0, 0.0, 0.0, 0.0};
  F[1] = txx * nx + txy * ny;
  F[2] = txy * nx + tyy * ny;
  F[3] = (uf * txx + vf * txy + k * gTx) * nx + (uf * txy + vf * tyy + k * gTy) * ny;
  return F;
}

}  // namespace fv
