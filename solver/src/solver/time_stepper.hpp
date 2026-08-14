#pragma once
#include "common/types.hpp"
#include "mesh/mesh.hpp"
#include "common/gas_model.hpp"

// Steady pseudo-time march with CFL ramping
void steady_march(RankMesh& rm, const CaseConfig& cfg, const GasModel& gas);

// Transient march (BDF2 or trapezoidal) with physical-time outer loop
void transient_march(RankMesh& rm, const CaseConfig& cfg, const GasModel& gas);
