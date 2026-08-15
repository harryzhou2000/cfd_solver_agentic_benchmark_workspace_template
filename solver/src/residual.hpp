#pragma once

#include <map>
#include <mpi.h>
#include <string>
#include <vector>

#include "config.hpp"
#include "gas.hpp"
#include "partition.hpp"
#include "reconstruction.hpp"
#include "types.hpp"

namespace cfd {

// Diagnostics extracted from a residual evaluation (global across ranks).
struct ResidualStats {
    double l2 = 0.0;    // sqrt( sum |R|^2 / (4 * n_cells_global) )
    double linf = 0.0;  // max component magnitude over owned cells
    double rho = 0.0;   // per-component L2 norms: sqrt( sum R_c^2 / n_cells )
    double rhou = 0.0;
    double rhov = 0.0;
    double rhoE = 0.0;
    double max_residual = 0.0;   // max |R| component over owned cells
    double max_residual_x = 0.0; // location of that cell
    double max_residual_y = 0.0;
};

// Finite-volume residual assembly: loops over internal and boundary faces,
// accumulates the numerical flux contributions into each owned cell's
// residual vector R_i = sum_faces F_n(U_i, U_j) * A_face.
//
//   mesh       : full mesh with face/boundary geometry
//   local_mesh : rank-local owned/ghost cell lists and global->local map
//   U_local    : conservative states for owned + ghost cells (ghosts must be
//                halo-exchanged by the caller before calling)
//   R_local    : residual for owned cells (resized to n_owned)
//   bc_map     : boundary family name -> BC type string (used as a fallback
//                when mesh.bface_bc_type holds BCType::Unknown)
//   viscosity  : 0 for inviscid runs; mu for laminar runs
//   grad_local : (optional) limited cell gradients for all local cells
//                (owned + exchanged ghosts); when provided, the face fluxes
//                use second-order reconstructed face states and the viscous
//                terms use face-averaged gradients. Null/empty selects the
//                first-order scheme.
//   prim_local : (optional) primitive states for all local cells; derived
//                from U_local when null
//
// Returns the global L2 residual norm (see ResidualStats).
double compute_residual(const Mesh& mesh, const LocalMesh& local_mesh,
                        const std::vector<Vector4>& U_local,
                        std::vector<Vector4>& R_local, const GasParams& gas,
                        const FreestreamParams& freestream,
                        const std::map<std::string, std::string>& bc_map,
                        double viscosity, double dissipation_scale,
                        MPI_Comm comm, ResidualStats* stats = nullptr,
                        const std::vector<Gradients>* grad_local = nullptr,
                        const std::vector<PrimitiveState>* prim_local =
                            nullptr);

// Local-only L2 norm helper over a per-owned-cell residual vector
// (no MPI reduction; used by unit tests).
double residual_l2_norm(const std::vector<Vector4>& residual,
                        cgsize_t n_cells_local);

}  // namespace cfd
