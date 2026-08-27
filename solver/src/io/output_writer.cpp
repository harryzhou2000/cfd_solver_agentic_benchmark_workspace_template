#include "io/output_writer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/exceptions.h"
#include "core/logging.h"
#include "core/path_utils.h"
#include "io/restart_io.h"
#include "io/vtu_writer.h"
#include "post/surface_output.h"
#include "version.h"

namespace cns2d {
namespace {
using json = nlohmann::json;

std::ofstream *asStream(void *p) { return static_cast<std::ofstream *>(p); }

// Fixed-precision numeric formatting so CSV files are stable and parseable.
std::string num(Real v) {
  std::ostringstream os;
  os << std::setprecision(12) << std::scientific << v;
  return os.str();
}

}  // namespace

std::string utcTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_utc{};
  gmtime_r(&t, &tm_utc);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return std::string(buffer);
}

OutputWriter::OutputWriter(const std::string &output_dir, const CaseInput &input,
                           const CommandLineOptions &options, SolverContext &context)
    : output_dir_(output_dir), input_(input), options_(options), context_(context) {
  is_root_ = context_.rank() == 0;
  log_path_ = joinPath(output_dir_, "stdout.log");
}

OutputWriter::~OutputWriter() {
  if (residual_stream_ != nullptr) {
    asStream(residual_stream_)->close();
    delete asStream(residual_stream_);
  }
  if (force_stream_ != nullptr) {
    asStream(force_stream_)->close();
    delete asStream(force_stream_);
  }
}

void OutputWriter::begin() {
  if (!is_root_) return;
  makeDirectories(output_dir_);

  auto *res = new std::ofstream(joinPath(output_dir_, "residuals.csv"), std::ios::trunc);
  if (!res->good()) {
    delete res;
    throw CnsError("cannot open residuals.csv in " + output_dir_);
  }
  (*res) << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
  residual_stream_ = res;

  auto *frc = new std::ofstream(joinPath(output_dir_, "forces.csv"), std::ios::trunc);
  if (!frc->good()) {
    delete frc;
    throw CnsError("cannot open forces.csv in " + output_dir_);
  }
  (*frc) << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
  force_stream_ = frc;
}

void OutputWriter::appendResidual(const ResidualRow &row) {
  if (!is_root_ || residual_stream_ == nullptr) return;
  std::ofstream &os = *asStream(residual_stream_);
  os << row.step << ',' << num(row.physical_time) << ',' << row.inner_iter << ',' << num(row.cfl)
     << ',' << num(row.dt);
  for (int k = 0; k < kNumVars; ++k) os << ',' << num(row.component[static_cast<std::size_t>(k)]);
  os << ',' << num(row.l2) << ',' << num(row.linf) << '\n';
}

void OutputWriter::appendForces(int step, Real physical_time, const ForceResult &f) {
  if (!is_root_ || force_stream_ == nullptr) return;
  std::ofstream &os = *asStream(force_stream_);
  os << step << ',' << num(physical_time) << ',' << num(f.cl) << ',' << num(f.cd) << ','
     << num(f.cmz) << ',' << num(f.pressure_drag) << ',' << num(f.viscous_drag) << ','
     << num(f.pressure_lift) << ',' << num(f.viscous_lift) << '\n';
}

