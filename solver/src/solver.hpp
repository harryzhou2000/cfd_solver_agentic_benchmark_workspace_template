#pragma once
#include "types.hpp"
#include "mesh.hpp"
#include "partitioner.hpp"
#include "gas.hpp"
#include "flux.hpp"
#include "reconstruction.hpp"
#include "case_config.hpp"
#include <mpi.h>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <nlohmann/json.hpp>

namespace cfd {

using json = nlohmann::json;

class CFDSolver {
public:
    MPI_Comm comm;
    int rank, nprocs;
    GasModel gas;
    CaseConfig cfg;
    DistMesh dm;
    std::unique_ptr<Reconstruction> recon;

    // Solution
    std::vector<ConsState> U;     // conservative (owned + ghost)
    std::vector<PrimState> W;     // primitive (owned + ghost)
    std::vector<ConsState> R;     // residual (owned only)
    std::vector<ConsState> dU;    // update (owned only)
    std::vector<Real> dtLocal;    // local time step (owned)
    std::vector<Real> diag;       // LU-SGS diagonal (owned)
    std::vector<Real> faceLambda; // spectral radius per face

    Real diagSafety = 1.0; // safety factor for LU-SGS diagonal

    bool skipReconstruction = false; // skip gradient/limiter for speed

    bool useGlobalDT = false; // use global (minimum) time step

    // Debug options
    bool useFirstOrder = false;
    bool useRusanov = false;
    bool skipGhostDU = true; // skip ghost dU exchange for simplicity
    Real cflCap = 1e9;

    // Previous time states for BDF2
    std::vector<ConsState> Un, Un1;

    // Inner iteration stats
    int obsMinInner = 1e9, obsMaxInner = 0;
    long totalInnerIters = 0, totalPhysicalSteps = 0;
    int innerTargetMisses = 0;
    Real lastInnerRatio = 1.0;
    Real sumInnerFraction = 0.0;

    // Residual norms
    Real initResL2 = 0;

    CFDSolver(MPI_Comm c) : comm(c) {
        MPI_Comm_rank(comm, &rank);
        MPI_Comm_size(comm, &nprocs);
    }

    void init(const CaseConfig& config, const DistMesh& mesh) {
        cfg = config;
        dm = mesh;
        gas = GasModel(cfg.gamma, cfg.R, cfg.prandtl);
        recon = std::make_unique<Reconstruction>(dm, gas, true);

        int nc = dm.nLocalCells();
        U.resize(nc);
        W.resize(nc);
        R.resize(dm.nOwned);
        dU.resize(dm.nOwned);
        dtLocal.resize(dm.nOwned);
        diag.resize(dm.nOwned);
        faceLambda.resize(dm.numFaces());

        // Initialize with freestream
        for (int i = 0; i < nc; i++) {
            U[i] = cfg.freestreamU;
            W[i] = cfg.freestreamW;
        }

        if (cfg.runType == "transient") {
            Un.resize(nc);
            Un1.resize(nc);
        }
    }

    // ==================== Halo Exchange ====================
    void exchangeHalo() {
        int ntag = 100;
        std::vector<MPI_Request> reqs;
        std::vector<std::vector<Real>> sendBufs(dm.neighborRanks.size());
        std::vector<std::vector<Real>> recvBufs(dm.neighborRanks.size());

        for (size_t i = 0; i < dm.neighborRanks.size(); i++) {
            int nbr = dm.neighborRanks[i];
            int sc = (int)dm.sendCells[i].size();
            int rc = (int)dm.recvCells[i].size();

            sendBufs[i].resize(sc * NEQ);
            recvBufs[i].resize(rc * NEQ);

            // Pack
            for (int j = 0; j < sc; j++) {
                int ci = dm.sendCells[i][j];
                for (int k = 0; k < NEQ; k++)
                    sendBufs[i][j*NEQ + k] = U[ci][k];
            }

            MPI_Request r1, r2;
            MPI_Irecv(recvBufs[i].data(), rc*NEQ, MPI_DOUBLE, nbr, ntag, comm, &r1);
            MPI_Isend(sendBufs[i].data(), sc*NEQ, MPI_DOUBLE, nbr, ntag, comm, &r2);
            reqs.push_back(r1);
            reqs.push_back(r2);
        }

        MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);

