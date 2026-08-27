#pragma once
#include "MeshData.hpp"
#include "Config.hpp"
#include <vector>

// Compute cell-centered least-squares gradients of all 4 conservative variables
// grad[c][k] = {dUk/dx, dUk/dy} for cell c, variable k
void computeGradients(const LocalMesh& lm,
                      const std::vector<StateVec>& states,
                      std::vector<StateGrad>& grads);

// Compute gradients of primitive variables: u, v, T
// prim_grads[c][0..2] = grad_u, grad_v, grad_T
using PrimGrads = std::array<GradVec, 3>;
void computePrimGradients(const LocalMesh& lm,
                          const std::vector<StateVec>& states,
                          double gamma, double R,
                          std::vector<PrimGrads>& prim_grads);
