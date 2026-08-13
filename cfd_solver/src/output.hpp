#pragma once
#include "solver.hpp"
#include <string>

namespace cfd {

void write_metadata(const Solver& solver, const std::string& output_dir,
                    idx_t steps_run, real_t final_res, bool converged,
                    const std::string& time_integrator = "pseudo_steady");

void write_run_status(const Solver& solver, const std::string& output_dir,
                      idx_t steps_run, real_t final_phys_time,
                      const std::string& status, real_t res_reduction,
                      real_t wall_time, const std::string& notes = "");

void write_surface(const Solver& solver, const std::string& output_dir);

void write_field_vtu(const Solver& solver, const std::string& output_dir);

void write_partition_diagnostics(const Solver& solver, const std::string& output_dir);

void write_restart(const Solver& solver, const std::string& output_dir);

void write_transient_metadata(const Solver& solver, const std::string& output_dir,
                              const struct TransientStats& stats, real_t wall_time);

} // namespace cfd
