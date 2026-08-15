#include "solver_core.hpp"

namespace fv {

Solver::Solver(const CaseConfig& c, LocalMesh& mesh, MPI_Comm comm_)
    : cfg(c), m(mesh), comm(comm_) {
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);
    gamma = cfg.gas.gamma;
    Rgas = cfg.gas.R;
    prandtl = cfg.gas.prandtl;
    cp = cfg.gas.cp();
    mu = cfg.mu;
    rhoFloor = 1e-10;
    pFloor = 1e-10;
    // The Rieper-style low-Mach dissipation fix is enabled for low-speed
    // transient cases (where the acoustic dissipation of Rusanov would
    // suppress the physical wake instability) or when explicitly requested.
    lowMachFixEff = cfg.low_mach_fix || (cfg.is_transient() && cfg.fs.mach < 0.3);

    Winf[PRHO] = cfg.fs.rho;
    Winf[PU] = cfg.fs.u;
    Winf[PV] = cfg.fs.v;
    Winf[PP] = cfg.fs.pressure;
    Winf[PT] = cfg.fs.T;

    int nC = m.nCells();
    Ufr.resize(nC);
    W_.resize(nC);
    grad_.resize(nC);
    phi_.resize(nC);
    Utarget.resize(m.nOwned);
    dtau.assign(m.nOwned, 0.0);
    diagD.assign(m.nOwned, 0.0);
    faceLam.assign(m.nFaces, 0.0);

    size_t maxSend = m.sendCells.size(), maxRecv = m.recvCells.size();
    sendBuf_.resize(std::max<size_t>(1, maxSend) * (2 * NPRIM));
    recvBuf_.resize(std::max<size_t>(1, maxRecv) * (2 * NPRIM));
}

Vec4 Solver::freestreamU() const {
    Vec4 U;
    conservativeFromW(Winf, U);
    return U;
}

void Solver::primitiveFromU(const Vec4& U, double W[NPRIM]) const {
    double rho = std::max(U[IRHO], rhoFloor);
    double u = U[IRHOU] / rho;
    double v = U[IRHOV] / rho;
    double p = (gamma - 1.0) * (U[IRHOE] - 0.5 * rho * (u * u + v * v));
    p = std::max(p, pFloor);
    W[PRHO] = rho;
    W[PU] = u;
    W[PV] = v;
    W[PP] = p;
    W[PT] = p / (rho * Rgas);
}

void Solver::conservativeFromW(const double W[NPRIM], Vec4& U) const {
    U[IRHO] = W[PRHO];
    U[IRHOU] = W[PRHO] * W[PU];
    U[IRHOV] = W[PRHO] * W[PV];
    U[IRHOE] = W[PP] / (gamma - 1.0) + 0.5 * W[PRHO] * (W[PU] * W[PU] + W[PV] * W[PV]);
}

// ---------------- halo exchange ----------------

void Solver::haloExchange(double* data, int stride) {
    int nnb = (int)m.sendRanks.size();
    std::vector<MPI_Request> reqs;
    reqs.reserve(2 * nnb);
    const int tag = 4242;
    for (int i = 0; i < nnb; ++i) {
        int cnt = (m.recvOff[i + 1] - m.recvOff[i]) * stride;
        MPI_Irecv(recvBuf_.data() + (size_t)m.recvOff[i] * stride, cnt, MPI_DOUBLE,
                  m.recvRanks[i], tag, comm, &reqs.emplace_back());
    }
    for (int i = 0; i < nnb; ++i) {
        int off = m.sendOff[i], cnt = m.sendOff[i + 1] - off;
        for (int k = 0; k < cnt; ++k) {
            const double* src = data + (size_t)m.sendCells[off + k] * stride;
            std::copy(src, src + stride, sendBuf_.data() + ((size_t)off + k) * stride);
        }
        MPI_Isend(sendBuf_.data() + (size_t)off * stride, cnt * stride, MPI_DOUBLE,
                  m.sendRanks[i], tag, comm, &reqs.emplace_back());
    }
    MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
    for (int i = 0; i < nnb; ++i) {
        int off = m.recvOff[i], cnt = m.recvOff[i + 1] - off;
        for (int k = 0; k < cnt; ++k) {
            double* dst = data + (size_t)m.recvCells[off + k] * stride;
            std::copy(recvBuf_.data() + ((size_t)off + k) * stride,
                      recvBuf_.data() + ((size_t)off + k + 1) * stride, dst);
        }
    }
}