void OutputWriter::writeFinalState(int step, Real physical_time) {
  // Surface rows (collective: every rank contributes its wall faces).
  std::vector<std::string> tag_names;
  const std::vector<SurfaceRow> rows = collectSurfaceRows(
      context_.mesh(), context_.flow(), context_.assembler(), context_.state(), context_.comm(),
      tag_names);

  if (is_root_) {
    std::ofstream os(joinPath(output_dir_, "surface.csv"), std::ios::trunc);
    if (!os.good()) throw CnsError("cannot open surface.csv in " + output_dir_);
    // surface.csv carries EXACTLY the contract header: the u/v/mach columns are
    // the boundary-condition values (zero velocity on a no-slip wall, zero
    // normal velocity on a slip wall).  The adjacent cell-centre values are
    // written to the companion file surface_cell_center.csv instead of extra
    // columns here, so the two can never be confused yet the contract header
    // stays byte-exact.
    os << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    for (const SurfaceRow &r : rows) {
      const std::string tag_name =
          (r.tag >= 0 && static_cast<std::size_t>(r.tag) < tag_names.size())
              ? tag_names[static_cast<std::size_t>(r.tag)]
              : std::string("unknown");
      os << num(r.x) << ',' << num(r.y) << ',' << num(r.nx) << ',' << num(r.ny) << ','
         << num(r.pressure) << ',' << num(r.cp) << ',' << num(r.cf) << ',' << num(r.rho) << ','
         << num(r.u) << ',' << num(r.v) << ',' << num(r.mach) << ',' << tag_name << '\n';
    }
    if (rows.empty()) {
      throw CnsError("no wall boundary faces were found; surface.csv would be empty");
    }

    // Companion file: adjacent cell-centre values and the raw wall shear, so the
    // report can compare boundary values against near-wall cell averages
    // explicitly rather than implying that one is the other.
    std::ofstream cc(joinPath(output_dir_, "surface_cell_center.csv"), std::ios::trunc);
    if (!cc.good()) throw CnsError("cannot open surface_cell_center.csv in " + output_dir_);
    cc << "x,y,nx,ny,cell_center_u,cell_center_v,cell_center_mach,tangential_wall_shear,"
          "boundary_u,boundary_v,boundary_mach,tag\n";
    for (const SurfaceRow &r : rows) {
      const std::string tag_name =
          (r.tag >= 0 && static_cast<std::size_t>(r.tag) < tag_names.size())
              ? tag_names[static_cast<std::size_t>(r.tag)]
              : std::string("unknown");
      cc << num(r.x) << ',' << num(r.y) << ',' << num(r.nx) << ',' << num(r.ny) << ','
         << num(r.cell_u) << ',' << num(r.cell_v) << ',' << num(r.cell_mach) << ','
         << num(r.tangential_shear) << ',' << num(r.u) << ',' << num(r.v) << ',' << num(r.mach)
         << ',' << tag_name << '\n';
    }
  }

  writeFieldVtu(joinPath(output_dir_, "field_final.vtu"), context_.mesh(), context_.flow(),
                context_.assembler(), context_.state(), context_.comm());
  writeRestart(joinPath(output_dir_, "restart_final.bin"), context_.mesh(), context_.state(),
               context_.comm(), step, physical_time);
}

void OutputWriter::writeIntermediateField(int index, Real physical_time) {
  char name[64];
  std::snprintf(name, sizeof(name), "field_%05d.vtu", index);
  writeFieldVtu(joinPath(output_dir_, name), context_.mesh(), context_.flow(), context_.assembler(),
                context_.state(), context_.comm());
  (void)physical_time;
}

