#pragma once
#include "partition_types.hpp"
#include "case_config.hpp"
#include "mesh_types.hpp"
#include <vector>
#include <string>

namespace solver {

struct ResidualNorms {
    double l2 = 0.0;
    double linf = 0.0;
};

struct ForceCoeffs {
    double cl = 0.0;
    double cd = 0.0;
    double cmz = 0.0;
    double pressure_drag = 0.0;
    double viscous_drag = 0.0;
    double pressure_lift = 0.0;
    double viscous_lift = 0.0;
};

// Compute flow residual on the distributed mesh
// state: flat array [num_owned*4 + num_ghost*4] interleaved
// residual: output, [num_owned*4]
// inviscid_flux_type: "rusanov" or "roe"
ResidualNorms compute_residual(const DistributedMesh& mesh,
                                std::vector<double>& state,
                                std::vector<double>& residual,
                                const CaseConfig& config,
                                const std::string& inviscid_flux_type,
                                double dissipation_scale = 1.0);

// Compute aerodynamic forces on wall boundaries
ForceCoeffs compute_forces(const DistributedMesh& mesh,
                            const std::vector<double>& state,
                            const CaseConfig& config);

} // namespace solver
