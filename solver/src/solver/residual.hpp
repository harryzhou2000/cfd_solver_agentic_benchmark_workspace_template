#pragma once

// Residual assembly: sums all face fluxes (inviscid + viscous) into the
// per-cell residual. First-order — face states are the cell-center values
// (no reconstruction; that is the next phase).

#include <mpi.h>

#include <vector>

#include "config/case_config.hpp"
#include "partition/partition.hpp"
#include "physics/gas_model.hpp"

namespace cfd {

// Local + globally reduced residual norms. l2_per_var / linf_per_var hold
// the per-conservative-variable values; l2 / linf are the aggregate over all
// components. L2 = sqrt(sum |R|^2 / n_owned_global), Linf = max |R| over all
// owned cells and components (MPI-reduced).
struct ResidualNorm {
  double l2 = 0.0;
  double linf = 0.0;
  double l2_per_var[NVARS] = {0.0, 0.0, 0.0, 0.0};
  double linf_per_var[NVARS] = {0.0, 0.0, 0.0, 0.0};
};

// Assembles the residual for all OWNED cells of this rank:
//   interior faces: flux to left (+), right (-)
//   boundary faces: BC flux to the owning cell
//   send faces: flux to the owned (left) cell only
//   recv faces: flux to the owned (right) cell only
// Viscous fluxes are included for interior/send/recv faces when the case is
// laminar and mu > 0; in the second-order path the viscous flux uses the
// face-averaged primitive gradients (viscous_flux_gradient) instead of the
// normal-difference approximation. Ghost cell residuals are not accumulated.
//
// When `gradients` is non-null, the face states are reconstructed
// second-order: each side is extrapolated to the face center with its
// (already limited) primitive gradient via reconstruct_face, and boundary
// fluxes use the reconstructed interior state. Otherwise the first-order
// cell-center states are used. gradients has NVARS*2 entries per LOCAL cell
// (ghost entries must be up to date) and is left untouched.
//
// U: flat conservative state (NVARS * local cells), residual: output, same
// size (zeroed). This variant performs NO MPI reductions; call
// compute_norms separately when a norm is needed.
void compute_residual(const std::vector<double>& U,
                      const DistributedMesh& dmesh, const CaseConfig& cfg,
                      double mu, std::vector<double>& residual,
                      const std::vector<double>* gradients = nullptr);

// Computes the L2/Linf norms of an arbitrary residual-like array over the
// OWNED cells (MPI-reduced). Used e.g. for the BDF2 total residual
// (spatial + physical-time terms).
ResidualNorm compute_norms(const std::vector<double>& R,
                           const DistributedMesh& dmesh, MPI_Comm comm);

}  // namespace cfd
