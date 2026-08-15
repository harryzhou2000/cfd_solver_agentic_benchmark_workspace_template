#include "output.h"

#include <nlohmann/json.hpp>
#include <filesystem>

namespace cfd {
namespace {

void write_csv_header(std::ofstream& f, const std::vector<std::string>& cols) {
  for (size_t i = 0; i < cols.size(); ++i) {
    if (i) f << ",";
    f << cols[i];
  }
  f << "\n";
}

}  // namespace

bool open_outputs(const CaseConfig& cfg, const std::string& outdir, Outputs& out,
                  std::string& err, bool append) {
  std::error_code ec;
  std::filesystem::create_directories(outdir, ec);
  out.outdir = outdir;
  std::ios::openmode mode = append ? (std::ios::out | std::ios::app)
                                   : (std::ios::out | std::ios::trunc);
  out.residuals_f.open(outdir + "/residuals.csv", mode);
  out.forces_f.open(outdir + "/forces.csv", mode);
  if (!out.residuals_f.is_open() || !out.forces_f.is_open()) {
    err = "cannot open output CSV files in " + outdir;
    return false;
  }
  if (!append) {
    write_csv_header(out.residuals_f,
                     {"step", "physical_time", "inner_iter", "cfl", "dt", "rho",
                      "rhou", "rhov", "rhoE", "residual_l2", "residual_linf"});
    write_csv_header(out.forces_f,
                     {"step", "physical_time", "cl", "cd", "cmz", "pressure_drag",
                      "viscous_drag", "pressure_lift", "viscous_lift"});
  }
  out.csv_open = true;
  return true;
}

void append_residual_row(Outputs& out, int step, double phys_time, int inner,
                         double cfl, double dt, const double res_comp[4],
                         double l2, double linf) {
  char buf[512];
  snprintf(buf, sizeof(buf),
           "%d,%.8e,%d,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e\n",
           step, phys_time, inner, cfl, dt, res_comp[0], res_comp[1],
           res_comp[2], res_comp[3], l2, linf);
  out.residuals_f << buf;
  out.residuals_f.flush();
}

void append_force_row(Outputs& out, int step, double phys_time, double cl, double cd,
                      double cmz, double pd, double vd, double pl, double vl) {
  char buf[512];
  snprintf(buf, sizeof(buf),
           "%d,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e\n",
           step, phys_time, cl, cd, cmz, pd, vd, pl, vl);
  out.forces_f << buf;
  out.forces_f.flush();
}

void close_outputs(Outputs& out) {
  if (out.residuals_f.is_open()) out.residuals_f.close();
  if (out.forces_f.is_open()) out.forces_f.close();
  out.csv_open = false;
}

bool write_field_vtu(const CaseConfig& cfg, const Mesh& mesh,
                     const std::vector<double>& owned_data, int ncomp, int rank,
                     const std::string& filename, std::string& err) {
  if (rank != 0) return true;
  FILE* f = fopen(filename.c_str(), "w");
  if (!f) {
    err = "cannot open field file " + filename;
    return false;
  }
  int nc = mesh.num_cells_global;
  int nn = mesh.num_nodes_global;
  fprintf(f, "<?xml version=\"1.0\"?>\n");
  fprintf(f, "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n");
  fprintf(f, "  <UnstructuredGrid>\n");
  fprintf(f, "    <Piece NumberOfPoints=\"%d\" NumberOfCells=\"%d\">\n", nn, nc);
  fprintf(f, "      <Points>\n");
  fprintf(f, "        <DataArray type=\"Float64\" Name=\"Points\" NumberOfComponents=\"3\" format=\"ascii\">\n");
  for (const Vec2& p : mesh.nodes) {
    fprintf(f, "%.12e %.12e 0.0\n", p.x, p.y);
  }
  fprintf(f, "        </DataArray>\n");
  fprintf(f, "      </Points>\n");
  fprintf(f, "      <Cells>\n");
  fprintf(f, "        <DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n");
  for (const Cell& c : mesh.cells) {
    for (int k = 0; k < c.nverts; ++k) fprintf(f, "%d ", c.nodes[k]);
    fprintf(f, "\n");
  }
  fprintf(f, "        </DataArray>\n");
  fprintf(f, "        <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n");
  long long off = 0;
  for (const Cell& c : mesh.cells) {
    off += c.nverts;
    fprintf(f, "%lld\n", off);
  }
  fprintf(f, "        </DataArray>\n");
  fprintf(f, "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n");
  for (const Cell& c : mesh.cells) {
    fprintf(f, "%d\n", c.nverts == 3 ? 5 : 9);
  }
  fprintf(f, "        </DataArray>\n");
  fprintf(f, "      </Cells>\n");
  fprintf(f, "      <CellData Scalars=\"density\">\n");
  const char* names[] = {"density", "velocity", "pressure", "mach",
                         "total_energy", "temperature", "rank", "vorticity"};
  int nv = (ncomp >= 9) ? 8 : 7;
  for (int v = 0; v < nv; ++v) {
    int vcomp = (v == 1) ? 3 : 1;
    fprintf(f, "        <DataArray type=\"Float64\" Name=\"%s\" NumberOfComponents=\"%d\" format=\"ascii\">\n",
            names[v], vcomp);
    for (int c = 0; c < nc; ++c) {
      const double* d = &owned_data[c * ncomp];
      if (v == 0) fprintf(f, "%.10e\n", d[0]);
      else if (v == 1) fprintf(f, "%.10e %.10e 0.0\n", d[1], d[2]);
      else if (v == 2) fprintf(f, "%.10e\n", d[3]);
      else if (v == 3) fprintf(f, "%.10e\n", d[4]);
      else if (v == 4) fprintf(f, "%.10e\n", d[5]);
      else if (v == 5) fprintf(f, "%.10e\n", d[6]);
      else if (v == 6) fprintf(f, "%.10e\n", d[7]);
      else fprintf(f, "%.10e\n", d[8]);
    }
    fprintf(f, "        </DataArray>\n");
  }
  fprintf(f, "      </CellData>\n");
  fprintf(f, "    </Piece>\n");
  fprintf(f, "  </UnstructuredGrid>\n");
  fprintf(f, "</VTKFile>\n");
  fclose(f);
  return true;
}

bool write_surface_csv(const std::vector<SurfaceRow>& rows, const std::string& outdir,
                       std::string& err) {
  std::ofstream f(outdir + "/surface.csv", std::ios::out | std::ios::trunc);
  if (!f.is_open()) {
    err = "cannot open surface.csv in " + outdir;
    return false;
  }
  f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
  for (const SurfaceRow& r : rows) {
    f << std::scientific << std::setprecision(10)
      << r.x << "," << r.y << "," << r.nx << "," << r.ny << "," << r.pressure
      << "," << r.cp << "," << r.cf << "," << r.rho << "," << r.u << "," << r.v
      << "," << r.mach << "," << r.tag << "\n";
  }
  f.close();
  return true;
}

bool write_restart(const std::string& path, const std::string& case_id, int step,
                   double phys_time, int nstates, const std::vector<double>& states,
                   std::string& err) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) {
    err = "cannot open restart file " + path;
    return false;
  }
  const char magic[8] = {'C', 'F', 'D', 'S', 'O', 'L', 'R', 'S'};
  fwrite(magic, 1, 8, f);
  int32_t version = 1;
  fwrite(&version, sizeof(version), 1, f);
  char cid[128] = {0};
  snprintf(cid, sizeof(cid), "%s", case_id.c_str());
  fwrite(cid, 1, 128, f);
  int32_t ncells = (int32_t)(states.size() / (size_t)(nstates * 4));
  fwrite(&ncells, sizeof(ncells), 1, f);
  fwrite(&nstates, sizeof(nstates), 1, f);
  fwrite(&step, sizeof(step), 1, f);
  fwrite(&phys_time, sizeof(phys_time), 1, f);
  fwrite(states.data(), sizeof(double), states.size(), f);
  fclose(f);
  return true;
}

