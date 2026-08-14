#pragma once
#include "common/types.hpp"
#include "mesh/mesh.hpp"
#include "common/gas_model.hpp"

// Compute full residual on the RankMesh using first-order FV
// Boundary conditions are applied internally
void compute_residual(RankMesh& rm, const GasModel& gas, const CaseConfig& cfg);

// Simplified version for API compatibility  
void compute_residual(const RankMesh& rm, const std::vector<StateVector>& state,
                      std::vector<StateVector>& residual,
                      const GasModel& gas);
