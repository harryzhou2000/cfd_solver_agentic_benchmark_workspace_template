#include "output/output_writer.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "mesh/mesh.hpp"
#include "physics/gas_model.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace cfd {

namespace {

// Creates the output directory if needed and returns the full path of a
// file inside it.
std::string out_path(const std::string& output_dir, const std::string& name) {
  if (!output_dir.empty()) {
    std::error_code ec;
    fs::create_directories(output_dir, ec);
  }
  return (fs::path(output_dir) / name).string();
}

void ensure_file(const std::string& path, const std::string& header) {
  std::ofstream f(path, std::ios::trunc);
  f << header << "\n";
}

void append_line(const std::string& path, const std::string& line) {
  std::ofstream f(path, std::ios::app);
  f << line << "\n";
}

}  // namespace

// ---------------------------------------------------------------------------
// residuals.csv / forces.csv
// ---------------------------------------------------------------------------

void write_residual_header(const std::string& output_dir) {
  if (output_dir.empty()) return;
  ensure_file(out_path(output_dir, "residuals.csv"),
              "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,"
              "residual_l2,residual_linf");
}

void write_residual_row(const std::string& output_dir, int step,
                        double physical_time, int inner_iter, double cfl,
                        double dt, double res_rho, double res_rhou,
                        double res_rhov, double res_rhoE, double res_l2,
                        double res_linf) {
  if (output_dir.empty()) return;
  append_line(out_path(output_dir, "residuals.csv"),
              fmt::format("{},{:.6e},{},{:.6e},{:.6e},{:.6e},{:.6e},{:.6e},"
                          "{:.6e},{:.6e},{:.6e}",
                          step, physical_time, inner_iter, cfl, dt, res_rho,
                          res_rhou, res_rhov, res_rhoE, res_l2, res_linf));
}

void write_forces_header(const std::string& output_dir) {
  if (output_dir.empty()) return;
  ensure_file(out_path(output_dir, "forces.csv"),
              "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,"
              "pressure_lift,viscous_lift");
}

void write_forces_row(const std::string& output_dir, int step,
                      double physical_time, double cl, double cd, double cmz,
                      double pressure_drag, double viscous_drag,
                      double pressure_lift, double viscous_lift) {
  if (output_dir.empty()) return;
  append_line(out_path(output_dir, "forces.csv"),
              fmt::format("{},{:.6e},{:.6e},{:.6e},{:.6e},{:.6e},{:.6e},"
                          "{:.6e},{:.6e}",
                          step, physical_time, cl, cd, cmz, pressure_drag,
                          viscous_drag, pressure_lift, viscous_lift));
}

// ---------------------------------------------------------------------------
// surface.csv
// ---------------------------------------------------------------------------