        // Unpack
        for (size_t i = 0; i < dm.neighborRanks.size(); i++) {
            int rc = (int)dm.recvCells[i].size();
            for (int j = 0; j < rc; j++) {
                int ci = dm.recvCells[i][j];
                for (int k = 0; k < NEQ; k++)
                    U[ci][k] = recvBufs[i][j*NEQ + k];
            }
        }

        // Update primitive for ghost cells
        for (int i = dm.nOwned; i < dm.nLocalCells(); i++) {
            W[i] = gas.consToPrim(U[i]);
        }
    }

    // Exchange dU for LU-SGS (optional, for better accuracy)
    void exchangeDU() {
        int ntag = 200;
        std::vector<MPI_Request> reqs;
        std::vector<std::vector<Real>> sendBufs(dm.neighborRanks.size());
        std::vector<std::vector<Real>> recvBufs(dm.neighborRanks.size());

        for (size_t i = 0; i < dm.neighborRanks.size(); i++) {
            int nbr = dm.neighborRanks[i];
            int sc = (int)dm.sendCells[i].size();
            int rc = (int)dm.recvCells[i].size();

            sendBufs[i].resize(sc * NEQ);
            recvBufs[i].resize(rc * NEQ);

            for (int j = 0; j < sc; j++) {
                int ci = dm.sendCells[i][j];
                for (int k = 0; k < NEQ; k++)
                    sendBufs[i][j*NEQ + k] = dU[ci][k];
            }

            MPI_Request r1, r2;
            // Receive into ghost dU (extend dU to include ghost)
            MPI_Irecv(recvBufs[i].data(), rc*NEQ, MPI_DOUBLE, nbr, ntag, comm, &r1);
            MPI_Isend(sendBufs[i].data(), sc*NEQ, MPI_DOUBLE, nbr, ntag, comm, &r2);
            reqs.push_back(r1);
            reqs.push_back(r2);
        }

        MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);

