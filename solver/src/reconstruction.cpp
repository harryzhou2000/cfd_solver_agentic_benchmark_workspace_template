// reconstruction.cpp - Least-squares gradients and Barth-Jespersen limiter.
#include "cfd2d.hpp"
#include <cmath>
#include <algorithm>

namespace cfd2d {

// Green-Gauss gradient at cell centers using face values.
std::vector<Eigen::Matrix<double,2,NEQ>> computeGradients(
    const LocalMesh& mesh, const StateVec& U, const GasModel& gas) {
    int n = mesh.cells.size();
    std::vector<Eigen::Matrix<double,2,NEQ>> grads(n);
    for (auto& g : grads) g.setZero();

    for (int f = 0; f < (int)mesh.faces.size(); f++) {
        const Face& face = mesh.faces[f];
        int lc = face.cells[0];
        int rc = face.cells[1];
        if (lc < 0 && rc < 0) continue;

        State Uface;
        if (lc >= 0 && rc >= 0) {
            Uface = 0.5 * (U[lc] + U[rc]);
        } else if (lc >= 0) {
            // Boundary face: apply BC to get ghost state, then average
            Prim Wc = conservativeToPrimitive(U[lc], gas);
            State Ughost = U[lc];
            if (face.bcType == (int)BCType::SlipWall) {
                double un = Wc(1)*face.nx + Wc(2)*face.ny;
                Prim Wb = Wc;
                Wb(1) -= 2*un*face.nx;
                Wb(2) -= 2*un*face.ny;
                Ughost = primitiveToConservative(Wb, gas);
            } else if (face.bcType == (int)BCType::NoSlipAdiabatic) {
                Prim Wb = Wc;
                Wb(1) = -Wc(1);
                Wb(2) = -Wc(2);
                Ughost = primitiveToConservative(Wb, gas);
            } else if (face.bcType == (int)BCType::Farfield) {
                // Use freestream-like: just use cell value
                Ughost = U[lc];
            }
            Uface = 0.5 * (U[lc] + Ughost);
        } else {
            Uface = U[rc];
        }

        double nxArea = face.nx * face.area;
        double nyArea = face.ny * face.area;

        if (lc >= 0) {
            for (int e = 0; e < NEQ; e++) {
                grads[lc](0, e) += Uface(e) * nxArea;
                grads[lc](1, e) += Uface(e) * nyArea;
            }
        }
        if (rc >= 0) {
            for (int e = 0; e < NEQ; e++) {
                grads[rc](0, e) -= Uface(e) * nxArea;
                grads[rc](1, e) -= Uface(e) * nyArea;
            }
        }
    }

    for (int c = 0; c < n; c++) {
        double invVol = 1.0 / std::max(mesh.cells[c].volume, 1e-30);
        grads[c] *= invVol;
    }
    return grads;
}

// Green-Gauss gradient on primitive variables (with BC-aware face values)
std::vector<Eigen::Matrix<double,2,NPRIM>> computePrimGradientsGG(
    const LocalMesh& mesh, const PrimVec& W, const GasModel& gas) {
    int n = mesh.cells.size();
    std::vector<Eigen::Matrix<double,2,NPRIM>> grads(n);
    for (auto& g : grads) g.setZero();

    for (int f = 0; f < (int)mesh.faces.size(); f++) {
        const Face& face = mesh.faces[f];
        int lc = face.cells[0];
        int rc = face.cells[1];
        if (lc < 0 && rc < 0) continue;

        Prim Wface;
        if (lc >= 0 && rc >= 0) {
            Wface = 0.5 * (W[lc] + W[rc]);
        } else if (lc >= 0) {
            Prim Wc = W[lc];
            Prim Wghost = Wc;
            if (face.bcType == (int)BCType::SlipWall) {
                double un = Wc(1)*face.nx + Wc(2)*face.ny;
                Wghost(1) = Wc(1) - 2*un*face.nx;
                Wghost(2) = Wc(2) - 2*un*face.ny;
            } else if (face.bcType == (int)BCType::NoSlipAdiabatic) {
                Wghost(1) = -Wc(1);
                Wghost(2) = -Wc(2);
            }
            Wface = 0.5 * (Wc + Wghost);
        } else {
            Wface = W[rc];
        }

        double nxArea = face.nx * face.area;
        double nyArea = face.ny * face.area;

        if (lc >= 0) {
            for (int e = 0; e < NPRIM; e++) {
                grads[lc](0, e) += Wface(e) * nxArea;
                grads[lc](1, e) += Wface(e) * nyArea;
            }
        }
        if (rc >= 0) {
            for (int e = 0; e < NPRIM; e++) {
                grads[rc](0, e) -= Wface(e) * nxArea;
                grads[rc](1, e) -= Wface(e) * nyArea;
            }
        }
    }

    for (int c = 0; c < n; c++) {
        double invVol = 1.0 / std::max(mesh.cells[c].volume, 1e-30);
        grads[c] *= invVol;
    }
    return grads;
}

// Barth-Jespersen limiter
std::vector<double> computeLimiters(const LocalMesh& mesh, const StateVec& U,
                                     const std::vector<Eigen::Matrix<double,2,NEQ>>& grads,
                                     const GasModel& gas) {
    int n = mesh.cells.size();
    std::vector<double> limiters(n, 1.0);

    for (int c = 0; c < n; c++) {
        if (mesh.cells[c].isGhost) continue;

        State Umin = U[c];
        State Umax = U[c];

        for (int nb : mesh.cells[c].neighbors) {
            if (nb < 0) continue;
            for (int e = 0; e < NEQ; e++) {
                Umin(e) = std::min(Umin(e), U[nb](e));
                Umax(e) = std::max(Umax(e), U[nb](e));
            }
        }

        double cellLimiter = 1.0;
        for (idx_t f : mesh.cells[c].faces) {
            const Face& face = mesh.faces[f];
            double dx = face.fcx - mesh.cells[c].xc;
            double dy = face.fcy - mesh.cells[c].yc;

            for (int e = 0; e < NEQ; e++) {
                double dU = grads[c](0, e) * dx + grads[c](1, e) * dy;
                double Uface = U[c](e) + dU;

                double phi;
                if (dU > 0) {
                    phi = (Umax(e) - U[c](e)) / std::max(dU, 1e-30);
                } else if (dU < 0) {
                    phi = (Umin(e) - U[c](e)) / std::min(dU, -1e-30);
                } else {
                    phi = 1.0;
                }
                phi = std::max(0.0, std::min(1.0, phi));
                cellLimiter = std::min(cellLimiter, phi);
            }
        }
        limiters[c] = cellLimiter;
    }
    return limiters;
}

void reconstructFace(const State& UL_cell, const State& UR_cell,
                     const Eigen::Matrix<double,2,NEQ>& gradL,
                     const Eigen::Matrix<double,2,NEQ>& gradR,
                     double limL, double limR,
                     double fcx, double fcy,
                     double xcL, double ycL, double xcR, double ycR,
                     State& UL, State& UR) {
    double dxL = fcx - xcL, dyL = fcy - ycL;
    double dxR = fcx - xcR, dyR = fcy - ycR;

    UL = UL_cell;
    UR = UR_cell;
    for (int e = 0; e < NEQ; e++) {
        UL(e) += limL * (gradL(0, e) * dxL + gradL(1, e) * dyL);
        UR(e) += limR * (gradR(0, e) * dxR + gradR(1, e) * dyR);
    }
}

} // namespace cfd2d