void write_surface(const std::string& output_dir, const std::vector<double>& U,
                   const DistributedMesh& dmesh, const CaseConfig& cfg,
                   MPI_Comm comm) {
  if (output_dir.empty()) return;
  int rank = 0, nranks = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nranks);

  const double gamma = cfg.gas.gamma;
  const double R = cfg.gas.R;
  const double q_inf = 0.5 * cfg.freestream.rho *
                       cfg.freestream.velocity_magnitude *
                       cfg.freestream.velocity_magnitude;
  const PrimitiveState inf = freestream_primitive(cfg);
  double mu = 0.0;
  if (cfg.physics.mode == "laminar" && cfg.physics.reynolds.has_value())
    mu = cfg.freestream.rho * cfg.freestream.velocity_magnitude *
         cfg.reference.reynolds_length / *cfg.physics.reynolds;

  // Collect this rank's wall-face rows (CSV text lines).
  std::vector<std::string> rows;
  for (const Face2D& f : dmesh.boundary_faces) {
    if (f.bc_type != BCType::SlipWall &&
        f.bc_type != BCType::NoSlipAdiabaticWall)
      continue;
    const int owner = f.left_cell >= 0 ? f.left_cell : f.right_cell;
    const PrimitiveState prim = cons_to_prim(
        U.data() + static_cast<std::size_t>(owner) * NVARS, gamma, R);
    const Vector3& cc = dmesh.cells[static_cast<std::size_t>(owner)].cell_center;

    // Boundary-value wall state.
    double u_w = 0.0, v_w = 0.0, rho_w = prim.rho, p_w = prim.p;
    if (f.bc_type == BCType::SlipWall) {
      // Slip wall: zero normal velocity, tangential velocity preserved
      // (boundary value, not the reflected flux state).
      const double vn = prim.u * f.normal.x + prim.v * f.normal.y;
      u_w = prim.u - vn * f.normal.x;
      v_w = prim.v - vn * f.normal.y;
    }
    const double V_w = std::sqrt(u_w * u_w + v_w * v_w);
    const double a_w = speed_of_sound(p_w, rho_w, gamma);
    const double cp = (p_w - inf.p) / q_inf;
    // Skin friction: tangential wall shear for no-slip walls, zero for
    // slip walls (no viscous wall force).
    const double tx = -f.normal.y;
    const double ty = f.normal.x;
    const double V_tang = prim.u * tx + prim.v * ty;
    const double dist = (f.center - cc).norm();
    const double tau_w =
        (f.bc_type == BCType::NoSlipAdiabaticWall && mu > 0.0 && dist > 0.0)
            ? mu * std::fabs(V_tang) / dist
            : 0.0;
    const double cf = tau_w / q_inf;

    rows.push_back(fmt::format(
        "{:.8e},{:.8e},{:.8e},{:.8e},{:.8e},{:.8e},{:.8e},{:.8e},{:.8e},"
        "{:.8e},{:.8e},{}",
        f.center.x, f.center.y, f.normal.x, f.normal.y, p_w, cp, cf, rho_w,
        u_w, v_w, V_w / a_w, f.bc_tag.empty() ? "wall" : f.bc_tag));
  }

  // Gather all rows to rank 0 (one string per rank; sizes first).
  std::string local = "";
  for (const auto& r : rows) {
    local += r;
    local += '\n';
  }
  std::vector<int> sizes(static_cast<std::size_t>(nranks));
  const int local_size = static_cast<int>(local.size());
  MPI_Gather(&local_size, 1, MPI_INT, sizes.data(), 1, MPI_INT, 0, comm);
  std::vector<int> displs(static_cast<std::size_t>(nranks), 0);
  int total = 0;
  for (int r = 0; r < nranks; ++r) {
    displs[static_cast<std::size_t>(r)] = total;
    total += sizes[static_cast<std::size_t>(r)];
  }
  std::string all(static_cast<std::size_t>(total), '\0');
  MPI_Gatherv(local.data(), local_size, MPI_CHAR, all.data(), sizes.data(),
              displs.data(), MPI_CHAR, 0, comm);

  if (rank != 0) return;
  std::ofstream f(out_path(output_dir, "surface.csv"), std::ios::trunc);
  f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
  f << all;
}

// ---------------------------------------------------------------------------
// Field files (VTU / PVTU)
// ---------------------------------------------------------------------------

