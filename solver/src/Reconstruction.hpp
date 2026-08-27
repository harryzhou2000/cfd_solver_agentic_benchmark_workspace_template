#pragma once
#include "MeshData.hpp"
#include <vector>

// Compute Barth-Jespersen limiters for each cell and each variable
// phi[c][k] in [0,1]
void computeLimiters(const LocalMesh& lm,
                     const std::vector<StateVec>& states,
                     const std::vector<StateGrad>& grads,
                     std::vector<std::array<double,4>>& limiters);

// Reconstruct state at a face point from cell center
// Returns the reconstructed state
StateVec reconstruct(int cell, double fx, double fy,
                     const LocalMesh& lm,
                     const std::vector<StateVec>& states,
                     const std::vector<StateGrad>& grads,
                     const std::vector<std::array<double,4>>& limiters);
