// cfd_solver entry point.
//
// Phase 5: second-order conservative finite-volume solver core with
// matrix-free LU-SGS implicit pseudo-time marching (steady) and BDF2
// physical-time marching with inner point-implicit iterations (transient).
//   cfd_solver solve --case <case.json> --output <dir> [--restart] [--report-level N]
//
// Every rank loads the full case and global mesh (serial preprocessing),
// partitions the mesh with METIS (METIS_PartGraphKway), builds its rank-local
// mesh (owned + ghost cells, faces, halo exchange pattern), validates the
// halo exchange, and then runs the second-order solver (least-squares
// gradients + Barth-Jespersen limiter, Rusanov flux, characteristic
// farfield, slip/no-slip walls, laminar viscous fluxes, matrix-free LU-SGS
// implicit marching) in steady or transient mode, writing the full
// OUTPUT_CONTRACT.md output set to the output directory.

#include <mpi.h>

#include <argparse/argparse.hpp>

#include <algorithm>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "case_reader.h"
#include "halo.h"
#include "mesh.h"
#include "output.h"
#include "partition.h"
#include "physics.h"
#include "solver.h"

namespace {

// Space-separated list helper for the diagnostics CSV.
template <typename T>
std::string join(const std::vector<T>& v) {
  std::string out;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i > 0) {
      out += ' ';
    }
    out += std::to_string(v[i]);
  }
  return out;
}

// Print this rank's partition statistics (called on every rank).
void print_partition_stats(const cfd::LocalMesh& lm) {
  std::printf("[rank %d/%d] partition: owned cells = %d, ghost cells = %d, "
              "boundary faces = %d, neighbor ranks = %zu (",
              lm.rank, lm.nranks, lm.n_owned, lm.n_ghost,
              lm.n_boundary_faces, lm.neighbor_ranks.size());
  for (size_t i = 0; i < lm.neighbor_ranks.size(); ++i) {
    std::printf("%s%d", i > 0 ? " " : "", lm.neighbor_ranks[i]);
  }
  std::printf("), sends = [");
  for (size_t i = 0; i < lm.halo_exchanges.size(); ++i) {
    std::printf("%s%zu", i > 0 ? " " : "",
                lm.halo_exchanges[i].send_cells.size());
  }
  std::printf("], recvs = [");
  for (size_t i = 0; i < lm.halo_exchanges.size(); ++i) {
    std::printf("%s%zu", i > 0 ? " " : "",
                lm.halo_exchanges[i].recv_cells.size());
  }
  std::printf("]\n");
}