void write_field_vtu(const std::string& output_dir, const std::string& stem,
                     const DistributedMesh& dmesh,
                     const std::vector<double>& U, const CaseConfig& cfg,
                     MPI_Comm comm) {
  if (output_dir.empty()) return;
  int rank = 0, nranks = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nranks);
  if (dmesh.vertices.empty()) return;  // no vertex coordinates available

  const double kGamma = cfg.gas.gamma;
  const double R = cfg.gas.R;

  // Local vertex table for the OWNED cells: global vertex id -> local id.
  std::map<long long, int> vmap;
  std::vector<Vector3> vcoords;
  auto local_vertex = [&](long long gv) {
    auto it = vmap.find(gv);
    if (it != vmap.end()) return it->second;
    const int id = static_cast<int>(vcoords.size());
    vmap.emplace(gv, id);
    vcoords.push_back(dmesh.vertices[static_cast<std::size_t>(gv)]);
    return id;
  };

  std::vector<long long> connectivity;
  std::vector<int> offsets;
  std::vector<int> types;
  std::vector<double> rho, velx, vely, velmag, pres, mach, temp, energy;
  std::vector<int> ranks;
  connectivity.reserve(static_cast<std::size_t>(dmesh.n_owned) * 4);
  for (long long c = 0; c < dmesh.n_owned; ++c) {
    const Cell2D& cell = dmesh.cells[static_cast<std::size_t>(c)];
    const int nv = static_cast<int>(cell.vertex_indices.size());
    for (int k = 0; k < nv; ++k)
      connectivity.push_back(local_vertex(cell.vertex_indices[static_cast<std::size_t>(k)]));
    const int off = offsets.empty() ? nv : offsets.back() + nv;
    offsets.push_back(off);
    types.push_back(cell.type == CellType::Triangle ? 5 : 9);

    const PrimitiveState p = cons_to_prim(
        U.data() + static_cast<std::size_t>(c) * NVARS, kGamma, R);
    const double v2 = p.u * p.u + p.v * p.v;
    (void)R;  // T comes from cons_to_prim above
    rho.push_back(p.rho);
    velx.push_back(p.u);
    vely.push_back(p.v);
    velmag.push_back(std::sqrt(v2));
    pres.push_back(p.p);
    mach.push_back(std::sqrt(v2) / p.a);
    temp.push_back(p.T);
    energy.push_back(U[static_cast<std::size_t>(c) * NVARS + 3]);
    ranks.push_back(rank);
  }

  const std::string piece =
      fmt::format("{}_rank{}.vtu", stem, rank);
  std::ofstream f(out_path(output_dir, piece), std::ios::trunc);
  f << "<?xml version=\"1.0\"?>\n";
  f << "<VTKFile type=\"UnstructuredGrid\" version=\"1.0\" "
       "byte_order=\"LittleEndian\">\n";
  f << "  <UnstructuredGrid>\n";
  f << fmt::format("    <Piece NumberOfPoints=\"{}\" NumberOfCells=\"{}\">\n",
                   vcoords.size(), offsets.size());
  // Points
  f << "      <Points>\n";
  f << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" "
       "format=\"ascii\">\n          ";
  for (const Vector3& v : vcoords)
    f << fmt::format("{:.10e} {:.10e} 0.0 ", v.x, v.y);
  f << "\n        </DataArray>\n      </Points>\n";
  // Cells
  f << "      <Cells>\n";
  f << "        <DataArray type=\"Int64\" Name=\"connectivity\" "
       "format=\"ascii\">\n          ";
  for (long long v : connectivity) f << v << " ";
  f << "\n        </DataArray>\n";
  f << "        <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">"
       "\n          ";
  for (int o : offsets) f << o << " ";
  f << "\n        </DataArray>\n";
  f << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">"
       "\n          ";
  for (int t : types) f << t << " ";
  f << "\n        </DataArray>\n      </Cells>\n";
  // CellData
  f << "      <CellData>\n";
  auto cell_array = [&](const char* name, const char* type,
                        const std::vector<double>& data) {
    f << fmt::format("        <DataArray type=\"{}\" Name=\"{}\" "
                     "format=\"ascii\">\n          ",
                     type, name);
    for (double v : data) f << fmt::format("{:.10e} ", v);
    f << "\n        </DataArray>\n";
  };
  cell_array("density", "Float64", rho);
  cell_array("velocity_x", "Float64", velx);
  cell_array("velocity_y", "Float64", vely);
  cell_array("velocity_magnitude", "Float64", velmag);
  cell_array("pressure", "Float64", pres);
  cell_array("mach", "Float64", mach);
  cell_array("temperature", "Float64", temp);
  cell_array("energy", "Float64", energy);
  f << "        <DataArray type=\"Int32\" Name=\"rank\" format=\"ascii\">"
       "\n          ";
  for (int r : ranks) f << r << " ";
  f << "\n        </DataArray>\n";
  f << "      </CellData>\n";
  f << "    </Piece>\n";
  f << "  </UnstructuredGrid>\n";
  f << "</VTKFile>\n";

  // Master .pvtu written by rank 0 after all pieces exist.
  MPI_Barrier(comm);
  if (rank == 0) {
    std::ofstream p(out_path(output_dir, stem + ".pvtu"), std::ios::trunc);
    p << "<?xml version=\"1.0\"?>\n";
    p << "<VTKFile type=\"PUnstructuredGrid\" version=\"1.0\">\n";
    p << "  <PUnstructuredGrid GhostLevel=\"0\">\n";
    p << "    <PCellData>\n";
    for (const char* n : {"density", "velocity_x", "velocity_y",
                          "velocity_magnitude", "pressure", "mach",
                          "temperature", "energy"})
      p << fmt::format(
            "      <PDataArray type=\"Float64\" Name=\"{}\"/>\n", n);
    p << "      <PDataArray type=\"Int32\" Name=\"rank\"/>\n";
    p << "    </PCellData>\n";
    p << "    <PPoints>\n";
    p << "      <PDataArray type=\"Float64\" NumberOfComponents=\"3\"/>\n";
    p << "    </PPoints>\n";
    for (int r = 0; r < nranks; ++r)
      p << fmt::format("    <Piece Source=\"{}\"/>\n",
                       fmt::format("{}_rank{}.vtu", stem, r));
    p << "  </PUnstructuredGrid>\n";
    p << "</VTKFile>\n";
  }
}

