#include "physics.hpp"

#include <algorithm>
#include <cmath>

namespace cfd {

namespace {

constexpr double kTiny = 1e-300;

// Solve a small 2x2 symmetric positive definite system with a small Tikhonov
// regularization for robustness on degenerate stencils.
Vec2 solve_2x2(double a11, double a12, double a22, double b1, double b2) {
    const double trace = std::max(a11 + a22, 0.0);
    const double reg = 1e-14 * trace + 1e-30;
    a11 += reg;
    a22 += reg;
    const double det = a11 * a22 - a12 * a12;
    if (std::fabs(det) < 1e-300) return {b1 / a11, b2 / a22};
    const double inv = 1.0 / det;
    return {(a22 * b1 - a12 * b2) * inv, (a11 * b2 - a12 * b1) * inv};
}

inline int gid(int cell, int var) { return cell * 8 + var * 2; }

}  // namespace

void compute_lsq_gradients(const DistributedMesh& mesh,
                           const std::vector<double>& U,
                           std::vector<double>& grad, const GasModel& gas) {
    grad.assign(mesh.n_local * 8, 0.0);
    for (int c = 0; c < mesh.n_owned; ++c) {
        const Primitive qc =
            cons_to_prim(VecN{U[c * kNC], U[c * kNC + 1], U[c * kNC + 2],
                              U[c * kNC + 3]},
                         gas);
        double a11 = 0.0, a12 = 0.0, a22 = 0.0;
        double b[4][2] = {{0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}};
        for (int f : mesh.cell_faces[c]) {
            const LocalFace& lf = mesh.faces[f];
            const int j = (lf.left == c) ? lf.right : lf.left;
            if (j < 0 || j == c) continue;
            const Primitive qj =
                cons_to_prim(VecN{U[j * kNC], U[j * kNC + 1], U[j * kNC + 2],
                                  U[j * kNC + 3]},
                             gas);
            const Vec2 d = mesh.cell_center[j] - mesh.cell_center[c];
            const double dn2 = d.norm2();
            const double w = 1.0 / (dn2 + 1e-24);
            a11 += w * d.x * d.x;
            a12 += w * d.x * d.y;
            a22 += w * d.y * d.y;
            const double dq[4] = {qj.rho - qc.rho, qj.u - qc.u, qj.v - qc.v,
                                  qj.p - qc.p};
            for (int v = 0; v < 4; ++v) {
                b[v][0] += w * d.x * dq[v];
                b[v][1] += w * d.y * dq[v];
            }
        }
        for (int v = 0; v < 4; ++v) {
            const Vec2 g = solve_2x2(a11, a12, a22, b[v][0], b[v][1]);
            grad[gid(c, v)] = g.x;
            grad[gid(c, v) + 1] = g.y;
        }
    }
}

void compute_limiters(const DistributedMesh& mesh,
                      const std::vector<double>& U,
                      const std::vector<double>& grad,
                      std::vector<double>& limiter, const Numerics& num) {
    limiter.assign(mesh.n_local, 1.0);
    for (int c = 0; c < mesh.n_owned; ++c) {
        const Primitive qc =
            cons_to_prim(VecN{U[c * kNC], U[c * kNC + 1], U[c * kNC + 2],
                              U[c * kNC + 3]},
                         num.gas);
        const double eps2 =
            std::pow(num.limiter_k * std::sqrt(std::max(mesh.cell_volume[c], 1e-12)),
                     3.0);
        double phi = 1.0;
        const double qv[4] = {qc.rho, qc.u, qc.v, qc.p};
        // One pass over the stencil to get the min/max neighbor value per
        // primitive variable.
        double qmax[4] = {-1e300, -1e300, -1e300, -1e300};
        double qmin[4] = {1e300, 1e300, 1e300, 1e300};
        for (int f2 : mesh.cell_faces[c]) {
            const LocalFace& lf2 = mesh.faces[f2];
            const int j2 = (lf2.left == c) ? lf2.right : lf2.left;
            if (j2 < 0 || j2 == c) continue;
            const Primitive q2 = cons_to_prim(
                VecN{U[j2 * kNC], U[j2 * kNC + 1], U[j2 * kNC + 2],
                     U[j2 * kNC + 3]},
                num.gas);
            const double qv2[4] = {q2.rho, q2.u, q2.v, q2.p};
            for (int v = 0; v < 4; ++v) {
                qmax[v] = std::max(qmax[v], qv2[v]);
                qmin[v] = std::min(qmin[v], qv2[v]);
            }
        }
        for (int f : mesh.cell_faces[c]) {
            const LocalFace& lf = mesh.faces[f];
            const int j = (lf.left == c) ? lf.right : lf.left;
            if (j < 0 || j == c) continue;
            for (int v = 0; v < 4; ++v) {
                const Vec2 d = mesh.cell_center[j] - mesh.cell_center[c];
                const double delta =
                    grad[gid(c, v)] * d.x + grad[gid(c, v) + 1] * d.y;
                if (delta > 1e-300) {
                    const double dplus = qmax[v] - qv[v];
                    const double num =
                        (dplus * dplus + eps2) * delta + 2.0 * delta * delta * dplus;
                    const double den =
                        dplus * dplus + 2.0 * delta * delta + dplus * delta + eps2;
                    const double ph =
                        den > 0.0 ? num / (den * std::max(delta, 1e-300)) : 1.0;
                    phi = std::min(phi, ph);
                } else if (delta < -1e-300) {
                    const double dminus = qmin[v] - qv[v];
                    const double num = (dminus * dminus + eps2) * delta +
                                       2.0 * delta * delta * dminus;
                    const double den = dminus * dminus + 2.0 * delta * delta +
                                       dminus * delta + eps2;
                    const double ph =
                        den > 0.0 ? num / (den * std::min(delta, -1e-300)) : 1.0;
                    phi = std::min(phi, ph);
                }
            }
        }
        limiter[c] = std::clamp(phi, 0.0, 1.0);
    }
}

bool reconstruct_face(const DistributedMesh& mesh, const LocalFace& face,
                      const std::vector<double>& U,
                      const std::vector<double>& grad,
                      const std::vector<double>& limiter, const Numerics& num,
                      VecN& UL, VecN& UR) {
    const int L = face.left;
    const int R = face.right;
    const auto cell_state = [&](int c) {
        return VecN{U[c * kNC], U[c * kNC + 1], U[c * kNC + 2], U[c * kNC + 3]};
    };
    const auto rebuild = [&](int c, const VecN& u, VecN& out) {
        if (std::getenv("CFD_FIRST_ORDER")) {
            out = u;
            return true;
        }
        const Primitive qc = cons_to_prim(u, num.gas);
        const Vec2 d = face.center - mesh.cell_center[c];
        const double phi = limiter[c];
        Primitive qf;
        qf.rho = qc.rho + phi * (grad[gid(c, 0)] * d.x + grad[gid(c, 0) + 1] * d.y);
        qf.u = qc.u + phi * (grad[gid(c, 1)] * d.x + grad[gid(c, 1) + 1] * d.y);
        qf.v = qc.v + phi * (grad[gid(c, 2)] * d.x + grad[gid(c, 2) + 1] * d.y);
        qf.p = qc.p + phi * (grad[gid(c, 3)] * d.x + grad[gid(c, 3) + 1] * d.y);
        if (qf.rho <= 0.0 || qf.p <= 0.0) return false;
        out = prim_to_cons(qf, num.gas);
        return true;
    };
    if (R < 0) {
        // Boundary face: only the interior side is reconstructed.
        UL = cell_state(L);
        if (!rebuild(L, UL, UL)) return false;
        UR = UL;
        return true;
    }
    UL = cell_state(L);
    UR = cell_state(R);
    const bool okL = rebuild(L, UL, UL);
    const bool okR = rebuild(R, UR, UR);
    if (!okL || !okR) {
        UL = cell_state(L);
        UR = cell_state(R);
        return false;
    }
    return true;
}

VecN inviscid_face_flux(const VecN& UL, const VecN& UR, const Vec2& n,
                        const Numerics& num) {
    const GasModel& gas = num.gas;
    if (num.flux_scheme == FluxScheme::Rusanov) {
        const Primitive qL = cons_to_prim(UL, gas);
        const Primitive qR = cons_to_prim(UR, gas);
        const double aL = sound_speed(qL, gas);
        const double aR = sound_speed(qR, gas);
        const double unL = qL.u * n.x + qL.v * n.y;
        const double unR = qR.u * n.x + qR.v * n.y;
        const double lam = std::max(std::fabs(unL) + aL, std::fabs(unR) + aR);
        const VecN FL = inviscid_flux_normal(qL, n, gas);
        const VecN FR = inviscid_flux_normal(qR, n, gas);
        VecN f;
        for (int i = 0; i < kNC; ++i)
            f[i] = 0.5 * (FL[i] + FR[i]) -
                   0.5 * lam * num.rusanov_dissipation_scale * (UR[i] - UL[i]);
        return f;
    }

    // Roe flux with the Harten-Yee entropy fix.
    const Primitive qL = cons_to_prim(UL, gas);
    const Primitive qR = cons_to_prim(UR, gas);
    const double aL = sound_speed(qL, gas);
    const double aR = sound_speed(qR, gas);
    const double HL = qL.p / ((gas.gamma - 1.0) * qL.rho) + qL.p / qL.rho +
                      0.5 * (qL.u * qL.u + qL.v * qL.v);
    const double HR = qR.p / ((gas.gamma - 1.0) * qR.rho) + qR.p / qR.rho +
                      0.5 * (qR.u * qR.u + qR.v * qR.v);

    const double sqL = std::sqrt(std::max(qL.rho, kTiny));
    const double sqR = std::sqrt(std::max(qR.rho, kTiny));
    const double inv = 1.0 / (sqL + sqR);
    const double rbar = sqL * sqR;
    const double ubar = (sqL * qL.u + sqR * qR.u) * inv;
    const double vbar = (sqL * qL.v + sqR * qR.v) * inv;
    const double Hbar = (sqL * HL + sqR * HR) * inv;
    const double a2 =
        (gas.gamma - 1.0) * (Hbar - 0.5 * (ubar * ubar + vbar * vbar));
    const double abar = std::sqrt(std::max(a2, kTiny));

    const double unL = qL.u * n.x + qL.v * n.y;
    const double unR = qR.u * n.x + qR.v * n.y;
    const double un = ubar * n.x + vbar * n.y;

    const double du = qR.u - qL.u;
    const double dv = qR.v - qL.v;
    const double drho = qR.rho - qL.rho;
    const double dp = qR.p - qL.p;
    const double dun = du * n.x + dv * n.y;
    const Vec2 t{-n.y, n.x};
    const double dut = du * t.x + dv * t.y;

    const double alpha1 = (dp - rbar * abar * dun) / (2.0 * a2);
    const double alpha2 = drho - dp / a2;
    const double alpha3 = rbar * dut;
    const double alpha4 = (dp + rbar * abar * dun) / (2.0 * a2);

    const double lam1 = un - abar;
    const double lam2 = un;
    const double lam4 = un + abar;
    const double delta =
        num.entropy_fix_delta0 *
        std::max(std::fabs(unL) + aL, std::fabs(unR) + aR);
    const auto psi = [&](double lam) {
        const double a = std::fabs(lam);
        if (a >= delta) return a;
        return 0.5 * (lam * lam + delta * delta) / delta;
    };
    const double l1 = psi(lam1);
    const double l2 = psi(lam2);
    const double l4 = psi(lam4);

    VecN FL = inviscid_flux_normal(qL, n, gas);
    VecN FR = inviscid_flux_normal(qR, n, gas);
    VecN diss;
    const double ke = 0.5 * (ubar * ubar + vbar * vbar);
    const double un_t = ubar * t.x + vbar * t.y;
    // r1
    diss[0] += l1 * alpha1;
    diss[1] += l1 * alpha1 * (ubar - abar * n.x);
    diss[2] += l1 * alpha1 * (vbar - abar * n.y);
    diss[3] += l1 * alpha1 * (Hbar - abar * un);
    // r2
    diss[0] += l2 * alpha2;
    diss[1] += l2 * alpha2 * ubar;
    diss[2] += l2 * alpha2 * vbar;
    diss[3] += l2 * alpha2 * ke;
    // r3
    diss[1] += l2 * alpha3 * t.x;
    diss[2] += l2 * alpha3 * t.y;
    diss[3] += l2 * alpha3 * un_t;
    // r4
    diss[0] += l4 * alpha4;
    diss[1] += l4 * alpha4 * (ubar + abar * n.x);
    diss[2] += l4 * alpha4 * (vbar + abar * n.y);
    diss[3] += l4 * alpha4 * (Hbar + abar * un);

    VecN f;
    for (int i = 0; i < kNC; ++i) f[i] = 0.5 * (FL[i] + FR[i]) - 0.5 * diss[i];
    return f;
}

VecN viscous_face_flux(const DistributedMesh& mesh, const LocalFace& face,
                       const VecN& UL, const VecN& UR,
                       const std::vector<double>& grad, const Numerics& num) {
    const GasModel& gas = num.gas;
    const int L = face.left;
    const int R = face.right;
    const Primitive qL = cons_to_prim(UL, gas);
    const Primitive qR = cons_to_prim(UR, gas);
    const double TL = temperature_of(qL, gas);
    const double TR = temperature_of(qR, gas);
    const Vec2 dr = mesh.cell_center[R] - mesh.cell_center[L];
    const double dr2 = dr.norm2() + 1e-24;

    const double varsL[3] = {qL.u, qL.v, TL};
    const double varsR[3] = {qR.u, qR.v, TR};
    // Primitive gradient indices: 1 -> u, 2 -> v, 3 -> p. Temperature gradient
    // is derived from rho/p: T = p/(rho R), grad T = (grad p - T grad rho)/rho.
    Vec2 gT[2];
    for (int side = 0; side < 2; ++side) {
        const int c = side == 0 ? L : R;
        const double rho = side == 0 ? qL.rho : qR.rho;
        const double T = side == 0 ? TL : TR;
        const Vec2 grho{grad[gid(c, 0)], grad[gid(c, 0) + 1]};
        const Vec2 gp{grad[gid(c, 3)], grad[gid(c, 3) + 1]};
        // T = p/(rho R)  =>  grad T = (grad p - T R grad rho)/(rho R).
        const double Rinv = 1.0 / std::max(gas.R, kTiny);
        gT[side] = ((gp - grho * (T * gas.R)) * Rinv) /
                   std::max(rho, kTiny);
    }

    Vec2 gu_face{0.0, 0.0}, gv_face{0.0, 0.0}, gT_face{0.0, 0.0};
    const double grads[3][2][2] = {
        {{grad[gid(L, 1)], grad[gid(L, 1) + 1]},
         {grad[gid(R, 1)], grad[gid(R, 1) + 1]}},
        {{grad[gid(L, 2)], grad[gid(L, 2) + 1]},
         {grad[gid(R, 2)], grad[gid(R, 2) + 1]}},
        {{gT[0].x, gT[0].y}, {gT[1].x, gT[1].y}}};
    for (int v = 0; v < 3; ++v) {
        Vec2 gavg = 0.5 * (Vec2{grads[v][0][0], grads[v][0][1]} +
                           Vec2{grads[v][1][0], grads[v][1][1]});
        const double dq = varsR[v] - varsL[v];
        const double corr = (gavg.dot(dr) - dq) / dr2;
        Vec2 g = gavg - corr * dr;
        if (v == 0) gu_face = g;
        if (v == 1) gv_face = g;
        if (v == 2) gT_face = g;
    }

    const double div = gu_face.x + gv_face.y;
    const double txx =
        num.viscosity * (2.0 * gu_face.x - (2.0 / 3.0) * div);
    const double tyy =
        num.viscosity * (2.0 * gv_face.y - (2.0 / 3.0) * div);
    const double txy = num.viscosity * (gu_face.y + gv_face.x);
    const double qx = -num.thermal_conductivity * gT_face.x;
    const double qy = -num.thermal_conductivity * gT_face.y;
    const Vec2 uf{0.5 * (qL.u + qR.u), 0.5 * (qL.v + qR.v)};

    VecN fv;
    fv[0] = 0.0;
    fv[1] = txx * face.n.x + txy * face.n.y;
    fv[2] = txy * face.n.x + tyy * face.n.y;
    fv[3] = (txx * uf.x + txy * uf.y) * face.n.x +
            (txy * uf.x + tyy * uf.y) * face.n.y - (qx * face.n.x + qy * face.n.y);
    return fv;
}

FaceFluxResult boundary_face_flux(const DistributedMesh& mesh,
                                  const LocalFace& face,
                                  const std::vector<double>& U,
                                  const std::vector<double>& grad,
                                  const Numerics& num,
                                  const Primitive& freestream,
                                  WallFaceData* wall_data) {
    const GasModel& gas = num.gas;
    const int c = face.left;
    const Primitive qc =
        cons_to_prim(VecN{U[c * kNC], U[c * kNC + 1], U[c * kNC + 2],
                          U[c * kNC + 3]},
                     gas);
    const Vec2 n = face.n;  // outward from the interior cell
    const Vec2 d = face.center - mesh.cell_center[c];
    FaceFluxResult res;

    if (face.bc == BcType::Farfield) {
        const Primitive& qinf = freestream;
        if (std::getenv("CFD_SIMPLE_FF")) {
            const VecN Uc = VecN{U[c * kNC], U[c * kNC + 1], U[c * kNC + 2],
                                 U[c * kNC + 3]};
            const VecN Ub = prim_to_cons(qinf, gas);
            Numerics local = num;
            local.flux_scheme = FluxScheme::Rusanov;
            local.rusanov_dissipation_scale = 1.0;
            res.inviscid = inviscid_face_flux(Uc, Ub, n, local);
            return res;
        }
        const double ainf = sound_speed(qinf, gas);
        const double ac = sound_speed(qc, gas);
        const double vnc = qc.u * n.x + qc.v * n.y;
        const double vninf = qinf.u * n.x + qinf.v * n.y;
        Primitive qb;
        if (vninf < -ainf) {
            qb = qinf;  // supersonic inflow
        } else if (vninf >= ainf) {
            qb = qc;  // supersonic outflow
        } else {
            const double rp = vnc + 2.0 * ac / (gas.gamma - 1.0);
            const double rm = vninf - 2.0 * ainf / (gas.gamma - 1.0);
            const double vnb = 0.5 * (rp + rm);
            const double ab = 0.25 * (gas.gamma - 1.0) * (rp - rm);
            double sb;
            Vec2 vt;
            if (vnb >= 0.0) {
                sb = qc.p / std::pow(std::max(qc.rho, kTiny), gas.gamma);
                vt = {qc.u, qc.v};
            } else {
                sb = qinf.p / std::pow(std::max(qinf.rho, kTiny), gas.gamma);
                vt = {qinf.u, qinf.v};
            }
            const double rho_b =
                std::pow(std::max(ab * ab / (gas.gamma * sb), kTiny),
                         1.0 / (gas.gamma - 1.0));
            qb.rho = rho_b;
            qb.p = sb * std::pow(rho_b, gas.gamma);
            const double vtb = vt.dot(n);
            qb.u = vnb * n.x + (vt.x - vtb * n.x);
            qb.v = vnb * n.y + (vt.y - vtb * n.y);
        }
        const VecN Uc =
            VecN{U[c * kNC], U[c * kNC + 1], U[c * kNC + 2], U[c * kNC + 3]};
        const VecN Ub = prim_to_cons(qb, gas);
        Numerics local = num;
        local.flux_scheme = FluxScheme::Rusanov;
        local.rusanov_dissipation_scale = 1.0;
        res.inviscid = inviscid_face_flux(Uc, Ub, n, local);
        return res;
    }

    // Wall treatment. Body outward normal (points from body into the fluid)
    // is the negative of the cell outward normal.
    const Vec2 nb = -n;
    Vec2 tb{nb.y, -nb.x};
    const double p_recon = qc.p + grad[gid(c, 3)] * d.x + grad[gid(c, 3) + 1] * d.y;
    const double p_wall = p_recon > 0.0 ? p_recon : qc.p;
    res.inviscid = VecN{0.0, p_wall * n.x, p_wall * n.y, 0.0};

    if (wall_data) {
        wall_data->pressure = p_wall;
        wall_data->n_b = nb;
        wall_data->t_b = tb;
        wall_data->rho_wall = qc.rho;
        wall_data->tau_n = {0.0, 0.0};
        wall_data->u_wall = {0.0, 0.0};
    }

    if (face.bc == BcType::NoSlipAdiabaticWall) {
        // Wall velocity gradient: normal derivative uses the zero wall
        // velocity; tangential derivatives come from the cell gradient.
        const double dn = std::max(d.norm(), 1e-12);
        const Vec2 gu{grad[gid(c, 1)], grad[gid(c, 1) + 1]};
        const Vec2 gv{grad[gid(c, 2)], grad[gid(c, 2) + 1]};
        const Vec2 gu_w =
            gu + ((-qc.u) / dn - gu.dot(n)) * n;
        const Vec2 gv_w =
            gv + ((-qc.v) / dn - gv.dot(n)) * n;
        const double div = gu_w.x + gv_w.y;
        const double txx =
            num.viscosity * (2.0 * gu_w.x - (2.0 / 3.0) * div);
        const double tyy =
            num.viscosity * (2.0 * gv_w.y - (2.0 / 3.0) * div);
        const double txy = num.viscosity * (gu_w.y + gv_w.x);
        // Traction exerted on the body (normal n_b), used for surface output
        // and forces. Adiabatic wall => zero heat flux; u_wall = 0 => no
        // viscous power term.
        // Traction that the fluid exerts on the body across the face is
        // tau . n_b; the viscous flux leaving the cell uses n = -n_b, so
        // F_v . n = tau . n = -tau . n_b.
        const Vec2 tau_nb{txx * nb.x + txy * nb.y,
                          txy * nb.x + tyy * nb.y};
        res.viscous = VecN{0.0, -tau_nb.x, -tau_nb.y, 0.0};
        if (wall_data) wall_data->tau_n = tau_nb;
    } else if (face.bc == BcType::SlipWall) {
        // Inviscid slip wall: boundary-state tangential velocity for output.
        if (wall_data) {
            const double vn = qc.u * n.x + qc.v * n.y;
            wall_data->u_wall = {qc.u - vn * n.x, qc.v - vn * n.y};
        }
    }
    return res;
}

void assemble_residual(const DistributedMesh& mesh,
                       const std::vector<double>& U,
                       const std::vector<double>& grad,
                       const std::vector<double>& limiter,
                       const Numerics& num, const Primitive& freestream,
                       std::vector<double>& residual) {
    residual.assign(mesh.n_owned * kNC, 0.0);
    for (int c = 0; c < mesh.n_owned; ++c) {
        double r[kNC] = {0.0, 0.0, 0.0, 0.0};
        for (int f : mesh.cell_faces[c]) {
            const LocalFace& lf = mesh.faces[f];
            VecN UL, UR;
            reconstruct_face(mesh, lf, U, grad, limiter, num, UL, UR);
            VecN flux;
            if (lf.bc == BcType::Interior) {
                flux = inviscid_face_flux(UL, UR, lf.n, num);
                if (num.viscosity > 0.0)
                    flux = flux - viscous_face_flux(mesh, lf, UL, UR, grad, num);
                if (lf.right == c) flux = flux * (-1.0);
            } else {
                FaceFluxResult bf =
                    boundary_face_flux(mesh, lf, U, grad, num, freestream,
                                       nullptr);
                flux = bf.inviscid - bf.viscous;
            }
            for (int i = 0; i < kNC; ++i) r[i] += flux[i] * lf.area;
            if (std::getenv("CFD_TRACE_NAN")) {
                bool bad = false;
                for (int i = 0; i < kNC; ++i)
                    if (!std::isfinite(flux[i])) bad = true;
                if (bad) {
                    std::fprintf(stderr,
                                 "[trace] NaN flux at cell=%d face L=%d R=%d "
                                 "bc=%s n=(%.3f,%.3f) UL=[%.6e %.6e %.6e %.6e] "
                                 "UR=[%.6e %.6e %.6e %.6e]\n",
                                 c, lf.left, lf.right, bc_to_string(lf.bc),
                                 lf.n.x, lf.n.y, UL[0], UL[1], UL[2], UL[3],
                                 UR[0], UR[1], UR[2], UR[3]);
                }
            }
        }
        for (int i = 0; i < kNC; ++i)
            residual[c * kNC + i] = r[i];
    }
}

ForceSum accumulate_wall_forces(const DistributedMesh& mesh,
                                const std::vector<double>& U,
                                const std::vector<double>& grad,
                                const Numerics& num,
                                const Primitive& freestream, double q_inf,
                                double ref_area, double ref_length,
                                const Vec2& moment_center) {
    ForceSum sum;
    const Vec2 drag_dir{freestream.u, freestream.v};
    const double vmag = drag_dir.norm();
    const Vec2 ddir = vmag > 0 ? drag_dir / vmag : Vec2{1.0, 0.0};
    const Vec2 ldir{-ddir.y, ddir.x};

    for (const auto& face : mesh.faces) {
        if (face.bc != BcType::SlipWall &&
            face.bc != BcType::NoSlipAdiabaticWall)
            continue;
        WallFaceData wd;
        boundary_face_flux(mesh, face, U, grad, num, freestream, &wd);
        // Traction on the body is sigma . n_b = -p n_b + tau . n_b.
        const Vec2 f_p = wd.n_b * (-wd.pressure);
        const Vec2 f_v = wd.tau_n;
        const Vec2 f_total = f_p + f_v;
        sum.pressure_drag += f_p.dot(ddir) * face.area;
        sum.pressure_lift += f_p.dot(ldir) * face.area;
        sum.viscous_drag += f_v.dot(ddir) * face.area;
        sum.viscous_lift += f_v.dot(ldir) * face.area;
        const Vec2 r = face.center - moment_center;
        sum.moment_z += r.cross(f_total) * face.area;
    }

    const double fscale = 2.0 / (freestream.rho * vmag * vmag * ref_area);
    const double mscale = 2.0 / (freestream.rho * vmag * vmag * ref_area *
                                 ref_length);
    sum.pressure_drag *= fscale;
    sum.viscous_drag *= fscale;
    sum.pressure_lift *= fscale;
    sum.viscous_lift *= fscale;
    sum.moment_z *= mscale;
    (void)q_inf;
    return sum;
}

}  // namespace cfd
