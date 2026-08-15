#pragma once

#include <string>

namespace cfd {

class Solver;

// Write every final output artifact required by OUTPUT_CONTRACT.md into
// out_dir: metadata.json, run_status.json, partition_diagnostics.csv,
// residuals.csv, forces.csv, surface.csv, field_final.vtu, restart_final.bin
// (+ restart_final.json).  Called by every rank.
void write_final_outputs(const Solver& s, const std::string& out_dir,
                         double wall_time_seconds, const std::string& command_line);

// Write a field file (VTU XML, one piece per rank).  `filename` is relative
// to the solver output directory.
void write_field_file(const Solver& s, const std::string& filename, double t, int step);

// Write a binary restart checkpoint (all ranks participate; written by
// rank 0).  Contains the full conservative state plus (for transient runs)
// the BDF2 histories.
void write_checkpoint(const Solver& s, const std::string& case_id, int step, double time);

// Load a restart checkpoint into the solver state.  Returns false with an
// error message if the file is missing or incompatible.
bool read_restart(Solver& s, const std::string& path, std::string& err);

}  // namespace cfd
