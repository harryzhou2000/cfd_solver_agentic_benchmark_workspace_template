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
  if (p.use_ausmup) {
    // AUSM+-up (Liou 2006, "A Sequel to AUSM, Part II", JCP 214:137-170).
    // Split flux F = F_conv + F_pres with low-Mach pressure-velocity coupling.
    // Formulas cross-confirmed vs SU2 (ausm_slau.cpp), myFoam (AUSMplusUpFlux.C),
    // and CATO (ausm_plus_solver.f90, which cites Liou eq numbers inline).
    //   F_conv = m12 * [1, u, v, H]^T_upwind  (mass flux m12 carries rho_up)
    //   F_pres = [0, p12*nx, p12*ny, 0]^T      (pressure only in momentum)
    // Two "-up" low-Mach terms (the key ingredients pstab lacked, done RIGHT):
    //   M_p : pressure jump  -> Mach/mass flux   (Delta-p coupling, Eq.21)
    //   p_u : normal-vel jump -> pressure        (Delta-Un coupling, Eq.26/75)
    // Constants (Liou 2006): beta=1/8 (M4), alpha=3/16 all-speed (P5), sigma=1,
    //   K_p=0.25, K_u=0.75 (cross-confirmed by 4 independent implementations).
    // Consistency: at uniform flow M4+ + M4- = M and P5+ + P5- = 1 (verified
    // algebraically and by unit test), so the flux reduces to the physical flux.
    const double gm1 = g.gm1();
    double rhoL = std::max(L.rho,1e-30), rhoR = std::max(R.rho,1e-30);
    double pL = L.p, pR = R.p;
    double aLv = g.soundSpeed(L), aRv = g.soundSpeed(R);
    double HL = (g.consFromPrim(L).rhoE() + pL) / rhoL;  // H=(E+p)/rho
    double HR = (g.consFromPrim(R).rhoE() + pR) / rhoR;
    // --- interface sound speed a_{1/2} (Liou Eq.28-30): enthalpy-based a* then
    //   per-side signed denominator, then min. At low Mach (|Un|<<a*) this
    //   reduces to a*, but the exact form stays robust through the startup
    //   transient where local cells may be briefly supersonic. ---
    double coef = 2.0*gm1/(g.gamma+1.0);
    double aStarL = std::sqrt(std::max(coef*std::fabs(HL), 1e-30));
    double aStarR = std::sqrt(std::max(coef*std::fabs(HR), 1e-30));
    double aHatL = aStarL*aStarL / std::max(aStarL,  unL);
    double aHatR = aStarR*aStarR / std::max(aStarR, -unR);
    double a12 = std::min(aHatL, aHatR);
    a12 = std::max(a12, 1e-12);   // positivity floor
    // Mach numbers (denominator is a_{1/2}, confirmed)
    double ML = unL / a12, MR = unR / a12;
    // --- Mach splitting M4 (Liou Eq.20, beta=1/8): M4+ + M4- = M ---
    const double beta_m4 = 1.0/8.0;
    auto M4p = [&](double M)->double{
      if (M >= 1.0) return 0.5*(M+M);
      if (M <= -1.0) return 0.0;
      double m1 = M+1.0, m2 = M*M-1.0;
      return 0.25*m1*m1 + beta_m4*m2*m2;
    };
    auto M4m = [&](double M)->double{
      if (M >= 1.0) return 0.0;
      if (M <= -1.0) return 0.5*(M+M);   // M<0: 0.5*(M-|M|)=M
      double m1 = M-1.0, m2 = M*M-1.0;
      return -0.25*m1*m1 - beta_m4*m2*m2;
    };
    // --- low-Mach scaling f_a (Eq.70-72): M0^2=min(1,max(Mbar^2,Minf^2)), f_a=M0(2-M0) ---
    double Mbar2 = 0.5*(unL*unL + unR*unR) / (a12*a12);
    double Minf2 = (p.ausmup_mcut > 0.0) ? p.ausmup_mcut*p.ausmup_mcut : 0.09;
    double M0sq = std::min(1.0, std::max(Mbar2, Minf2));
    double M0 = std::sqrt(M0sq);
    double fa = M0*(2.0 - M0);            // f_a(0)=0, f_a(1)=1
    fa = std::max(fa, 1e-6);              // avoid 1/fa singularity
    // --- P5 pressure splitting (Eq.24), all-speed alpha (Eq.76): alpha=(3/16)(-4+5 fa^2) ---
    double alpha_p5 = (3.0/16.0)*(-4.0 + 5.0*fa*fa);
    auto P5p = [&](double M)->double{
      if (M >= 1.0) return 1.0;
      if (M <= -1.0) return 0.0;
      double m1 = M+1.0, m2 = M*M-1.0;
      return 0.25*m1*m1*(2.0-M) + alpha_p5*M*m2*m2;
    };
    auto P5m = [&](double M)->double{
      if (M >= 1.0) return 0.0;
      if (M <= -1.0) return 1.0;
      double m1 = M-1.0, m2 = M*M-1.0;
      return 0.25*m1*m1*(2.0+M) - alpha_p5*M*m2*m2;
    };
    // --- M_p: pressure-jump -> Mach coupling (Eq.21). rho_{1/2}=0.5(rhoL+rhoR). ---
    //   M_p = -(Kp/fa) * max(1 - sigma*Mbar^2, 0) * (pR-pL)/(rho12*a12^2)
    double rho12 = 0.5*(rhoL + rhoR);
    double sigma = 1.0;
    double Kp = p.ausmup_kp;
    double Mp = 0.0;
    if (Kp > 0.0) {
      double cut = std::max(1.0 - sigma*Mbar2, 0.0);
      Mp = -(Kp/fa) * cut * (pR - pL) / (rho12 * a12 * a12);
    }
    double M12 = M4p(ML) + M4m(MR) + Mp;
    // --- p_{1/2} (Eq.75): P5*p + p_u, where p_u is the velocity-jump -> pressure coupling ---
    double PpL = P5p(ML), PmR = P5m(MR);
    double p12 = PpL*pL + PmR*pR;
    double Ku = p.ausmup_ku;
    if (Ku > 0.0) {
      // p_u = -Ku * P5+(ML)*P5-(MR) * (rhoL+rhoR) * (fa*a12) * (unR-unL)   (Eq.26)
      // This damps the high-freq pressure mode driven by normal-velocity jumps
      // (the cap-1.0 blowup mode); the shedding shear is mostly TANGENTIAL so
      // unR-unL across shear-aligned faces is small -> minimal over-damping.
      double pu = -Ku * PpL * PmR * (rhoL + rhoR) * (fa * a12) * (unR - unL);
      p12 += pu;
    }
    // --- mass flux + upwind convective state (chosen by sign of M12) ---
    double m12 = a12 * M12;               // velocity scale (rho enters via rho_up)
    double rho_up, u_up, v_up, H_up;
    if (m12 >= 0.0) { rho_up = rhoL; u_up = L.u; v_up = L.v; H_up = HL; }
    else            { rho_up = rhoR; u_up = R.u; v_up = R.v; H_up = HR; }
    F.rho()  = m12 * rho_up;
    F.rhou() = m12 * rho_up * u_up + p12 * nx;
    F.rhov() = m12 * rho_up * v_up + p12 * ny;
    F.rhoE() = m12 * rho_up * H_up;       // rho*H = rho*E + p (pressure carried in energy)
    return F;
  }
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
  if (p.use_hllc) {
    // HLLC (Harten-Lax-van Leer-Contact, Toro) 3-wave solver. Resolves the
    // contact/shear wave (less dissipative than Rusanov/HLL) and its multi-
    // wave pressure coupling is more low-Mach robust than Roe (less carbuncle
    // / odd-even decoupling), so it may capture Re200 shedding where Roe blows
    // up and Rusanov over-damps. Formulas from Toro, Riemann Solvers.
    double rhoL = L.rho, rhoR = R.rho, pL = L.p, pR = R.p;
    // Roe-averaged state for the Einfeldt wave-speed bounds
    double srL = std::sqrt(std::max(rhoL,1e-30)), srR = std::sqrt(std::max(rhoR,1e-30));
    double denr = srL + srR;
    double uRoe = (srL*L.u + srR*R.u)/denr, vRoe = (srL*L.v + srR*R.v)/denr;
    double HL = (g.consFromPrim(L).rhoE() + pL)/std::max(rhoL,1e-30);
    double HR = (g.consFromPrim(R).rhoE() + pR)/std::max(rhoR,1e-30);
    double HRoe = (srL*HL + srR*HR)/denr;
    double aRoe = std::sqrt(std::max(1e-30, g.gm1()*(HRoe - 0.5*(uRoe*uRoe + vRoe*vRoe))));
    double unRoe = uRoe*nx + vRoe*ny;
    double SL = std::min(unL - aL, unRoe - aRoe);
    double SR = std::max(unR + aR, unRoe + aRoe);
    double coefL = rhoL*(SL - unL), coefR = rhoR*(SR - unR);
    double denom_sm = coefL - coefR;
    double SM = (denom_sm != 0.0)
      ? (pR - pL + coefL*unL - coefR*unR) / denom_sm : 0.5*(unL + unR);
    double pStar = rhoL*(unL - SL)*(unL - SM) + pL;
    // tangential basis (t perpendicular to n) -- contact preserves tang. vel.
    double tx = -ny, ty = nx;
    double utL = L.u*tx + L.v*ty, utR = R.u*tx + R.v*ty;
    double rhoStL = rhoL*(SL - unL)/(SL - SM);
    double rhoStR = rhoR*(SR - unR)/(SR - SM);
    double ustL = SM*nx + utL*tx, vstL = SM*ny + utL*ty;
    double ustR = SM*nx + utR*tx, vstR = SM*ny + utR*ty;
    double gm1 = g.gm1();
    double EstL = pStar/gm1 + 0.5*rhoStL*(ustL*ustL + vstL*vstL);
    double EstR = pStar/gm1 + 0.5*rhoStR*(ustR*ustR + vstR*vstR);
    Cons ULc = g.consFromPrim(L), URc = g.consFromPrim(R);
    Cons UstL, UstR;
    UstL.rho() = rhoStL; UstL.rhou() = rhoStL*ustL; UstL.rhov() = rhoStL*vstL; UstL.rhoE() = EstL;
    UstR.rho() = rhoStR; UstR.rhou() = rhoStR*ustR; UstR.rhov() = rhoStR*vstR; UstR.rhoE() = EstR;
    if (SL >= 0.0) { F = FL; }
    else if (SM >= 0.0) { for (int k=0;k<NEQ;++k) F.v[k]=FL.v[k]+SL*(UstL.v[k]-ULc.v[k]); }
    else if (SR >= 0.0) { for (int k=0;k<NEQ;++k) F.v[k]=FR.v[k]+SR*(UstR.v[k]-URc.v[k]); }
    else { F = FR; }
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
  // Low-Mach pressure stabilization (Rhie-Chow-like): a 2nd-order (scalar-
  // linearizable) pressure-gradient-driven momentum coupling that damps the
  // high-frequency pressure checkerboard (the cap-1.0 blowup mode at low
  // Mach, |un| << a) without damping the low-frequency shedding. For a
  // pressure peak (dp=R.p-L.p<0) this adds a positive outward momentum flux
  // (R[lc]-=flux/A -> dU/dt<0 at the peak), breaking the decoupling.
  if (p.pstab_k > 0.0) {
    double amp = p.pstab_k * (a - std::fabs(un));
    if (amp > 0.0) {
      F.rhou() -= amp * dp * nx;
      F.rhov() -= amp * dp * ny;
      F.rhoE() -= amp * dp * un;
    }
  }
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
