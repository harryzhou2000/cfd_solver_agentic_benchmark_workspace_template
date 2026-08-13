// implicit.cpp - LU-SGS implicit solver implementation including BDF2 variant.
#include "solver.hpp"
#include <cmath>
#include <algorithm>

namespace cfd2d {

// LU-SGS with BDF2 source term for dual-time stepping.
// Solves: (1/dtau + bdf2_a0 + spectralRadius) * dU = totalRhs
// Then updates: U += dU
void Solver::solveLU_SGS_BDF2(StateVec& U, const StateVec& totalRhs, double dtau, double bdf2_a0) {
    int nOwned = localMesh_.nOwned;
    
    // Compute spectral radii
    std::vector<double> spectralRadius(nOwned, 0.0);
    for (int c = 0; c < nOwned; c++) {
        Prim W = conservativeToPrimitive(U[c], gas_);
        double a = soundSpeed(W, gas_);
        double sr = 0;
        for (idx_t f : localMesh_.cells[c].faces) {
            Face& face = localMesh_.faces[f];
            double un = W(1)*face.nx + W(2)*face.ny;
            sr += (std::abs(un) + a) * face.area;
        }
        spectralRadius[c] = sr / std::max(localMesh_.cells[c].volume, 1e-30);
    }
    
    // Add viscous spectral radius
    double mu = case_.physics.mu();
    if (case_.physics.viscous && mu > 0) {
        for (int c = 0; c < nOwned; c++) {
            Prim W = conservativeToPrimitive(U[c], gas_);
            double viscSr = 0;
        for (idx_t f : localMesh_.cells[c].faces) {
            Face& face = localMesh_.faces[f];
            viscSr += 2.0 * mu / (W(0) + 1e-30) * face.area;
        }
        spectralRadius[c] += viscSr;
        }
    }
    
    // Diagonal implicit update
    StateVec dU(nOwned);
    for (int c = 0; c < nOwned; c++) {
        double diag = 1.0/dtau + bdf2_a0 + spectralRadius[c];
        dU[c] = totalRhs[c] / diag;
        U[c] += dU[c];
    }
    exchangeHalo(U);
}

} // namespace cfd2d