void OutputWriter::writePartitionDiagnostics() {
  // Every rank reports its own diagnostics; rank 0 assembles the table.
  const PartitionDiagnostics &d = context_.mesh().diagnostics();
  const int size = context_.size();

  // Fixed-width payload: counts plus up to 'size' neighbour ids.
  std::vector<long long> payload;
  payload.reserve(6 + static_cast<std::size_t>(size));
  payload.push_back(d.num_cells_owned);
  payload.push_back(d.num_cells_ghost);
  payload.push_back(d.num_boundary_faces);
  payload.push_back(d.num_neighbor_ranks);
  payload.push_back(d.send_cells);
  payload.push_back(d.recv_cells);
  payload.resize(6 + static_cast<std::size_t>(size), -1);
  for (std::size_t i = 0; i < d.neighbor_ranks.size() && i < static_cast<std::size_t>(size); ++i) {
    payload[6 + i] = d.neighbor_ranks[i];
  }

  const int stride = static_cast<int>(payload.size());
  std::vector<long long> gathered;
  if (is_root_) gathered.resize(static_cast<std::size_t>(stride) * static_cast<std::size_t>(size));
  MPI_Gather(payload.data(), stride, MPI_LONG_LONG, is_root_ ? gathered.data() : nullptr, stride,
             MPI_LONG_LONG, 0, context_.comm());

  if (!is_root_) return;

  std::ofstream os(joinPath(output_dir_, "partition_diagnostics.csv"), std::ios::trunc);
  if (!os.good()) throw CnsError("cannot open partition_diagnostics.csv in " + output_dir_);
  os << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,"
        "send_cells,recv_cells\n";

  long long min_owned = 0;
  long long max_owned = 0;
  long long total_owned = 0;
  for (int r = 0; r < size; ++r) {
    const long long *p = gathered.data() + static_cast<std::size_t>(r) * stride;
    const long long owned = p[0];
    const long long ghost = p[1];
    const long long bfaces = p[2];
    const long long nneigh = p[3];
    const long long send = p[4];
    const long long recv = p[5];
    std::ostringstream neighbors;
    for (long long i = 0; i < nneigh; ++i) {
      if (i > 0) neighbors << ' ';
      neighbors << p[6 + i];
    }
    os << r << ',' << owned << ',' << ghost << ',' << bfaces << ',' << nneigh << ',' << '"'
       << neighbors.str() << '"' << ',' << send << ',' << recv << '\n';
    if (r == 0) {
      min_owned = owned;
      max_owned = owned;
    } else {
      min_owned = std::min(min_owned, owned);
      max_owned = std::max(max_owned, owned);
    }
    total_owned += owned;
  }
  os.flush();

  // A JSON companion carrying the global summaries the contract asks for.
  json summary;
  summary["num_parts"] = size;
  summary["partitioner"] = context_.mesh().partitionerName();
  summary["edge_cut"] = context_.mesh().edgeCut();
  summary["num_cells_global"] = context_.mesh().numCellsGlobal();
  summary["min_owned_cells"] = min_owned;
  summary["max_owned_cells"] = max_owned;
  summary["mean_owned_cells"] = static_cast<double>(total_owned) / static_cast<double>(size);
  summary["load_balance_ratio"] =
      max_owned > 0 ? static_cast<double>(max_owned) * static_cast<double>(size) /
                          static_cast<double>(std::max<long long>(total_owned, 1))
                    : 1.0;
  summary["halo_exchange"] = context_.halo().pattern();
  json ranks = json::array();
  for (int r = 0; r < size; ++r) {
    const long long *p = gathered.data() + static_cast<std::size_t>(r) * stride;
    json entry;
    entry["rank"] = r;
    entry["num_cells_owned"] = p[0];
    entry["num_cells_ghost"] = p[1];
    entry["num_boundary_faces"] = p[2];
    entry["num_neighbor_ranks"] = p[3];
    entry["send_cells"] = p[4];
    entry["recv_cells"] = p[5];
    json neigh = json::array();
    for (long long i = 0; i < p[3]; ++i) neigh.push_back(p[6 + i]);
    entry["neighbor_ranks"] = neigh;
    ranks.push_back(entry);
  }
  summary["ranks"] = ranks;

  std::ofstream js(joinPath(output_dir_, "partition_diagnostics.json"), std::ios::trunc);
  if (!js.good()) throw CnsError("cannot open partition_diagnostics.json in " + output_dir_);
  js << summary.dump(2) << '\n';
}