void write_field_final(const std::string& output_dir,
                       const DistributedMesh& dmesh,
                       const std::vector<double>& U, const CaseConfig& cfg,
                       MPI_Comm comm) {
  write_field_vtu(output_dir, "field_final", dmesh, U, cfg, comm);
}

// ---------------------------------------------------------------------------
// metadata.json
// ---------------------------------------------------------------------------

namespace {

// Fills the metadata fields common to steady and transient runs.
json metadata_base(const DistributedMesh& dmesh, const CaseConfig& cfg,
                   const std::string& solver_name,
                   const std::string& solver_version,
                   const std::string& git_revision,
                   const std::string& started_utc,
                   const std::string& convergence_status, bool completed,
                   bool transient, double typical_inner,
                   long long inner_iterations_total, int steps_run,
                   int observed_min_inner, int observed_max_inner,
                   long long inner_target_misses,
                   double inner_target_converged_fraction,
                   double last_inner_residual_ratio) {
  json j;
  j["case_id"] = cfg.case_id;
  j["solver_name"] = solver_name;
  j["solver_version"] = solver_version;
  j["git_revision"] = git_revision.empty() ? json(nullptr) : json(git_revision);
  j["mpi_ranks"] = dmesh.nranks;
  j["mesh_file"] = cfg.mesh.file;
  j["num_cells_global"] = dmesh.info.n_owned;  // rank-0 owned == serial when
                                               // gathered; corrected below
  j["num_faces_global"] = 0;
  j["num_cells_owned_local"] = dmesh.info.n_owned;
  j["num_cells_ghost_local"] = dmesh.info.n_ghost;
  j["partitioner"] = "metis_kway";
  j["partition_edge_cut"] = dmesh.info.edge_cut;
  j["halo_exchange"] = "neighbor_isend_irecv";
  j["full_state_replication_during_iterations"] = false;
  j["full_mesh_replication_during_iterations"] = false;
  j["equation_set"] = "compressible_navier_stokes_2d";
  j["inviscid_flux"] = "rusanov_llf";
  j["entropy_fix"] = nullptr;
  j["viscous_flux"] =
      (cfg.physics.mode == "laminar") ? "gradient_based" : "disabled";
  j["time_integrator"] =
      transient ? "bdf2" : "pseudo_time_steady";
  j["implicit_solver"] = "lusgs_block";
  j["reconstruction"] = "least_squares";
  j["limiter"] = "barth_jespersen";
  j["spatial_order_claimed"] = 2;
  j["positivity_preservation"] = "reconstruction_fallback";
  j["wall_boundary_output_semantics"] = "boundary_value";
  j["true_bdf2_inner_loop"] = transient;
  j["typical_inner_iterations"] = typical_inner;
  j["min_inner_iterations"] =
      cfg.run_control.min_inner_iterations.value_or(0);
  j["max_inner_iterations"] =
      cfg.run_control.max_inner_iterations.value_or(0);
  j["observed_min_inner_iterations"] = observed_min_inner;
  j["observed_max_inner_iterations"] = observed_max_inner;
  j["inner_residual_reduction_target"] =
      cfg.run_control.inner_residual_reduction_target.value_or(0.0);
  j["inner_target_misses"] = inner_target_misses;
  j["inner_target_converged_fraction"] = inner_target_converged_fraction;
  j["last_inner_residual_ratio"] = last_inner_residual_ratio;
  j["start_time_utc"] = started_utc;
  j["end_time_utc"] = utc_now_iso8601();
  j["completed"] = completed;
  j["convergence_status"] = convergence_status;
  j["inner_iterations_total"] = inner_iterations_total;
  j["steps_run"] = steps_run;
  return j;
}

// Computes the global counts via an MPI reduction of the rank-local
// diagnostics (metadata is written by rank 0 only).
void fix_global_counts(json& j, const DistributedMesh& dmesh, MPI_Comm comm) {
  long long owned = dmesh.info.n_owned;
  long long faces = dmesh.info.n_boundary_faces +
                    dmesh.info.n_interior_faces + dmesh.info.n_send_faces +
                    dmesh.info.n_recv_faces;
  long long mpifaces =
      dmesh.info.n_send_faces + dmesh.info.n_recv_faces;
  long long owned_g = 0, faces_g = 0, mpifaces_g = 0;
  MPI_Allreduce(&owned, &owned_g, 1, MPI_LONG_LONG, MPI_SUM, comm);
  MPI_Allreduce(&faces, &faces_g, 1, MPI_LONG_LONG, MPI_SUM, comm);
  MPI_Allreduce(&mpifaces, &mpifaces_g, 1, MPI_LONG_LONG, MPI_SUM, comm);
  // MPI faces are stored on two ranks; count them once. Cross-rank faces
  // (edge cut) are then added back since each is a real global face.
  j["num_cells_global"] = owned_g;
  j["num_faces_global"] = faces_g - mpifaces_g + dmesh.info.edge_cut;
}

}  // namespace

