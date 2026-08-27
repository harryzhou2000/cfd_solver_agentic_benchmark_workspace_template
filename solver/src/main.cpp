// Command-line driver.
//
//   mpirun -np <ranks> cfd2d solve --case <case.json> --output <dir>
//                                  [--restart <file>] [--report-level brief|full]
//
// Every numerical option has a switch with a documented default; the eight
// benchmark cases run with identical options and differ only through their
// JSON input files.
#include <mpi.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/CaseConfig.hpp"
#include "core/Exception.hpp"
#include "core/Log.hpp"
#include "core/Version.hpp"
#include "io/OutputWriter.hpp"
#include "mesh/CgnsReader.hpp"
#include "mesh/Geometry.hpp"
#include "mesh/MeshDistributor.hpp"
#include "mesh/Partitioner.hpp"
#include "numerics/ImplicitSolver.hpp"
#include "numerics/RunLoop.hpp"
#include "numerics/SolverOptions.hpp"
#include "numerics/SpatialOperator.hpp"
#include "numerics/Verification.hpp"

namespace cfd {
namespace {

using json = nlohmann::json;

struct CliArgs {
  std::string command;
  std::map<std::string, std::string> opts;
  std::vector<std::string> positional;
};

const char* kUsage =
    "Usage:\n"
    "  cfd2d solve --case <case.json> --output <dir> [options]\n"
    "  cfd2d inspect-mesh --mesh <file.cgns> [--node-merge-tol <tol>]\n"
    "  cfd2d verify [--levels <n>] [--base <n>] [--mesh-jitter <f>] [--case <case.json>]\n"
    "               [--out <file.json>]\n"
    "  cfd2d version\n"
    "\n"
    "Required solve options:\n"
    "  --case <path>            benchmark case JSON (INPUT_FORMAT.md)\n"
    "  --output <dir>           output directory (created if missing)\n"
    "Optional solve options:\n"
    "  --restart <file>         restart from a previously written restart_final.bin\n"
    "  --report-level brief|full\n"
    "  --flux roe|hllc|rusanov          (default hllc)\n"
    "  --entropy-fix <x>                Harten-Yee coefficient (default 0.1)\n"
    "  --shock-fix <x>                  multidimensional shock-fix strength; blends the\n"
    "                                   contact-resolving flux towards Rusanov on faces\n"
    "                                   lying along a strong shock (default 6, 0 disables)\n"
    "  --limiter venkatakrishnan|barth|none   (default venkatakrishnan)\n"
    "  --venk-k <x>                     Venkatakrishnan constant (default 5)\n"
    "  --first-order                    disable linear reconstruction (debug only)\n"
    "  --freeze-limiter-step <n>        hold the limiter fixed from step n on (default: three\n"
    "                                   CFL-ramp lengths; 0 disables). Removes the residual\n"
    "                                   limit cycle of the non-differentiable min/max stencil\n"
    "  --inner-sweeps <n>               LU-SGS sweeps per transient inner iteration (default 2)\n"
    "  --cfl-scale <x>                  multiplies the case CFL schedule (default 1)\n"
    "  --no-adaptive-cfl                disable the residual-based CFL back-off safeguard\n"
    "  --time-integrator bdf2|trapezoidal  second-order physical-time scheme for transient\n"
    "                                   runs (default bdf2; the supplied case files accept\n"
    "                                   either)\n"
    "  --inner-target <x>               override run_control.inner_residual_reduction_target\n"
    "                                   (only a stricter, i.e. smaller, value is accepted)\n"
    "  --max-steps <n>                  override run_control.max_steps (debug)\n"
    "  --final-time <x>                 override run_control.final_time (debug)\n"
    "  --no-intermediate-fields         skip transient field snapshots\n"
    "  --field-precision 32|64          VTU payload precision (default 32)\n"
    "  --progress-every <n>             log cadence (default 100)\n"
    "  --node-merge-tol <tol>           geometric node merge fallback (default 0 = off)\n";

CliArgs parseArgs(int argc, char** argv) {
  CliArgs a;
  if (argc < 2) CFD_THROW("no command given\n" << kUsage);
  a.command = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string s = argv[i];
    if (s.rfind("--", 0) == 0) {
      const std::string key = s.substr(2);
      static const std::vector<std::string> flags = {"first-order", "no-intermediate-fields",
                                                    "no-adaptive-cfl"};
      const bool is_flag = std::find(flags.begin(), flags.end(), key) != flags.end();
      if (is_flag) {
        a.opts[key] = "1";
      } else {
        CFD_CHECK(i + 1 < argc, "option --" << key << " requires a value\n" << kUsage);
        a.opts[key] = argv[++i];
      }
    } else {
      a.positional.push_back(s);
    }
  }
  return a;
}

std::string opt(const CliArgs& a, const std::string& k, const std::string& def = "") {
  auto it = a.opts.find(k);
  return it == a.opts.end() ? def : it->second;
}

double optReal(const CliArgs& a, const std::string& k, double def) {
  auto it = a.opts.find(k);
  if (it == a.opts.end()) return def;
  try { return std::stod(it->second); }
  catch (...) { CFD_THROW("option --" << k << " expects a number, got '" << it->second << "'"); }
}

long optInt(const CliArgs& a, const std::string& k, long def) {
  auto it = a.opts.find(k);
  if (it == a.opts.end()) return def;
  try { return std::stol(it->second); }
  catch (...) { CFD_THROW("option --" << k << " expects an integer, got '" << it->second << "'"); }
}

SolverOptions buildOptions(const CliArgs& a) {
  SolverOptions o;
  const std::string flux = opt(a, "flux", "hllc");
  if (flux == "roe") o.flux = RiemannScheme::kRoe;
  else if (flux == "hllc") o.flux = RiemannScheme::kHllc;
  else if (flux == "rusanov" || flux == "llf") o.flux = RiemannScheme::kRusanov;
  else CFD_THROW("unknown --flux '" << flux << "' (roe|hllc|rusanov)");

  const std::string lim = opt(a, "limiter", "venkatakrishnan");
  if (lim == "venkatakrishnan" || lim == "venk") o.limiter = LimiterType::kVenkatakrishnan;
  else if (lim == "barth" || lim == "barth_jespersen") o.limiter = LimiterType::kBarthJespersen;
  else if (lim == "none") o.limiter = LimiterType::kNone;
  else CFD_THROW("unknown --limiter '" << lim << "' (venkatakrishnan|barth|none)");

  o.entropy_fix = optReal(a, "entropy-fix", o.entropy_fix);
  o.shock_fix = optReal(a, "shock-fix", o.shock_fix);
  o.venkatakrishnan_k = optReal(a, "venk-k", o.venkatakrishnan_k);
  o.second_order = a.opts.find("first-order") == a.opts.end();
  o.inner_sweeps = static_cast<int>(optInt(a, "inner-sweeps", o.inner_sweeps));
  CFD_CHECK(o.inner_sweeps >= 1, "--inner-sweeps must be >= 1");
  o.limiter_freeze_step = static_cast<int>(optInt(a, "freeze-limiter-step", -1));
  CFD_CHECK(o.limiter_freeze_step >= -1, "--freeze-limiter-step must be >= 0 (0 disables)");
  o.cfl_scale = optReal(a, "cfl-scale", o.cfl_scale);
  o.adaptive_cfl = a.opts.find("no-adaptive-cfl") == a.opts.end();
  CFD_CHECK(o.cfl_scale > 0.0, "--cfl-scale must be positive");
  o.write_intermediate_fields = a.opts.find("no-intermediate-fields") == a.opts.end();
  o.field_precision = static_cast<int>(optInt(a, "field-precision", o.field_precision));
  CFD_CHECK(o.field_precision == 32 || o.field_precision == 64, "--field-precision must be 32 or 64");
  o.progress_every = static_cast<int>(optInt(a, "progress-every", o.progress_every));
  CFD_CHECK(o.progress_every >= 1, "--progress-every must be >= 1");
  const std::string rl = opt(a, "report-level", "full");
  CFD_CHECK(rl == "brief" || rl == "full", "--report-level must be 'brief' or 'full'");
  if (rl == "brief") o.progress_every = static_cast<int>(optInt(a, "progress-every", 1000));
  return o;
}

std::string utcNow() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

int runInspect(const CliArgs& a) {
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  const std::string mesh_path = opt(a, "mesh");
  CFD_CHECK(!mesh_path.empty(), "inspect-mesh requires --mesh <file.cgns>");
  if (rank != 0) return 0;
  GlobalMesh mesh;
  CgnsReadOptions ro;
  ro.node_merge_tol = optReal(a, "node-merge-tol", 0.0);
  readCgnsMesh(mesh_path, ro, mesh);
  Real xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
  for (Index i = 0; i < mesh.numNodes(); ++i) {
    xmin = std::min(xmin, mesh.x[i]); xmax = std::max(xmax, mesh.x[i]);
    ymin = std::min(ymin, mesh.y[i]); ymax = std::max(ymax, mesh.y[i]);
  }
  Real vmin = 1e300, vmax = -1e300, vtot = 0.0;
  for (Index c = 0; c < mesh.numCells(); ++c) {
    const Real v = std::abs(geom::signedArea(mesh.x.data(), mesh.y.data(), mesh.cellNodePtr(c),
                                             mesh.cellSize(c)));
    vmin = std::min(vmin, v); vmax = std::max(vmax, v); vtot += v;
  }
  std::map<int, int> shape_hist;
  for (Index c = 0; c < mesh.numCells(); ++c) shape_hist[mesh.cellSize(c)]++;
  // Per-patch extent and total length, useful to confirm reference lengths.
  std::vector<Real> plen(mesh.patches.size(), 0.0);
  std::vector<Real> pxmin(mesh.patches.size(), 1e300), pxmax(mesh.patches.size(), -1e300);
  std::vector<Real> pymin(mesh.patches.size(), 1e300), pymax(mesh.patches.size(), -1e300);
  for (Index f = 0; f < mesh.numFaces(); ++f) {
    const Index p = mesh.face_patch[f];
    if (p < 0) continue;
    const Index a = mesh.face_n0[f], b = mesh.face_n1[f];
    plen[p] += geom::edgeLength(mesh.x[a], mesh.y[a], mesh.x[b], mesh.y[b]);
    for (Index nn : {a, b}) {
      pxmin[p] = std::min(pxmin[p], mesh.x[nn]); pxmax[p] = std::max(pxmax[p], mesh.x[nn]);
      pymin[p] = std::min(pymin[p], mesh.y[nn]); pymax[p] = std::max(pymax[p], mesh.y[nn]);
    }
  }
  for (std::size_t p = 0; p < mesh.patches.size(); ++p) {
    LOG() << "patch '" << mesh.patches[p].name << "': perimeter " << plen[p] << ", x ["
          << pxmin[p] << ", " << pxmax[p] << "], y [" << pymin[p] << ", " << pymax[p] << "]\n";
  }
  LOG() << "bounding box: x [" << xmin << ", " << xmax << "]  y [" << ymin << ", " << ymax << "]\n"
        << "cell area: min " << vmin << "  max " << vmax << "  total " << vtot << "\n";
  for (const auto& kv : shape_hist)
    LOG() << "cells with " << kv.first << " nodes: " << kv.second << "\n";
  return 0;
}

int runVerify(const CliArgs& a) {
  int rank = 0, size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  Logger::instance().setRank(rank, size);
  const int levels = static_cast<int>(optInt(a, "levels", 4));
  const int base = static_cast<int>(optInt(a, "base", 16));
  CFD_CHECK(levels >= 2 && levels <= 6, "--levels must be between 2 and 6");
  CFD_CHECK(base >= 4, "--base must be >= 4");
  const std::string case_path = opt(a, "case");
  const Real jitter = optReal(a, "mesh-jitter", 0.0);
  CFD_CHECK(jitter >= 0.0 && jitter < 0.3, "--mesh-jitter must be in [0, 0.3)");
  const SolverOptions o = buildOptions(a);
  const VerificationReport rep = runVerification(
      levels, base, case_path.empty() ? nullptr : &case_path, o, MPI_COMM_WORLD, jitter);
  const std::string out = opt(a, "out", "report/verification.json");
  if (rank == 0) {
    std::error_code ec;
    const std::filesystem::path p(out);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    writeVerificationJson(rep, out);
    LOG() << "wrote " << out << "\n";
  }
  return 0;
}

int runSolve(const CliArgs& a) {
  MPI_Comm comm = MPI_COMM_WORLD;
  int rank = 0, size = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);
  Logger::instance().setRank(rank, size);