void OutputWriter::writeMetadata(const MethodDescription &methods, const RunOutcome &outcome,
                                 const std::string &start_time_utc,
                                 const std::string &end_time_utc) {
  if (!is_root_) return;
  const DistributedMesh &mesh = context_.mesh();
  const RunControl &rc = input_.run_control;

  json m;
  m["case_id"] = input_.case_id;
  m["solver_name"] = kSolverName;
  m["solver_version"] = kSolverVersion;
  m["git_revision"] = std::string(kGitRevision).empty() ? json(nullptr) : json(kGitRevision);
  m["mpi_ranks"] = context_.size();
  m["mesh_file"] = input_.mesh_file;
  m["num_cells_global"] = mesh.numCellsGlobal();
  m["num_faces_global"] = mesh.numFacesGlobal();
  m["num_cells_owned_local"] = mesh.numOwned();
  m["num_cells_ghost_local"] = mesh.numGhost();
  m["partitioner"] = mesh.partitionerName();
  m["partition_edge_cut"] = mesh.edgeCut();
  m["halo_exchange"] = context_.halo().pattern();
  m["full_state_replication_during_iterations"] = false;
  m["full_mesh_replication_during_iterations"] = false;
  m["equation_set"] = "compressible_navier_stokes_2d";
  m["inviscid_flux"] = methods.inviscid_flux;
  m["entropy_fix"] = methods.entropy_fix.empty() ? json(nullptr) : json(methods.entropy_fix);
  m["viscous_flux"] = methods.viscous_flux;
  m["time_integrator"] = methods.time_integrator;
  m["implicit_solver"] = methods.implicit_solver;
  m["reconstruction"] = methods.reconstruction;
  m["limiter"] = methods.limiter;
  m["spatial_order_claimed"] = methods.spatial_order_claimed;
  m["positivity_preservation"] = methods.positivity_preservation;
  m["wall_boundary_output_semantics"] = methods.wall_boundary_output_semantics;
  m["true_bdf2_inner_loop"] = methods.true_bdf2_inner_loop;

  // Inner-solve statistics actually observed during the run.
  const InnerSolveStatistics &st = outcome.inner_stats;
  m["typical_inner_iterations"] = static_cast<int>(std::lround(st.meanInner()));
  m["mean_inner_iterations"] = st.meanInner();
  m["min_inner_iterations"] = rc.min_inner_iterations;
  m["max_inner_iterations"] = rc.max_inner_iterations;
  m["observed_min_inner_iterations"] = st.min_inner;
  m["observed_max_inner_iterations"] = st.max_inner;
  m["inner_residual_reduction_target"] = rc.inner_residual_reduction_target;
  m["inner_target_misses"] = st.target_misses;
  m["inner_target_converged_fraction"] = st.convergedFraction();
  m["last_inner_residual_ratio"] = st.last_ratio;

  m["start_time_utc"] = start_time_utc;
  m["end_time_utc"] = end_time_utc;
  m["completed"] = outcome.completed;
  m["convergence_status"] = outcome.convergence_status;

  // Extra, non-contract fields that make the run auditable.
  json extra;
  extra["physics_mode"] = input_.physics.mode == PhysicsMode::kLaminar ? "laminar" : "inviscid";
  extra["reynolds"] = input_.physics.reynolds;
  extra["reference_viscosity"] = context_.flow().transport.referenceViscosity();
  extra["freestream_mach"] = input_.freestream.mach;
  extra["cfl_initial"] = rc.cfl_initial;
  extra["cfl_max"] = rc.cfl_max;
  extra["pseudo_cfl_ramp_steps"] = rc.pseudo_cfl_ramp_steps;
  extra["max_steps_requested"] = rc.max_steps;
  extra["time_step"] = rc.time_step;
  extra["final_time_requested"] = rc.final_time;
  extra["residual_reduction_orders"] = outcome.residual_reduction_orders;
  extra["initial_residual_l2"] = outcome.initial_residual;
  extra["final_residual_l2"] = outcome.final_residual;
  extra["positivity_fallback_events"] = outcome.positivity_fallbacks;
  extra["halo_exchange_count"] = context_.halo().numExchanges();
  extra["halo_doubles_sent"] = context_.halo().numDoublesSent();
  extra["boundary_families"] = json::array();
  for (std::size_t i = 0; i < mesh.boundaryNames().size(); ++i) {
    json b;
    b["name"] = mesh.boundaryNames()[i];
    b["type"] = bcTypeName(mesh.boundaryTypes()[i]);
    extra["boundary_families"].push_back(b);
  }
  m["run_details"] = extra;

  std::ofstream os(joinPath(output_dir_, "metadata.json"), std::ios::trunc);
  if (!os.good()) throw CnsError("cannot open metadata.json in " + output_dir_);
  os << m.dump(2) << '\n';
}

void OutputWriter::writeRunStatus(const RunOutcome &outcome, Real wall_time_seconds) {
  if (!is_root_) return;
  json s;
  s["case_id"] = input_.case_id;
  s["command"] = options_.command_line;
  s["mpi_ranks"] = context_.size();
  s["wall_time_seconds"] = wall_time_seconds;
  s["final_step"] = outcome.final_step;
  s["final_physical_time"] = outcome.final_physical_time;
  s["convergence_status"] = outcome.convergence_status;
  s["residual_reduction_orders"] = outcome.residual_reduction_orders;
  s["notes"] = outcome.notes;

  std::ofstream os(joinPath(output_dir_, "run_status.json"), std::ios::trunc);
  if (!os.good()) throw CnsError("cannot open run_status.json in " + output_dir_);
  os << s.dump(2) << '\n';
}

}  // namespace cns2d