void write_metadata(const std::string& output_dir, const DistributedMesh& dmesh,
                    const CaseConfig& cfg, const SteadyResult& result,
                    const std::string& solver_name,
                    const std::string& solver_version,
                    const std::string& git_revision,
                    const std::string& started_utc) {
  if (output_dir.empty()) return;
  const double typical =
      result.steps_run > 0
          ? static_cast<double>(result.inner_iterations_total) /
                static_cast<double>(result.steps_run)
          : 0.0;
  json j = metadata_base(
      dmesh, cfg, solver_name, solver_version, git_revision, started_utc,
      result.convergence_status,
      /*completed=*/result.convergence_status == "converged",
      /*transient=*/false, typical, result.inner_iterations_total,
      result.steps_run, result.observed_min_inner_iterations,
      result.observed_max_inner_iterations, result.inner_target_misses,
      result.inner_target_converged_fraction, result.last_inner_residual_ratio);
  fix_global_counts(j, dmesh, MPI_COMM_WORLD);
  j["final_residual_l2"] = result.final_residual_l2;
  j["final_residual_linf"] = result.final_residual_linf;
  j["residual_reduction_orders"] = result.residual_reduction_orders;
  j["final_cfl"] = result.final_cfl;
  std::ofstream f(out_path(output_dir, "metadata.json"), std::ios::trunc);
  f << j.dump(2) << "\n";
}

void write_metadata(const std::string& output_dir, const DistributedMesh& dmesh,
                    const CaseConfig& cfg, const TransientResult& result,
                    const std::string& solver_name,
                    const std::string& solver_version,
                    const std::string& git_revision,
                    const std::string& started_utc) {
  if (output_dir.empty()) return;
  const double typical =
      result.steps_run > 0
          ? static_cast<double>(result.inner_iterations_total) /
                static_cast<double>(result.steps_run)
          : 0.0;
  json j = metadata_base(
      dmesh, cfg, solver_name, solver_version, git_revision, started_utc,
      result.convergence_status,
      /*completed=*/true, /*transient=*/true, typical,
      result.inner_iterations_total, static_cast<int>(result.steps_run),
      result.observed_min_inner_iterations,
      result.observed_max_inner_iterations, result.inner_target_misses,
      result.inner_target_converged_fraction, result.last_inner_residual_ratio);
  fix_global_counts(j, dmesh, MPI_COMM_WORLD);
  j["final_physical_time"] = result.final_time;
  j["time_step"] = cfg.run_control.time_step.value_or(0.0);
  j["final_time"] = cfg.run_control.final_time.value_or(0.0);
  std::ofstream f(out_path(output_dir, "metadata.json"), std::ios::trunc);
  f << j.dump(2) << "\n";
}

// ---------------------------------------------------------------------------
// run_status.json
// ---------------------------------------------------------------------------