  const std::string case_path = opt(a, "case");
  const std::string out_dir = opt(a, "output");
  CFD_CHECK(!case_path.empty(), "solve requires --case <case.json>\n" << kUsage);
  CFD_CHECK(!out_dir.empty(), "solve requires --output <dir>\n" << kUsage);

  CaseConfig cfg = CaseConfig::loadFromFile(case_path);
  SolverOptions options = buildOptions(a);
  if (a.opts.count("max-steps")) cfg.run.max_steps = static_cast<int>(optInt(a, "max-steps", 0));
  if (a.opts.count("final-time")) cfg.run.final_time = optReal(a, "final-time", 0.0);
  if (a.opts.count("time-integrator")) {
    const std::string ti = opt(a, "time-integrator");
    if (ti == "bdf2") cfg.run.time_integrator = TimeIntegratorType::kBdf2;
    else if (ti == "trapezoidal") cfg.run.time_integrator = TimeIntegratorType::kTrapezoidal;
    else CFD_THROW("unknown --time-integrator '" << ti << "' (bdf2|trapezoidal)");
  }
  if (a.opts.count("inner-target")) {
    const Real t = optReal(a, "inner-target", cfg.run.inner_residual_reduction_target);
    CFD_CHECK(t > 0.0 && t <= cfg.run.inner_residual_reduction_target,
              "--inner-target " << t << " must be positive and at least as strict as the case "
              "value " << cfg.run.inner_residual_reduction_target);
    cfg.run.inner_residual_reduction_target = t;
  }