bool read_restart(const std::string& path, std::string& case_id, int& step,
                  double& phys_time, int& nstates, std::vector<double>& states,
                  std::string& err) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    err = "cannot open restart file " + path;
    return false;
  }
  char magic[8] = {0};
  if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "CFDSOLRS", 8) != 0) {
    fclose(f);
    err = "restart file has invalid magic: " + path;
    return false;
  }
  int32_t version = 0;
  fread(&version, sizeof(version), 1, f);
  if (version != 1) {
    fclose(f);
    err = "restart file has unsupported version";
    return false;
  }
  char cid[128] = {0};
  fread(cid, 1, 128, f);
  case_id = cid;
  int32_t ncells = 0;
  fread(&ncells, sizeof(ncells), 1, f);
  fread(&nstates, sizeof(nstates), 1, f);
  fread(&step, sizeof(step), 1, f);
  fread(&phys_time, sizeof(phys_time), 1, f);
  states.resize((size_t)ncells * (size_t)nstates * 4);
  size_t got = fread(states.data(), sizeof(double), states.size(), f);
  fclose(f);
  if (got != states.size()) {
    err = "restart file truncated: " + path;
    return false;
  }
  return true;
}

bool write_metadata_json(const CaseConfig& cfg, const LocalMesh& lm,
                         const RunStats& stats, const std::string& outdir,
                         const std::string& solver_version, double wall_time,
                         const std::string& start_utc, const std::string& end_utc,
                         bool completed, const std::string& git_revision,
                         std::string& err) {
  nlohmann::json j;
  j["case_id"] = cfg.case_id;
  j["solver_name"] = "cfd_solver";
  j["solver_version"] = solver_version;
  j["git_revision"] = git_revision.empty() ? nullptr : git_revision;
  j["mpi_ranks"] = lm.nranks;
  j["mesh_file"] = cfg.mesh_file;
  j["num_cells_global"] = lm.num_cells_global;
  j["num_faces_global"] = lm.num_faces_global;
  j["num_cells_owned_local"] = lm.nowned;
  j["num_cells_ghost_local"] = lm.nghost;
  j["partitioner"] = "metis_kway";
  j["partition_edge_cut"] = lm.edgecut;
  j["halo_exchange"] = "neighbor_isend_irecv";
  j["full_state_replication_during_iterations"] = false;
  j["full_mesh_replication_during_iterations"] = false;
  j["equation_set"] = "compressible_navier_stokes_2d";
  j["inviscid_flux"] = "roe_harten_yee";
  j["entropy_fix"] = "harten_yee";
  j["viscous_flux"] = cfg.is_viscous() ? "green_gauss_lsq_gradient_average" : "disabled";
  j["time_integrator"] = cfg.is_transient() ? "bdf2" : "implicit_euler_pseudo_time";
  j["implicit_solver"] = "lussgs_matrix_free";
  j["reconstruction"] = "weighted_least_squares_piecewise_linear";
  j["limiter"] = "barth_jespersen";
  j["spatial_order_claimed"] = 2;
  j["positivity_preservation"] = "barth_jespersen_limiter_with_first_order_fallback";
  j["wall_boundary_output_semantics"] = "boundary_value";
  j["true_bdf2_inner_loop"] = cfg.is_transient();
  j["typical_inner_iterations"] = stats.mean_inner;
  j["min_inner_iterations"] = cfg.min_inner_iterations;
  j["max_inner_iterations"] = cfg.max_inner_iterations;
  j["observed_min_inner_iterations"] = stats.min_inner;
  j["observed_max_inner_iterations"] = stats.max_inner;
  j["inner_residual_reduction_target"] = cfg.inner_residual_reduction_target;
  j["inner_target_misses"] = stats.target_misses;
  j["inner_target_converged_fraction"] = stats.converged_fraction;
  j["last_inner_residual_ratio"] = stats.last_inner_ratio;
  j["start_time_utc"] = start_utc;
  j["end_time_utc"] = end_utc;
  j["completed"] = completed;
  j["convergence_status"] = stats.convergence_status;
  j["wall_time_seconds"] = wall_time;
  j["residual_reduction_orders"] = stats.residual_reduction_orders;
  j["positivity_fallback_cell_steps"] = stats.positivity_fallbacks;
  j["positivity_limited_updates"] = stats.update_limits;
  std::ofstream f(outdir + "/metadata.json", std::ios::out | std::ios::trunc);
  if (!f.is_open()) {
    err = "cannot open metadata.json in " + outdir;
    return false;
  }
  f << j.dump(2) << "\n";
  f.close();
  return true;
}

