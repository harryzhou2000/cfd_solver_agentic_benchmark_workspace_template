#pragma once
#include "common/types.hpp"
#include "mesh/mesh.hpp"
#include "common/gas_model.hpp"

// Set up solver data structures and initial conditions
void solver_setup(RankMesh& rm, const CaseConfig& cfg, const GasModel& gas);
