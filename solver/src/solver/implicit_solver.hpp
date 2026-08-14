#pragma once
#include "common/types.hpp"
#include "mesh/mesh.hpp"
#include "common/gas_model.hpp"

// Implicit LU-SGS solver for steady/transient systems
void lu_sgs_sweep(RankMesh& rm, const std::vector<Real>& dt,
                  const GasModel& gas, const CaseConfig& cfg,
                  int inner_iterations);

// Jacobi iteration (fallback)
void jacobi_sweep(RankMesh& rm, const std::vector<Real>& dt,
                  const GasModel& gas, int iterations);