bool write_partition_diagnostics(const LocalMesh& lm, const std::string& outdir,
                                 std::string& err) {
  // Every rank formats its row; rank 0 gathers and writes the CSV.
  std::ostringstream ss;
  ss << lm.rank << "," << lm.nowned << "," << lm.nghost << ","
     << (int)lm.boundary_faces.size() << "," << (int)lm.neighbors.size() << ",";
  for (size_t i = 0; i < lm.neighbors.size(); ++i) {
    if (i) ss << ";";
    ss << lm.neighbors[i];
  }
  ss << ",";
  for (size_t i = 0; i < lm.send_cells.size(); ++i) {
    if (i) ss << ";";
    ss << (int)lm.send_cells[i].size();
  }
  ss << ",";
  for (size_t i = 0; i < lm.recv_cells.size(); ++i) {
    if (i) ss << ";";
    ss << (int)lm.recv_cells[i].size();
  }
  std::string row = ss.str();
  std::vector<int> counts(lm.nranks), disp(lm.nranks);
  int mylen = (int)row.size();
  MPI_Gather(&mylen, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  int total = 0;
  if (lm.rank == 0) {
    for (int q = 0; q < lm.nranks; ++q) {
      disp[q] = total;
      total += counts[q];
    }
  }
  std::vector<char> all(total);
  MPI_Gatherv(row.data(), mylen, MPI_CHAR, all.data(), counts.data(), disp.data(),
              MPI_CHAR, 0, MPI_COMM_WORLD);
  if (lm.rank == 0) {
    std::ofstream f(outdir + "/partition_diagnostics.csv", std::ios::out | std::ios::trunc);
    if (!f.is_open()) {
      err = "cannot open partition_diagnostics.csv in " + outdir;
      return false;
    }
    f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
    for (int q = 0; q < lm.nranks; ++q) {
      f.write(all.data() + disp[q], counts[q]);
      f << "\n";
    }
    f.close();
  }
  return true;
}

bool write_run_status(const CaseConfig& cfg, const RunStats& stats,
                      const std::string& outdir, const std::string& command,
                      int mpi_ranks, double wall_time, std::string& err) {
  nlohmann::json j;
  j["case_id"] = cfg.case_id;
  j["command"] = command;
  j["mpi_ranks"] = mpi_ranks;
  j["wall_time_seconds"] = wall_time;
  j["final_step"] = stats.steps_done;
  j["final_physical_time"] = cfg.is_transient() ? stats.steps_done * cfg.time_step : 0.0;
  j["convergence_status"] = stats.convergence_status;
  j["residual_reduction_orders"] = stats.residual_reduction_orders;
  j["notes"] = stats.notes;
  std::ofstream f(outdir + "/run_status.json", std::ios::out | std::ios::trunc);
  if (!f.is_open()) {
    err = "cannot open run_status.json in " + outdir;
    return false;
  }
  f << j.dump(2) << "\n";
  f.close();
  return true;
}

}  // namespace cfd