// ---------------- primitives / gradients / limiter ----------------

void Solver::computePrimitive(const std::vector<Vec4>& U, std::vector<std::array<double, NPRIM>>& W) {
    for (int c = 0; c < m.nCells(); ++c)
        primitiveFromU(U[c], W[c].data());
}

void Solver::boundaryValue(int bcTag, const double Wi[NPRIM], double nx, double ny, double Wb[NPRIM]) {
    BCType t = cfg.bc_map[bcTag].second;
    switch (t) {
        case BCType::Farfield:
            for (int k = 0; k < NPRIM; ++k) Wb[k] = Winf[k];
            break;
        case BCType::SlipWall: {
            double un = Wi[PU] * nx + Wi[PV] * ny;
            Wb[PRHO] = Wi[PRHO];
            Wb[PU] = Wi[PU] - un * nx;
            Wb[PV] = Wi[PV] - un * ny;
            Wb[PP] = Wi[PP];
            Wb[PT] = Wi[PT];
            break;
        }
        case BCType::NoSlipAdiabatic:
            Wb[PRHO] = Wi[PRHO];
            Wb[PU] = 0.0;
            Wb[PV] = 0.0;
            Wb[PP] = Wi[PP];
            Wb[PT] = Wi[PT]; // adiabatic: dT/dn = 0 -> wall T = cell T
            break;
    }
}

void Solver::computeGradients(const std::vector<std::array<double, NPRIM>>& W,
                              std::vector<std::array<double, 2 * NPRIM>>& grad) {
    for (int c = 0; c < m.nOwned; ++c) grad[c].fill(0.0);
    double Wb[NPRIM];
    for (int f = 0; f < m.nFaces; ++f) {
        int cl = m.faceCl[f], cr = m.faceCr[f];
        if (cr >= 0) {
            for (int k = 0; k < NPRIM; ++k) {
                double dv = W[cr][k] - W[cl][k];
                grad[cl][2 * k] += m.lsqLx[f] * dv;
                grad[cl][2 * k + 1] += m.lsqLy[f] * dv;
                if (cr < m.nOwned) {
                    grad[cr][2 * k] -= m.lsqRx[f] * dv;
                    grad[cr][2 * k + 1] -= m.lsqRy[f] * dv;
                }
            }
        } else {
            boundaryValue(m.faceBc[f], W[cl].data(), m.faceNx[f], m.faceNy[f], Wb);
            for (int k = 0; k < NPRIM; ++k) {
                double dv = Wb[k] - W[cl][k];
                grad[cl][2 * k] += m.lsqLx[f] * dv;
                grad[cl][2 * k + 1] += m.lsqLy[f] * dv;
            }
        }
    }
    haloExchange(&grad_[0][0], 2 * NPRIM);
}