// Write the partition diagnostics CSV: rank 0 gathers one row per rank and
// writes the file once (header + all rows in a single pass). Non-zero ranks
// never open the file, so there are no concurrent appends to a shared file.
// The gather uses point-to-point messages (no collectives) and is therefore
// immune to ordering constraints; I/O failures on rank 0 are reported and
// skipped rather than thrown, so a filesystem problem cannot abort the run.
void write_partition_diagnostics(const cfd::LocalMesh& lm,
                                 const std::string& out_dir) {
  constexpr int kTag = 4243;  // distinct from the halo exchange tag

  // 1. Build this rank's CSV row.
  std::vector<int> send_counts;
  std::vector<int> recv_counts;
  send_counts.reserve(lm.halo_exchanges.size());
  recv_counts.reserve(lm.halo_exchanges.size());
  for (const auto& ex : lm.halo_exchanges) {
    send_counts.push_back(static_cast<int>(ex.send_cells.size()));
    recv_counts.push_back(static_cast<int>(ex.recv_cells.size()));
  }
  std::string row = std::to_string(lm.rank) + ',' +
                    std::to_string(lm.n_owned) + ',' +
                    std::to_string(lm.n_ghost) + ',' +
                    std::to_string(lm.n_boundary_faces) + ',' +
                    std::to_string(lm.neighbor_ranks.size()) + ',' +
                    join(lm.neighbor_ranks) + ',' + join(send_counts) + ',' +
                    join(recv_counts);

  // 2. Gather all rows on rank 0 (rank 0 keeps its own row).
  std::vector<std::string> rows;
  if (lm.rank == 0) {
    rows.resize(static_cast<size_t>(lm.nranks));
    rows[0] = row;
    for (int r = 1; r < lm.nranks; ++r) {
      int len = 0;
      MPI_Recv(&len, 1, MPI_INT, r, kTag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      rows[static_cast<size_t>(r)].resize(static_cast<size_t>(len));
      if (len > 0) {
        MPI_Recv(rows[static_cast<size_t>(r)].data(), len, MPI_CHAR, r, kTag,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      }
    }
  } else {
    const int len = static_cast<int>(row.size());
    MPI_Send(&len, 1, MPI_INT, 0, kTag, MPI_COMM_WORLD);
    if (len > 0) {
      MPI_Send(row.data(), len, MPI_CHAR, 0, kTag, MPI_COMM_WORLD);
    }
  }

  // 3. Rank 0 creates the output directory and writes header + rows once.
  if (lm.rank == 0) {
    try {
      std::filesystem::create_directories(out_dir);
      const std::string path =
          (std::filesystem::path(out_dir) / "partition_diagnostics.csv")
              .string();
      std::ofstream out(path, std::ios::trunc);
      if (!out) {
        std::fprintf(stderr, "[rank %d] warning: cannot create %s\n",
                     lm.rank, path.c_str());
        return;
      }
      out << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,"
             "num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
      for (const std::string& r : rows) {
        out << r << '\n';
      }
    } catch (const std::exception& e) {
      std::fprintf(stderr, "[rank %d] warning: diagnostics write failed: %s\n",
                   lm.rank, e.what());
    }
  }
}

// Verify the per-cell face CSR invariants (offsets structure, data range,
// counts vs. an independent recount from the face list, and completeness:
// every owned cell must see exactly one CSR entry per edge, == cell.n_nodes).
// Returns the number of errors found (0 == consistent).
int verify_cell_face_csr(const cfd::LocalMesh& lm) {
  int errors = 0;
  const int n = lm.n_owned;
  const size_t n_faces = lm.faces.size();

  if (lm.cell_faces_offsets.size() != static_cast<size_t>(n) + 1) {
    std::fprintf(stderr,
                 "[rank %d] CSR error: offsets size %zu != n_owned+1 (%d)\n",
                 lm.rank, lm.cell_faces_offsets.size(), n + 1);
    ++errors;
    return errors;
  }
  if (lm.cell_faces_offsets[0] != 0) {
    std::fprintf(stderr, "[rank %d] CSR error: offsets[0] = %d != 0\n",
                 lm.rank, lm.cell_faces_offsets[0]);
    ++errors;
  }
  if (lm.cell_faces_data.size() !=
      static_cast<size_t>(lm.cell_faces_offsets.back())) {
    std::fprintf(stderr,
                 "[rank %d] CSR error: data size %zu != offsets.back() %d\n",
                 lm.rank, lm.cell_faces_data.size(),
                 lm.cell_faces_offsets.back());
    ++errors;
  }

  // Independent recount of faces touching each owned cell.
  std::vector<int> expected(n, 0);
  for (const cfd::LocalMesh::LocalFace& f : lm.faces) {
    if (f.left >= 0 && f.left < n) {
      ++expected[f.left];
    }
    if (f.right >= 0 && f.right < n) {
      ++expected[f.right];
    }
  }
  for (int i = 0; i < n; ++i) {
    const int csr_count =
        lm.cell_faces_offsets[i + 1] - lm.cell_faces_offsets[i];
    if (csr_count != expected[i]) {
      std::fprintf(stderr,
                   "[rank %d] CSR error: cell %d CSR count %d != recount %d\n",
                   lm.rank, i, csr_count, expected[i]);
      ++errors;
    }
    if (csr_count != lm.cells[i].n_nodes) {
      std::fprintf(stderr,
                   "[rank %d] CSR error: cell %d has %d faces in CSR but "
                   "%d edges\n",
                   lm.rank, i, csr_count, lm.cells[i].n_nodes);
      ++errors;
    }
    for (int k = lm.cell_faces_offsets[i]; k < lm.cell_faces_offsets[i + 1];
         ++k) {
      const int fi = lm.cell_faces_data[k];
      if (fi < 0 || static_cast<size_t>(fi) >= n_faces) {
        std::fprintf(stderr,
                     "[rank %d] CSR error: cell %d references face %d "
                     "out of range\n",
                     lm.rank, i, fi);
        ++errors;
      }
    }
  }
  return errors;
}

// Halo exchange self-test: tag every local state with the owning rank number,
// exchange, and verify each ghost cell carries the value of its owning rank.
// `scratch` must already be init()'d against lm (pre-allocated once in main).
// Returns the number of mismatched ghost cells on this rank (0 == pass).
int halo_exchange_test(const cfd::LocalMesh& lm,
                       const cfd::PartitionResult& part,
                       cfd::HaloScratch& scratch) {  std::vector<cfd::ConsState> U(lm.cells.size());
  for (cfd::ConsState& u : U) {
    u.rho = static_cast<double>(lm.rank);
  }

  cfd::exchange_halo_data(U, lm, scratch);

  int errors = 0;
  for (int i = lm.n_owned; i < static_cast<int>(lm.cells.size()); ++i) {
    const int owner = part.cell_part[lm.local_to_global[i]];
    const double expected = static_cast<double>(owner);
    if (U[i].rho != expected) {
      ++errors;
      if (errors <= 5) {
        std::fprintf(stderr,
                     "[rank %d] halo mismatch: ghost cell %d (global %d) "
                     "expected rho = %.0f (owner rank %d), got %.0f\n",
                     lm.rank, i, lm.local_to_global[i], expected, owner,
                     U[i].rho);
      }
    }
    // Momentum/energy must be untouched by the exchange.
    if (U[i].rhou != 0.0 || U[i].rhov != 0.0 || U[i].rhoE != 0.0) {
      ++errors;
    }
  }
  return errors;
}

}  // namespace

int main(int argc, char* argv[]) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  int nranks = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);

  // Wall-clock and epoch time at the start of the run (for run_status.json
  // and metadata.json).
  const double wall_start = MPI_Wtime();
  const double epoch_start = static_cast<double>(std::time(nullptr));

  argparse::ArgumentParser program("cfd_solver", "0.2.0");
  program.add_description(
      "2D unstructured finite-volume CFD solver "
      "(Phase 5: steady LU-SGS + transient BDF2, full output contract)");

  argparse::ArgumentParser solve_cmd("solve");
  solve_cmd.add_description(
      "Load a case and its mesh, partition with METIS, build the rank-local "
      "mesh, and run the second-order finite-volume solver (steady or "
      "transient per run_control.type).");
  solve_cmd.add_argument("--case")
      .required()
      .help("Path to the case JSON file");
  solve_cmd.add_argument("--output")
      .required()
      .help("Directory where solver output will be written");
  solve_cmd.add_argument("--restart")
      .default_value(std::string(""))
      .help("Path to a restart file (restart_final.bin format) to use as "
            "the initial condition; empty = start from freestream");
  solve_cmd.add_argument("--report-level")
      .default_value(std::string("brief"))
      .help("Reporting detail level: 'brief' or 'full' (accepted for CLI "
            "compatibility; not yet acted on)");
  solve_cmd.add_argument("--max-steps")
      .scan<'i', int>()
      .default_value(-1)
      .help("Override the case's max_steps (<= 0 uses the case value)");
  program.add_subparser(solve_cmd);

  // Argument parsing happens after MPI_Init so MPI-consumed argv entries
  // (e.g. Open MPI's hidden args) are already stripped.
  try {
    program.parse_args(argc, argv);
  } catch (const std::exception& e) {
    if (rank == 0) {
      std::cerr << "cfd_solver: argument error: " << e.what() << "\n\n";
      std::cerr << program << "\n";
    }
    MPI_Finalize();
    return 1;
  }

  if (!program.is_subcommand_used("solve")) {
    if (rank == 0) {
      std::cerr << "cfd_solver: no command given (expected: solve)\n\n";
      std::cerr << program << "\n";
    }
    MPI_Finalize();
    return 1;
  }

  const auto& cmd = program.at<argparse::ArgumentParser>("solve");
  const std::string case_path = cmd.get<std::string>("--case");
  const std::string out_dir = cmd.get<std::string>("--output");
  const int cli_max_steps = cmd.get<int>("--max-steps");
  // CLI contract: --restart <file> and --report-level brief|full must parse
  // (the report level is accepted for compatibility and not yet acted on).
  const std::string restart_path = cmd.get<std::string>("--restart");
  const std::string report_level = cmd.get<std::string>("--report-level");
  if (report_level != "brief" && report_level != "full" && rank == 0) {
    std::cerr << "cfd_solver: warning: unknown --report-level '" << report_level
              << "' (expected 'brief' or 'full'); using 'brief'\n";
  }

  // stdout.log: every rank redirects its stdout into the output directory's
  // stdout.log (the directory is created first; create_directories is
  // idempotent and safe to call on every rank). stderr stays on the console.
  try {
    std::filesystem::create_directories(out_dir);
  } catch (const std::exception& e) {
    if (rank == 0) {
      std::cerr << "cfd_solver: warning: cannot create output directory "
                << out_dir << ": " << e.what() << "\n";
    }
  }
  {
    const std::string log_path =
        (std::filesystem::path(out_dir) / "stdout.log").string();
    if (std::freopen(log_path.c_str(), "w", stdout) == nullptr) {
      std::fprintf(stderr, "[rank %d] warning: cannot redirect stdout to %s\n",
                   rank, log_path.c_str());
    }
  }

  int exit_code = 0;
  try {
    // ---- Case and mesh (every rank reads the full serial mesh) ----
    const cfd::RunConfig config = cfd::load_case(case_path);
    if (rank == 0) {
      cfd::print_case_summary(config);
    }

    std::map<std::string, cfd::BCType> bc_map;
    for (const auto& [family, bc] : config.boundary_conditions) {
      bc_map[family] = cfd::bc_type_from_string(bc);
    }
    const cfd::Mesh mesh = cfd::read_mesh(config.mesh_file, bc_map);
    if (rank == 0) {
      cfd::print_mesh_summary(mesh);
    }

    // ---- Partitioning (identical on every rank) ----
    const cfd::PartitionConfig pcfg{nranks};
    const cfd::PartitionResult part = cfd::partition_mesh(mesh, pcfg);
    if (rank == 0) {
      std::printf("\n=== Phase 2: MPI Partitioning ===\n");
      std::printf("Ranks       : %d\n", nranks);
      std::printf("Global cells: %zu\n", mesh.cells.size());
      std::printf("METIS edge cut: %d\n", part.edge_cut);
    }

    // ---- Rank-local mesh ----
    const cfd::LocalMesh lm =
        cfd::build_local_mesh(mesh, part, rank, nranks);
    print_partition_stats(lm);

    // ---- Verify the per-cell face CSR (owned cells) ----
    {
      const int csr_errors = verify_cell_face_csr(lm);
      int total_csr_errors = 0;
      MPI_Reduce(&csr_errors, &total_csr_errors, 1, MPI_INT, MPI_SUM, 0,
                 MPI_COMM_WORLD);
      std::printf("[rank %d/%d] cell-face CSR: %s (%zu entries, %zu faces)\n",
                  rank, nranks, csr_errors == 0 ? "OK" : "FAIL",
                  lm.cell_faces_data.size(), lm.faces.size());
      if (rank == 0) {
        if (total_csr_errors == 0) {
          std::printf("Cell-face CSR verified on all ranks: PASS "
                      "(every owned cell has one entry per edge)\n");
        } else {
          std::printf("Cell-face CSR verified on all ranks: FAIL "
                      "(%d errors)\n",
                      total_csr_errors);
          exit_code = 1;
        }
      }
    }

    // ---- Verification: sum of owned cells == global cell count ----
    {
      const long long owned = lm.n_owned;
      long long total_owned = 0;
      MPI_Reduce(const_cast<long long*>(&owned), &total_owned, 1,
                 MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
      if (rank == 0) {
        const bool ok = total_owned == static_cast<long long>(mesh.cells.size());
        std::printf("Owned-cell sum over ranks: %lld of %zu global cells -> %s\n",
                    total_owned, mesh.cells.size(),
                    ok ? "OK" : "MISMATCH!");
        if (!ok) {
          exit_code = 1;
        }
      }
    }

    // ---- Diagnostics CSV ----
    write_partition_diagnostics(lm, out_dir);

    // ---- Halo exchange self-test ----
    // Scratch buffers are allocated once and reused (no per-call allocation,
    // matching the Phase 3 solver iteration pattern).
    cfd::HaloScratch scratch;
    scratch.init(lm);
    const int local_errors = halo_exchange_test(lm, part, scratch);
    int total_errors = 0;
    MPI_Reduce(&local_errors, &total_errors, 1, MPI_INT, MPI_SUM, 0,
               MPI_COMM_WORLD);
    std::printf("[rank %d/%d] halo exchange test: %s "
                "(%d ghost cells verified)\n",
                rank, nranks,
                local_errors == 0 ? "PASS" : "FAIL", lm.n_ghost);
    if (rank == 0) {
      if (total_errors == 0) {
        std::printf("Halo exchange test across all ranks: PASS "
                    "(no mismatches)\n");
      } else {
        std::printf("Halo exchange test across all ranks: FAIL "
                    "(%d mismatched ghost cells)\n",
                    total_errors);
        exit_code = 1;
      }
    }

    // ---- Phase 3: first-order finite-volume solver ----
    // Validate that every boundary face has a usable BC type BEFORE the
    // solver loop: an invalid BC would throw inside compute_residual on the
    // owning rank alone, and that rank would then leave the others blocked
    // in MPI collectives (hang). The check is collective, so all ranks agree
    // on the exit path.
    {
      int n_invalid_bc = 0;
      for (const cfd::LocalMesh::LocalFace& f : lm.faces) {
        if (f.right == -1 && f.bc_type == cfd::BCType::Invalid) {
          ++n_invalid_bc;
        }
      }
      int n_invalid_bc_global = 0;
      MPI_Allreduce(&n_invalid_bc, &n_invalid_bc_global, 1, MPI_INT,
                    MPI_SUM, MPI_COMM_WORLD);
      if (n_invalid_bc_global > 0) {
        if (rank == 0) {
          std::fprintf(stderr,
                       "cfd_solver: error: %d boundary faces have an invalid "
                       "BC type (check the case boundary_conditions against "
                       "the mesh families)\n",
                       n_invalid_bc_global);
        }
        MPI_Finalize();
        return 1;
      }
    }

    // Initial condition: freestream on all local cells (ghosts are refreshed
    // by the halo exchange inside the solver). The scratch buffers from the
    // halo test above are reused (already init'd against lm).
    cfd::SolverConfig scfg = cfd::make_solver_config(config);
    if (cli_max_steps > 0) {
      scfg.max_steps = cli_max_steps;
    }
    std::vector<cfd::ConsState> U_local(lm.cells.size());
    const cfd::ConsState U_fs =
        cfd::freestream_to_cons(config.freestream, config.gas);
    for (cfd::ConsState& u : U_local) {
      u = U_fs;
    }

    // --restart <file>: load the owned-cell states from the restart file
    // (collective read). On any failure (missing file, rank-count or
    // partition mismatch, truncated data) fall back to the freestream IC
    // and warn on rank 0. The marching loops skip their freestream
    // re-initialization when the restart was loaded.
    if (!restart_path.empty()) {
      double restart_time = 0.0;
      scfg.restart = cfd::read_restart_binary(restart_path, U_local, lm,
                                              restart_time);
      if (rank == 0) {
        if (scfg.restart) {
          std::printf("Restart: loaded owned-cell states from '%s' "
                      "(restart physical time %.8g); marching starts from "
                      "t = 0 with this state\n",
                      restart_path.c_str(), restart_time);
        } else {
          std::printf("Restart: cannot use '%s' (missing or incompatible "
                      "restart file); starting from freestream instead\n",
                      restart_path.c_str());
        }
      }
    }

    // ---- Dispatch: steady pseudo-time marching or transient BDF2 ----
    cfd::SolverStats final_stats;
    cfd::GradientHaloScratch grad_scratch;
    const bool is_transient = config.run_type == "transient";
    int solver_rc = 0;
    if (is_transient) {
      cfd::run_transient_solver(U_local, lm, config, scfg, config.gas,
                                config.freestream, scratch, grad_scratch,
                                out_dir, &final_stats);
      if (!final_stats.converged) {
        solver_rc = 1;  // failed or empty transient run
      }
    } else {
      solver_rc = cfd::run_steady_solver(config, scfg, U_local, lm, scratch,
                                         out_dir, &final_stats);
    }
    if (solver_rc != 0) {
      exit_code = 1;
    }
    if (rank == 0) {
      std::printf("Phase 5 complete: %s solve finished (output dir '%s').\n",
                  is_transient ? "transient BDF2" : "steady LU-SGS",
                  out_dir.c_str());
    }

    // ---- Phase 5: full output contract ----
    // Written after the solver returns so the final field / surface /
    // restart / metadata describe the final state. All ranks participate in
    // the collective writers; rank 0 additionally writes the JSON files.
    const double wall_time = MPI_Wtime() - wall_start;
    const double epoch_end = static_cast<double>(std::time(nullptr));
    const std::string solver_name = "cfd_solver";
    const std::string solver_version = "0.2.0";
    // The run only counts as a full transient run when the physical time
    // actually reached final_time (a max_steps cap truncates it).
    const bool truncated =
        is_transient && config.final_time > 0.0 &&
        final_stats.physical_time < config.final_time - 1e-9;

    // Convergence status semantics: only mark a run complete when the
    // intended end state was reached — "converged" only when the steady
    // residual target was met, "statistically_periodic" only for a full
    // (non-truncated) transient run; everything else is "incomplete"
    // (stopped at a step cap) or "failed" (non-finite state/residual).
    std::string convergence_status;
    if (solver_rc != 0) {
      convergence_status = "failed";
    } else if (is_transient) {
      convergence_status = truncated ? "incomplete" : "statistically_periodic";
    } else {
      convergence_status = final_stats.converged ? "converged" : "incomplete";
    }
    const bool completed =
        (solver_rc == 0) && convergence_status != "incomplete";

    // surface.csv (collective gather to rank 0).
    cfd::write_surface_csv(
        (std::filesystem::path(out_dir) / "surface.csv").string(), lm,
        U_local, config.gas, config.freestream, scfg);

    // field_final.pvtu + per-rank .vtu pieces.
    cfd::write_field_vtu_piece(
        (std::filesystem::path(out_dir) /
         ("field_final_p" + std::to_string(rank) + ".vtu"))
            .string(),
        lm, U_local, config.gas, rank);
    if (rank == 0) {
      cfd::write_field_pvtu(
          (std::filesystem::path(out_dir) / "field_final.pvtu").string(),
          nranks);
    }

    // restart_final.bin (collective MPI-IO).
    cfd::write_restart_binary(
        (std::filesystem::path(out_dir) / "restart_final.bin").string(),
        U_local, lm, nranks, final_stats.physical_time);

    if (rank == 0) {
      const int final_step = final_stats.step;
      const double conv_frac =
          final_step > 0
              ? std::max(0.0, 1.0 - static_cast<double>(
                                        final_stats.inner_target_misses) /
                                        final_step)
              : 1.0;
      cfd::write_metadata(
          (std::filesystem::path(out_dir) / "metadata.json").string(), config,
          lm, nranks, static_cast<int>(mesh.cells.size()),
          static_cast<int>(mesh.faces.size()), part.edge_cut, solver_name,
          solver_version, scfg, epoch_start, epoch_end, completed,
          convergence_status, config.case_id, config.mesh_file,
          final_stats.min_inner_its, final_stats.max_inner_its,
          final_stats.mean_inner_its, final_stats.inner_target_misses,
          conv_frac, final_stats.last_inner_residual_ratio, is_transient);

      // run_status.json.
      std::string command = "cfd_solver solve --case " + case_path +
                            " --output " + out_dir;
      if (cli_max_steps > 0) {
        command += " --max-steps " + std::to_string(cli_max_steps);
      }
      if (!restart_path.empty()) {
        command += " --restart " + restart_path;
      }
      std::string notes;
      if (solver_rc != 0) {
        notes = "solver failed (non-finite state/residual)";
      } else if (is_transient) {
        notes = truncated
                    ? "transient run truncated by max_steps before "
                      "final_time; not a full periodic run"
                    : "full transient run to final_time";
      } else if (!final_stats.converged) {
        notes = "steady run stopped at max_steps before reaching the "
                "residual reduction target";
      } else {
        notes = "steady run converged";
      }
      cfd::write_run_status(
          (std::filesystem::path(out_dir) / "run_status.json").string(),
          config, nranks, wall_time, final_step, final_stats.physical_time,
          convergence_status, final_stats.residual_reduction_orders, command,
          notes);

      std::printf("Output files written to '%s': metadata.json, "
                  "partition_diagnostics.csv, residuals.csv, forces.csv, "
                  "surface.csv, field_final.pvtu (+ per-rank .vtu), "
                  "restart_final.bin, run_status.json, stdout.log\n",
                  out_dir.c_str());
    }

    // ---- All ranks must agree on the exit code ----
    // Rank 0's decision (set by the owned-sum check and the halo test
    // results) is broadcast so every rank returns the same status.
    MPI_Bcast(&exit_code, 1, MPI_INT, 0, MPI_COMM_WORLD);
  } catch (const std::exception& e) {
    if (rank == 0) {
      std::cerr << "cfd_solver: error: " << e.what() << "\n";
    }
    exit_code = 1;
  }

  MPI_Finalize();
  return exit_code;
}
