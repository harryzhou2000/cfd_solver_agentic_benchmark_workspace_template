#pragma once
#include "MeshData.hpp"
#include "Config.hpp"
#include <mpi.h>
#include <vector>

// LU-SGS implicit solver.
// mach_ref: if > 0, use preconditioned (Turkel-style) off-diagonal wave speed
//           to match the preconditioned spectral_radii in the diagonal, so
//           diagonal dominance is preserved for low-Mach viscous flows.
//           Pass cfg.freestream.mach when M<0.3 and mu>0, else 0.
void lusgsSolve(const LocalMesh& lm,
                const CaseConfig& cfg,
                const std::vector<StateVec>& residuals,
                const std::vector<double>& spectral_radii,
                const std::vector<StateVec>& states,
                const std::vector<double>& dt_local,
                double gamma,
                double mu,
                double mach_ref,
                MPI_Comm comm,
                std::vector<StateVec>& dU);