  if (rank == 0) {
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    CFD_CHECK(std::filesystem::is_directory(out_dir),
              "cannot create output directory '" << out_dir << "'");
  }
  MPI_Barrier(comm);
  Logger::instance().openFile(out_dir + "/stdout.log");

  std::string command;
  {
    std::ostringstream os;
    os << "mpirun -np " << size << " cfd2d solve --case " << case_path << " --output " << out_dir;
    for (const auto& kv : a.opts) {
      if (kv.first == "case" || kv.first == "output") continue;
      os << " --" << kv.first << (kv.second == "1" ? "" : " " + kv.second);
    }
    command = os.str();
  }

  const std::string start_utc = utcNow();
  const double t_start = MPI_Wtime();

  LOG() << "==================================================================\n"
        << kSolverName << " " << kSolverVersion << " (" << kGitRevision << ", " << kBuildType
        << ")\n"
        << "case      : " << cfg.case_id << " -- " << cfg.description << "\n"
        << "mesh      : " << cfg.mesh_file << "\n"
        << "mode      : " << toString(cfg.mode)
        << (cfg.mode == PhysicsMode::kLaminar
                ? "  Re=" + std::to_string(cfg.reynolds) + "  mu=" +
                      std::to_string(cfg.molecularViscosity())
                : "")
        << "\n"
        << "freestream: M=" << cfg.freestream.mach << " aoa=" << cfg.freestream.aoa_degrees
        << " rho=" << cfg.freestream.rho << " p=" << cfg.freestream.pressure << "\n"
        << "ranks     : " << size << "\n"
        << "==================================================================\n";