void Solver::computeLimiter(const std::vector<std::array<double, NPRIM>>& W,
                            const std::vector<std::array<double, 2 * NPRIM>>& grad,
                            std::vector<std::array<double, NPRIM>>& phi) {
    // Venkatakrishnan smooth limiter (better convergence than Barth-Jespersen,
    // less clipping of smooth extrema). Falls back to positivity clipping at use sites.
    double Wb[NPRIM];
    const double K3 = 1.0; // Venkatakrishnan constant K^3 applied via h^3
    for (int c = 0; c < m.nOwned; ++c) {
        double qmin[NVAR], qmax[NVAR];
        for (int k = 0; k < NVAR; ++k) { qmin[k] = W[c][k]; qmax[k] = W[c][k]; }
        for (int t = m.cellFaceOff[c]; t < m.cellFaceOff[c + 1]; ++t) {
            int f = m.cellFace[t];
            int nb = (m.faceCl[f] == c) ? m.faceCr[f] : m.faceCl[f];
            if (nb >= 0) {
                for (int k = 0; k < NVAR; ++k) {
                    qmin[k] = std::min(qmin[k], W[nb][k]);
                    qmax[k] = std::max(qmax[k], W[nb][k]);
                }
            } else {
                boundaryValue(m.faceBc[f], W[c].data(), m.faceNx[f], m.faceNy[f], Wb);
                for (int k = 0; k < NVAR; ++k) {
                    qmin[k] = std::min(qmin[k], Wb[k]);
                    qmax[k] = std::max(qmax[k], Wb[k]);
                }
            }
        }
        double h = std::sqrt(m.cellVol[c]);
        double eps2 = K3 * h * h * h; // (K h)^3 with K = 1
        for (int k = 0; k < NVAR; ++k) phi[c][k] = 1.0;
        phi[c][PT] = 1.0;
        for (int t = m.cellFaceOff[c]; t < m.cellFaceOff[c + 1]; ++t) {
            int f = m.cellFace[t];
            double rx, ry;
            if (m.faceCl[f] == c) { rx = m.faceRxL[f]; ry = m.faceRyL[f]; }
            else { rx = m.faceRxR[f]; ry = m.faceRyR[f]; }
            for (int k = 0; k < NVAR; ++k) {
                double dq = grad[c][2 * k] * rx + grad[c][2 * k + 1] * ry;
                if (dq == 0.0) continue;
                double dref = (dq > 0.0) ? (qmax[k] - W[c][k]) : (qmin[k] - W[c][k]);
                double num = dref * dref + eps2 + 2.0 * dref * dq;
                double den = dref * dref + eps2 + dref * dq + 2.0 * dq * dq;
                double lim = (den > 0.0) ? num / den : 1.0;
                lim = std::max(0.0, lim);
                if (lim < phi[c][k]) phi[c][k] = lim;
            }
        }
        for (int k = 0; k < NVAR; ++k) phi[c][k] = std::max(0.0, std::min(1.0, phi[c][k]));
    }
    haloExchange(&phi_[0][0], NPRIM);
}

// ---------------- fluxes ----------------

void Solver::inviscidFlux(const double WL[NPRIM], const double WR[NPRIM],
                          double nx, double ny, double S, double flux[NVAR]) {
    double unL = WL[PU] * nx + WL[PV] * ny;
    double unR = WR[PU] * nx + WR[PV] * ny;
    double aL = std::sqrt(gamma * WL[PP] / WL[PRHO]);
    double aR = std::sqrt(gamma * WR[PP] / WR[PRHO]);
    double EL = WL[PP] / (gamma - 1.0) + 0.5 * WL[PRHO] * (WL[PU] * WL[PU] + WL[PV] * WL[PV]);
    double ER = WR[PP] / (gamma - 1.0) + 0.5 * WR[PRHO] * (WR[PU] * WR[PU] + WR[PV] * WR[PV]);
    double FL[NVAR] = {
        WL[PRHO] * unL,
        WL[PRHO] * WL[PU] * unL + WL[PP] * nx,
        WL[PRHO] * WL[PV] * unL + WL[PP] * ny,
        unL * (EL + WL[PP])
    };
    double FR[NVAR] = {
        WR[PRHO] * unR,
        WR[PRHO] * WR[PU] * unR + WR[PP] * nx,
        WR[PRHO] * WR[PV] * unR + WR[PP] * ny,
        unR * (ER + WR[PP])
    };
    Vec4 UL, UR;
    conservativeFromW(WL, UL);
    conservativeFromW(WR, UR);
    double s = std::max(std::abs(unL) + aL, std::abs(unR) + aR);
    double scale = cfg.rc.rusanov_dissipation_scale;
    if (lowMachFixEff) {
        // Low-Mach fix (Rieper-style): scale the dissipation by the local face
        // Mach number, recovering full dissipation at shocks (M >= 1) and
        // reducing the excessive acoustic dissipation in low-speed regions.
        double mf = std::max(std::abs(unL) / aL, std::abs(unR) / aR);
        scale *= std::min(1.0, std::max(mf, 0.05) / 0.3);
    }
    for (int k = 0; k < NVAR; ++k)
        flux[k] = (0.5 * (FL[k] + FR[k]) - 0.5 * scale * s * (UR[k] - UL[k])) * S;
}

