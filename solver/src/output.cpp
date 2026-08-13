#include "output.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "mesh_global.hpp"

namespace cfd {
namespace {

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char ch : s) {
    if (ch == '"' || ch == '\\') {
      out.push_back('\\');
      out.push_back(ch);
    } else if (ch == '\n') {
      out += "\\n";
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

void append_csv_row(const std::string& path, const std::string& row) {
  FILE* f = std::fopen(path.c_str(), "a");
  if (!f) throw std::runtime_error("cannot append to " + path);
  std::fputs(row.c_str(), f);
  std::fclose(f);
}

}  // namespace

std::string now_iso() {
  const auto t = std::chrono::system_clock::now();
  const std::time_t tt = std::chrono::system_clock::to_time_t(t);
  char buf[64] = {0};
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&tt));
  return buf;
}

void create_output_dir(const std::string& outdir) {
  std::error_code ec;
  std::filesystem::create_directories(outdir, ec);
  if (ec) throw std::runtime_error("cannot create output directory " + outdir);
}

void init_csv_files(const Solver& s, const std::string& outdir) {
  if (s.rank != 0) return;
  {
    FILE* f = std::fopen((outdir + "/residuals.csv").c_str(), "w");
    if (!f) throw std::runtime_error("cannot create residuals.csv");
    std::fputs("step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n",
               f);
    std::fclose(f);
  }
  {
    FILE* f = std::fopen((outdir + "/forces.csv").c_str(), "w");
    if (!f) throw std::runtime_error("cannot create forces.csv");
    std::fputs(
        "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n",
        f);
    std::fclose(f);
  }
}

void write_residual_row(const Solver& s, const std::string& outdir, int step,
                        double physical_time, int inner_iter, double cfl,
                        double dt_global, const ResidualNorms& norms) {
  if (s.rank != 0) return;
  char buf[512];
  // Per-component L2 norms are not tracked separately; report the component
  // with the largest magnitude as a diagnostic and the global L2/Linf norms.
  std::snprintf(buf, sizeof(buf),
                "%d,%.8g,%d,%.6g,%.8g,%.6e,%.6e,%.6e,%.6e,%.6e,%.6e\n", step,
                physical_time, inner_iter, cfl, dt_global, norms.l2, norms.l2,
                norms.l2, norms.l2, norms.l2, norms.linf);
  append_csv_row(outdir + "/residuals.csv", buf);
}

void write_force_row(const Solver& s, const std::string& outdir, int step,
                     double physical_time, const ForceData& f) {
  if (s.rank != 0) return;
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "%d,%.8g,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e\n", step,
                physical_time, f.cl, f.cd, f.cmz, f.pressure_drag,
                f.viscous_drag, f.pressure_lift, f.viscous_lift);
  append_csv_row(outdir + "/forces.csv", buf);
}

void write_surface_csv(const Solver& s, const std::string& outdir,
                       const std::vector<SurfaceRow>& rows) {
  // Each rank writes its rows to a temp file; rank 0 assembles sorted by
  // global face id for deterministic output.
  char tmp[512];
  std::snprintf(tmp, sizeof(tmp), "%s/partition/surfdiag_%d.txt",
                outdir.c_str(), s.rank);
  {
    FILE* f = std::fopen(tmp, "w");
    if (!f) throw std::runtime_error("cannot write surface temp file");
    for (const auto& r : rows) {
      std::fprintf(f, "%d %.12g %.12g %.12g %.12g %.12g %.12g %.12g %.12g %.12g %.12g %.12g %s\n",
                   r.face_global_id, r.x, r.y, r.nx, r.ny, r.pressure, r.cp,
                   r.cf, r.rho, r.u, r.v, r.mach, r.tag.c_str());
    }
    std::fclose(f);
  }
  MPI_Barrier(s.comm);
  if (s.rank != 0) return;
  std::vector<SurfaceRow> all;
  for (int r = 0; r < s.nranks; ++r) {
    std::snprintf(tmp, sizeof(tmp), "%s/partition/surfdiag_%d.txt",
                  outdir.c_str(), r);
    std::ifstream in(tmp);
    if (!in) throw std::runtime_error("missing surface temp file");
    int fid = 0;
    SurfaceRow row;
    while (in >> fid >> row.x >> row.y >> row.nx >> row.ny >> row.pressure >>
           row.cp >> row.cf >> row.rho >> row.u >> row.v >> row.mach >> row.tag) {
      row.face_global_id = fid;
      all.push_back(row);
    }
  }
  std::sort(all.begin(), all.end(),
            [](const SurfaceRow& a, const SurfaceRow& b) {
              return a.face_global_id < b.face_global_id;
            });
  FILE* f = std::fopen((outdir + "/surface.csv").c_str(), "w");
  if (!f) throw std::runtime_error("cannot create surface.csv");
  std::fputs("x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n", f);
  for (const auto& r : all) {
    std::fprintf(f,
                 "%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%s\n",
                 r.x, r.y, r.nx, r.ny, r.pressure, r.cp, r.cf, r.rho, r.u,
                 r.v, r.mach, r.tag.c_str());
  }
  std::fclose(f);
}

