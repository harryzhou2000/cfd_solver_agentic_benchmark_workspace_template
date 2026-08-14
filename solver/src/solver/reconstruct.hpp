#pragma once
#include "common/types.hpp"
#include "common/gas_model.hpp"

// Piecewise-linear reconstruction to obtain left/right face states
void reconstruct_faces(const std::vector<StateVector>& cell_state,
                       const std::vector<Vec2>& grad_rho,
                       const std::vector<Grad2>& grad_u,
                       const std::vector<Grad2>& grad_v,
                       const std::vector<Grad2>& grad_T,
                       const GasModel& gas,
                       std::vector<PrimVector>& left_prim,
                       std::vector<PrimVector>& right_prim);