void write_run_status(const std::string& output_dir, const CaseConfig& cfg,
                      const std::string& convergence_status, int steps_run,
                      double final_physical_time,
                      double residual_reduction_orders, int mpi_ranks,
                      double wall_time_seconds, const std::string& command) {
  if (output_dir.empty()) return;
  json j;
  j["case_id"] = cfg.case_id;
  j["command"] = command;
  j["mpi_ranks"] = mpi_ranks;
  j["wall_time_seconds"] = wall_time_seconds;
  j["final_step"] = steps_run;
  j["final_physical_time"] = final_physical_time;
  j["convergence_status"] = convergence_status;
  j["residual_reduction_orders"] = residual_reduction_orders;
  j["notes"] = "";
  std::ofstream f(out_path(output_dir, "run_status.json"), std::ios::trunc);
  f << j.dump(2) << "\n";
}

// ---------------------------------------------------------------------------
// partition diagnostics
// ---------------------------------------------------------------------------

void write_partition_diagnostics(const std::string& output_dir,
                                 const DistributedMesh& dmesh,
                                 MPI_Comm comm) {
  if (output_dir.empty()) return;
  int rank = 0, nranks = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nranks);

  std::string nbrs;
  for (std::size_t i = 0; i < dmesh.info.neighbor_ranks.size(); ++i) {
    if (i) nbrs += ";";
    nbrs += std::to_string(dmesh.info.neighbor_ranks[i]);
  }
  std::string send_cells;
  for (std::size_t i = 0; i < dmesh.info.send_counts.size(); ++i) {
    if (i) send_cells += ";";
    send_cells += std::to_string(dmesh.info.send_counts[i]);
  }
  std::string recv_cells;
  for (std::size_t i = 0; i < dmesh.info.recv_counts.size(); ++i) {
    if (i) recv_cells += ";";
    recv_cells += std::to_string(dmesh.info.recv_counts[i]);
  }

  // Fixed-size POD per rank (safe to gather), plus fixed char buffers for
  // the variable-length strings.
  struct Row {
    int rank;
    long long owned, ghost, boundary;
    int nnbrs;
  };
  struct RowBuf {
    char nbrs[512];
    char send[512];
    char recv[512];
  };
  Row local{rank, dmesh.info.n_owned, dmesh.info.n_ghost,
            dmesh.info.n_boundary_faces,
            static_cast<int>(dmesh.info.neighbor_ranks.size())};
  RowBuf local_buf{};
  std::snprintf(local_buf.nbrs, sizeof(local_buf.nbrs), "%s", nbrs.c_str());
  std::snprintf(local_buf.send, sizeof(local_buf.send), "%s",
                send_cells.c_str());
  std::snprintf(local_buf.recv, sizeof(local_buf.recv), "%s",
                recv_cells.c_str());

  std::vector<Row> all;
  std::vector<RowBuf> all_buf;
  if (rank == 0) {
    all.resize(static_cast<std::size_t>(nranks));
    all_buf.resize(static_cast<std::size_t>(nranks));
  }
  MPI_Gather(&local, sizeof(Row), MPI_BYTE, all.data(), sizeof(Row),
             MPI_BYTE, 0, comm);
  MPI_Gather(&local_buf, sizeof(RowBuf), MPI_BYTE, all_buf.data(),
             sizeof(RowBuf), MPI_BYTE, 0, comm);

  if (rank != 0) return;
  std::ofstream f(out_path(output_dir, "partition_diagnostics.csv"),
                  std::ios::trunc);
  f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,"
       "num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
  for (int r = 0; r < nranks; ++r) {
    const Row& row = all[static_cast<std::size_t>(r)];
    const RowBuf& buf = all_buf[static_cast<std::size_t>(r)];
    f << fmt::format("{},{},{},{},{},{},{},{}\n", row.rank, row.owned,
                     row.ghost, row.boundary, row.nnbrs, buf.nbrs, buf.send,
                     buf.recv);
  }

  // JSON variant with global summaries.
  json jj = json::array();
  long long owned_min = all.front().owned, owned_max = 0, owned_sum = 0;
  for (int r = 0; r < nranks; ++r) {
    const Row& row = all[static_cast<std::size_t>(r)];
    const RowBuf& buf = all_buf[static_cast<std::size_t>(r)];
    owned_min = std::min(owned_min, row.owned);
    owned_max = std::max(owned_max, row.owned);
    owned_sum += row.owned;
    jj.push_back({{"rank", row.rank},
                  {"num_cells_owned", row.owned},
                  {"num_cells_ghost", row.ghost},
                  {"num_boundary_faces", row.boundary},
                  {"num_neighbor_ranks", row.nnbrs},
                  {"neighbor_ranks", buf.nbrs},
                  {"send_cells", buf.send},
                  {"recv_cells", buf.recv}});
  }
  const double mean = static_cast<double>(owned_sum) / nranks;
  json js;
  js["per_rank"] = jj;
  js["edge_cut"] = dmesh.info.edge_cut;
  js["owned_min"] = owned_min;
  js["owned_max"] = owned_max;
  js["owned_mean"] = mean;
  js["load_balance_ratio"] = mean > 0.0 ? owned_max / mean : 0.0;
  std::ofstream fj(out_path(output_dir, "partition_diagnostics.json"),
                   std::ios::trunc);
  fj << js.dump(2) << "\n";
}