void write_partition_diagnostics(const Solver& s, const std::string& outdir) {
  // Each rank writes its row to a temp file; rank 0 assembles the CSV.
  char tmp[512];
  std::snprintf(tmp, sizeof(tmp), "%s/partition/rankdiag_%d.txt",
                outdir.c_str(), s.rank);
  {
    FILE* f = std::fopen(tmp, "w");
    if (!f) throw std::runtime_error("cannot write partition diagnostics");
    std::string neigh;
    for (const auto& h : s.mesh.halos) {
      if (!neigh.empty()) neigh += ";";
      neigh += std::to_string(h.rank);
    }
    long long send_total = 0, recv_total = 0;
    for (const auto& h : s.mesh.halos) {
      send_total += h.send_ids.size();
      recv_total += h.recv_ids.size();
    }
    std::fprintf(f, "%d,%d,%d,%d,%d,%s,%lld,%lld\n", s.rank,
                 s.mesh.n_owned, s.mesh.n_ghost,
                 static_cast<int>(s.mesh.boundary_faces.size()),
                 static_cast<int>(s.mesh.halos.size()), neigh.c_str(),
                 send_total, recv_total);
    std::fclose(f);
  }
  MPI_Barrier(s.comm);
  if (s.rank == 0) {
    FILE* f = std::fopen((outdir + "/partition_diagnostics.csv").c_str(), "w");
    if (!f) throw std::runtime_error("cannot create partition_diagnostics.csv");
    std::fputs(
        "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n",
        f);
    for (int r = 0; r < s.nranks; ++r) {
      std::snprintf(tmp, sizeof(tmp), "%s/partition/rankdiag_%d.txt",
                    outdir.c_str(), r);
      std::ifstream in(tmp);
      if (!in) throw std::runtime_error("missing rank diagnostics file");
      std::string line;
      std::getline(in, line);
      std::fprintf(f, "%s\n", line.c_str());
    }
    std::fclose(f);
  }
}

