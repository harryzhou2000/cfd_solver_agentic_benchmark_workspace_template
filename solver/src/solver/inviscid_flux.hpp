#pragma once
#include "common/types.hpp"
#include "common/gas_model.hpp"

// Rusanov (LLF) Riemann solver flux
StateVector rusanov_flux(const PrimVector& left, const PrimVector& right,
                         const Vec2& normal, const GasModel& gas,
                         Real dissipation_scale = 1.0);

// Roe flux with entropy fix
StateVector roe_flux(const PrimVector& left, const PrimVector& right,
                     const Vec2& normal, const GasModel& gas);