  // ---- serial preprocessing on rank 0 -> distributed solver mesh ----
  GlobalMesh gmesh;
  PartitionResult part;
  CgnsMeshInfo minfo;
  double t_mesh = MPI_Wtime();
  if (rank == 0) {
    CgnsReadOptions ro;
    ro.node_merge_tol = optReal(a, "node-merge-tol", 0.0);
    minfo = readCgnsMesh(cfg.mesh_file, ro, gmesh);
    part = partitionMesh(gmesh, size);
    LOG() << "partitioner: " << part.method << ", edge cut " << part.edge_cut << "\n";
  }
  LocalMesh lmesh;
  distributeMesh(gmesh, part, comm, lmesh);
  PartitionResult().ordered_cells.swap(part.ordered_cells);
  std::vector<int>().swap(part.part);
  t_mesh = MPI_Wtime() - t_mesh;

  const Real geo_err = globalMax(lmesh.checkGeometry(), comm);
  CFD_CHECK(geo_err < 1e-9,
            "face/cell geometry consistency check failed (relative volume error " << geo_err << ")");
  const long long owned_total = globalSumLL(lmesh.num_owned, comm);
  CFD_CHECK(owned_total == lmesh.global_num_cells,
            "partition lost cells: " << owned_total << " owned vs " << lmesh.global_num_cells
            << " global");
  LOG() << "mesh distributed in " << std::fixed << std::setprecision(3) << t_mesh
        << " s; geometry check (divergence theorem) max relative error " << std::scientific
        << geo_err << "\n";