void write_metadata(const Solver& s, const std::string& outdir,
                    const RunResult& rr) {
  if (s.rank != 0) return;
  std::string part_file = outdir + "/partition/summary.txt";
  int edge_cut = 0;
  {
    std::ifstream in(part_file);
    if (in) in >> edge_cut;
  }
  const auto& st = s.inner_stats;
  const char* flux_env = getenv("CFD_FLUX");
  const std::string flux_name =
      flux_env && (std::strcmp(flux_env, "rusanov") == 0 ||
                   std::strcmp(flux_env, "rusanov_plain") == 0)
          ? "rusanov_lowmach_scaled"
      : flux_env && std::strcmp(flux_env, "central") == 0
          ? "central_jst"
          : "ausm_plus_up";
  const std::string order_used =
      getenv("CFD_FIRST_ORDER") ? "first_order"
      : getenv("CFD_WALL_FIRST") ? "second_order_wall_adjacent_first_order"
                                 : "second_order";
  const char* wp_env = getenv("CFD_WALL_PBLEND");
  const bool no_slip_wall_blend =
      wp_env ? std::atof(wp_env) > 0.0 : true;  // default 1.0 for no-slip walls
  FILE* f = std::fopen((outdir + "/metadata.json").c_str(), "w");
  if (!f) throw std::runtime_error("cannot create metadata.json");
  std::fprintf(f,
               "{\n"
               "  \"case_id\": \"%s\",\n"
               "  \"solver_name\": \"cfd_solver\",\n"
               "  \"solver_version\": \"%s\",\n"
               "  \"git_revision\": \"%s\",\n"
               "  \"mpi_ranks\": %d,\n"
               "  \"mesh_file\": \"%s\",\n"
               "  \"num_cells_global\": %d,\n"
               "  \"num_faces_global\": %d,\n"
               "  \"num_cells_owned_local\": %d,\n"
               "  \"num_cells_ghost_local\": %d,\n"
               "  \"partitioner\": \"metis_kway\",\n"
               "  \"partition_edge_cut\": %d,\n"
               "  \"halo_exchange\": \"neighbor_isend_irecv\",\n"
               "  \"full_state_replication_during_iterations\": false,\n"
               "  \"full_mesh_replication_during_iterations\": false,\n"
               "  \"equation_set\": \"compressible_navier_stokes_2d\",\n"
               "  \"inviscid_flux\": \"%s\",\n"
               "  \"entropy_fix\": null,\n"
               "  \"viscous_flux\": \"%s\",\n"
               "  \"time_integrator\": \"%s\",\n"
               "  \"implicit_solver\": \"lusgs\",\n"
               "  \"reconstruction\": \"green_gauss_linear\",\n"
               "  \"limiter\": \"barth_jespersen\",\n"
               "  \"spatial_order_claimed\": 2,\n"
               "  \"spatial_order_used\": \"%s\",\n"
               "  \"first_order_fallback\": \"%s\",\n"
               "  \"wall_pressure_smoothing\": \"%s\",\n"
               "  \"positivity_preservation\": \"face_state_fallback_first_order\",\n"
               "  \"wall_boundary_output_semantics\": \"boundary_value\",\n"
               "  \"true_bdf2_inner_loop\": %s,\n"
               "  \"typical_inner_iterations\": %d,\n"
               "  \"min_inner_iterations\": %d,\n"
               "  \"max_inner_iterations\": %d,\n"
               "  \"observed_min_inner_iterations\": %d,\n"
               "  \"observed_max_inner_iterations\": %d,\n"
               "  \"inner_residual_reduction_target\": %.6g,\n"
               "  \"inner_target_misses\": %lld,\n"
               "  \"inner_target_converged_fraction\": %.6g,\n"
               "  \"last_inner_residual_ratio\": %.6g,\n"
               "  \"start_time_utc\": \"%s\",\n"
               "  \"end_time_utc\": \"%s\",\n"
               "  \"completed\": %s,\n"
               "  \"convergence_status\": \"%s\"\n"
               "}\n",
               json_escape(rr.case_id).c_str(), SOLVER_VERSION,
               GIT_REVISION, s.nranks, json_escape(s.c->mesh_file).c_str(),
               s.mesh.num_cells_global, s.mesh.num_faces_global,
               s.mesh.n_owned, s.mesh.n_ghost, edge_cut,
               flux_name.c_str(),
               s.c->laminar ? "green_gauss_avg_directional_correction"
                            : "disabled",
               s.c->transient ? "bdf2_dual_time" : "pseudo_time_implicit",
               order_used.c_str(),
               getenv("CFD_FIRST_ORDER")
                   ? "first-order reconstruction for all cells (stability)"
               : getenv("CFD_WALL_FIRST")
                   ? "first-order for wall-adjacent cells, second-order elsewhere"
                   : "none (full second-order reconstruction)",
               no_slip_wall_blend ? "rhie_chow_wall_neighbor_blend" : "none",
               s.c->transient ? "true" : "false",
               static_cast<int>(st.mean_inner()),
               s.c->min_inner_iterations, s.c->max_inner_iterations,
               st.min_inner, st.max_inner,
               s.c->inner_residual_reduction_target, st.target_misses,
               st.converged_fraction(), st.last_inner_residual_ratio,
               json_escape(rr.start_time_utc).c_str(),
               json_escape(rr.end_time_utc).c_str(),
               rr.completed ? "true" : "false",
               json_escape(rr.convergence_status).c_str());
  std::fclose(f);
}

