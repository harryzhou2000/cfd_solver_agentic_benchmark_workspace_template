#pragma once
#include "common/types.hpp"
#include "common/gas_model.hpp"

// Viscous flux contribution at a face
StateVector viscous_flux(const PrimVector& state, const Grad2& grad_u,
                         const Grad2& grad_v, const Grad2& grad_T,
                         const Vec2& normal, const GasModel& gas,
                         Real viscosity);
