// cns2d -- command-line entry point.
//
//   mpirun -np <ranks> cns2d solve --case <case.json> --output <dir>
//                                 [--restart <file>] [--report-level brief|full]
//
// Exit status is 0 on a normal completion and nonzero for malformed input,
// missing meshes, unsupported boundary conditions, failed initialisation, or a
// numerically failed run.
#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

#ifndef OMPI_SKIP_MPICXX
#define OMPI_SKIP_MPICXX 1
#endif
#ifndef MPICH_SKIP_MPICXX
#define MPICH_SKIP_MPICXX 1
#endif
#include <mpi.h>

#include "core/case_input.h"
#include "core/exceptions.h"
#include "core/logging.h"
#include "core/options.h"
#include "core/path_utils.h"
#include "io/output_writer.h"
#include "mesh/mesh_verification.h"
#include "solve/solver_context.h"
#include "solve/steady_driver.h"
#include "solve/transient_driver.h"
#include "version.h"

namespace {

using namespace cns2d;

// Describe the discretization for metadata.json, derived from what the solver
// actually configured (not from a hard-coded string), so the metadata cannot
// claim a method the run did not use.
MethodDescription describeMethods(const SolverContext &context, bool transient) {
  const SchemeOptions &scheme = context.scheme();
  MethodDescription m;
  m.inviscid_flux = riemannFluxName(scheme.inviscid_flux) +
                    "_approximate_riemann_with_hllc_positivity_fallback";
  m.entropy_fix = entropyFixName(scheme.inviscid_flux);
  m.viscous_flux = scheme.viscous
                       ? "newtonian_stress_fourier_heat_flux_least_squares_primitive_gradients"
                       : "disabled_inviscid_mode";
  m.time_integrator = transient ? "bdf2_dual_time_implicit_outer_physical_inner_nonlinear"
                                : "implicit_pseudo_time_local_cfl";
  m.implicit_solver = context.implicitSolverLabel();
  m.reconstruction = scheme.second_order
                         ? "piecewise_linear_least_squares_primitive_gradients"
                         : "first_order_cell_average";
  m.limiter = limiterName(scheme.limiter);
  m.spatial_order_claimed = scheme.second_order ? 2 : 1;
  m.positivity_preservation =
      "limited_reconstruction_with_progressive_face_state_clipping_and_conserved_state_floor";
  m.wall_boundary_output_semantics = "boundary_value";
  m.true_bdf2_inner_loop = transient;
  return m;
}

int runSolve(const CommandLineOptions &options, MPI_Comm comm, int rank) {
  const auto wall_start = std::chrono::steady_clock::now();
  const std::string start_time_utc = utcTimestamp();

  // Parse and validate the case on every rank: the inputs are small, and doing
  // it everywhere means a malformed case fails identically on all ranks.
  const CaseInput input = loadCaseInput(options.case_path);

  // The output directory and log file must exist before the mesh is read so
  // setup messages are captured in stdout.log.
  if (rank == 0) makeDirectories(options.output_dir);
  MPI_Barrier(comm);
  Logger::instance().openLogFile(joinPath(options.output_dir, "stdout.log"));

  logInfo(std::string(kSolverName) + " " + kSolverVersion +
          (std::string(kGitRevision).empty() ? "" : std::string(" (git ") + kGitRevision + ")"));
  logInfo("command: " + options.command_line);
  logInfo("case: " + input.case_id + " -- " + input.description);
  logInfo("case file: " + input.case_file_path);
  logInfo("mesh file: " + input.mesh_file);
  {
    int size = 1;
    MPI_Comm_size(comm, &size);
    logInfo("MPI ranks: " + std::to_string(size));
  }
  for (const auto &kv : input.boundary_conditions) {
    logInfo("boundary family '" + kv.first + "' -> " + bcTypeName(kv.second));
  }

  SolverContext context(input, options, comm);
  logInfo(context.mesh().describe());
  logInfo(formatString("partitioner: %s, global edge cut %lld",
                       context.mesh().partitionerName().c_str(),
                       static_cast<long long>(context.mesh().edgeCut())));

  const SchemeOptions &scheme = context.scheme();
  logInfo(formatString(
      "numerics: %s inviscid flux (%s), %s reconstruction, %s limiter, %s, viscous terms %s",
      riemannFluxName(scheme.inviscid_flux).c_str(), entropyFixName(scheme.inviscid_flux).c_str(),
      scheme.second_order ? "piecewise-linear least-squares" : "first-order",
      limiterName(scheme.limiter).c_str(), context.implicitSolverLabel().c_str(),
      scheme.viscous ? "enabled" : "disabled"));

  OutputWriter writer(options.output_dir, input, options, context);
  writer.begin();
  writer.writePartitionDiagnostics();

  context.initializeState();

  // Verify the mesh metrics and free-stream preservation before spending time on
  // a solve.  A geometric inconsistency is reported here rather than surfacing
  // later as a residual that will not converge.
  //
  // --report-level selects how much of this is printed: 'full' reports every
  // measured quantity, while 'brief' prints only a pass/fail summary and any
  // warning.  The checks themselves always run; only the reporting differs, so
  // the level can never change the computed result.
  {
    const GeometryVerification verification =
        verifyMeshGeometry(context.mesh(), context.flow(), context.assembler(), context.halo());
    if (options.report_level == ReportLevel::kFull) {
      logInfo(describeVerification(verification));
    } else {
      // The wording matches describeVerification: hard geometric identities that
      // no valid mesh may violate already threw, so reaching here means the mesh
      // is usable.  'passed' additionally requires exact uniform-flow
      // preservation, which these stretched meshes miss at the 1e-11 level, and
      // that is reported as CHECK rather than as a failure.
      logInfo(formatString("mesh verification: %s (run with --report-level full for the "
                           "individual measured quantities)",
                           verification.passed ? "PASS" : "CHECK"));
    }
    // Re-establish the initial state, which the verification overwrote.
    context.initializeState();
  }

  const bool transient = input.run_control.type == RunType::kTransient;
  RunOutcome outcome = transient ? runTransient(context, writer) : runSteady(context, writer);

  // Final-state files are written from the same state the last force row used.
  writer.writeFinalState(outcome.final_step, outcome.final_physical_time);

  const auto wall_end = std::chrono::steady_clock::now();
  const double wall_seconds =
      std::chrono::duration_cast<std::chrono::duration<double>>(wall_end - wall_start).count();

  writer.writeMetadata(describeMethods(context, transient), outcome, start_time_utc, utcTimestamp());
  writer.writeRunStatus(outcome, wall_seconds);

  logInfo(formatString("run finished: status '%s', %d steps, t = %.6g, %.2f residual orders, "
                       "%.1f s wall time",
                       outcome.convergence_status.c_str(), outcome.final_step,
                       outcome.final_physical_time, outcome.residual_reduction_orders,
                       wall_seconds));
  logInfo("notes: " + outcome.notes);
  if (outcome.positivity_fallbacks > 0) {
    logWarn(formatString("positivity fallback was triggered %lld time(s) during the run",
                         outcome.positivity_fallbacks));
  }

  if (!outcome.completed) {
    logError("the run did not complete successfully; results are marked failed");
    return 3;
  }
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  int mpi_init_result = MPI_Init(&argc, &argv);
  if (mpi_init_result != MPI_SUCCESS) {
    std::fprintf(stderr, "cns2d: MPI_Init failed\n");
    return 1;
  }

  int rank = 0;
  int size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  cns2d::Logger::instance().configure(rank, size);

  int status = 0;
  try {
    const cns2d::CommandLineOptions options = cns2d::parseCommandLine(argc, argv);
    if (options.command == "help") {
      if (rank == 0) std::fputs(cns2d::usageText().c_str(), stdout);
    } else {
      status = runSolve(options, MPI_COMM_WORLD, rank);
    }
  } catch (const cns2d::CnsError &e) {
    cns2d::logError(e.what());
    status = 2;
  } catch (const std::exception &e) {
    cns2d::logError(std::string("unexpected error: ") + e.what());
    status = 2;
  }

  cns2d::Logger::instance().closeLogFile();

  // Any rank failing must fail the whole job, otherwise mpirun could report
  // success while one partition diverged.
  int global_status = status;
  MPI_Allreduce(&status, &global_status, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
  MPI_Finalize();
  return global_status;
}