// Analytic normal-flux Jacobian (unit normal) applied to dU.
void Solver::applyEulerJac(const Vec4& Uc, double nx, double ny, const Vec4& dU, Vec4& out) {
    double W[NPRIM];
    primitiveFromU(Uc, W);
    double rho = W[PRHO], u = W[PU], v = W[PV];
    double un = u * nx + v * ny;
    double q2 = u * u + v * v;
    double gm1 = gamma - 1.0;
    double E = Uc[IRHOE] / rho;
    double H = (Uc[IRHOE] + W[PP]) / rho;

    const double* d = dU.data();
    out[IRHO] = nx * d[IRHOU] + ny * d[IRHOV];
    out[IRHOU] = (nx * gm1 * q2 / 2 - u * un) * d[IRHO]
               + (un + u * nx - gm1 * u * nx) * d[IRHOU]   // un + u nx (1 - gm1)
               + (u * ny - gm1 * v * nx) * d[IRHOV]
               + gm1 * nx * d[IRHOE];
    out[IRHOV] = (ny * gm1 * q2 / 2 - v * un) * d[IRHO]
               + (v * nx - gm1 * u * ny) * d[IRHOU]
               + (un + v * ny - gm1 * v * ny) * d[IRHOV]
               + gm1 * ny * d[IRHOE];
    out[IRHOE] = un * (gm1 * q2 - gamma * E) * d[IRHO]
               + (nx * H - gm1 * u * un) * d[IRHOU]
               + (ny * H - gm1 * v * un) * d[IRHOV]
               + gamma * un * d[IRHOE];
}

// Viscous flux through an interior face given both side states and cell gradients.
void Solver::viscousFaceFlux(const std::vector<std::array<double, NPRIM>>& W,
                             const std::vector<std::array<double, 2 * NPRIM>>& grad,
                             int f, double flux[NVAR], double* tauWall) {
    int cl = m.faceCl[f], cr = m.faceCr[f];
    double nx = m.faceNx[f], ny = m.faceNy[f], S = m.faceS[f];
    double dudx, dudy, dvdx, dvdy, dTdx, dTdy, uf, vf;
    if (cr >= 0) {
        dudx = 0.5 * (grad[cl][2 * PU] + grad[cr][2 * PU]);
        dudy = 0.5 * (grad[cl][2 * PU + 1] + grad[cr][2 * PU + 1]);
        dvdx = 0.5 * (grad[cl][2 * PV] + grad[cr][2 * PV]);
        dvdy = 0.5 * (grad[cl][2 * PV + 1] + grad[cr][2 * PV + 1]);
        dTdx = 0.5 * (grad[cl][2 * PT] + grad[cr][2 * PT]);
        dTdy = 0.5 * (grad[cl][2 * PT + 1] + grad[cr][2 * PT + 1]);
        uf = 0.5 * (W[cl][PU] + W[cr][PU]);
        vf = 0.5 * (W[cl][PV] + W[cr][PV]);
    } else {
        // no-slip wall: wall velocity is zero; use interior cell gradient
        dudx = grad[cl][2 * PU]; dudy = grad[cl][2 * PU + 1];
        dvdx = grad[cl][2 * PV]; dvdy = grad[cl][2 * PV + 1];
        dTdx = 0.0; dTdy = 0.0; // adiabatic: zero normal heat flux
        uf = 0.0; vf = 0.0;
    }
    double div = dudx + dvdy;
    double txx = mu * (2.0 * dudx - 2.0 / 3.0 * div);
    double tyy = mu * (2.0 * dvdy - 2.0 / 3.0 * div);
    double txy = mu * (dudy + dvdx);
    double t1 = txx * nx + txy * ny;
    double t2 = txy * nx + tyy * ny;
    double k = mu * cp / prandtl;
    flux[IRHO] = 0.0;
    flux[IRHOU] = t1 * S;
    flux[IRHOV] = t2 * S;
    flux[IRHOE] = ((txx * uf + txy * vf + k * dTdx) * nx +
                   (txy * uf + tyy * vf + k * dTdy) * ny) * S;
    if (tauWall) { tauWall[0] = t1; tauWall[1] = t2; }
}

// ---------------- full spatial residual ----------------

