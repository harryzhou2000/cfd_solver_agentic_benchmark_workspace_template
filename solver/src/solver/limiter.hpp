#pragma once
#include "common/types.hpp"
#include "common/gas_model.hpp"

// Barth-Jespersen slope limiter
void apply_limiter(std::vector<Vec2>& grad_rho,
                   std::vector<Grad2>& grad_u,
                   std::vector<Grad2>& grad_v,
                   std::vector<Grad2>& grad_T,
                   const std::vector<StateVector>& state,
                   const GasModel& gas);