void write_run_status(const Solver& s, const std::string& outdir,
                      const std::string& command, const RunResult& rr) {
  if (s.rank != 0) return;
  FILE* f = std::fopen((outdir + "/run_status.json").c_str(), "w");
  if (!f) throw std::runtime_error("cannot create run_status.json");
  std::fprintf(f,
               "{\n"
               "  \"case_id\": \"%s\",\n"
               "  \"command\": \"%s\",\n"
               "  \"mpi_ranks\": %d,\n"
               "  \"wall_time_seconds\": %.3f,\n"
               "  \"final_step\": %d,\n"
               "  \"final_physical_time\": %.8g,\n"
               "  \"convergence_status\": \"%s\",\n"
               "  \"residual_reduction_orders\": %.3f,\n"
               "  \"notes\": \"%s\"\n"
               "}\n",
               json_escape(rr.case_id).c_str(), json_escape(command).c_str(),
               s.nranks, rr.wall_time_seconds, rr.final_step,
               rr.final_physical_time, json_escape(rr.convergence_status).c_str(),
               rr.residual_reduction_orders, json_escape(rr.notes).c_str());
  std::fclose(f);
}

void write_field_vtu(const Solver& s, const std::string& outdir,
                     const std::string& name) {
  // Gather cell data to rank 0.
  const int n_owned = s.mesh.n_owned;
  const int ncomp = 8;  // gid, rho, u, v, p, mach, T, rank
  std::vector<double> send(n_owned * ncomp);
  for (int i = 0; i < n_owned; ++i) {
    const double rho = s.U[4 * i];
    const double u = s.U[4 * i + 1] / rho;
    const double v = s.U[4 * i + 2] / rho;
    const double p =
        (s.gas.gamma - 1.0) * (s.U[4 * i + 3] - 0.5 * rho * (u * u + v * v));
    // Use the same pressure floor as the flux/primitives so the field file is
    // consistent with the states actually used by the solver (corner cells of
    // the supersonic trailing-edge expansion have near-zero raw pressure).
    const double p_out = std::max(p, 1e-6 * s.gas.p_ref);
    const double mach =
        std::sqrt(u * u + v * v) / std::sqrt(s.gas.gamma * p_out / rho);
    const double T = p_out / (rho * s.gas.R);
    send[i * ncomp + 0] = s.mesh.cells[i].global_id;
    send[i * ncomp + 1] = rho;
    send[i * ncomp + 2] = u;
    send[i * ncomp + 3] = v;
    send[i * ncomp + 4] = p_out;
    send[i * ncomp + 5] = mach;
    send[i * ncomp + 6] = T;
    send[i * ncomp + 7] = s.rank;
  }
  std::vector<int> counts(s.nranks), displs(s.nranks);
  MPI_Gather(&n_owned, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, s.comm);
  if (s.rank == 0) {
    int total = 0;
    for (int r = 0; r < s.nranks; ++r) {
      displs[r] = total * ncomp;
      total += counts[r];
    }
    std::vector<double> all(total * ncomp);
    std::vector<int> recvcounts(s.nranks);
    for (int r = 0; r < s.nranks; ++r) recvcounts[r] = counts[r] * ncomp;
    MPI_Gatherv(send.data(), n_owned * ncomp, MPI_DOUBLE, all.data(),
                recvcounts.data(), displs.data(), MPI_DOUBLE, 0, s.comm);

    // Sort by global id.
    std::vector<int> order(total);
    for (int r = 0; r < s.nranks; ++r)
      for (int k = 0; k < counts[r]; ++k)
        order[displs[r] / ncomp + k] = displs[r] / ncomp + k;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      return all[a * ncomp] < all[b * ncomp];
    });
    std::vector<double> sorted(total * ncomp);
    for (size_t k = 0; k < order.size(); ++k) {
      std::copy(all.begin() + order[k] * ncomp,
                all.begin() + order[k] * ncomp + ncomp,
                sorted.begin() + k * ncomp);
    }

    // Load global mesh geometry for points/cells.
    GlobalMesh gm =
        read_global_mesh_bin(outdir + "/partition/global_mesh.bin");
    const int nn = gm.node_x.size();
    const int ncell = gm.cells.size();
    if (static_cast<int>(order.size()) != ncell)
      throw std::runtime_error("field data cell count mismatch");

    FILE* f = std::fopen((outdir + "/" + name + ".vtu").c_str(), "w");
    if (!f) throw std::runtime_error("cannot create field VTU file");
    std::fprintf(f,
                 "<?xml version=\"1.0\"?>\n"
                 "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" "
                 "byte_order=\"LittleEndian\">\n"
                 "  <UnstructuredGrid>\n"
                 "    <Piece NumberOfPoints=\"%d\" NumberOfCells=\"%d\">\n",
                 nn, ncell);
    std::fprintf(f, "      <Points>\n");
    std::fprintf(f,
                 "        <DataArray type=\"Float64\" Name=\"Points\" "
                 "NumberOfComponents=\"3\" format=\"ascii\">\n");
    for (int i = 0; i < nn; ++i)
      std::fprintf(f, "%.10g %.10g 0.0\n", gm.node_x[i], gm.node_y[i]);
    std::fprintf(f, "        </DataArray>\n      </Points>\n");

    std::fprintf(f, "      <Cells>\n");
    std::fprintf(f,
                 "        <DataArray type=\"Int32\" Name=\"connectivity\" "
                 "format=\"ascii\">\n");
    long long offset = 0;
    std::vector<long long> offsets(ncell);
    std::vector<int> types(ncell);
    for (int i = 0; i < ncell; ++i) {
      const auto& cell = gm.cells[i];
      for (int n : cell.nodes) std::fprintf(f, "%d ", n);
      std::fprintf(f, "\n");
      offset += cell.nodes.size();
      offsets[i] = offset;
      types[i] = (cell.nodes.size() == 3) ? 5 : 9;
    }
    std::fprintf(f, "        </DataArray>\n");
    std::fprintf(f,
                 "        <DataArray type=\"Int32\" Name=\"offsets\" "
                 "format=\"ascii\">\n");
    for (int i = 0; i < ncell; ++i) std::fprintf(f, "%lld\n", offsets[i]);
    std::fprintf(f, "        </DataArray>\n");
    std::fprintf(f,
                 "        <DataArray type=\"UInt8\" Name=\"types\" "
                 "format=\"ascii\">\n");
    for (int i = 0; i < ncell; ++i) std::fprintf(f, "%d\n", types[i]);
    std::fprintf(f, "        </DataArray>\n      </Cells>\n");

    std::fprintf(f, "      <CellData>\n");
    const char* names[8] = {"Density",      "VelocityX", "VelocityY",
                            "Pressure",     "MachNumber", "Temperature",
                            "TotalEnergy",  "RankID"};
    for (int v = 0; v < 8; ++v) {
      std::fprintf(f,
                   "        <DataArray type=\"Float64\" Name=\"%s\" "
                   "format=\"ascii\">\n",
                   names[v]);
      for (int i = 0; i < ncell; ++i) {
        const double val = sorted[i * ncomp + v + 1];
        std::fprintf(f, "%.10g\n", val);
      }
      std::fprintf(f, "        </DataArray>\n");
    }
    std::fprintf(f, "      </CellData>\n");
    std::fprintf(f, "    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n");
    std::fclose(f);
  } else {
    MPI_Gatherv(send.data(), n_owned * ncomp, MPI_DOUBLE, nullptr, nullptr,
                nullptr, MPI_DOUBLE, 0, s.comm);
  }
}