  const std::vector<PartitionDiagnostics> diags = gatherPartitionDiagnostics(lmesh, comm);
  writePartitionDiagnostics(out_dir, diags, lmesh.partition_edge_cut, rank);
  if (rank == 0) {
    long long minc = -1, maxc = 0, tot = 0, maxg = 0, totn = 0;
    for (const auto& d : diags) {
      minc = (minc < 0) ? d.num_cells_owned : std::min<long long>(minc, d.num_cells_owned);
      maxc = std::max<long long>(maxc, d.num_cells_owned);
      maxg = std::max<long long>(maxg, d.num_cells_ghost);
      tot += d.num_cells_owned;
      totn += d.num_neighbor_ranks;
    }
    LOG() << "partition: owned cells min " << minc << " max " << maxc << " mean "
          << std::fixed << std::setprecision(1) << (double)tot / diags.size()
          << ", load balance " << std::setprecision(4) << (double)maxc * diags.size() / tot
          << ", max ghost " << maxg << ", mean neighbours " << std::setprecision(2)
          << (double)totn / diags.size() << "\n";
  }

  // ---- solver ----
  SpatialOperator op(lmesh, cfg, options, comm);
  ImplicitSolver solver(op);
  op.initializeFreestream();

  const std::size_t nt = static_cast<std::size_t>(lmesh.numTotalCells()) * kNVar;
  std::vector<Real> un, unm1;
  Real start_time = 0.0;
  long long start_step = 0;
  bool have_history = false;
  const std::string restart_in = opt(a, "restart");
  if (!restart_in.empty()) {
    un.assign(nt, 0.0);
    unm1.assign(nt, 0.0);
    const RestartInfo ri = readRestart(restart_in, op, &un, &unm1);
    start_time = ri.time;
    start_step = ri.step;
    have_history = ri.levels == 3;
    LOG() << "restarted from '" << restart_in << "' at t=" << start_time << " step=" << start_step
          << " (" << ri.levels << " time levels)\n";
  }

  // The case file states the capabilities it requires; check them against what
  // this invocation actually does and record the outcome in the metadata.
  bool numerics_ok = true;
  {
    auto check = [&](bool ok, const std::string& what) {
      LOG() << "  numerics_required: " << (ok ? "[ok]   " : "[MISS] ") << what << "\n";
      if (!ok) numerics_ok = false;
    };
    LOG() << "case numerics requirements:\n";
    if (cfg.required_spatial_order > 0)
      check(!(cfg.required_spatial_order >= 2) || options.second_order,
            "spatial_order >= " + std::to_string(cfg.required_spatial_order) +
                " (reconstruction " + (options.second_order ? "linear" : "first order") + ")");
    if (!cfg.required_inviscid_flux.empty())
      check(cfg.required_inviscid_flux != "approximate_riemann" ||
                options.flux == RiemannScheme::kRoe || options.flux == RiemannScheme::kHllc ||
                options.flux == RiemannScheme::kRusanov,
            "inviscid_flux = " + cfg.required_inviscid_flux + " (using " +
                std::string(toString(options.flux)) + ")");
    if (!cfg.required_viscous_flux.empty())
      check(cfg.required_viscous_flux != "required" || cfg.mode == PhysicsMode::kLaminar,
            "viscous_flux = " + cfg.required_viscous_flux);
    if (!cfg.required_main_time_method.empty())
      check(cfg.required_main_time_method == "implicit", "main_time_method = " +
            cfg.required_main_time_method + " (solver path is implicit)");
    if (cfg.required_transient_order > 0)
      check(cfg.run.type != RunType::kTransient || cfg.required_transient_order <= 2,
            "transient_order >= " + std::to_string(cfg.required_transient_order) +
                " (BDF2/trapezoidal are second order)");
    if (!numerics_ok)
      LOG() << "  WARNING: this run does not satisfy every capability the case file requests; "
               "metadata records numerics_required_satisfied=false.\n";
  }