void Solver::computeResidual(std::vector<Vec4>& U, std::vector<Vec4>& R) {
    haloExchange(&U[0][0], NVAR);
    computePrimitive(U, W_);
    computeGradients(W_, grad_);
    computeLimiter(W_, grad_, phi_);
    if (cfg.dbg_first_order)
        for (auto& p : phi_) p.fill(0.0);

    for (int c = 0; c < m.nOwned; ++c) R[c] = {0.0, 0.0, 0.0, 0.0};

    bool viscous = cfg.is_viscous();
    double WL[NPRIM], WR[NPRIM];
    for (int f = 0; f < m.nFaces; ++f) {
        int cl = m.faceCl[f], cr = m.faceCr[f];
        double nx = m.faceNx[f], ny = m.faceNy[f], S = m.faceS[f];
        double flux[NVAR];
        if (cr >= 0) {
            // limited linear reconstruction on both sides
            bool okL = true, okR = true;
            for (int k = 0; k < NVAR; ++k) {
                WL[k] = W_[cl][k] + phi_[cl][k] * (grad_[cl][2 * k] * m.faceRxL[f] + grad_[cl][2 * k + 1] * m.faceRyL[f]);
                WR[k] = W_[cr][k] + phi_[cr][k] * (grad_[cr][2 * k] * m.faceRxR[f] + grad_[cr][2 * k + 1] * m.faceRyR[f]);
            }
            if (WL[PRHO] < rhoFloor || WL[PP] < pFloor) { for (int k = 0; k < NVAR; ++k) WL[k] = W_[cl][k]; okL = false; }
            if (WR[PRHO] < rhoFloor || WR[PP] < pFloor) { for (int k = 0; k < NVAR; ++k) WR[k] = W_[cr][k]; okR = false; }
            (void)okL; (void)okR;
            WL[PT] = WL[PP] / (WL[PRHO] * Rgas);
            WR[PT] = WR[PP] / (WR[PRHO] * Rgas);
            inviscidFlux(WL, WR, nx, ny, S, flux);
            if (viscous) {
                double fv[NVAR];
                viscousFaceFlux(W_, grad_, f, fv, nullptr);
                for (int k = 0; k < NVAR; ++k) flux[k] -= fv[k];
            }
            for (int k = 0; k < NVAR; ++k) {
                R[cl][k] += flux[k];
                if (cr < m.nOwned) R[cr][k] -= flux[k];
            }
        } else {
            BCType bt = cfg.bc_map[m.faceBc[f]].second;
            for (int k = 0; k < NVAR; ++k)
                WL[k] = W_[cl][k] + phi_[cl][k] * (grad_[cl][2 * k] * m.faceRxL[f] + grad_[cl][2 * k + 1] * m.faceRyL[f]);
            if (WL[PRHO] < rhoFloor || WL[PP] < pFloor)
                for (int k = 0; k < NVAR; ++k) WL[k] = W_[cl][k];
            WL[PT] = WL[PP] / (WL[PRHO] * Rgas);
            if (bt == BCType::Farfield) {
                inviscidFlux(WL, Winf, nx, ny, S, flux);
                for (int k = 0; k < NVAR; ++k) R[cl][k] += flux[k];
            } else if (bt == BCType::SlipWall) {
                if (getenv("FV_WALL_MIRROR")) {
                    // weak wall via mirrored state in the Riemann flux
                    double un = WL[PU] * nx + WL[PV] * ny;
                    double WG[NPRIM];
                    WG[PRHO] = WL[PRHO];
                    WG[PU] = WL[PU] - 2.0 * un * nx;
                    WG[PV] = WL[PV] - 2.0 * un * ny;
                    WG[PP] = WL[PP];
                    WG[PT] = WL[PT];
                    inviscidFlux(WL, WG, nx, ny, S, flux);
                    for (int k = 0; k < NVAR; ++k) R[cl][k] += flux[k];
                } else {
                    // wall pressure = adjacent cell value (zero normal pressure gradient)
                    R[cl][IRHOU] += W_[cl][PP] * nx * S;
                    R[cl][IRHOV] += W_[cl][PP] * ny * S;
                }
            } else { // NoSlipAdiabatic
                R[cl][IRHOU] += W_[cl][PP] * nx * S;
                R[cl][IRHOV] += W_[cl][PP] * ny * S;
                if (viscous) {
                    double fv[NVAR];
                    viscousFaceFlux(W_, grad_, f, fv, nullptr);
                    for (int k = 0; k < NVAR; ++k) R[cl][k] -= fv[k];
                }
            }
        }
    }
}

// ---------------- implicit operator (LU-SGS) ----------------

