#pragma once

#include "mesh/mesh_types.hpp"
#include "partition/partition_types.hpp"
#include <string>

namespace cfd {

/// Orchestrate the full solve: build rank-local solver arrays from the
/// partition, run the implicit steady (pseudo-time) loop or the BDF2
/// transient (dual-time) loop, and write the output pipeline
/// (residuals.csv, forces.csv, surface.csv, field_final.vtu,
/// metadata.json, run_status.json, restart_final.bin).
///
/// All output files are written by rank 0. The function exits cleanly
/// (returns) when the run converges or max_steps is reached.
///
/// @param mesh           Global serial mesh (geometry + connectivity reference)
/// @param rp             Rank-local partition (owned/ghost/faces/neighbors)
/// @param config         Case configuration (freestream, run control, outputs)
/// @param output_dir     Directory for all output files (created if missing)
/// @param partition_edge_cut  METIS edge cut reported in metadata.json
void run_solver(const Mesh& mesh, const RankPartition& rp,
                const CaseConfig& config, const std::string& output_dir,
                Int partition_edge_cut = 0);

} // namespace cfd