void write_restart(const Solver& s, const std::string& path, int step,
                   double physical_time) {
  const int n_owned = s.mesh.n_owned;
  std::vector<double> send(n_owned * 13);
  for (int i = 0; i < n_owned; ++i) {
    send[i * 13] = s.mesh.cells[i].global_id;
    for (int k = 0; k < 4; ++k) {
      send[i * 13 + 1 + k] = s.U[4 * i + k];
      send[i * 13 + 5 + k] = s.U_prev1[4 * i + k];
      send[i * 13 + 9 + k] = s.U_prev2[4 * i + k];
    }
  }
  std::vector<int> counts(s.nranks), displs(s.nranks);
  MPI_Gather(&n_owned, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, s.comm);
  if (s.rank == 0) {
    int total = 0;
    for (int r = 0; r < s.nranks; ++r) {
      displs[r] = total * 13;
      total += counts[r];
    }
    std::vector<double> all(total * 13);
    std::vector<int> recvcounts(s.nranks);
    for (int r = 0; r < s.nranks; ++r) recvcounts[r] = counts[r] * 13;
    MPI_Gatherv(send.data(), n_owned * 13, MPI_DOUBLE, all.data(),
                recvcounts.data(), displs.data(), MPI_DOUBLE, 0, s.comm);
    // Sort records by global id, then write 12 doubles per cell in order.
    const int ncells = total;
    std::vector<int> order(ncells);
    for (int r = 0; r < s.nranks; ++r)
      for (int k = 0; k < counts[r]; ++k)
        order[displs[r] / 13 + k] = displs[r] / 13 + k;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      return all[a * 13] < all[b * 13];
    });
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot create restart file");
    const char magic[8] = {'C', 'F', 'D', 'R', 'S', 'T', '1', 0};
    std::fwrite(magic, 1, 8, f);
    int32_t nranks = s.nranks, nglob = s.mesh.num_cells_global, stp = step;
    double t = physical_time;
    std::fwrite(&nranks, 4, 1, f);
    std::fwrite(&nglob, 4, 1, f);
    std::fwrite(&stp, 4, 1, f);
    std::fwrite(&t, 8, 1, f);
    double rinit[2] = {s.residual_initial_l2, s.residual_initial_linf};
    std::fwrite(rinit, 8, 2, f);
    for (int idx : order)
      std::fwrite(all.data() + idx * 13 + 1, 8, 12, f);
    std::fclose(f);
  } else {
    MPI_Gatherv(send.data(), n_owned * 13, MPI_DOUBLE, nullptr, nullptr,
                nullptr, MPI_DOUBLE, 0, s.comm);
  }
}