  LOG() << "numerics: flux=" << toString(options.flux)
        << " entropy_fix=" << options.entropy_fix
        << " shock_fix=" << options.shock_fix
        << " reconstruction=" << (options.second_order ? "linear_least_squares" : "first_order")
        << " limiter=" << toString(options.limiter) << " (K=" << options.venkatakrishnan_k << ")"
        << (options.limiter_freeze_step == 0 ? " limiter_freezing=off" : "")
        << " implicit=lu_sgs_matrix_free\n";

  RunResult result;
  if (cfg.run.type == RunType::kSteady) {
    LOG() << "steady pseudo-time march: max_steps=" << cfg.run.max_steps
          << " cfl " << cfg.run.cfl_initial << " -> " << cfg.run.cfl_max << " over "
          << cfg.run.pseudo_cfl_ramp_steps << " steps, inner sweeps "
          << cfg.run.min_inner_iterations << "-" << cfg.run.max_inner_iterations
          << " (linear target " << cfg.run.inner_residual_reduction_target << ")\n";
    result = runSteady(op, solver, out_dir);
  } else {
    LOG() << "transient dual-time march: dt=" << cfg.run.time_step << " to t="
          << cfg.run.final_time << " ("
          << (long long)std::llround((cfg.run.final_time - start_time) / cfg.run.time_step)
          << " physical steps), inner iterations " << cfg.run.min_inner_iterations << "-"
          << cfg.run.max_inner_iterations << " target "
          << cfg.run.inner_residual_reduction_target << ", integrator "
          << (cfg.run.time_integrator == TimeIntegratorType::kBdf2 ? "BDF2" : "trapezoidal")
          << "\n";
    result = runTransient(op, solver, out_dir, start_time, start_step, un, unm1, have_history);
  }

  // ---- final outputs, all from the same final state ----
  op.evaluateResidual();   // refresh boundary states for surface output
  writeSurfaceCsv(out_dir, op);
  writeVtu(out_dir + "/field_final.vtu", op, 64);
  if (cfg.run.type == RunType::kTransient) {
    writeRestart(out_dir + "/restart_final.bin", op, result.final_physical_time, result.final_step,
                 &un, &unm1);
  } else {
    writeRestart(out_dir + "/restart_final.bin", op, 0.0, result.final_step, nullptr, nullptr);
  }

  const double wall = MPI_Wtime() - t_start;
  const std::string end_utc = utcNow();

  long long pos_fallbacks = globalSumLL(op.stats().positivity_fallbacks, comm);
  long long backtracks = globalSumLL(op.stats().update_backtracks, comm);