// ---------------------------------------------------------------------------
// restart
// ---------------------------------------------------------------------------

void write_restart(const std::string& output_dir, const DistributedMesh& dmesh,
                   const std::vector<double>& U, const CaseConfig& cfg,
                   MPI_Comm comm) {
  if (output_dir.empty()) return;
  int rank = 0, nranks = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nranks);

  // Per-rank owned-cell data: (global id, 4 doubles).
  std::vector<long long> gids(static_cast<std::size_t>(dmesh.n_owned));
  std::vector<double> data(static_cast<std::size_t>(dmesh.n_owned) * NVARS);
  for (long long c = 0; c < dmesh.n_owned; ++c) {
    gids[static_cast<std::size_t>(c)] =
        dmesh.global_cell_id[static_cast<std::size_t>(c)];
    for (int k = 0; k < NVARS; ++k)
      data[static_cast<std::size_t>(c) * NVARS + k] =
          U[static_cast<std::size_t>(c) * NVARS + k];
  }

  // Gather counts, then the arrays, to rank 0.
  std::vector<int> counts(static_cast<std::size_t>(nranks));
  const int local_n = static_cast<int>(dmesh.n_owned);
  MPI_Gather(&local_n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
  std::vector<int> displ(static_cast<std::size_t>(nranks), 0);
  long long total = 0;
  for (int r = 0; r < nranks; ++r) {
    displ[static_cast<std::size_t>(r)] = static_cast<int>(total);
    total += counts[static_cast<std::size_t>(r)];
  }
  std::vector<long long> gids_all(static_cast<std::size_t>(total));
  std::vector<double> data_all(static_cast<std::size_t>(total) * NVARS);
  MPI_Gatherv(gids.data(), local_n, MPI_LONG_LONG, gids_all.data(),
              counts.data(), displ.data(), MPI_LONG_LONG, 0, comm);
  // The double buffer is NVARS entries per cell: scale counts and displs.
  std::vector<int> counts_v(static_cast<std::size_t>(nranks));
  std::vector<int> displ_v(static_cast<std::size_t>(nranks), 0);
  for (int r = 0; r < nranks; ++r) {
    counts_v[static_cast<std::size_t>(r)] = counts[static_cast<std::size_t>(r)] * NVARS;
    displ_v[static_cast<std::size_t>(r)] = displ[static_cast<std::size_t>(r)] * NVARS;
  }
  MPI_Gatherv(data.data(), local_n * NVARS, MPI_DOUBLE, data_all.data(),
              counts_v.data(), displ_v.data(), MPI_DOUBLE, 0, comm);

  if (rank != 0) return;
  // Order by global cell id for a canonical restart layout.
  std::vector<std::size_t> order(static_cast<std::size_t>(total));
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(),
            [&](std::size_t a, std::size_t b) { return gids_all[a] < gids_all[b]; });
  json cells = json::array();
  for (std::size_t idx : order) {
    json cj;
    cj["global_id"] = gids_all[idx];
    cj["U"] = {data_all[idx * NVARS + 0], data_all[idx * NVARS + 1],
               data_all[idx * NVARS + 2], data_all[idx * NVARS + 3]};
    cells.push_back(std::move(cj));
  }
  json j;
  j["case_id"] = cfg.case_id;
  j["mesh_file"] = cfg.mesh.file;
  j["mpi_ranks"] = nranks;
  j["num_cells_global"] = total;
  j["nvars"] = NVARS;
  j["cells"] = std::move(cells);
  std::ofstream f(out_path(output_dir, "restart_final.json"), std::ios::trunc);
  f << j.dump() << "\n";
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

std::string utc_now_iso8601() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

}  // namespace cfd