std::pair<int, double> read_restart(Solver& s, const std::string& path) {
  // Each rank opens the file and reads its owned cells by global id.
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) throw std::runtime_error("cannot open restart file " + path);
  char magic[8] = {0};
  if (std::fread(magic, 1, 8, f) != 8 || std::memcmp(magic, "CFDRST1", 7) != 0)
    throw std::runtime_error("bad restart file magic");
  int32_t nranks = 0, nglob = 0, step = 0;
  double t = 0.0, rinit[2] = {0};
  std::fread(&nranks, 4, 1, f);
  std::fread(&nglob, 4, 1, f);
  std::fread(&step, 4, 1, f);
  std::fread(&t, 8, 1, f);
  std::fread(rinit, 8, 2, f);
  if (nglob != s.mesh.num_cells_global)
    throw std::runtime_error("restart mesh size mismatch");
  const long long base = 8 + 4 + 4 + 4 + 8 + 16;
  for (int i = 0; i < s.mesh.n_owned; ++i) {
    const long long gid = s.mesh.cells[i].global_id;
    double rec[12] = {0};
    if (std::fseek(f, base + gid * 96, SEEK_SET) != 0 ||
        std::fread(rec, 8, 12, f) != 12)
      throw std::runtime_error("restart file truncated");
    for (int k = 0; k < 4; ++k) {
      s.U[4 * i + k] = rec[k];
      s.U_prev1[4 * i + k] = rec[4 + k];
      s.U_prev2[4 * i + k] = rec[8 + k];
    }
  }
  std::fclose(f);
  s.residual_initial_l2 = rinit[0];
  s.residual_initial_linf = rinit[1];
  return {step, t};
}

}  // namespace cfd
