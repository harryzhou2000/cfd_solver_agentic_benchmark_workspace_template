#pragma once
#include "MeshData.hpp"
#include "Config.hpp"
#include <vector>

struct ResidualContext {
    const LocalMesh& lm;
    const CaseConfig& cfg;
    const std::vector<StateVec>& states;
    const std::vector<StateGrad>& grads;
    const std::vector<std::array<double,4>>& limiters;
    const std::vector<std::array<GradVec,3>>& prim_grads;  // u,v,T gradients
    double mu;  // viscosity
    double k_cond;  // thermal conductivity
};

// Compute the FVM residual R(U) for all owned cells
// residuals[i] = -R_i (right-hand side of dU/dt = -R)
// spectral_radii[i] = estimate of max wave speed for cell i
void computeResidual(const ResidualContext& ctx,
                     std::vector<StateVec>& residuals,
                     std::vector<double>& spectral_radii);
