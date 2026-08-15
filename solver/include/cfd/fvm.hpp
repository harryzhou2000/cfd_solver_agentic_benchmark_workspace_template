#pragma once

#include <array>
#include <vector>

#include "cfd/case_config.hpp"
#include "cfd/mesh.hpp"
#include "cfd/partition.hpp"
#include "cfd/state.hpp"

namespace cfd {

struct Grad2 {
  Real x{0.0};
  Real y{0.0};
};

using PrimitiveGrad = std::array<Grad2, 4>;

struct ResidualResult {
  std::vector<Conserved> residual;
  std::vector<Real> spectral_radius;
  Real l2{0.0};
  Real linf{0.0};
  Real rho_l2{0.0};
  Real rhou_l2{0.0};
  Real rhov_l2{0.0};
  Real rhoE_l2{0.0};
};

struct ForceResult {
  Real cl{0.0};
  Real cd{0.0};
  Real cmz{0.0};
  Real pressure_drag{0.0};
  Real viscous_drag{0.0};
  Real pressure_lift{0.0};
  Real viscous_lift{0.0};
};

std::vector<Conserved> initialize_state(const LocalMesh& mesh, const CaseConfig& cfg, int rank);
void exchange_halos(std::vector<Conserved>& state, const HaloPlan& halo, MPI_Comm comm);
ResidualResult compute_residual(const LocalMesh& mesh, const CaseConfig& cfg,
                                const std::vector<Conserved>& state, MPI_Comm comm,
                                bool second_order);
Conserved compute_cell_residual_first_order(const LocalMesh& mesh, const CaseConfig& cfg,
                                            const std::vector<Conserved>& state, int cell);
Conserved compute_cell_residual_first_order_with_state(const LocalMesh& mesh, const CaseConfig& cfg,
                                                       const std::vector<Conserved>& state,
                                                       int cell,
                                                       const Conserved& cell_state);
ForceResult compute_forces(const LocalMesh& mesh, const CaseConfig& cfg,
                           const std::vector<Conserved>& state, MPI_Comm comm);
std::vector<std::string> make_surface_rows(const LocalMesh& mesh, const CaseConfig& cfg,
                                           const std::vector<Conserved>& state);

}  // namespace cfd