        // Unpack into ghost dU (stored in a temporary)
        // For simplicity, ghost dU is stored separately
        // Actually we need ghost dU for LU-SGS backward sweep
        // Let's store it in a separate array
        ghostDU.resize(dm.nLocalCells());
        for (int i = 0; i < dm.nOwned; i++) ghostDU[i] = dU[i];
        for (size_t i = 0; i < dm.neighborRanks.size(); i++) {
            int rc = (int)dm.recvCells[i].size();
            for (int j = 0; j < rc; j++) {
                int ci = dm.recvCells[i][j];
                for (int k = 0; k < NEQ; k++)
                    ghostDU[ci][k] = recvBufs[i][j*NEQ + k];
            }
        }
    }
    std::vector<ConsState> ghostDU;

    // ==================== Residual Assembly ====================
    void computeResidual(Real physicalTimeTerm = 0.0, bool transient = false,
                         Real dt = 0.0, int bdfOrder = 1) {
        // Assumes halo already exchanged and W updated for all cells
        if (!skipReconstruction) {
            recon->computeGradients(W);
            recon->computeLimiter(W);
        }

        for (int i = 0; i < dm.nOwned; i++)
            R[i] = {0, 0, 0, 0};

        Real mu = cfg.mu;
        Real k_thermal = (cfg.mode == "laminar") ? mu * gas.cp / cfg.prandtl : 0.0;

        for (int fi = 0; fi < dm.numFaces(); fi++) {
            int lc = dm.faceLc[fi];
            int rc = dm.faceRc[fi];
            Real area = dm.faceArea[fi];
            Real nx = dm.faceNx[fi] / area; // unit normal
            Real ny = dm.faceNy[fi] / area;
            BCType bc = dm.faceBc[fi];

            PrimState WL, WR;
            ConsState Fn;

            if (bc == BCType::None) {
                // Internal face
                recon->reconstructFace(fi, W, WL, WR);
                recon->enforcePositivity(WL);
                recon->enforcePositivity(WR);

                // Inviscid flux
                Fn = roeFlux(gas, WL, WR, nx, ny, 0.1);
                // Inviscid flux
                if (useRusanov) {
                    Fn = rusanovFlux(gas, WL, WR, nx, ny, cfg.rusanovScale);
                } else {
                    Fn = roeFlux(gas, WL, WR, nx, ny, 0.1);
                }

                // Viscous flux
                if (cfg.mode == "laminar" && mu > 0) {
                    addViscousFlux(fi, WL, WR, nx, ny, mu, k_thermal, Fn);
                }
            } else {
                // Boundary face
                computeBoundaryFlux(fi, nx, ny, area, mu, k_thermal, WL, Fn);
            }

            // Add to residual (Fn is per unit area, multiply by area)
            for (int k = 0; k < NEQ; k++) {
                R[lc][k] -= Fn[k] * area;
                if (rc >= 0 && rc < dm.nOwned) {
                    R[rc][k] += Fn[k] * area;
                }
            }
        }

        // Add physical time term for transient
        if (transient && dt > 0) {
            Real coef = (bdfOrder == 2) ? 3.0/(2.0*dt) : 1.0/dt;
            for (int i = 0; i < dm.nOwned; i++) {
                ConsState timeTerm;
                if (bdfOrder == 2) {
                    for (int k = 0; k < NEQ; k++)
                        timeTerm[k] = coef * (3.0*U[i][k] - 4.0*Un[i][k] + Un1[i][k]);
                } else {
                    for (int k = 0; k < NEQ; k++)
                        timeTerm[k] = coef * (U[i][k] - Un[i][k]);
                }
                // R_stored = -R_spatial - V*BDF2_term = -R_total
                for (int k = 0; k < NEQ; k++)
                    R[i][k] -= timeTerm[k] * dm.cellVol[i];
            }
        }
    }

    void addViscousFlux(int fi, const PrimState& WL, const PrimState& WR,
                        Real nx, Real ny, Real mu, Real k_th, ConsState& Fn) {
        int lc = dm.faceLc[fi];
        int rc = dm.faceRc[fi];

        // Use corrected gradient: average LS gradient + FD normal correction
        // This is essential for stability on stretched meshes
        Real dudx, dudy, dvdx, dvdy, dTdx, dTdy;
        if (rc >= 0) {
            // Distance vector from L to R
            Real dx = dm.cellCx[rc] - dm.cellCx[lc];
            Real dy = dm.cellCy[rc] - dm.cellCy[lc];
            Real dist2 = dx*dx + dy*dy;
            Real dist = std::sqrt(dist2);
            if (dist < 1e-15) dist = 1e-15;

            // For each variable, compute corrected gradient
            // grad_corrected = grad_avg + (dphi/dn_FD - grad_avg·n) * n
            auto correctedGrad = [&](int varIdx, Real& gx, Real& gy) {
                Real gxL = recon->grad[(lc*5+varIdx)*2+0];
                Real gyL = recon->grad[(lc*5+varIdx)*2+1];
                Real gxR = recon->grad[(rc*5+varIdx)*2+0];
                Real gyR = recon->grad[(rc*5+varIdx)*2+1];
                Real gx_avg = 0.5*(gxL + gxR);
                Real gy_avg = 0.5*(gyL + gyR);
                // Normal derivative from FD
                Real dphi = W[rc][varIdx] - W[lc][varIdx];
                Real dn_FD = dphi / dist;
                // Normal derivative from averaged gradient
                Real dn_avg = gx_avg * (dx/dist) + gy_avg * (dy/dist);
                // Corrected gradient
                Real correction = dn_FD - dn_avg;
                gx = gx_avg + correction * (dx/dist);
                gy = gy_avg + correction * (dy/dist);
            };
            correctedGrad(1, dudx, dudy);
            correctedGrad(2, dvdx, dvdy);
            correctedGrad(4, dTdx, dTdy);
        } else {
            // Boundary face: use cell gradient directly
            dudx = recon->grad[(lc*5+1)*2+0];
            dudy = recon->grad[(lc*5+1)*2+1];
            dvdx = recon->grad[(lc*5+2)*2+0];
            dvdy = recon->grad[(lc*5+2)*2+1];
            dTdx = recon->grad[(lc*5+4)*2+0];
            dTdy = recon->grad[(lc*5+4)*2+1];
        }

        Real u = 0.5*(WL[1] + WR[1]);
        Real v = 0.5*(WL[2] + WR[2]);
        ConsState Fv = viscousFluxNormal(gas, u, v, dudx, dudy, dvdx, dvdy,
                                         dTdx, dTdy, nx, ny, mu, k_th);
        for (int k = 0; k < NEQ; k++)
            Fn[k] -= Fv[k]; // viscous flux is subtracted (RHS of NS eq)
    }

    void computeBoundaryFlux(int fi, Real nx, Real ny, Real area,
                             Real mu, Real k_th, PrimState& WL, ConsState& Fn) {
        int lc = dm.faceLc[fi];
        BCType bc = dm.faceBc[fi];
        WL = W[lc]; // first-order at boundary for stability

        if (bc == BCType::Farfield) {
            PrimState WR = cfg.freestreamW;
            // Use Roe flux with freestream
            Fn = roeFlux(gas, WL, WR, nx, ny, 0.1);
            if (cfg.mode == "laminar" && mu > 0) {
                addViscousFlux(fi, WL, WR, nx, ny, mu, k_th, Fn);
            }
        } else if (bc == BCType::SlipWall) {
            // Mirror normal velocity
            Real un = WL[1]*nx + WL[2]*ny;
            PrimState WR = WL;
            WR[1] = WL[1] - 2.0*un*nx;
            WR[2] = WL[2] - 2.0*un*ny;
            Fn = roeFlux(gas, WL, WR, nx, ny, 0.1);
        } else if (bc == BCType::NoSlipAdiabaticWall) {
            // Direct wall flux: pressure only, no mass/energy flux
            Real p = std::max(WL[3], 1e-12);
            Fn[0] = 0.0;
            Fn[1] = p * nx;
            Fn[2] = p * ny;
            Fn[3] = 0.0;
            // Add viscous flux at wall
            // Add viscous flux at wall using LS gradient (stable for stretched meshes)
            if (cfg.mode == "laminar" && mu > 0) {
                Real dudx = recon->grad[(lc*5+1)*2+0];
                Real dudy = recon->grad[(lc*5+1)*2+1];
                Real dvdx = recon->grad[(lc*5+2)*2+0];
                Real dvdy = recon->grad[(lc*5+2)*2+1];
                Real div = dudx + dvdy;
                Real tauxx = mu*(2.0*dudx - 2.0/3.0*div);
                Real tauyy = mu*(2.0*dvdy - 2.0/3.0*div);
                Real tauxy = mu*(dudy + dvdx);
                Fn[1] -= (tauxx*nx + tauxy*ny);
                Fn[2] -= (tauxy*nx + tauyy*ny);
            }
        }
    }

    // ==================== Spectral Radii and Time Step ====================
    void computeSpectralRadii(Real cfl) {
        for (int fi = 0; fi < dm.numFaces(); fi++) {
            int lc = dm.faceLc[fi];
            int rc = dm.faceRc[fi];
            Real area = dm.faceArea[fi];
            Real nx = dm.faceNx[fi] / area;
            Real ny = dm.faceNy[fi] / area;

            Real rho, u, v, p, a;
            rho = std::max(W[lc][0], 1e-12);
            p = std::max(W[lc][3], 1e-12);
            u = W[lc][1]; v = W[lc][2];
            a = gas.soundSpeed(rho, p);
            Real un = std::abs(u*nx + v*ny);
            Real lambda = (un + a) * area;

            // Viscous spectral radius
            if (cfg.mode == "laminar" && cfg.mu > 0) {
                Real dist;
                if (rc >= 0) {
                    Real dx = dm.cellCx[rc] - dm.cellCx[lc];
                    Real dy = dm.cellCy[rc] - dm.cellCy[lc];
                    dist = std::sqrt(dx*dx + dy*dy);
                } else {
                    // Boundary face: distance from cell center to face
                    Real dx = dm.faceFx[fi] - dm.cellCx[lc];
                    Real dy = dm.faceFy[fi] - dm.cellCy[lc];
                    dist = 2.0 * std::sqrt(dx*dx + dy*dy); // approximate full distance
                }
                if (dist > 1e-15) {
                    lambda += 4.0 * cfg.mu / std::max(rho, 1e-8) * area / dist;
                }
            }
            faceLambda[fi] = lambda;
        }

        // Compute local time step and diagonal
        for (int i = 0; i < dm.nOwned; i++) {
            Real sumLambda = 0;
            for (int fi : dm.cellFaces[i]) {
                sumLambda += faceLambda[fi];
            }
            if (sumLambda < 1e-15) sumLambda = 1e-15;
            dtLocal[i] = cfl * dm.cellVol[i] / sumLambda;
        }

        // If using global dt, find minimum dt and use for all cells
        if (useGlobalDT) {
            Real localMinDT = dtLocal[0];
            for (int i = 1; i < dm.nOwned; i++)
                localMinDT = std::min(localMinDT, dtLocal[i]);
            Real globalMinDT;
            MPI_Allreduce(&localMinDT, &globalMinDT, 1, MPI_DOUBLE, MPI_MIN, comm);
            for (int i = 0; i < dm.nOwned; i++) {
                dtLocal[i] = globalMinDT;
            }
        }

        // Compute diagonal using dtLocal
        for (int i = 0; i < dm.nOwned; i++) {
            Real sumLambda = 0;
            for (int fi : dm.cellFaces[i]) {
                sumLambda += faceLambda[fi];
            }
            Real dt = dtLocal[i];
            diag[i] = dm.cellVol[i] / dt + diagSafety * 0.5 * sumLambda;
        }
    }

    void addPhysicalTimeDiag(Real dt, int bdfOrder) {
        Real coef = (bdfOrder == 2) ? 3.0/(2.0*dt) : 1.0/dt;
        for (int i = 0; i < dm.nOwned; i++) {
            diag[i] += coef * dm.cellVol[i];
        }
    }

    // ==================== LU-SGS ====================
    void luSGS(int nSweeps) {
        // Initialize dU = 0
        for (int i = 0; i < dm.nOwned; i++)
            dU[i] = {0, 0, 0, 0};

        for (int sweep = 0; sweep < nSweeps; sweep++) {
            // Forward sweep
            for (int i = 0; i < dm.nOwned; i++) {
                // R[i] = -R_spatial, so (D+L+U)*dU = R[i] gives correct sign
                ConsState rhs;
                for (int k = 0; k < NEQ; k++) rhs[k] = R[i][k];

                for (int fi : dm.cellFaces[i]) {
                    int nbr = (dm.faceLc[fi] == i) ? dm.faceRc[fi] : dm.faceLc[fi];
                    if (nbr < 0 || nbr >= i) continue; // lower neighbors only
                    Real lam = 0.5 * faceLambda[fi];
                    if (nbr < dm.nOwned) {
                        for (int k = 0; k < NEQ; k++)
                            rhs[k] += lam * dU[nbr][k];
                    }
                }
                for (int k = 0; k < NEQ; k++)
                    dU[i][k] = rhs[k] / diag[i];
            }

            // Exchange ghost dU for backward sweep (optional)
            if (!skipGhostDU) {
                exchangeDU();
            } else {
                if ((int)ghostDU.size() != dm.nLocalCells()) ghostDU.resize(dm.nLocalCells());
                for (int i = 0; i < dm.nOwned; i++) ghostDU[i] = dU[i];
                for (int i = dm.nOwned; i < dm.nLocalCells(); i++) ghostDU[i] = {0,0,0,0};
            }

            // Backward sweep
            for (int i = dm.nOwned - 1; i >= 0; i--) {
                for (int fi : dm.cellFaces[i]) {
                    int nbr = (dm.faceLc[fi] == i) ? dm.faceRc[fi] : dm.faceLc[fi];
                    if (nbr < 0 || nbr <= i) continue; // upper neighbors only
                    Real lam = 0.5 * faceLambda[fi];
                    Real du_nbr[NEQ];
                    if (nbr < dm.nOwned) {
                        for (int k = 0; k < NEQ; k++) du_nbr[k] = dU[nbr][k];
                    } else {
                        for (int k = 0; k < NEQ; k++) du_nbr[k] = ghostDU[nbr][k];
                    }
                    for (int k = 0; k < NEQ; k++)
                        dU[i][k] += lam * du_nbr[k] / diag[i];
                }
            }
        }
    }

    // Apply update with positivity check
    void applyUpdate(Real scale = 1.0) {
        for (int i = 0; i < dm.nOwned; i++) {
            ConsState Unew;
            for (int k = 0; k < NEQ; k++)
                Unew[k] = U[i][k] + scale * dU[i][k];

            // Positivity check
            Real rho = Unew[0];
            Real p = gas.pressure(Unew);
            if (rho < 1e-8 || p < 1e-8 || !std::isfinite(rho) || !std::isfinite(p)) {
                // Clip: revert to old state or clip
                Unew = U[i];
                if (Unew[0] < 1e-8) Unew[0] = 1e-8;
                Real pe = gas.pressure(Unew);
                if (pe < 1e-8) {
                    Real kinetic = 0.5*Unew[0]*(Unew[1]*Unew[1]+Unew[2]*Unew[2])/(Unew[0]*Unew[0]);
                    Unew[3] = 1e-8/(gas.gamma_m1) + kinetic;
                }
            }
            U[i] = Unew;
            W[i] = gas.consToPrim(U[i]);
        }
    }

    // ==================== Residual Norms ====================
    Real computeResidualL2() {
        Real localSum[NEQ] = {};
        for (int i = 0; i < dm.nOwned; i++) {
            for (int k = 0; k < NEQ; k++)
                localSum[k] += R[i][k]*R[i][k];
        }
        Real globalSum[NEQ];
        MPI_Allreduce(localSum, globalSum, NEQ, MPI_DOUBLE, MPI_SUM, comm);
        Real total = 0;
        for (int k = 0; k < NEQ; k++) total += globalSum[k];
        return std::sqrt(total);
    }

    // Check if value is finite, return large number if not
    Real safeValue(Real x) const {
        if (!std::isfinite(x)) return 1e10;
        return x;
    }

    void getResidualComponents(Real comps[NEQ], Real& l2, Real& linf) {
        Real localSum[NEQ] = {};
        Real localMax[NEQ] = {};
        for (int i = 0; i < dm.nOwned; i++) {
            for (int k = 0; k < NEQ; k++) {
                localSum[k] += R[i][k]*R[i][k];
                localMax[k] = std::max(localMax[k], std::abs(R[i][k]));
            }
        }
        Real globalSum[NEQ], globalMax[NEQ];
        MPI_Allreduce(localSum, globalSum, NEQ, MPI_DOUBLE, MPI_SUM, comm);
        MPI_Allreduce(localMax, globalMax, NEQ, MPI_DOUBLE, MPI_MAX, comm);
        Real nc_global;
        int nOwned = dm.nOwned;
        MPI_Allreduce(&nOwned, &nc_global, 1, MPI_INT, MPI_SUM, comm);
        for (int k = 0; k < NEQ; k++) {
            comps[k] = safeValue(std::sqrt(globalSum[k]) / std::max(1, (int)nc_global));
        }
        l2 = safeValue(std::sqrt(globalSum[0] + globalSum[1] + globalSum[2] + globalSum[3]));
        linf = safeValue(std::max({globalMax[0], globalMax[1], globalMax[2], globalMax[3]}));
    }

    // ==================== Force Computation ====================
    struct Forces {
        Real cl=0, cd=0, cmz=0;
        Real pDrag=0, vDrag=0, pLift=0, vLift=0;
    };

    Forces computeForces() {
        Forces f;
        Real mu = cfg.mu;
        Real qInf = cfg.qInf;
        Real areaRef = cfg.refArea;
        Real lenRef = cfg.refLength;

        for (int fi = 0; fi < dm.numFaces(); fi++) {
            BCType bc = dm.faceBc[fi];
            if (bc == BCType::None || bc == BCType::Farfield) continue;

            int lc = dm.faceLc[fi];
            Real area = dm.faceArea[fi];
            Real nx = dm.faceNx[fi] / area; // unit normal (outward)
            Real ny = dm.faceNy[fi] / area;
            Real p = std::max(W[lc][3], 1e-12);
            Real pGauge = p - cfg.pInf;  // gauge pressure for force computation

            // Pressure force on body: use gauge pressure (p - pInf)
            Real fpx = pGauge * nx * area;
            Real fpy = pGauge * ny * area;

            // Viscous force
            Real fvx = 0, fvy = 0;
            if (bc == BCType::NoSlipAdiabaticWall && cfg.mode == "laminar" && mu > 0) {
                // Use LS gradient for wall shear (stable for stretched meshes)
                Real dudx = recon->grad[(lc*5+1)*2+0];
                Real dudy = recon->grad[(lc*5+1)*2+1];
                Real dvdx = recon->grad[(lc*5+2)*2+0];
                Real dvdy = recon->grad[(lc*5+2)*2+1];
                Real div = dudx + dvdy;
                Real tauxx = mu*(2.0*dudx - 2.0/3.0*div);
                Real tauyy = mu*(2.0*dvdy - 2.0/3.0*div);
                Real tauxy = mu*(dudy + dvdx);
                // Viscous force on body: F = -(tau · n) * area
                fvx = -(tauxx*nx + tauxy*ny) * area;
                fvy = -(tauxy*nx + tauyy*ny) * area;
            }

            Real fx = fpx + fvx;
            Real fy = fpy + fvy;

            f.cd += fx;
            f.cl += fy;

            // Moment about (mcx, mcy)
            Real rx = dm.faceFx[fi] - cfg.mcx;
            Real ry = dm.faceFy[fi] - cfg.mcy;
            f.cmz += (rx*fy - ry*fx);

            f.pDrag += fpx;
            f.vDrag += fvx;
        f.pLift += fpy;
        f.vLift += fvy;
        }

        // MPI reduce
        Real local[7] = {f.cl, f.cd, f.cmz, f.pDrag, f.vDrag, f.pLift, f.vLift};
        Real global[7];
        MPI_Allreduce(local, global, 7, MPI_DOUBLE, MPI_SUM, comm);

        // local[0]=f.cl=Fy, local[1]=f.cd=Fx, local[2]=cmz, local[3..6]=forces
        Real scale = 1.0 / (qInf * areaRef);
        f.cl = global[0] * scale;       // lift = Fy
        f.cd = global[1] * scale;       // drag = Fx
        f.cmz = global[2] * scale / lenRef;
        f.pDrag = global[3] * scale;
        f.vDrag = global[4] * scale;
        f.pLift = global[5] * scale;
        f.vLift = global[6] * scale;

        // Clamp non-finite values for safe output
        auto safeF = [](Real x) -> Real { return std::isfinite(x) ? x : 0.0; };
        f.cl = safeF(f.cl); f.cd = safeF(f.cd); f.cmz = safeF(f.cmz);
        f.pDrag = safeF(f.pDrag); f.vDrag = safeF(f.vDrag);
        f.pLift = safeF(f.pLift); f.vLift = safeF(f.vLift);

        return f;
    }
};

} // namespace cfd
