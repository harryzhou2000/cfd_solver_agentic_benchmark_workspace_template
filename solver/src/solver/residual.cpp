#include "residual.hpp"
#include "solver/inviscid_flux.hpp"
#include "solver/boundary.hpp"
#include <cmath>
#include <iostream>

// Compute full residual on the RankMesh using first-order FV
void compute_residual(RankMesh& rm, const GasModel& gas, const CaseConfig& cfg) {
    int n_owned = (int)rm.owned_state.size();
    int n_ghost = (int)rm.ghost_state.size();
    
    // Zero residuals
    for (int i = 0; i < n_owned; i++) {
        rm.owned_residual[i] = StateVector::Zero();
    }
    
    const auto& faces = rm.internal_faces;
    
    // Debug: count boundary faces by type
    static bool first_call = true;
    if (first_call) {
        int farfield_count = 0, wall_count = 0, undef_count = 0, internal_count = 0;
        for (size_t fi = 0; fi < faces.size(); fi++) {
            if (faces[fi].right < 0 && faces[fi].left >= 0) {
                if (faces[fi].bc_type == BCType::Farfield) farfield_count++;
                else if (faces[fi].bc_type == BCType::SlipWall) wall_count++;
                else undef_count++;
            } else {
                internal_count++;
            }
        }
        std::cout << "  Face BC breakdown: internal=" << internal_count
                  << " farfield=" << farfield_count << " wall=" << wall_count
                  << " undef=" << undef_count << std::endl;
        
        // Print cell volume stats
        Real vmin = 1e30, vmax = 0, vsum = 0;
        for (int i = 0; i < n_owned; i++) {
            Real v = rm.owned_cells[i].volume;
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
            vsum += v;
        }
        std::cout << "  Cell volume: min=" << vmin << " max=" << vmax << " avg=" << vsum/n_owned << std::endl;
        first_call = false;
    }
    
    for (size_t fi = 0; fi < faces.size(); fi++) {
        const Face& face = faces[fi];
        
        // Determine left and right states
        PrimVector left_W, right_W;
        
        if (face.left >= 0 && face.left < n_owned) {
            left_W = gas.conservative_to_primitive(rm.owned_state[face.left]);
        } else if (face.left < 0) {
            Index gi = -face.left - 1;
            if (gi < n_ghost) left_W = gas.conservative_to_primitive(rm.ghost_state[gi]);
            else continue;
        }
        
        if (face.right >= 0 && face.right < n_owned) {
            right_W = gas.conservative_to_primitive(rm.owned_state[face.right]);
        } else if (face.right < 0) {
            Index gi = -face.right - 1;
            if (gi < n_ghost) right_W = gas.conservative_to_primitive(rm.ghost_state[gi]);
            else {
                // Boundary face: apply BC based on face.bc_type
                if (face.bc_type == BCType::Farfield) {
                    right_W = farfield_state(face.normal, left_W, gas, cfg.mach, cfg.rho_inf, cfg.u_inf, cfg.v_inf, cfg.p_inf);
                } else if (face.bc_type == BCType::SlipWall) {
                    right_W = slip_wall_state(face.normal, left_W, gas);
                } else if (face.bc_type == BCType::NoSlipAdiabaticWall) {
                    right_W = no_slip_adiabatic_state(face.normal, left_W, gas);
                } else {
                    right_W = left_W; // Undefined BC: copy interior state
                }
            }
        }
        
        // Compute flux based on face type
        StateVector flux;
        if (face.right < 0 && face.bc_type == BCType::SlipWall) {
            // For inviscid slip wall: direct pressure flux
            // F = [0, p*nx, p*ny, 0]^T  (zero mass/energy flux, pressure force only)
            Real p_wall = left_W[3];  // interior pressure
            flux(0) = 0.0;
            flux(1) = p_wall * face.normal.x();
            flux(2) = p_wall * face.normal.y();
            flux(3) = 0.0;
        } else if (face.right < 0 && face.bc_type == BCType::NoSlipAdiabaticWall) {
            // For viscous no-slip: set zero velocity at wall ghost
            right_W = no_slip_adiabatic_state(face.normal, left_W, gas);
            Real diss_scale = cfg.rusanov_dissipation_scale;
            flux = rusanov_flux(left_W, right_W, face.normal, gas, diss_scale);
        } else {
            // Internal or farfield face: standard flow flux
            Real diss_scale = cfg.rusanov_dissipation_scale;
            flux = rusanov_flux(left_W, right_W, face.normal, gas, diss_scale);
        }
        
        // Accumulate: dU/dt = -1/V * sum(flux*area), so residual += flux*area
        if (face.left >= 0 && face.left < n_owned) {
            rm.owned_residual[face.left] += flux * face.area;
        }
        
        if (face.right >= 0 && face.right < n_owned) {
            // For right cell, flux through face in opposite direction
            rm.owned_residual[face.right] -= flux * face.area;
        }
    }
}

void compute_residual(const RankMesh& rm, const std::vector<StateVector>& state,
                      std::vector<StateVector>& residual,
                      const GasModel& gas) {
    (void)rm; (void)state; (void)residual; (void)gas;
}
