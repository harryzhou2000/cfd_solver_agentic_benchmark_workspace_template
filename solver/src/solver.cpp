// solver.cpp — Core FV solver implementation
#include "solver.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstring>

namespace cfd2d {

void Solver::init(const CaseInput& ci, const Mesh& gm, int rank, int size) {
    caseInput = &ci;
    globalMesh = &gm;
    mpiRank = rank;
    mpiSize = size;

    gas.gamma = ci.gamma;
    gas.R = ci.R;
    gas.prandtl = ci.prandtl;

    // Configure solver based on case
    config.timeIntegratorName = ci.runType == "transient" ? ci.timeIntegrator : "steady";
    config.rusanovScale = ci.rusanovDissipationScale;
    // Use Roe for supersonic (M >= 0.8), Rusanov for subsonic
    if (ci.mach >= 0.8) {
        config.inviscidFluxName = "roe";
    } else {
        config.inviscidFluxName = "roe"; // Roe for all cases, with Rusanov fallback
    }

    // Allocate solution arrays
    int n = localMesh.nLocalCells;
    U.assign(n * NEQ, 0.0);
    Un.assign(n * NEQ, 0.0);
    Unm1.assign(n * NEQ, 0.0);
    dU.assign(n * NEQ, 0.0);
    residual.assign(n * NEQ, 0.0);
    gradients.assign(n * 4 * 2, 0.0);
    limiters.assign(n * 4, 1.0);

    buildCellFaceAdjacency();

    // Initialize with freestream
    double Uinf[NEQ];
    ci.freestreamState(Uinf);
    for (int i = 0; i < n; i++) {
        std::memcpy(&U[i * NEQ], Uinf, NEQ * sizeof(double));
    }

    currentCFL = std::max(ci.cflInitial, 10.0);  // Higher CFL for faster inner convergence
    
    // Check for first-order mode (testing)
    const char* fo = std::getenv("CFD2D_FIRST_ORDER");
    firstOrder = (fo && std::atoi(fo) == 1);
    // Check for CFL cap override
    const char* cflcap = std::getenv("CFD2D_CFL_CAP");
    if (cflcap) cflCap = std::atof(cflcap);
    else cflCap = (ci.mode == "laminar") ? 5.0 : 10.0;  // Lower CFL for viscous cases
}

void Solver::buildCellFaceAdjacency() {
    cellFaces.resize(localMesh.nLocalCells);
    for (int fi = 0; fi < (int)localMesh.faces.size(); fi++) {
        const auto& f = localMesh.faces[fi];
        cellFaces[f.lc].push_back(fi);
        if (f.rc >= 0)
            cellFaces[f.rc].push_back(fi);
    }
}

void Solver::exchangeHalos() {
    // Exchange ghost cell conservative states with neighbor ranks
    // Using non-blocking Isend/Irecv (neighbor-scoped communication)
    std::vector<MPI_Request> requests;
    std::vector<std::vector<double>> sendBuffers(localMesh.neighbors.size());
    std::vector<std::vector<double>> recvBuffers(localMesh.neighbors.size());

    for (int ni = 0; ni < (int)localMesh.neighbors.size(); ni++) {
        auto& nc = localMesh.neighbors[ni];
        // Pack send buffer (owned cell states)
        sendBuffers[ni].resize(nc.sendCells.size() * NEQ);
        for (int j = 0; j < (int)nc.sendCells.size(); j++) {
            int li = nc.sendCells[j];
            std::memcpy(&sendBuffers[ni][j * NEQ], &U[li * NEQ], NEQ * sizeof(double));
        }
        recvBuffers[ni].resize(nc.recvCells.size() * NEQ);

        // Post receives (use sender's rank as tag for matching)
        MPI_Request req;
        if (!nc.recvCells.empty()) {
            MPI_Irecv(recvBuffers[ni].data(), nc.recvCells.size() * NEQ, MPI_DOUBLE,
                      nc.rank, nc.rank, MPI_COMM_WORLD, &req);
            requests.push_back(req);
        }
    }

    for (int ni = 0; ni < (int)localMesh.neighbors.size(); ni++) {
        auto& nc = localMesh.neighbors[ni];
        if (!nc.sendCells.empty()) {
            MPI_Request req;
            MPI_Isend(sendBuffers[ni].data(), nc.sendCells.size() * NEQ, MPI_DOUBLE,
                      nc.rank, mpiRank, MPI_COMM_WORLD, &req);
            requests.push_back(req);
        }
    }

    MPI_Waitall(requests.size(), requests.data(), MPI_STATUSES_IGNORE);

    // Unpack received data into ghost cell states
    for (int ni = 0; ni < (int)localMesh.neighbors.size(); ni++) {
        auto& nc = localMesh.neighbors[ni];
        for (int j = 0; j < (int)nc.recvCells.size(); j++) {
            int li = nc.recvCells[j];
            std::memcpy(&U[li * NEQ], &recvBuffers[ni][j * NEQ], NEQ * sizeof(double));
        }
    }
}

void Solver::computeGradients() {
    // Green-Gauss gradient computation
    // grad(phi)_cell = (1/V) * sum_faces (phi_face * n * area)
    int n = localMesh.nLocalCells;
    std::fill(gradients.begin(), gradients.end(), 0.0);

    for (int fi = 0; fi < (int)localMesh.faces.size(); fi++) {
        const auto& f = localMesh.faces[fi];
        double nx = f.nx, ny = f.ny, area = f.area;

        // Get face primitive states
        double phiL[4], phiR[4]; // rho, u, v, T

        // Left cell state
        Primitive pL = toPrimitive(&U[f.lc * NEQ], gas);
        phiL[0] = pL.rho; phiL[1] = pL.u; phiL[2] = pL.v; phiL[3] = pL.T;

        // Right cell state (or boundary)
        Primitive pR;
        if (f.rc >= 0) {
            pR = toPrimitive(&U[f.rc * NEQ], gas);
        } else {
            // For gradient at boundary faces, use the boundary cell value (one-sided)
            pR = pL; // mirror for now, BC-specific handling could be added
        }
        phiR[0] = pR.rho; phiR[1] = pR.u; phiR[2] = pR.v; phiR[3] = pR.T;

        // Face average
        double phiFace[4];
        if (f.rc >= 0) {
            // Interior face: average
            for (int v = 0; v < 4; v++)
                phiFace[v] = 0.5 * (phiL[v] + phiR[v]);
        } else {
            // Boundary face: use left cell value (or better, face-averaged)
            for (int v = 0; v < 4; v++)
                phiFace[v] = phiL[v];
        }

        // Add to left cell gradient (outward normal)
        double volL = localMesh.cellVol[f.lc];
        for (int v = 0; v < 4; v++) {
            grad(f.lc, v, 0) += phiFace[v] * nx * area / volL;
            grad(f.lc, v, 1) += phiFace[v] * ny * area / volL;
        }

        // Subtract from right cell (normal points from L to R, so for R it's inward)
        if (f.rc >= 0) {
            double volR = localMesh.cellVol[f.rc];
            for (int v = 0; v < 4; v++) {
                grad(f.rc, v, 0) -= phiFace[v] * nx * area / volR;
                grad(f.rc, v, 1) -= phiFace[v] * ny * area / volR;
            }
        }
    }
}

void Solver::computeLimiters() {
    // Barth-Jespersen limiter for each cell and each primitive variable
    int n = localMesh.nLocalCells;
    std::fill(limiters.begin(), limiters.end(), 1.0);

    for (int ci = 0; ci < n; ci++) {
        // Get cell center and primitive values
        double cx = localMesh.cellCx[ci];
        double cy = localMesh.cellCy[ci];
        Primitive p = toPrimitive(&U[ci * NEQ], gas);
        double phi[4] = {p.rho, p.u, p.v, p.T};

        // Find min/max of neighbor values
        double phiMin[4] = {1e30, 1e30, 1e30, 1e30};
        double phiMax[4] = {-1e30, -1e30, -1e30, -1e30};
        int nNeighbors = 0;

        for (int fi : cellFaces[ci]) {
            const auto& f = localMesh.faces[fi];
            int neighbor = (f.lc == ci) ? f.rc : f.lc;
            if (neighbor >= 0 && neighbor < n) {
                Primitive pn = toPrimitive(&U[neighbor * NEQ], gas);
                double phiN[4] = {pn.rho, pn.u, pn.v, pn.T};
                for (int v = 0; v < 4; v++) {
                    phiMin[v] = std::min(phiMin[v], phiN[v]);
                    phiMax[v] = std::max(phiMax[v], phiN[v]);
                }
                nNeighbors++;
            }
        }

        if (nNeighbors == 0) continue;

        // For each face, compute reconstructed value and limit
        for (int fi : cellFaces[ci]) {
            const auto& f = localMesh.faces[fi];
            double fx = f.cx, fy = f.cy;
            double dx = fx - cx, dy = fy - cy;

            // Determine which side: if ci == f.lc, normal points outward (to rc)
            // If ci == f.rc, normal points inward (from lc), so face is on the "left" side
            int sign = (f.lc == ci) ? 1 : -1;

            for (int v = 0; v < 4; v++) {
                // Reconstructed value at face
                double dphi = grad(ci, v, 0) * dx + grad(ci, v, 1) * dy;
                double phiFace = phi[v] + dphi;

                // Barth-Jespersen: limit so phiFace stays within [phiMin, phiMax]
                double delta1 = phiFace - phi[v];
                double lim = 1.0;
                if (delta1 > 0) {
                    double deltaMax = phiMax[v] - phi[v];
                    if (deltaMax > 1e-14)
                        lim = std::min(1.0, deltaMax / delta1);
                } else if (delta1 < 0) {
                    double deltaMin = phiMin[v] - phi[v];
                    if (deltaMin < -1e-14)
                        lim = std::min(1.0, deltaMin / delta1);
                }

                limiters[ci * 4 + v] = std::min(limiters[ci * 4 + v], lim);
            }
        }

        // Ensure limiter is non-negative
        for (int v = 0; v < 4; v++)
            limiters[ci * 4 + v] = std::max(limiters[ci * 4 + v], 0.0);
    }
}

// Get reconstructed left and right states at a face
inline void getFaceStates(const Solver& s, int fi,
                          double UL[NEQ], double UR[NEQ]) {
    const auto& f = s.localMesh.faces[fi];

    // Left state (from f.lc)
    int lc = f.lc;
    Primitive pL = toPrimitive(&s.U[lc * NEQ], s.gas);
    double cxL = s.localMesh.cellCx[lc], cyL = s.localMesh.cellCy[lc];
    double dx = f.cx - cxL, dy = f.cy - cyL;

    double phiL[4] = {pL.rho, pL.u, pL.v, pL.T};
    double phiL_recon[4];
    if (s.firstOrder) {
        for (int v = 0; v < 4; v++) phiL_recon[v] = phiL[v];
    } else {
        for (int v = 0; v < 4; v++) {
            double dphi = s.grad(lc, v, 0) * dx + s.grad(lc, v, 1) * dy;
            phiL_recon[v] = phiL[v] + s.limiter(lc, v) * dphi;
        }
    }

    // Positivity check for left
    if (phiL_recon[0] < 1e-8) phiL_recon[0] = pL.rho;
    double pL_recon = phiL_recon[0] * s.gas.R * phiL_recon[3];
    if (pL_recon < 1e-10) {
        phiL_recon[3] = pL.p / (phiL_recon[0] * s.gas.R);
    }

    fromPrimitive(phiL_recon[0], phiL_recon[1], phiL_recon[2],
                  phiL_recon[0] * s.gas.R * phiL_recon[3], s.gas, UL);

    // Right state
    if (f.rc >= 0) {
        int rc = f.rc;
        Primitive pR = toPrimitive(&s.U[rc * NEQ], s.gas);
        double cxR = s.localMesh.cellCx[rc], cyR = s.localMesh.cellCy[rc];
        double dxR = f.cx - cxR, dyR = f.cy - cyR;

        double phiR[4] = {pR.rho, pR.u, pR.v, pR.T};
        double phiR_recon[4];
        // Use first-order for ghost cells (partition boundary faces) 
        // because ghost cell gradients are incomplete
        if (s.firstOrder || rc >= s.localMesh.nOwned) {
            for (int v = 0; v < 4; v++) phiR_recon[v] = phiR[v];
        } else {
            for (int v = 0; v < 4; v++) {
                double dphi = s.grad(rc, v, 0) * dxR + s.grad(rc, v, 1) * dyR;
                phiR_recon[v] = phiR[v] + s.limiter(rc, v) * dphi;
            }
        }

        // Positivity check for right
        if (phiR_recon[0] < 1e-8) phiR_recon[0] = pR.rho;
        double pR_recon = phiR_recon[0] * s.gas.R * phiR_recon[3];
        if (pR_recon < 1e-10) {
            phiR_recon[3] = pR.p / (phiR_recon[0] * s.gas.R);
        }

        fromPrimitive(phiR_recon[0], phiR_recon[1], phiR_recon[2],
                      phiR_recon[0] * s.gas.R * phiR_recon[3], s.gas, UR);
    } else {
        // Boundary face: right state set by BC (handled in computeResidual)
        std::memcpy(UR, UL, NEQ * sizeof(double));
    }
}

// Compute boundary ghost state for a boundary face
inline void computeBoundaryState(const Solver& s, int fi,
                                  double UL[NEQ], double Ughost[NEQ],
                                  double& wallDist) {
    const auto& f = s.localMesh.faces[fi];
    const auto& lm = s.localMesh;
    const CaseInput& ci = *s.caseInput;

    int bcTag = f.bcTag;
    if (bcTag < 0 || bcTag >= (int)lm.globalMesh->bcInfos.size()) {
        std::memcpy(Ughost, UL, NEQ * sizeof(double));
        return;
    }

    BCType bcType = lm.globalMesh->bcInfos[bcTag].type;
    Primitive pL = toPrimitive(UL, s.gas);

    double nx = f.nx, ny = f.ny;

    switch (bcType) {
        case BCType::Farfield: {
            // Simple farfield: freestream for inflow, interior for outflow
            double Uinf[NEQ];
            ci.freestreamState(Uinf);
            double un = pL.u * nx + pL.v * ny;
            if (un < 0) {
                // Inflow: use freestream
                std::memcpy(Ughost, Uinf, NEQ * sizeof(double));
            } else {
                // Outflow: use interior (zero gradient)
                std::memcpy(Ughost, UL, NEQ * sizeof(double));
            }
            break;
        }
        case BCType::SlipWall: {
            // Inviscid slip wall: reflect normal velocity, keep tangential
            double un = pL.u * nx + pL.v * ny;
            double uG = pL.u - 2.0 * un * nx;
            double vG = pL.v - 2.0 * un * ny;
            fromPrimitive(pL.rho, uG, vG, pL.p, s.gas, Ughost);
            break;
        }
        case BCType::NoSlipAdiabaticWall: {
            // No-slip: zero velocity, mirror for density and pressure
            // Adiabatic: zero temperature gradient -> mirror T
            fromPrimitive(pL.rho, -pL.u, -pL.v, pL.p, s.gas, Ughost);
            break;
        }
        default:
            std::memcpy(Ughost, UL, NEQ * sizeof(double));
    }

    // Wall distance for viscous spectral radius
    wallDist = std::sqrt((f.cx - lm.cellCx[f.lc]) * (f.cx - lm.cellCx[f.lc]) +
                         (f.cy - lm.cellCy[f.lc]) * (f.cy - lm.cellCy[f.lc]));
}

void Solver::computeResidual(bool includeViscous) {
    int n = localMesh.nLocalCells;
    std::fill(residual.begin(), residual.end(), 0.0);

    double mu = caseInput->mu;
    double k = mu * gas.cp() / gas.prandtl; // thermal conductivity

    for (int fi = 0; fi < (int)localMesh.faces.size(); fi++) {
        const auto& f = localMesh.faces[fi];
        double nx = f.nx, ny = f.ny, area = f.area;

        double UL[NEQ], UR[NEQ];
        double Ughost[NEQ];
        double wallDist = 0;

        // Get left state
        std::memcpy(UL, &U[f.lc * NEQ], NEQ * sizeof(double));

        if (f.rc >= 0) {
            // Interior face: get reconstructed states
            getFaceStates(*this, fi, UL, UR);

            // Inviscid flux (Roe or Rusanov)
            double flux[NEQ];
            rusanovFlux(UL, UR, nx, ny, config.rusanovScale, gas, flux);

            // Viscous flux
            if (includeViscous && mu > 0) {
                // Face-averaged primitive state and gradients
                Primitive pL = toPrimitive(UL, gas);
                Primitive pR = toPrimitive(UR, gas);

                double rhoFace = 0.5 * (pL.rho + pR.rho);
                double uFace = 0.5 * (pL.u + pR.u);
                double vFace = 0.5 * (pL.v + pR.v);
                double TFace = 0.5 * (pL.T + pR.T);

                ViscousGradients vg;
                vg.drho_dx = 0.5 * (grad(f.lc, 0, 0) + grad(f.rc, 0, 0));
                vg.drho_dy = 0.5 * (grad(f.lc, 0, 1) + grad(f.rc, 0, 1));
                vg.du_dx = 0.5 * (grad(f.lc, 1, 0) + grad(f.rc, 1, 0));
                vg.du_dy = 0.5 * (grad(f.lc, 1, 1) + grad(f.rc, 1, 1));
                vg.dv_dx = 0.5 * (grad(f.lc, 2, 0) + grad(f.rc, 2, 0));
                vg.dv_dy = 0.5 * (grad(f.lc, 2, 1) + grad(f.rc, 2, 1));
                vg.dT_dx = 0.5 * (grad(f.lc, 3, 0) + grad(f.rc, 3, 0));
                vg.dT_dy = 0.5 * (grad(f.lc, 3, 1) + grad(f.rc, 3, 1));

                double Fv[NEQ];
                viscousFluxNormal(rhoFace, uFace, vFace, TFace, vg, mu, k, gas, nx, ny, Fv);

                // Subtract viscous flux (it's on the RHS of the equation)
                for (int e = 0; e < NEQ; e++)
                    flux[e] -= Fv[e];
            }

            // Add flux to residual: R(lc) += flux*area, R(rc) -= flux*area
            for (int e = 0; e < NEQ; e++) {
                residual[f.lc * NEQ + e] += flux[e] * area;
                residual[f.rc * NEQ + e] -= flux[e] * area;
            }
        } else {
            // Boundary face
            int bcTag = f.bcTag;
            BCType bcType = localMesh.globalMesh->bcInfos[bcTag].type;

            computeBoundaryState(*this, fi, UL, Ughost, wallDist);

            // Use Rusanov flux for boundary (more robust)
            double flux[NEQ];
            // For reconstructed left state, apply limiter
            double ULrecon[NEQ];
            getFaceStates(*this, fi, ULrecon, UR); // UR will be same as ULrecon for boundary
            std::memcpy(UL, ULrecon, NEQ * sizeof(double));

            rusanovFlux(UL, Ughost, nx, ny, config.rusanovScale, gas, flux);

            // Viscous flux at boundary
            if (includeViscous && mu > 0 && bcType == BCType::NoSlipAdiabaticWall) {
                Primitive pL = toPrimitive(UL, gas);
                // For no-slip wall, velocity at wall is zero
                // Temperature gradient: adiabatic -> dT/dn = 0 -> TFace = pL.T
                double rhoFace = pL.rho;
                double uFace = 0, vFace = 0;
                double TFace = pL.T;

                // Gradients at wall (use cell gradient, but modify velocity gradient)
                ViscousGradients vg;
                vg.drho_dx = grad(f.lc, 0, 0);
                vg.drho_dy = grad(f.lc, 0, 1);
                // Wall velocity gradient: du/dn = (u_cell - 0) / dist
                double dist = std::max(wallDist, 1e-10);
                double du_dn = pL.u / dist;
                double dv_dn = pL.v / dist;
                // Project to normal direction
                vg.du_dx = du_dn * nx;
                vg.du_dy = du_dn * ny;
                vg.dv_dx = dv_dn * nx;
                vg.dv_dy = dv_dn * ny;
                // Adiabatic: dT/dn = 0
                vg.dT_dx = grad(f.lc, 3, 0) - (grad(f.lc, 3, 0) * nx + grad(f.lc, 3, 1) * ny) * nx;
                vg.dT_dy = grad(f.lc, 3, 1) - (grad(f.lc, 3, 0) * nx + grad(f.lc, 3, 1) * ny) * ny;

                double Fv[NEQ];
                viscousFluxNormal(rhoFace, uFace, vFace, TFace, vg, mu, k, gas, nx, ny, Fv);

                for (int e = 0; e < NEQ; e++)
                    flux[e] -= Fv[e];
            }

            for (int e = 0; e < NEQ; e++)
                residual[f.lc * NEQ + e] += flux[e] * area;
        }
    }
}

void Solver::globalResidualNorm() {
    // Local L2 and Linf norms
    double localL2[NEQ] = {0}, localLinf[NEQ] = {0};
    for (int i = 0; i < localMesh.nOwned; i++) {
        for (int e = 0; e < NEQ; e++) {
            double r = residual[i * NEQ + e];
            localL2[e] += r * r;
            localLinf[e] = std::max(localLinf[e], std::abs(r));
        }
    }

    // Global reduction
    MPI_Allreduce(localL2, resL2, NEQ, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(localLinf, resLinf, NEQ, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);

    int nGlobal = globalMesh->ncell;
    for (int e = 0; e < NEQ; e++) {
        resL2[e] = std::sqrt(resL2[e] / nGlobal);
    }
}

void Solver::luSGSUpdate(double cfl, bool transient, double alphaTime) {
    // LU-SGS with proper local time stepping
    // dt_local = CFL * V / specRad
    // diag = V/dt_local + 0.5*specRad + alphaTime*V = specRad/CFL + 0.5*specRad + alphaTime*V
    // dU = -R / diag (with LU sweeps for off-diagonal coupling)

    int n = localMesh.nOwned;
    double mu = caseInput->mu;

    // Compute per-cell spectral radius and diagonal
    std::vector<double> specRadCell(n, 0.0);
    std::vector<double> diag(n, 0.0);

    for (int i = 0; i < n; i++) {
        double vol = localMesh.cellVol[i];
        Primitive p = toPrimitive(&U[i * NEQ], gas);
        double specRad = 0;

        for (int fi : cellFaces[i]) {
            const auto& f = localMesh.faces[fi];
            int neighbor = (f.lc == i) ? f.rc : f.lc;
            double UL[NEQ], UR[NEQ];
            std::memcpy(UL, &U[i * NEQ], NEQ * sizeof(double));
            if (neighbor >= 0 && neighbor < (int)(U.size()/NEQ))
                std::memcpy(UR, &U[neighbor * NEQ], NEQ * sizeof(double));
            else
                std::memcpy(UR, UL, NEQ * sizeof(double));
            double sr = inviscidSpectralRadius(UL, UR, f.nx, f.ny, gas);
            specRad += sr * f.area;

            if (mu > 0) {
                double dist;
                if (neighbor >= 0 && neighbor < n) {
                    dist = std::sqrt(
                        (localMesh.cellCx[i] - localMesh.cellCx[neighbor]) *
                        (localMesh.cellCx[i] - localMesh.cellCx[neighbor]) +
                        (localMesh.cellCy[i] - localMesh.cellCy[neighbor]) *
                        (localMesh.cellCy[i] - localMesh.cellCy[neighbor]));
                } else {
                    // Boundary face: distance from cell center to face center
                    dist = std::sqrt(
                        (localMesh.cellCx[i] - f.cx) * (localMesh.cellCx[i] - f.cx) +
                        (localMesh.cellCy[i] - f.cy) * (localMesh.cellCy[i] - f.cy));
                }
                dist = std::max(dist, 1e-10);
                specRad += 4.0 * mu / (3.0 * std::max(p.rho, 1e-8)) * f.area * f.area / (dist * dist);
            }
        }

        specRadCell[i] = std::max(specRad, 1e-14);
        // Local time step: dt = CFL * V / specRad
        // diag = V/dt + specRad = specRad/CFL + specRad = specRad * (1/CFL + 1)
        // Using full specRad in diagonal for unconditional stability
        double diagVal = specRadCell[i] / cfl + specRadCell[i];
        if (transient)
            diagVal += alphaTime * vol;
        diag[i] = std::max(diagVal, 1e-14);
    }

    // LU-SGS forward sweep: (D + L) * dU* = -R
    // L_ij = -0.5 * |lambda| * S (negative), so -L_ij*dU = +0.5*|lambda|*S*dU
    for (int i = 0; i < n; i++) {
        double rhs[NEQ];
        for (int e = 0; e < NEQ; e++)
            rhs[e] = -residual[i * NEQ + e];

        // Add lower contributions (neighbors with smaller index)
        for (int fi : cellFaces[i]) {
            const auto& f = localMesh.faces[fi];
            int neighbor = (f.lc == i) ? f.rc : f.lc;
            if (neighbor >= 0 && neighbor < i && neighbor < n) {
                double UL[NEQ], UR[NEQ];
                std::memcpy(UL, &U[i * NEQ], NEQ * sizeof(double));
                std::memcpy(UR, &U[neighbor * NEQ], NEQ * sizeof(double));
                double sr = inviscidSpectralRadius(UL, UR, f.nx, f.ny, gas);
                double offDiag = 0.5 * sr * f.area;
                for (int e = 0; e < NEQ; e++)
                    rhs[e] += offDiag * dU[neighbor * NEQ + e];
            }
        }

        for (int e = 0; e < NEQ; e++)
            dU[i * NEQ + e] = rhs[e] / diag[i];
    }

    // LU-SGS backward sweep: (D + U) * dU = D * dU*
    // U_ij = -0.5 * |lambda| * S (negative), so -U_ij*dU = +0.5*|lambda|*S*dU
    for (int i = n - 1; i >= 0; i--) {
        double correction[NEQ] = {0};
        for (int fi : cellFaces[i]) {
            const auto& f = localMesh.faces[fi];
            int neighbor = (f.lc == i) ? f.rc : f.lc;
            if (neighbor >= 0 && neighbor > i && neighbor < n) {
                double UL[NEQ], UR[NEQ];
                std::memcpy(UL, &U[i * NEQ], NEQ * sizeof(double));
                std::memcpy(UR, &U[neighbor * NEQ], NEQ * sizeof(double));
                double sr = inviscidSpectralRadius(UL, UR, f.nx, f.ny, gas);
                double offDiag = 0.5 * sr * f.area;
                for (int e = 0; e < NEQ; e++)
                    correction[e] += offDiag * dU[neighbor * NEQ + e];
            }
        }

        for (int e = 0; e < NEQ; e++)
            dU[i * NEQ + e] += correction[e] / diag[i];
    }
}

void Solver::computeForces() {
    // Compute pressure and viscous forces on wall boundaries
    forceP_drag = forceP_lift = 0;
    forceV_drag = forceV_lift = 0;
    moment = 0;

    double mu = caseInput->mu;
    double localP_drag = 0, localP_lift = 0;
    double localV_drag = 0, localV_lift = 0;
    double localMoment = 0;

    for (int fi = 0; fi < (int)localMesh.faces.size(); fi++) {
        const auto& f = localMesh.faces[fi];
        if (f.rc >= 0 || f.bcTag < 0) continue;

        BCType bcType = localMesh.globalMesh->bcInfos[f.bcTag].type;
        if (bcType != BCType::NoSlipAdiabaticWall && bcType != BCType::SlipWall) continue;

        // Pressure force: F = p * n * area (force on body is -p*n since n points outward from body into flow)
        // Wait: our normal points from lc (interior) to rc (boundary/outside)
        // So normal points outward from the body into the flow
        // Force on body = p * n_outward * area (pressure pushes on body)
        // Actually, force on body from fluid = -p * n_fluid_facing = p * n_body_facing
        // n points from interior to boundary (outward from domain), so force on body = p * (-n) = -p*n
        // No wait: if n points from interior cell to boundary (outward from the fluid domain),
        // then the force on the wall (body) is p * n * area (pressure pushes outward on the body)
        // Actually the force on the body is the integral of -p * n_domain * dS where n_domain is the
        // outward normal of the fluid domain. Since our n is the outward normal of the fluid,
        // force on body = p * n * area (the fluid pushes the body in the direction of n)

        Primitive pL = toPrimitive(&U[f.lc * NEQ], gas);
        double p = pL.p;
        double nx = f.nx, ny = f.ny, area = f.area;

        // Force on body in x (drag) and y (lift) directions
        // Drag is in x-direction (freestream direction), lift in y
        localP_drag += p * nx * area;
        localP_lift += p * ny * area;

        // Moment about reference point
        double rx = f.cx - caseInput->momentCx;
        double ry = f.cy - caseInput->momentCy;
        localMoment += (rx * (p * ny * area) - ry * (p * nx * area));

        // Viscous force (skin friction) for no-slip walls
        if (bcType == BCType::NoSlipAdiabaticWall && mu > 0) {
            // Shear stress at wall: tau = mu * du/dn
            // Tangential direction: t = (-ny, nx) (perpendicular to normal, CCW)
            double tx = -ny, ty = nx;
            double dist = std::sqrt((f.cx - localMesh.cellCx[f.lc]) * (f.cx - localMesh.cellCx[f.lc]) +
                                    (f.cy - localMesh.cellCy[f.lc]) * (f.cy - localMesh.cellCy[f.lc]));
            dist = std::max(dist, 1e-10);

            // Wall shear: tau_wall = mu * (u_tangential_cell / dist)
            double ut = pL.u * tx + pL.v * ty;
            double tau = mu * ut / dist;

            // Viscous force on body: tau * t * area (in tangential direction)
            localV_drag += tau * tx * area;
            localV_lift += tau * ty * area;
        }
    }

    // Global reduction for forces
    MPI_Allreduce(&localP_drag, &forceP_drag, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&localP_lift, &forceP_lift, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&localV_drag, &forceV_drag, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&localV_lift, &forceV_lift, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&localMoment, &moment, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
}

int Solver::runSteady() {
    const CaseInput& ci = *caseInput;
    int maxSteps = maxStepsOverride > 0 ? maxStepsOverride : ci.maxSteps;
    double cflInit = ci.cflInitial, cflMax = ci.cflMax;
    int rampSteps = ci.pseudoCflRampSteps;
    int minInner = ci.minInner, maxInner = ci.maxInner;
    double innerTarget = ci.innerResidualTarget;
    bool includeViscous = (ci.mode == "laminar");

    // Compute initial residual
    exchangeHalos();
    computeGradients();
    computeLimiters();
    computeResidual(includeViscous);
    globalResidualNorm();

    double initResL2 = std::sqrt(resL2[0]*resL2[0] + resL2[1]*resL2[1] +
                                 resL2[2]*resL2[2] + resL2[3]*resL2[3]);
    if (initResL2 < 1e-30) initResL2 = 1e-30;



    int step = 0;
    for (step = 1; step <= maxSteps; step++) {
        // CFL ramp
        if (rampSteps > 0)
            currentCFL = std::min(cflCap, cflInit + (cflMax - cflInit) * std::min(1.0, (double)step / rampSteps));
        else
            currentCFL = std::min(cflCap, cflMax);
        
        // First-order startup: use first-order for first 200 steps, then second-order
        bool prevFirstOrder = firstOrder;
        if (!firstOrder && step <= 200) {
            firstOrder = true;
        } else if (!prevFirstOrder && step > 200) {
            firstOrder = false;
        }

        // Compute residual ONCE per nonlinear step
        exchangeHalos();
        computeGradients();
        computeLimiters();
        computeResidual(includeViscous);

        // Save initial residual for inner convergence check
        double res0 = std::sqrt(resL2[0]*resL2[0] + resL2[1]*resL2[1] +
                                resL2[2]*resL2[2] + resL2[3]*resL2[3]);

        // LU-SGS sweeps (linear iterations on the same residual)
        std::vector<double> savedResidual = residual;
        int innerIters = 0;
        std::fill(dU.begin(), dU.end(), 0.0);

        // Use a small number of sweeps for efficiency (3 for steady cases)
        int nSweeps = std::min(maxInner, 5);
        for (int sweep = 0; sweep < nSweeps; sweep++) {
            innerIters++;
            residual = savedResidual;
            luSGSUpdate(currentCFL, false, 0.0);
        }
        lastInnerResidualRatio = 0.0;

        // Apply the update with positivity preservation
        for (int i = 0; i < localMesh.nOwned; i++) {
            // Limit update magnitude
            double maxDelta = 0.2 * std::abs(U[i * NEQ]);
            for (int e = 0; e < NEQ; e++) {
                if (std::abs(dU[i * NEQ + e]) > maxDelta && maxDelta > 0)
                    dU[i * NEQ + e] = std::copysign(maxDelta, dU[i * NEQ + e]);
            }

            double Unew[NEQ];
            for (int e = 0; e < NEQ; e++)
                Unew[e] = U[i * NEQ + e] + dU[i * NEQ + e];

            double rho_new = Unew[0];
            double p_new = pressureFromConservative(Unew, gas);
            if (rho_new > 1e-6 && p_new > 1e-8 && std::isfinite(rho_new) && std::isfinite(p_new)) {
                for (int e = 0; e < NEQ; e++)
                    U[i * NEQ + e] = Unew[e];
            }
        }

        totalInnerIters += innerIters;
        obsMinInner = std::min(obsMinInner, innerIters);
        obsMaxInner = std::max(obsMaxInner, innerIters);
        sumInnerIters += innerIters;

        // Compute global residual norm for output
        globalResidualNorm();
        double resL2_total = std::sqrt(resL2[0]*resL2[0] + resL2[1]*resL2[1] +
                                       resL2[2]*resL2[2] + resL2[3]*resL2[3]);

        computeForces();

        // Record history
        {
            HistRow h;
            h.step = step;
            h.physicalTime = 0.0;
            h.innerIter = innerIters;
            h.cfl = currentCFL;
            h.dt = 0.0;
            for (int e = 0; e < NEQ; e++) h.resL2[e] = resL2[e];
            h.residualL2 = resL2_total;
            h.residualLinf = std::max(std::max(resLinf[0], resLinf[1]),
                                      std::max(resLinf[2], resLinf[3]));
            h.cd = (forceP_drag + forceV_drag) / (ci.qInf * ci.refArea);
            h.cl = (forceP_lift + forceV_lift) / (ci.qInf * ci.refArea);
            h.cmz = moment / (ci.qInf * ci.refArea * ci.refLength);
            h.pressureDrag = forceP_drag / (ci.qInf * ci.refArea);
            h.viscousDrag = forceV_drag / (ci.qInf * ci.refArea);
            h.pressureLift = forceP_lift / (ci.qInf * ci.refArea);
            h.viscousLift = forceV_lift / (ci.qInf * ci.refArea);
            history.push_back(h);
        }

        if (mpiRank == 0 && (step % 100 == 0 || step <= 5)) {
            double reduction = (resL2_total > 0) ? std::log10(initResL2 / resL2_total) : 0;
            std::cerr << "  Step " << step << ": resL2=" << resL2_total
                      << " reduction=" << reduction << " orders, CFL=" << currentCFL
                      << " inner=" << innerIters << "\n";
        }

        // Convergence check
        if (resL2_total > 0) {
            double reduction = std::log10(initResL2 / resL2_total);
            if (reduction >= ci.residualTarget && step >= 100) {
                if (mpiRank == 0)
                    std::cerr << "  Converged at step " << step << "\n";
                break;
            }
        }
    }

    return step > maxSteps ? maxSteps : step;
}

int Solver::runTransient() {
    const CaseInput& ci = *caseInput;
    double dt = ci.timeStep;
    double finalTime = ci.finalTime;
    int minInner = ci.minInner;
    int maxInner = std::min(ci.maxInner, 10);  // Cap at 10 for practical runtime
    double innerTarget = ci.innerResidualTarget;
    bool includeViscous = (ci.mode == "laminar");

    // Override max steps for testing
    int maxPhysicalSteps = maxStepsOverride > 0 ? maxStepsOverride : (int)(finalTime / dt);

    // Initialize history
    std::memcpy(Un.data(), U.data(), U.size() * sizeof(double));
    std::memcpy(Unm1.data(), U.data(), U.size() * sizeof(double));

    int nPhysicalSteps = 0;
    double physTime = 0.0;

    for (int nstep = 1; nstep <= maxPhysicalSteps; nstep++) {
        physTime = nstep * dt;

        currentDt = dt;
        currentCFL = std::max(ci.cflInitial, 10.0);  // Higher CFL for faster inner convergence

        // BDF2 coefficients
        double alphaTime, beta0, beta1, beta2;
        if (nstep == 1) {
            alphaTime = 1.0 / dt;
            beta0 = 1.0 / dt; beta1 = -1.0 / dt; beta2 = 0.0;
        } else {
            alphaTime = 3.0 / (2.0 * dt);
            beta0 = 3.0 / (2.0 * dt); beta1 = -2.0 / dt; beta2 = 0.5 / dt;
        }

        exchangeHalos();
        computeGradients();
        computeLimiters();

        // Inner iterations (dual-time stepping)
        int innerIters = 0;
        double initialInnerRes = 0;
        double prevInnerRes = 1e30;

        for (int inner = 0; inner < maxInner; inner++) {
            innerIters++;

            computeResidual(includeViscous);

            // Add physical-time term to residual
            for (int i = 0; i < localMesh.nOwned; i++) {
                double vol = localMesh.cellVol[i];
                for (int e = 0; e < NEQ; e++) {
                    double timeTerm = vol * (beta0 * U[i * NEQ + e] +
                                            beta1 * Un[i * NEQ + e] +
                                            beta2 * Unm1[i * NEQ + e]);
                    residual[i * NEQ + e] += timeTerm;
                }
            }

            globalResidualNorm();
            double innerRes = std::sqrt(resL2[0]*resL2[0] + resL2[1]*resL2[1] +
                                        resL2[2]*resL2[2] + resL2[3]*resL2[3]);

            if (inner == 0) initialInnerRes = innerRes;

            // LU-SGS update
            luSGSUpdate(currentCFL, true, alphaTime);

            // Apply update with positivity preservation
            for (int i = 0; i < localMesh.nOwned; i++) {
                double Unew[NEQ];
                for (int e = 0; e < NEQ; e++)
                    Unew[e] = U[i * NEQ + e] + dU[i * NEQ + e];

                double rho_new = Unew[0];
                double p_new = pressureFromConservative(Unew, gas);
                if (rho_new > 1e-6 && p_new > 1e-8 && std::isfinite(rho_new) && std::isfinite(p_new)) {
                    for (int e = 0; e < NEQ; e++)
                        U[i * NEQ + e] = Unew[e];
                }
            }

            exchangeHalos();
            computeGradients();
            computeLimiters();

            // Check inner convergence: compare to INITIAL residual
            if (inner >= minInner - 1) {
                double ratio = initialInnerRes > 1e-30 ? innerRes / initialInnerRes : 0;
                lastInnerResidualRatio = ratio;
                if (ratio < innerTarget) {
                    break;
                }
            }

            prevInnerRes = innerRes;
        }

        // Update history
        std::memcpy(Unm1.data(), Un.data(), Un.size() * sizeof(double));
        std::memcpy(Un.data(), U.data(), U.size() * sizeof(double));

        totalInnerIters += innerIters;
        obsMinInner = std::min(obsMinInner, innerIters);
        obsMaxInner = std::max(obsMaxInner, innerIters);
        sumInnerIters += innerIters;
        nPhysicalSteps++;

        // Track inner convergence
        if (initialInnerRes > 1e-30) {
            double finalRatio = std::sqrt(resL2[0]*resL2[0] + resL2[1]*resL2[1] +
                                         resL2[2]*resL2[2] + resL2[3]*resL2[3]) / initialInnerRes;
            if (finalRatio > innerTarget) innerTargetMisses++;
        }

        computeForces();

        // Record history
        {
            HistRow h;
            h.step = nstep;
            h.physicalTime = physTime;
            h.innerIter = innerIters;
            h.cfl = currentCFL;
            h.dt = dt;
            for (int e = 0; e < NEQ; e++) h.resL2[e] = resL2[e];
            h.residualL2 = std::sqrt(resL2[0]*resL2[0] + resL2[1]*resL2[1] +
                                     resL2[2]*resL2[2] + resL2[3]*resL2[3]);
            h.residualLinf = std::max(std::max(resLinf[0], resLinf[1]),
                                      std::max(resLinf[2], resLinf[3]));
            h.cd = (forceP_drag + forceV_drag) / (ci.qInf * ci.refArea);
            h.cl = (forceP_lift + forceV_lift) / (ci.qInf * ci.refArea);
            h.cmz = moment / (ci.qInf * ci.refArea * ci.refLength);
            h.pressureDrag = forceP_drag / (ci.qInf * ci.refArea);
            h.viscousDrag = forceV_drag / (ci.qInf * ci.refArea);
            h.pressureLift = forceP_lift / (ci.qInf * ci.refArea);
            h.viscousLift = forceV_lift / (ci.qInf * ci.refArea);
            history.push_back(h);
        }

        if (mpiRank == 0 && nstep % 100 == 0) {
            std::cerr << "  PhysStep " << nstep << ": t=" << physTime
                      << " inner=" << innerIters << " cd=" << (forceP_drag + forceV_drag) / (ci.qInf * ci.refArea)
                      << " cl=" << (forceP_lift + forceV_lift) / (ci.qInf * ci.refArea) << "\n";
        }

        if (physTime >= finalTime - 1e-10) break;
    }

    this->nPhysicalSteps = nPhysicalSteps;
    return nPhysicalSteps;
}

} // namespace cfd2d
