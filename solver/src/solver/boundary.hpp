#pragma once
#include "common/types.hpp"
#include "mesh/mesh.hpp"
#include "common/gas_model.hpp"

// Apply boundary conditions: modify left/right states on boundary faces
void apply_boundary_conditions(const CaseConfig& cfg,
                               std::vector<PrimVector>& left_prim,
                               std::vector<PrimVector>& right_prim,
                               const std::vector<StateVector>& cell_state,
                               const RankMesh& rm,
                               const GasModel& gas);

// Boundary state helpers - used directly in residual assembly
PrimVector farfield_state(const Vec2& normal, const PrimVector& interior,
                          const GasModel& gas, Real mach_inf, Real rho_inf, 
                          Real u_inf, Real v_inf, Real p_inf);

PrimVector slip_wall_state(const Vec2& normal, const PrimVector& interior,
                           const GasModel& gas);

PrimVector no_slip_adiabatic_state(const Vec2& normal, const PrimVector& interior,
                                    const GasModel& gas);
