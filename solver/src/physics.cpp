#include "physics.h"

namespace cfd {

Cons flux_roe(const Prim& wl, const Prim& wr, const Cons& Ul, const Cons& Ur,
              double nx, double ny, const GasModel& gas, double eps_fix) {
  const double gm1 = gas.gamma - 1.0;
  double srl = std::sqrt(wl.rho), srr = std::sqrt(wr.rho);
  double den = srl + srr;
  double wlw = srl / den, wrw = srr / den;
  double u = wlw * wl.u + wrw * wr.u;
  double v = wlw * wl.v + wrw * wr.v;
  double Hl = (gas.gamma / gm1) * (wl.p / wl.rho) + 0.5 * (wl.u * wl.u + wl.v * wl.v);
  double Hr = (gas.gamma / gm1) * (wr.p / wr.rho) + 0.5 * (wr.u * wr.u + wr.v * wr.v);
  double H = wlw * Hl + wrw * Hr;
  double a2 = gm1 * (H - 0.5 * (u * u + v * v));
  if (a2 <= 0.0) a2 = 1.0e-12;
  double a = std::sqrt(a2);
  double un = u * nx + v * ny;

  // Entropy fix threshold (Harten-Yee).
  double delta = eps_fix * (a + std::abs(un));
  auto fix = [delta](double l) {
    if (std::abs(l) < delta) return (l * l + delta * delta) / (2.0 * delta);
    return std::abs(l);
  };

  double drho = wr.rho - wl.rho;
  double dun = (wr.u - wl.u) * nx + (wr.v - wl.v) * ny;
  double dut = -(wr.u - wl.u) * ny + (wr.v - wl.v) * nx;
  double dp = wr.p - wl.p;
  double rho_bar = srl * srr;  // sqrt(rhoL rhoR)

  double a1 = (dp - rho_bar * a * dun) / (2.0 * a2);
  double a2w = drho - dp / a2;
  double a3 = rho_bar * dut;
  double a4 = (dp + rho_bar * a * dun) / (2.0 * a2);

  double l1 = fix(un - a);
  double l2 = fix(un);
  double l3 = fix(un);
  double l4 = fix(un + a);

  // Dissipation D = sum |lambda| alpha r.
  double D1 = l1 * a1 + l2 * a2w + l4 * a4;
  double D2 = l1 * a1 * (u - a * nx) + l2 * a2w * u + l3 * a3 * (-ny) + l4 * a4 * (u + a * nx);
  double D3 = l1 * a1 * (v - a * ny) + l2 * a2w * v + l3 * a3 * nx + l4 * a4 * (v + a * ny);
  double D4 = l1 * a1 * (H - a * un) + l2 * a2w * (0.5 * (u * u + v * v)) +
              l3 * a3 * (-u * ny + v * nx) + l4 * a4 * (H + a * un);

  Cons Fl = flux_euler(wl, nx, ny, gas);
  Cons Fr = flux_euler(wr, nx, ny, gas);
  Cons F;
  F.rho = 0.5 * (Fl.rho + Fr.rho) - 0.5 * D1;
  F.rhou = 0.5 * (Fl.rhou + Fr.rhou) - 0.5 * D2;
  F.rhov = 0.5 * (Fl.rhov + Fr.rhov) - 0.5 * D3;
  F.rhoE = 0.5 * (Fl.rhoE + Fr.rhoE) - 0.5 * D4;
  return F;
}

}  // namespace cfd