  if (rank == 0) {
    json md;
    md["case_id"] = cfg.case_id;
    md["solver_name"] = kSolverName;
    md["solver_version"] = kSolverVersion;
    md["git_revision"] = std::string(kGitRevision).empty() ? json(nullptr) : json(kGitRevision);
    md["mpi_ranks"] = size;
    md["mesh_file"] = cfg.mesh_file;
    md["num_cells_global"] = lmesh.global_num_cells;
    md["num_faces_global"] = lmesh.global_num_faces;
    md["num_boundary_faces_global"] = lmesh.global_num_boundary_faces;
    md["num_cells_owned_local"] = lmesh.num_owned;
    md["num_cells_ghost_local"] = lmesh.num_ghost;
    CFD_CHECK(!part.method.empty(), "internal: partition method label was lost");
    md["partitioner"] = part.method;
    md["partition_edge_cut"] = lmesh.partition_edge_cut;
    md["halo_exchange"] = op.halo().pattern();
    md["full_state_replication_during_iterations"] = false;
    md["full_mesh_replication_during_iterations"] = false;
    md["equation_set"] = "compressible_navier_stokes_2d";
    md["inviscid_flux"] = toString(options.flux);
    md["entropy_fix"] = (options.flux == RiemannScheme::kRoe && options.entropy_fix > 0.0)
                            ? json("harten_yee_acoustic_fields")
                            : json(nullptr);
    md["shock_fix"] = options.shock_fix > 0.0
                          ? json("multidimensional_pressure_gradient_rusanov_blend")
                          : json(nullptr);
    md["shock_fix_strength"] = options.shock_fix;
    md["viscous_flux"] = (cfg.mode == PhysicsMode::kLaminar)
                             ? "newtonian_stress_fourier_heat_flux_corrected_face_gradients"
                             : "disabled_inviscid_case";
    md["time_integrator"] = (cfg.run.type == RunType::kSteady)
                                ? "implicit_backward_euler_local_pseudo_time"
                                : (cfg.run.time_integrator == TimeIntegratorType::kBdf2
                                       ? "bdf2_dual_time"
                                       : "trapezoidal_dual_time");
    md["implicit_solver"] = "matrix_free_lu_sgs_symmetric_gauss_seidel";
    md["reconstruction"] = options.second_order
                               ? "piecewise_linear_weighted_least_squares_primitive"
                               : "first_order";
    md["limiter"] = toString(options.limiter);
    md["limiter_constant_K"] = options.venkatakrishnan_k;
    md["limiter_freeze_step_option"] = options.limiter_freeze_step;
    md["limiter_freeze_step"] =
        (cfg.run.type == RunType::kTransient)
            ? 0
            : (options.limiter_freeze_step >= 0
                   ? options.limiter_freeze_step
                   : (cfg.run.pseudo_cfl_ramp_steps > 0 ? 3 * cfg.run.pseudo_cfl_ramp_steps : 0));
    md["spatial_order_claimed"] = options.second_order ? 2 : 1;
    md["numerics_required_satisfied"] = numerics_ok;
    md["positivity_preservation"] =
        "first_order_fallback_on_negative_reconstructed_rho_or_p_plus_update_line_search";
    md["positivity_fallback_face_states"] = pos_fallbacks;
    md["update_line_search_events"] = backtracks;
    md["wall_boundary_output_semantics"] = "boundary_value";
    md["true_bdf2_inner_loop"] = (cfg.run.type == RunType::kTransient);
    md["typical_inner_iterations"] = result.inner.mean();
    md["min_inner_iterations"] = cfg.run.min_inner_iterations;
    md["max_inner_iterations"] = cfg.run.max_inner_iterations;
    md["observed_min_inner_iterations"] =
        result.inner.steps > 0 ? result.inner.min_inner : 0;
    md["observed_max_inner_iterations"] = result.inner.max_inner;
    md["observed_mean_inner_iterations"] = result.inner.mean();
    md["inner_residual_reduction_target"] = cfg.run.inner_residual_reduction_target;
    md["inner_residual_norm"] = (cfg.run.type == RunType::kTransient)
                                    ? cfg.run.inner_residual_norm
                                    : std::string("linear_system_residual");
    md["inner_target_misses"] = result.inner.target_misses;
    md["inner_target_converged_fraction"] = result.inner.convergedFraction();
    md["last_inner_residual_ratio"] = result.inner.last_ratio;
    md["bdf2_history_update"] = cfg.run.bdf2_history_update;
    md["cfl_initial"] = cfg.run.cfl_initial;
    md["cfl_max"] = cfg.run.cfl_max;
    md["cfl_scale"] = options.cfl_scale;
    md["effective_cfl_initial"] = cfg.run.cfl_initial * options.cfl_scale;
    md["effective_cfl_max"] = cfg.run.cfl_max * options.cfl_scale;
    md["pseudo_cfl_ramp_steps"] = cfg.run.pseudo_cfl_ramp_steps;
    md["adaptive_cfl_safeguard"] = options.adaptive_cfl;
    md["physical_time_step"] = cfg.run.time_step;
    md["final_physical_time"] = result.final_physical_time;
    md["mesh_zones"] = minfo.num_zones;
    md["mesh_nodes_global"] = minfo.nodes_before_merge - minfo.nodes_merged;
    md["start_time_utc"] = start_utc;
    md["end_time_utc"] = end_utc;
    md["wall_time_seconds"] = wall;
    md["completed"] = (result.convergence_status != "failed");
    md["convergence_status"] = result.convergence_status;
    md["residual_reduction_orders"] = result.residual_reduction_orders;
    md["final_cl"] = result.final_forces.cl;
    md["final_cd"] = result.final_forces.cd;
    md["final_cmz"] = result.final_forces.cmz;
    md["final_pressure_drag"] = result.final_forces.pressure_drag;
    md["final_viscous_drag"] = result.final_forces.viscous_drag;
    md["final_normal_viscous_drag"] = result.final_forces.normal_viscous_drag;
    md["boundary_condition_map"] = [&] {
      json b = json::object();
      for (const auto& kv : cfg.boundary_conditions) b[kv.first] = toString(kv.second);
      return b;
    }();
    md["command"] = command;
    std::ofstream(out_dir + "/metadata.json") << md.dump(2) << "\n";

    json st;
    st["case_id"] = cfg.case_id;
    st["command"] = command;
    st["mpi_ranks"] = size;
    st["wall_time_seconds"] = wall;
    st["final_step"] = result.final_step;
    st["final_physical_time"] = result.final_physical_time;
    st["convergence_status"] = result.convergence_status;
    st["residual_reduction_orders"] = result.residual_reduction_orders;
    st["notes"] = result.notes;
    std::ofstream(out_dir + "/run_status.json") << st.dump(2) << "\n";
  }