void Solver::buildImplicitOperator(const std::vector<Vec4>& U, double cfl, double dtPhysAlpha) {
    Ufr = U;
    timeAlpha = dtPhysAlpha;
    bool viscous = cfg.is_viscous();

    for (int f = 0; f < m.nFaces; ++f) {
        int cl = m.faceCl[f], cr = m.faceCr[f];
        double WL[NPRIM];
        primitiveFromU(U[cl], WL);
        double uL = WL[PU], vL = WL[PV], aL = std::sqrt(gamma * WL[PP] / WL[PRHO]);
        double lam;
        if (cr >= 0) {
            double WR[NPRIM];
            primitiveFromU(U[cr], WR);
            double un = 0.5 * ((uL + WR[PU]) * m.faceNx[f] + (vL + WR[PV]) * m.faceNy[f]);
            double af = 0.5 * (aL + std::sqrt(gamma * WR[PP] / WR[PRHO]));
            lam = (std::abs(un) + af) * m.faceS[f];
            if (viscous) {
                double rhof = 0.5 * (WL[PRHO] + WR[PRHO]);
                double Vf = 0.5 * (m.cellVol[cl] + m.cellVol[cr]);
                double lv = std::max(4.0 / 3.0, gamma / prandtl) * mu / rhof *
                            m.faceS[f] * m.faceS[f] / Vf;
                lam += lv;
            }
        } else {
            double un = uL * m.faceNx[f] + vL * m.faceNy[f];
            lam = (std::abs(un) + aL) * m.faceS[f];
            if (viscous) {
                double lv = std::max(4.0 / 3.0, gamma / prandtl) * mu / WL[PRHO] *
                            m.faceS[f] * m.faceS[f] / m.cellVol[cl];
                lam += lv;
            }
        }
        faceLam[f] = lam;
    }

    for (int c = 0; c < m.nOwned; ++c) {
        double sr = 0.0;
        for (int t = m.cellFaceOff[c]; t < m.cellFaceOff[c + 1]; ++t)
            sr += faceLam[m.cellFace[t]];
        sr = std::max(sr, 1e-30);
        dtau[c] = cfl * m.cellVol[c] / sr;
        double D = m.cellVol[c] / dtau[c] + dtPhysAlpha * m.cellVol[c];
        for (int t = m.cellFaceOff[c]; t < m.cellFaceOff[c + 1]; ++t) {
            int f = m.cellFace[t];
            D += (m.faceCr[f] >= 0 ? 0.5 : 1.0) * faceLam[f];
        }
        diagD[c] = D;
    }
}

void Solver::lusgsSweep(const std::vector<Vec4>& R, std::vector<Vec4>& dU) {
    // forward sweep
    for (int i = 0; i < m.nOwned; ++i) {
        Vec4 acc = {-R[i][0], -R[i][1], -R[i][2], -R[i][3]};
        for (int t = m.cellFaceOff[i]; t < m.cellFaceOff[i + 1]; ++t) {
            int f = m.cellFace[t];
            int j = m.faceCr[f];
            if (j < 0 || j >= m.nOwned) continue;
            if (m.faceCl[f] != i) j = m.faceCl[f];
            if (j >= i) continue;
            double snx = (m.faceCl[f] == i) ? m.faceNx[f] : -m.faceNx[f];
            double sny = (m.faceCl[f] == i) ? m.faceNy[f] : -m.faceNy[f];
            Vec4 ajdu;
            applyEulerJac(Ufr[j], snx, sny, dU[j], ajdu);
            double coef = 0.5 * m.faceS[f];
            for (int k = 0; k < NVAR; ++k)
                acc[k] -= coef * ajdu[k] - 0.5 * faceLam[f] * dU[j][k];
        }
        double inv = 1.0 / diagD[i];
        for (int k = 0; k < NVAR; ++k) dU[i][k] = acc[k] * inv;
    }
    // backward sweep
    for (int i = m.nOwned - 1; i >= 0; --i) {
        Vec4 acc = {0.0, 0.0, 0.0, 0.0};
        for (int t = m.cellFaceOff[i]; t < m.cellFaceOff[i + 1]; ++t) {
            int f = m.cellFace[t];
            int j = m.faceCr[f];
            if (j < 0 || j >= m.nOwned) continue;
            if (m.faceCl[f] != i) j = m.faceCl[f];
            if (j <= i) continue;
            double snx = (m.faceCl[f] == i) ? m.faceNx[f] : -m.faceNx[f];
            double sny = (m.faceCl[f] == i) ? m.faceNy[f] : -m.faceNy[f];
            Vec4 ajdu;
            applyEulerJac(Ufr[j], snx, sny, dU[j], ajdu);
            double coef = 0.5 * m.faceS[f];
            for (int k = 0; k < NVAR; ++k)
                acc[k] += coef * ajdu[k] - 0.5 * faceLam[f] * dU[j][k];
        }
        double inv = 1.0 / diagD[i];
        for (int k = 0; k < NVAR; ++k) dU[i][k] -= acc[k] * inv;
    }
}

