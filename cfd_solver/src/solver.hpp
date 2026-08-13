#pragma once
#include "types.hpp"
#include "mesh.hpp"
#include "io.hpp"
#include <mpi.h>
#include <vector>
#include <array>

namespace cfd {

class Solver {
public:
    Solver(const CaseInput& ci, int mpi_rank, int mpi_size);
    
    void init(const std::string& mesh_file);
    void run(const std::string& output_dir);
    
    // Access
    const CaseInput& case_input() const { return ci_; }
    const LocalMesh& local_mesh() const { return local_mesh_; }
    
    // MPI
    int rank() const { return rank_; }
    int size() const { return size_; }
    
    // State and derived quantities
    const std::vector<StateVec>& U() const { return U_; }
    real_t gamma() const { return ci_.gamma; }
    real_t R_gas() const { return ci_.R; }
    real_t prandtl() const { return ci_.prandtl; }
    
    // BC access
    const std::map<idx_t, BCType>& bc_type_map() const { return bc_type_map_; }
    
    // Physics helpers
    real_t pressure(const StateVec& U) const;
    real_t temperature(const StateVec& U) const;
    real_t speed_of_sound(const StateVec& U) const;
    real_t Mach(const StateVec& U) const;
    StateVec primitive_to_conservative(real_t rho, real_t u, real_t v, real_t p) const;
    
    // Force computation
    void compute_forces(real_t& cl, real_t& cd, real_t& cmz,
                        real_t& pressure_drag, real_t& viscous_drag,
                        real_t& pressure_lift, real_t& viscous_lift);
    
    // Residual
    void compute_residual(const std::vector<StateVec>& U, std::vector<StateVec>& R,
                          real_t& res_l2, real_t& res_linf);
    
    // State sync
    void sync_ghost_states(std::vector<StateVec>& U);

private:
    CaseInput ci_;
    int rank_, size_;
    Mesh global_mesh_;
    PartitionInfo partition_;
    LocalMesh local_mesh_;
    std::vector<StateVec> U_;        // conservative state (owned + ghost)
    std::vector<real_t> dt_local_;   // local time step
    
    // BC tag mapping
    std::map<idx_t, BCType> bc_type_map_;  // tag -> type
};

// Flux functions
StateVec inviscid_flux_x(const StateVec& U, real_t gamma);
StateVec inviscid_flux_y(const StateVec& U, real_t gamma);
StateVec roe_flux(const StateVec& UL, const StateVec& UR,
                  const Vec2& normal, real_t gamma, real_t entropy_fix = 0.1);
StateVec rusanov_flux(const StateVec& UL, const StateVec& UR,
                      const Vec2& normal, real_t gamma, real_t diss_scale = 1.0);
StateVec viscous_flux(const StateVec& U, const Vec2& grad_rho,
                      const Vec2& grad_rhou, const Vec2& grad_rhov,
                      const Vec2& grad_rhoE, const Vec2& normal,
                      real_t gamma, real_t R, real_t prandtl, real_t mu);

} // namespace cfd