  LOG() << "------------------------------------------------------------------\n"
        << "status    : " << result.convergence_status << "\n"
        << "steps     : " << result.final_step
        << (cfg.run.type == RunType::kTransient
                ? "  t=" + std::to_string(result.final_physical_time)
                : "")
        << "\n"
        << "residual  : " << std::scientific << std::setprecision(4) << result.final_residual
        << " (" << std::fixed << std::setprecision(2) << result.residual_reduction_orders
        << " orders)\n"
        << "forces    : cl=" << std::setprecision(6) << result.final_forces.cl
        << " cd=" << result.final_forces.cd << " cmz=" << result.final_forces.cmz << "\n"
        << "            pressure drag " << result.final_forces.pressure_drag
        << ", skin friction drag " << result.final_forces.viscous_drag
        << ", normal viscous drag " << result.final_forces.normal_viscous_drag << "\n"
        << "inner     : mean " << std::setprecision(2) << result.inner.mean() << " min "
        << (result.inner.steps > 0 ? result.inner.min_inner : 0) << " max "
        << result.inner.max_inner << ", target met on " << std::setprecision(2)
        << 100.0 * result.inner.convergedFraction() << "%\n"
        << "robustness: " << pos_fallbacks << " first-order face fallbacks, " << backtracks
        << " update line searches\n"
        << "wall time : " << std::fixed << std::setprecision(2) << wall << " s\n"
        << "notes     : " << result.notes << "\n"
        << "------------------------------------------------------------------\n";

  Logger::instance().close();
  return (result.convergence_status == "failed") ? 2 : 0;
}

}  // namespace
}  // namespace cfd

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  cfd::Logger::instance().setRank(rank, size);
  int status = 0;
  try {
    const cfd::CliArgs args = cfd::parseArgs(argc, argv);
    if (args.command == "solve") {
      status = cfd::runSolve(args);
    } else if (args.command == "inspect-mesh") {
      status = cfd::runInspect(args);
    } else if (args.command == "verify") {
      status = cfd::runVerify(args);
    } else if (args.command == "version" || args.command == "--version") {
      if (rank == 0)
        std::cout << cfd::kSolverName << " " << cfd::kSolverVersion << " (" << cfd::kGitRevision
                  << ")\n";
    } else if (args.command == "help" || args.command == "--help" || args.command == "-h") {
      if (rank == 0) std::cout << cfd::kUsage;
    } else {
      CFD_THROW("unknown command '" << args.command << "'\n" << cfd::kUsage);
    }
  } catch (const std::exception& e) {
    std::cerr << "ERROR (rank " << rank << "): " << e.what() << std::endl;
    std::cerr.flush();
    if (size > 1) MPI_Abort(MPI_COMM_WORLD, 1);
    MPI_Finalize();
    return 1;
  }
  MPI_Finalize();
  return status;
}