void Solver::applyUpdate(std::vector<Vec4>& U, const std::vector<Vec4>& dU) {
    double omega0 = omega;
    if (const char* s = getenv("FV_OMEGA")) omega0 = std::atof(s);
    for (int c = 0; c < m.nOwned; ++c) {
        double alpha = omega0;
        Vec4 Unew;
        for (int attempt = 0; attempt < 8; ++attempt) {
            for (int k = 0; k < NVAR; ++k) Unew[k] = U[c][k] + alpha * dU[c][k];
            double W[NPRIM];
            bool ok = Unew[IRHO] > rhoFloor;
            if (ok) {
                primitiveFromU(Unew, W);
                ok = W[PP] > pFloor && W[PRHO] > rhoFloor;
            }
            if (ok) break;
            alpha *= 0.5;
            if (attempt == 7) { Unew = U[c]; alpha = 0.0; }
        }
        U[c] = Unew;
    }
}

// ---------------- norms / forces / surface ----------------

void Solver::residualNorms(const std::vector<Vec4>& R, double l2var[NVAR], double& l2, double& linf) {
    double sum[NVAR] = {0, 0, 0, 0};
    double mx = 0.0;
    for (int c = 0; c < m.nOwned; ++c)
        for (int k = 0; k < NVAR; ++k) {
            sum[k] += R[c][k] * R[c][k];
            mx = std::max(mx, std::abs(R[c][k]));
        }
    double gsum[NVAR];
    MPI_Allreduce(sum, gsum, NVAR, MPI_DOUBLE, MPI_SUM, comm);
    double gmx;
    MPI_Allreduce(&mx, &gmx, 1, MPI_DOUBLE, MPI_MAX, comm);
    double tot = 0.0;
    for (int k = 0; k < NVAR; ++k) {
        l2var[k] = std::sqrt(gsum[k] / (double)m.nCellsGlobal);
        tot += gsum[k];
    }
    l2 = std::sqrt(tot / (double)(NVAR * m.nCellsGlobal));
    linf = gmx;
}

Forces Solver::computeForces(const std::vector<Vec4>& Uin) {
    std::vector<Vec4>& U = const_cast<std::vector<Vec4>&>(Uin);
    haloExchange(&U[0][0], NVAR);
    computePrimitive(U, W_);
    computeGradients(W_, grad_);
    computeLimiter(W_, grad_, phi_);

    bool viscous = cfg.is_viscous();
    double fx_p = 0, fy_p = 0, fx_v = 0, fy_v = 0, mz = 0;
    for (int bf : m.bfaces) {
        BCType bt = cfg.bc_map[m.faceBc[bf]].second;
        if (bt == BCType::Farfield) continue;
        int cl = m.faceCl[bf];
        double nx = m.faceNx[bf], ny = m.faceNy[bf], S = m.faceS[bf];
        // wall pressure = adjacent cell-center value (zero normal pressure gradient at wall)
        double pface = W_[cl][PP];
        double fx = pface * nx * S, fy = pface * ny * S;
        fx_p += fx; fy_p += fy;
        double fvx = 0, fvy = 0;
        if (viscous && bt == BCType::NoSlipAdiabatic) {
            double fv[NVAR], tauW[2];
            viscousFaceFlux(W_, grad_, bf, fv, tauW);
            fvx = -tauW[0] * S; fvy = -tauW[1] * S; // force on body = -traction on fluid
            fx_v += fvx; fy_v += fvy;
        }
        double rx = m.faceCx[bf] - cfg.ref_moment_center[0];
        double ry = m.faceCy[bf] - cfg.ref_moment_center[1];
        mz += rx * (fy + fvy) - ry * (fx + fvx);
    }
    double loc[5] = {fx_p, fy_p, fx_v, fy_v, mz};
    double glob[5];
    MPI_Allreduce(loc, glob, 5, MPI_DOUBLE, MPI_SUM, comm);

    double qA = cfg.fs.q * cfg.ref_area;
    double qAL = qA * cfg.ref_length;
    double aoa = cfg.fs.aoa_deg * M_PI / 180.0;
    double ex = std::cos(aoa), ey = std::sin(aoa);       // drag direction
    double lx = -std::sin(aoa), ly = std::cos(aoa);      // lift direction
    Forces F;
    F.pressure_drag = (glob[0] * ex + glob[1] * ey) / qA;
    F.pressure_lift = (glob[0] * lx + glob[1] * ly) / qA;
    F.viscous_drag = (glob[2] * ex + glob[3] * ey) / qA;
    F.viscous_lift = (glob[2] * lx + glob[3] * ly) / qA;
    F.cd = F.pressure_drag + F.viscous_drag;
    F.cl = F.pressure_lift + F.viscous_lift;
    F.cmz = glob[4] / qAL;
    return F;
}

