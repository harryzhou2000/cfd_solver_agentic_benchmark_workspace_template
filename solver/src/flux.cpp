// flux.cpp - Gas model helpers, inviscid fluxes (Roe + Rusanov), viscous fluxes.
//
// All inviscid Riemann fluxes work in the face-normal frame:
//   The caller provides UL, UR in the GLOBAL (x,y) frame plus the face normal
//   (nx, ny).  We rotate velocities to (normal, tangential), solve the 1-D
//   Riemann problem in the normal direction, and rotate the resulting flux
//   back to the global frame.
#include "cfd2d.hpp"
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace cfd2d {

double Freestream::T() const {
    return pressure / (rho * 1.0);  // R=1 nondimensional
}

double PhysicsConfig::mu() const {
    // Nondimensional: mu = rho_inf * U_inf * L_ref / Re = 1/Re
    if (reynolds <= 0.0) return 0.0;
    return 1.0 / reynolds;
}

Prim conservativeToPrimitive(const State& U, const GasModel& gas) {
    Prim W;
    W(0) = U(0);
    double invRho = 1.0 / U(0);
    W(1) = U(1) * invRho;
    W(2) = U(2) * invRho;
    double ke = 0.5 * (U(1)*U(1) + U(2)*U(2)) * invRho;
    double e = (U(3) - ke) * invRho;
    W(3) = (gas.gamma - 1.0) * U(0) * e;
    W(4) = W(3) / (W(0) * gas.R);
    return W;
}

State primitiveToConservative(const Prim& W, const GasModel& gas) {
    State U;
    U(0) = W(0);
    U(1) = W(0) * W(1);
    U(2) = W(0) * W(2);
    double e = W(3) / ((gas.gamma - 1.0) * W(0));
    double ke = 0.5 * W(0) * (W(1)*W(1) + W(2)*W(2));
    U(3) = e * W(0) + ke;
    return U;
}

double soundSpeed(const Prim& W, const GasModel& gas) {
    return std::sqrt(std::max(gas.gamma * W(3) / std::max(W(0), 1e-30), 1e-30));
}

// --- Rotate state: express velocities in (normal, tangential) frame ---
static inline State rotateState(const State& U, double nx, double ny) {
    // new_u = u*nx + v*ny  (normal)
    // new_v = -u*ny + v*nx (tangential)
    State R = U;
    double invRho = 1.0/U(0);
    double u = U(1)*invRho, v = U(2)*invRho;
    double un = u*nx + v*ny;
    double ut = -u*ny + v*nx;
    R(1) = U(0)*un;
    R(2) = U(0)*ut;
    return R;
}

static inline State rotateFluxBack(const State& Fn, double nx, double ny) {
    // Fn is flux in (normal, tangential) frame.
    // To get global flux F*nx + G*ny we map back:
    // F_global_momentum_x = Fn_momentum_normal*nx - Fn_momentum_tangential*ny
    // F_global_momentum_y = Fn_momentum_normal*ny + Fn_momentum_tangential*nx
    State F = Fn;
    double fn1 = Fn(1), fn2 = Fn(2);
    F(1) = fn1*nx - fn2*ny;
    F(2) = fn1*ny + fn2*nx;
    return F;
}

State eulerFlux(const State& U, const GasModel& gas) {
    Prim W = conservativeToPrimitive(U, gas);
    double rho = W(0), u = W(1), v = W(2), p = W(3);
    double H = (U(3) + p) / rho;
    State F;
    F(0) = rho * u;
    F(1) = rho * u * u + p;
    F(2) = rho * u * v;
    F(3) = rho * u * H;
    return F;
}

State rusanovFlux(const State& UL, const State& UR, const GasModel& gas, double scale) {
    // Rusanov / local Lax-Friedrichs flux.
    // UL, UR are in the GLOBAL frame; (nx,ny) is embedded via rotation.
    // Caller is expected to pass already-rotated states OR we handle rotation.
    // For this function, we assume the caller already rotated to normal frame
    // and passes nx=1, ny=0 implicitly.
    Prim WL = conservativeToPrimitive(UL, gas);
    Prim WR = conservativeToPrimitive(UR, gas);
    double aL = soundSpeed(WL, gas);
    double aR = soundSpeed(WR, gas);
    double smax = std::max(std::abs(WL(1)) + aL, std::abs(WR(1)) + aR) * scale;

    // Flux in normal frame (x = normal direction)
    State FL, FR;
    double rhoL=WL(0), uL=WL(1), vL=WL(2), pL=WL(3);
    double HL=(UL(3)+pL)/rhoL;
    FL(0)=rhoL*uL; FL(1)=rhoL*uL*uL+pL; FL(2)=rhoL*uL*vL; FL(3)=rhoL*uL*HL;
    double rhoR=WR(0), uR=WR(1), vR=WR(2), pR=WR(3);
    double HR=(UR(3)+pR)/rhoR;
    FR(0)=rhoR*uR; FR(1)=rhoR*uR*uR+pR; FR(2)=rhoR*uR*vR; FR(3)=rhoR*uR*HR;

    return 0.5*(FL + FR) - 0.5*smax*(UR - UL);
}

// Roe approximate Riemann solver in the normal frame.
// UL, UR must already be rotated so that the face normal is the x-axis.
// Returns the numerical flux in the normal frame.
static State roeFluxNormal(const State& UL, const State& UR, const GasModel& gas) {
    double rhoL = UL(0), rhoR = UR(0);
    if (rhoL <= 0 || rhoR <= 0) {
        // Fallback to Rusanov for nonphysical states
        return rusanovFlux(UL, UR, gas, 1.0);
    }
    double uL = UL(1)/rhoL, uR = UR(1)/rhoR;
    double vL = UL(2)/rhoL, vR = UR(2)/rhoR;
    double pL = (gas.gamma-1.0)*(UL(3) - 0.5*rhoL*(uL*uL+vL*vL));
    double pR = (gas.gamma-1.0)*(UR(3) - 0.5*rhoR*(uR*uR+vR*vR));
    if (pL <= 0 || pR <= 0) return rusanovFlux(UL, UR, gas, 1.0);

    double HL = (UL(3)+pL)/rhoL;
    double HR = (UR(3)+pR)/rhoR;

    double sqRhoL = std::sqrt(rhoL);
    double sqRhoR = std::sqrt(rhoR);
    double invSum = 1.0/(sqRhoL+sqRhoR);
    double rhoBar = sqRhoL*sqRhoR;
    double uBar = (sqRhoL*uL + sqRhoR*uR)*invSum;
    double vBar = (sqRhoL*vL + sqRhoR*vR)*invSum;
    double HBar = (sqRhoL*HL + sqRhoR*HR)*invSum;
    double a2 = (gas.gamma-1.0)*(HBar - 0.5*(uBar*uBar+vBar*vBar));
    if (a2 <= 0) a2 = 1e-12;
    double aBar = std::sqrt(a2);

    // Left and right fluxes (normal frame)
    State FL, FR;
    FL(0)=rhoL*uL; FL(1)=rhoL*uL*uL+pL; FL(2)=rhoL*uL*vL; FL(3)=rhoL*uL*HL;
    FR(0)=rhoR*uR; FR(1)=rhoR*uR*uR+pR; FR(2)=rhoR*uR*vR; FR(3)=rhoR*uR*HR;

    // Jumps
    double drho = rhoR - rhoL;
    double du   = uR - uL;
    double dv   = vR - vL;
    double dp   = pR - pL;

    // Wave strengths (Toro / Roe)
    double b1 = dp/(2.0*a2) - rhoBar*du/(2.0*aBar);  // u-a wave
    double b2 = drho - dp/a2;                          // entropy (shear in mass) wave
    double b3 = rhoBar*dv;                             // shear wave (tangential velocity)
    double b4 = dp/(2.0*a2) + rhoBar*du/(2.0*aBar);  // u+a wave

    // Eigenvalues
    double lam1 = uBar - aBar;
    double lam2 = uBar;
    double lam3 = uBar + aBar;

    // Harten-Yee entropy fix
    double eps = 0.1*aBar;
    auto efix = [&](double lam) -> double {
        double al = std::fabs(lam);
        return (al < eps) ? (0.5*(lam*lam/eps + eps)) : al;
    };
    double a1 = efix(lam1);
    double a2w = efix(lam2);
    double a3 = efix(lam3);

    double q2 = 0.5*(uBar*uBar + vBar*vBar);

    // Dissipation = sum |lambda|*alpha*r
    // r1=[1,u-a,v,H-u*a], r2=[1,u,v,q2], r3=[0,0,1,v], r4=[1,u+a,v,H+u*a]
    State diss;
    diss(0) = a1*b1*1.0 + a2w*b2*1.0 + a3*b4*1.0;
    diss(1) = a1*b1*(uBar-aBar) + a2w*b2*uBar + a3*b4*(uBar+aBar);
    diss(2) = a1*b1*vBar + a2w*b2*vBar + a2w*b3*1.0 + a3*b4*vBar;
    diss(3) = a1*b1*(HBar-uBar*aBar) + a2w*b2*q2 + a2w*b3*vBar + a3*b4*(HBar+uBar*aBar);

    return 0.5*(FL+FR) - 0.5*diss;
}

State roeFlux(const State& UL, const State& UR, const GasModel& gas, double scale) {
    // This is the public API: UL, UR in global frame, we assume caller rotates.
    // Actually, for the solver we'll call the normal-frame version directly
    // after rotating.  This wrapper assumes states are already in normal frame.
    (void)scale;
    return roeFluxNormal(UL, UR, gas);
}

// Public function: compute inviscid numerical flux given global-frame states
// and face normal.  This is the main entry point used by the solver.
State inviscidNumericalFlux(const State& UL, const State& UR,
                            double nx, double ny, const GasModel& gas,
                            const std::string& fluxType, double rusanovScale) {
    // Rotate to normal frame
    State ULn = rotateState(UL, nx, ny);
    State URn = rotateState(UR, nx, ny);

    State Fn;
    if (fluxType == "roe") {
        Fn = roeFluxNormal(ULn, URn, gas);
    } else {
        Fn = rusanovFlux(ULn, URn, gas, rusanovScale);
    }
    // Rotate flux back to global frame
    return rotateFluxBack(Fn, nx, ny);
}

State viscousFaceFlux(const Prim& WL, const Prim& WR, const ViscousData& grad,
                      double nx, double ny, double mu, double k, const GasModel& gas) {
    (void)WL; (void)WR; (void)gas;
    double dudx=grad.dudx, dudy=grad.dudy, dvdx=grad.dvdx, dvdy=grad.dvdy;
    double dTdx=grad.dTdx, dTdy=grad.dTdy;

    double div = dudx + dvdy;
    double tau_xx = mu*(2.0*dudx - 2.0/3.0*div);
    double tau_yy = mu*(2.0*dvdy - 2.0/3.0*div);
    double tau_xy = mu*(dudy + dvdx);
    double qx = -k*dTdx;
    double qy = -k*dTdy;

    double u = 0.5*(WL(1)+WR(1));
    double v = 0.5*(WL(2)+WR(2));

    State Fv;
    Fv(0) = 0.0;
    Fv(1) = tau_xx*nx + tau_xy*ny;
    Fv(2) = tau_xy*nx + tau_yy*ny;
    Fv(3) = (u*tau_xx + v*tau_xy - qx)*nx + (u*tau_xy + v*tau_yy - qy)*ny;
    return Fv;
}

} // namespace cfd2d
