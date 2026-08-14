#pragma once
#include "common/types.hpp"
#include "common/gas_model.hpp"
#include "mesh/mesh.hpp"

// Compute cell-centered gradients using weighted least-squares
// Input: cell states; Output: gradients (d/dx, d/dy) per cell
void compute_gradients_lsq(const RankMesh& rm,
                           const std::vector<StateVector>& state,
                           std::vector<Vec2>& grad_rho,
                           std::vector<Grad2>& grad_u,
                           std::vector<Grad2>& grad_v,
                           std::vector<Grad2>& grad_T,
                           const GasModel& gas);