std::vector<SurfaceRow> Solver::surfaceRows(const std::vector<Vec4>& Uin) {
    std::vector<Vec4>& U = const_cast<std::vector<Vec4>&>(Uin);
    haloExchange(&U[0][0], NVAR);
    computePrimitive(U, W_);
    computeGradients(W_, grad_);
    computeLimiter(W_, grad_, phi_);

    bool viscous = cfg.is_viscous();
    std::vector<SurfaceRow> rows;
    for (int bf : m.bfaces) {
        BCType bt = cfg.bc_map[m.faceBc[bf]].second;
        if (bt == BCType::Farfield) continue;
        int cl = m.faceCl[bf];
        double nx = m.faceNx[bf], ny = m.faceNy[bf], S = m.faceS[bf];
        // boundary state at the wall face
        double Wf[NPRIM];
        for (int k = 0; k < NVAR; ++k)
            Wf[k] = W_[cl][k] + phi_[cl][k] * (grad_[cl][2 * k] * m.faceRxL[bf] + grad_[cl][2 * k + 1] * m.faceRyL[bf]);
        if (Wf[PRHO] < rhoFloor || Wf[PP] < pFloor)
            for (int k = 0; k < NVAR; ++k) Wf[k] = W_[cl][k];
        Wf[PT] = Wf[PP] / (Wf[PRHO] * Rgas);

        SurfaceRow r;
        r.x = m.faceCx[bf]; r.y = m.faceCy[bf];
        r.nx = nx; r.ny = ny;
        r.tag = m.faceBc[bf];
        r.cf = 0.0;
        // wall pressure reported as adjacent cell value (boundary value, dpg/dn = 0)
        double pwall = W_[cl][PP];
        if (bt == BCType::NoSlipAdiabatic) {
            r.pressure = pwall;
            r.rho = W_[cl][PRHO];
            r.u = 0.0; r.v = 0.0; r.mach = 0.0;
            if (viscous) {
                double fv[NVAR], tauW[2];
                viscousFaceFlux(W_, grad_, bf, fv, tauW);
                double tx = -ny, ty = nx; // face tangent
                r.cf = (tauW[0] * tx + tauW[1] * ty) / cfg.fs.q;
            }
        } else { // slip wall: tangential velocity preserved, normal velocity zero
            double un = Wf[PU] * nx + Wf[PV] * ny;
            double ub = Wf[PU] - un * nx, vb = Wf[PV] - un * ny;
            r.pressure = pwall;
            r.rho = W_[cl][PRHO];
            r.u = ub; r.v = vb;
            double a = std::sqrt(gamma * pwall / W_[cl][PRHO]);
            r.mach = std::hypot(ub, vb) / a;
        }
        r.cp = (r.pressure - cfg.fs.pressure) / cfg.fs.q;
        rows.push_back(r);
    }
    return rows;
}

void Solver::computeCellVorticity(std::vector<Vec4>& U, std::vector<double>& omega) {
    haloExchange(&U[0][0], NVAR);
    computePrimitive(U, W_);
    computeGradients(W_, grad_);
    omega.assign(m.nOwned, 0.0);
    for (int c = 0; c < m.nOwned; ++c)
        omega[c] = grad_[c][2 * PV] - grad_[c][2 * PU + 1];
}

} // namespace fv
