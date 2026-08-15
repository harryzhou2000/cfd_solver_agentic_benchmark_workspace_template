// Physics implementation: Roe/Rusanov flux, viscous flux, BC ghost states,
// force integration.
#include "physics.hpp"
#include <algorithm>
#include <cmath>

namespace cfd {

void Physics::init(const GasModel& g, const Freestream& f, const Reference& r,
                   bool lam, double Re, double rusanov, bool roe) {
  gas = g; fs = f; ref = r;
  laminar = lam; reynolds = Re; rusanov_scale = rusanov; use_roe = roe;
  W_inf.rho = fs.rho; W_inf.u = fs.u(); W_inf.v = fs.v(); W_inf.p = fs.pressure;
  U_inf = gas.consFromPrim(W_inf);
  q_inf = 0.5 * fs.rho * fs.velocity * fs.velocity;
  if (laminar && Re > 0.0) {
    // mu = rho_inf * U_inf * L_ref / Re  (constant viscosity matching Re)
    mu = fs.rho * fs.velocity * ref.reynolds_length / Re;
    k_thermal = mu * gas.cp() / gas.Pr;
  } else {
    mu = 0.0; k_thermal = 0.0;
  }
}

Cons inviscidNormalFlux(const GasModel& gas, const Prim& w, double nx, double ny) {
  Cons F;
  double un = w.u * nx + w.v * ny;
  double rho = w.rho, p = w.p;
  double rhoE = gas.consFromPrim(w).rhoE();  // rho*E per volume
  F.rho()  = rho * un;
  F.rhou() = rho * w.u * un + p * nx;
  F.rhov() = rho * w.v * un + p * ny;
  F.rhoE() = (rhoE + p) * un;
  return F;
}

Cons numericalInviscidFlux(const Physics& p, const Prim& L, const Prim& R,
                            double nx, double ny) {
  const GasModel& g = p.gas;
  Cons FL = inviscidNormalFlux(g, L, nx, ny);
  Cons FR = inviscidNormalFlux(g, R, nx, ny);
  Cons F;
  for (int i = 0; i < NEQ; ++i) F.v[i] = 0.5 * (FL.v[i] + FR.v[i]);
  double aL = g.soundSpeed(L), aR = g.soundSpeed(R);
  double unL = L.u*nx + L.v*ny, unR = R.u*nx + R.v*ny;
  if (!p.use_roe) {
    // Rusanov / local Lax-Friedrichs: -0.5*alpha*(R-L), alpha=max(|un|+a)
    double alpha = std::max(std::fabs(unL) + aL, std::fabs(unR) + aR) * p.rusanov_scale;
    F.rho()  -= 0.5*alpha*(R.rho - L.rho);
    F.rhou() -= 0.5*alpha*(R.rho*R.u - L.rho*L.u);
    F.rhov() -= 0.5*alpha*(R.rho*R.v - L.rho*L.v);
    Cons UR = g.consFromPrim(R), UL = g.consFromPrim(L);
    F.rhoE() -= 0.5*alpha*(UR.rhoE() - UL.rhoE());
    return F;
  }
  // Roe average
  double srL = std::sqrt(std::max(L.rho, 1e-30));
  double srR = std::sqrt(std::max(R.rho, 1e-30));
  double denom = srL + srR;
  double u = (srL*L.u + srR*R.u) / denom;
  double v = (srL*L.v + srR*R.v) / denom;
  double HL = (g.consFromPrim(L).rhoE() + L.p) / std::max(L.rho,1e-30);  // H = (E+p)/rho
  double HR = (g.consFromPrim(R).rhoE() + R.p) / std::max(R.rho,1e-30);
  double H = (srL*HL + srR*HR) / denom;
  double V2 = u*u + v*v;
  double a2 = std::max(1e-30, g.gm1() * (H - 0.5*V2));
  double a = std::sqrt(a2);
  double un = u*nx + v*ny;
  // primitive jumps
  double drho = R.rho - L.rho, du = R.u - L.u, dv = R.v - L.v, dp = R.p - L.p;
  double dun = du*nx + dv*ny;            // jump in normal velocity
  double dut = du*(-ny) + dv*nx;         // jump in tangential velocity
  double rho_roe = srL * srR;            // Roe-averaged density (for shear term)
  // wave strengths
  double alpha1 = (dp - rho_roe*a*dun) / (2.0*a2);  // |un-a| wave
  double alpha2 = drho - dp/a2;                      // entropy |un| wave
  double alpha3 = rho_roe * dut;                     // shear |un| wave
  double alpha4 = (dp + rho_roe*a*dun) / (2.0*a2);  // |un+a| wave
  // eigenvalues with Harten-Yee entropy fix
  auto efix = [a](double lam) {
    double delta = 0.1 * a;          // Harten entropy coefficient
    if (lam < -delta) return -lam;   // |lam| with sign convention below
    if (lam >  delta) return  lam;
    return lam*lam/(2.0*delta) + delta*0.5;  // smoothed |lam|
  };
  // |lambda_k| values (entropy-fixed)
  double aL1 = efix(un - a);
  double aL2 = efix(un);
  double aL4 = efix(un + a);
  // dissipation = sum |lambda_k| alpha_k r_k ; r_k as conservative increments
  // r1=[1,u-a*nx,v-a*ny,H-a*un], r2=[1,u,v,0.5V2], r3=[0,-ny,nx,un_t], r4=[1,u+a*nx,v+a*ny,H+a*un]
  double unt = u*(-ny) + v*nx;
  double H_minus = H - a*un, H_plus = H + a*un;
  double r1v[4] = {1, u - a*nx, v - a*ny, H_minus};
  double r2v[4] = {1, u, v, 0.5*V2};
  double r3v[4] = {0, -ny, nx, unt};
  double r4v[4] = {1, u + a*nx, v + a*ny, H_plus};
  // alpha3 (shear) is associated with the un wave together with alpha2
  double diss[4] = {0,0,0,0};
  for (int k = 0; k < 4; ++k) {
    diss[k] += aL1 * alpha1 * r1v[k];
    diss[k] += aL2 * alpha2 * r2v[k];
    diss[k] += aL2 * alpha3 * r3v[k];
    diss[k] += aL4 * alpha4 * r4v[k];
  }
  // convert r-energy entry (H ... / the 4th component) back to conservative
  // rhoE increment: dF_rhoE uses the same Roe eigenvector 4th comp which is
  // already per-mass energy H -> the conservative flux dissipation uses it
  // directly (Roe's H-based eigenvector). No extra scaling needed.
  for (int k = 0; k < 4; ++k) F.v[k] -= 0.5 * diss[k];
  return F;
}

Cons viscousNormalFlux(const Physics& p, double u, double v, double T,
                        double ux, double uy, double vx, double vy,
                        double Tx, double Ty, double nx, double ny) {
  // Physical viscous flux in direction n for the compressible NS in
  // conservative form  dU/dt + div(F_inviscid - tau_dot + q) = 0, i.e. the
  // viscous part of the numerical flux (to ADD to the inviscid flux) is
  //   F_visc = [0, -(tau.n)_x, -(tau.n)_y, -(u*tau_nx + v*tau_ny) + q.n]
  // with tau the Newtonian stress tensor and q = -k grad T (Fourier). The
  // NEGATIVE sign on the stress terms is essential: the stress removes
  // momentum/kinetic energy (dissipation). A flipped sign makes viscosity
  // anti-diffusive and blows up low-Mach viscous cases from step 0.
  Cons F;  // zero mass entry
  if (!p.laminar || p.mu <= 0.0) return F;
  double mu = p.mu;
  double div = ux + vy;
  double txx = 2.0*mu*ux - (2.0/3.0)*mu*div;
  double tyy = 2.0*mu*vy - (2.0/3.0)*mu*div;
  double txy = mu*(uy + vx);
  double qx = -p.k_thermal * Tx;
  double qy = -p.k_thermal * Ty;
  double taunx = txx*nx + txy*ny;   // (tau.n)_x
  double tauny = txy*nx + tyy*ny;   // (tau.n)_y
  double qn = qx*nx + qy*ny;        // q.n  (heat flux in direction n)
  F.rhou() = -taunx;
  F.rhov() = -tauny;
  F.rhoE() = -(u*taunx + v*tauny) + qn;
  return F;
}

Prim bcGhostState(const Physics& p, BCType bc, const Prim& Wc, double nx, double ny) {
  Prim g = Wc;
  switch (bc) {
    case BCType::Farfield: {
      // Weakly-imposed Riemann farfield: ghost = freestream for ALL faces so the
      // boundary flux is a Riemann solve (interior vs freestream) that damps
      // outgoing waves toward the freestream (non-reflecting-ish). At the
      // farfield the field is ~freestream so this is consistent and stable.
      g = p.W_inf;
      break;
    }
    case BCType::SlipWall: {
      // reflect normal velocity: u_ghost = u_cell - 2*un*n
      double un = Wc.u*nx + Wc.v*ny;
      g.u = Wc.u - 2.0*un*nx;
      g.v = Wc.v - 2.0*un*ny;
      g.rho = Wc.rho; g.p = Wc.p;
      break;
    }
    case BCType::NoSlipAdiabaticWall: {
      // no-slip: mirror velocity so face value is 0; adiabatic: dT/dn=0
      g.u = -Wc.u;
      g.v = -Wc.v;
      g.rho = Wc.rho;
      g.p = Wc.p;            // dp/dn ~ 0 (zero pressure gradient at wall)
      // temperature: adiabatic -> T_ghost = T_cell (zero normal gradient)
      break;
    }
    default: break;
  }
  return g;
}

Forces computeForceCoeffs(const Physics& p,
    const std::vector<double>& wcx, const std::vector<double>& wcy,
    const std::vector<double>& wSx, const std::vector<double>& wSy,
    const std::vector<double>& wlen,
    const std::vector<double>& wp,
    const std::vector<double>& wtx, const std::vector<double>& wty) {
  Forces f{0,0,0,0,0,0,0};
  double Fpx=0, Fpy=0, Fvx=0, Fvy=0, Mz=0;
  double p_ref = p.fs.pressure;
  for (size_t i = 0; i < wcx.size(); ++i) {
    double len = wlen[i];
    double nx = wSx[i]/std::max(len,1e-30), ny = wSy[i]/std::max(len,1e-30);
    // pressure force on body = sum (p - p_inf) * S  (S = outward-from-fluid normal)
    Fpx += (wp[i] - p_ref) * wSx[i];
    Fpy += (wp[i] - p_ref) * wSy[i];
    // viscous traction t = (tx,ty); tangential (skin friction) = t - (t.n)n
    double tx = wtx[i], ty = wty[i];
    double tn = tx*nx + ty*ny;
    double ttx = tx - tn*nx, tty = ty - tn*ny;   // tangential traction
    Fvx += ttx;   // already scaled by len? traction passed as per-face full (incl len). See solver.
    Fvy += tty;
    // moment about reference center
    double rx = wcx[i] - p.ref.moment_center.x;
    double ry = wcy[i] - p.ref.moment_center.y;
    double fxt = (wp[i]-p_ref)*wSx[i] + ttx;
    double fyt = (wp[i]-p_ref)*wSy[i] + tty;
    Mz += rx*fyt - ry*fxt;
  }
  double q = std::max(p.q_inf, 1e-30);
  double A = std::max(p.ref.area, 1e-30);
  // drag/lift along freestream direction (cos a, sin a) and perpendicular
  double ca = std::cos(p.fs.aoa_rad), sa = std::sin(p.fs.aoa_rad);
  f.pressure_drag = (Fpx*ca + Fpy*sa)/(q*A);
  f.pressure_lift = (-Fpx*sa + Fpy*ca)/(q*A);
  f.viscous_drag  = (Fvx*ca + Fvy*sa)/(q*A);
  f.viscous_lift  = (-Fvx*sa + Fvy*ca)/(q*A);
  f.cd = f.pressure_drag + f.viscous_drag;
  f.cl = f.pressure_lift + f.viscous_lift;
  f.cmz = Mz / (q*A*p.ref.length);
  return f;
}

}  // namespace cfd
