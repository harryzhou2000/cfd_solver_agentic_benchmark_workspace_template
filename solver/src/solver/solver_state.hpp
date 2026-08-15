#pragma once

#include "types.hpp"
#include "mesh/mesh_types.hpp"
#include "partition/partition_types.hpp"
#include <vector>

namespace cfd {

// --- Solver runtime state ---

struct CellState {
    Conserved U;         // conservative variables
    Conserved U_prev;    // previous time step (for BDF2)
    Conserved U_prev2;   // two steps ago (for BDF2)
    Conserved residual;  // current residual R(U)
    Vec2 centroid;       // cell centroid (from mesh)
    Real volume{0};      // cell volume (from mesh)
};

struct FaceFlux {
    Conserved flux;      // numerical flux through the face
    Real max_wave_speed{0}; // for local time step
};

struct SolverConfig {
    Real cfl{1.0};
    Real cfl_max{100.0};
    Int cfl_ramp_steps{0};
    Int current_step{0};
    Real current_time{0};

    // Inner iteration parameters
    Int min_inner_iterations{3};
    Int max_inner_iterations{50};
    Real inner_residual_target{0.01};

    // Convergence
    Real residual_initial{0};
    Real residual_current{0};
    Int max_steps{0};
    Real residual_reduction_target{4.0}; // orders of magnitude
    bool converged{false};
};

// Solver stores per-rank cell states and face data
struct SolverState {
    // Geometry
    std::vector<Vec2> cell_centroids;
    std::vector<Real> cell_volumes;
    std::vector<Vec2> face_centroids;
    std::vector<FaceNormal> face_normals;

    // Solution
    std::vector<Conserved> U;       // n_local cells (owned + ghost)
    std::vector<Conserved> U_n;     // previous physical time state
    std::vector<Conserved> U_nm1;   // two steps ago (BDF2)

    // Residual
    std::vector<Conserved> residual;

    // Spectral radii for local time stepping
    std::vector<Real> spec_rad_conv;  // convective spectral radius per cell
    std::vector<Real> spec_rad_visc;  // viscous spectral radius per cell

    // Boundary face info (local indexing)
    struct BFaceInfo {
        Int local_face_id;
        BcType bc_type;
        Int patch_idx;
    };
    std::vector<BFaceInfo> boundary_faces;

    // Inner iteration statistics
    Int total_inner_iterations{0};
    Int inner_target_misses{0};
    Int inner_steps_completed{0};
    Real last_inner_residual_ratio{1.0};
    Int observed_min_inner{0};
    Int observed_max_inner{0};
};

} // namespace cfd
