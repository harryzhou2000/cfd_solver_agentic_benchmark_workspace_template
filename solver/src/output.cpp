#include "output.hpp"

#include "solver.hpp"

#include <mpi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace cfd {

namespace {

std::string iso8601_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&tt, &tm);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

std::string fmt_g(double v) {
  std::ostringstream os;
  os << std::setprecision(12) << v;
  return os.str();
}

// Gather variable-length strings from all ranks to rank 0.
std::vector<std::string> gather_strings(const std::string& local) {
  int rank = 0, nranks = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);
  std::vector<int> lens(static_cast<size_t>(nranks));
  const int mylen = static_cast<int>(local.size());
  MPI_Gather(&mylen, 1, MPI_INT, lens.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  std::vector<std::string> out(static_cast<size_t>(nranks));
  if (rank == 0) {
    std::vector<int> displs(static_cast<size_t>(nranks));
    int total = 0;
    for (int r = 0; r < nranks; ++r) {
      displs[static_cast<size_t>(r)] = total;
      total += lens[static_cast<size_t>(r)];
    }
    std::string buf(static_cast<size_t>(total), '\0');
    MPI_Gatherv(const_cast<char*>(local.data()), mylen, MPI_CHAR, buf.data(), lens.data(),
                displs.data(), MPI_CHAR, 0, MPI_COMM_WORLD);
    for (int r = 0; r < nranks; ++r)
      out[static_cast<size_t>(r)] =
          buf.substr(static_cast<size_t>(displs[static_cast<size_t>(r)]),
                     static_cast<size_t>(lens[static_cast<size_t>(r)]));
  } else {
    MPI_Gatherv(const_cast<char*>(local.data()), mylen, MPI_CHAR, nullptr, nullptr,
                nullptr, MPI_CHAR, 0, MPI_COMM_WORLD);
  }
  return out;
}

void write_csv(const std::string& path, const std::string& header,
               const std::vector<std::string>& rows) {
  std::ofstream f(path);
  f << header << "\n";
  for (const auto& r : rows) f << r << "\n";
}

std::string qinf_str(const Solver& s) { return fmt_g(s.qinf); }

// Reconstructed wall-face pressure and boundary velocity values used in the
// surface output (matches the values used in the force integration).
void wall_boundary_values(const Solver& s, int loc, const BFace& bf,
                          double& p_f, double& ub, double& vb) {
  const Partition& p = s.part;
  std::array<double, 4> q;
  {
    const double* u = &s.U[static_cast<size_t>(loc) * NVAR];
    q[0] = u[0];
    q[1] = u[1] / u[0];
    q[2] = u[2] / u[0];
    q[3] = (s.cfg.gamma - 1.0) * (u[3] - 0.5 * u[0] * (q[1] * q[1] + q[2] * q[2]));
  }
  const double* g = &s.grads_[static_cast<size_t>(loc) * 8];
  const double psi = s.psi_[static_cast<size_t>(loc)];
  const RankCell& c = p.cells[static_cast<size_t>(loc)];
  p_f = q[3] + psi * (g[6] * (bf.fx - c.cx) + g[7] * (bf.fy - c.cy));
  const double vn = q[1] * bf.nx + q[2] * bf.ny;
  if (bf.type == BcType::NoSlipAdiabaticWall) {
    ub = 0.0;
    vb = 0.0;
  } else {
    // slip wall: zero normal velocity, tangential preserved
    ub = q[1] - vn * bf.nx;
    vb = q[2] - vn * bf.ny;
  }
}

}  // namespace

void write_field_file(const Solver& s, const std::string& filename, double t, int step) {
  const std::string path = std::filesystem::path(s.out_dir) / filename;
  const Partition& p = s.part;
  const int n_owned = p.num_cells_owned;

  std::ostringstream pts, conn, offs, types;
  std::vector<std::ostringstream> cell_data(8);
  const char* cell_names[] = {"Density",    "VelocityX",  "VelocityY",  "Pressure",
                              "Mach",       "TotalEnergy", "Temperature", "PartitionId"};
  const int n_points = static_cast<int>(p.vx.size());
  pts << std::setprecision(12);
  for (int k = 0; k < n_points; ++k)
    pts << p.vx[static_cast<size_t>(k)] << " " << p.vy[static_cast<size_t>(k)] << " 0 ";

  int offset = 0;
  for (int i = 0; i < n_owned; ++i) {
    const RankCell& c = p.cells[static_cast<size_t>(i)];
    for (int k = 0; k < c.nv; ++k)
      conn << p.v_map[static_cast<size_t>(c.verts[k])] << " ";
    offset += c.nv;
    offs << offset << " ";
    types << (c.nv == 3 ? 5 : 9) << " ";

    const double* u = &s.U[static_cast<size_t>(i) * NVAR];
    const double rho = u[0];
    const double ux = u[1] / rho, uy = u[2] / rho;
    const double pr = s.gas.pressure(u[0], u[1], u[2], u[3]);
    const double a = std::sqrt(s.cfg.gamma * pr / rho);
    const double mach = std::sqrt(ux * ux + uy * uy) / a;
    const double T = pr / (rho * s.gas.R);
    cell_data[0] << fmt_g(rho) << " ";
    cell_data[1] << fmt_g(ux) << " ";
    cell_data[2] << fmt_g(uy) << " ";
    cell_data[3] << fmt_g(pr) << " ";
    cell_data[4] << fmt_g(mach) << " ";
    cell_data[5] << fmt_g(u[3]) << " ";
    cell_data[6] << fmt_g(T) << " ";
    cell_data[7] << p.rank << " ";
  }

  std::ostringstream piece;
  piece << std::setprecision(12);
  if (p.rank == 0)
    piece << "<FieldData>\n"
          << "<DataArray type=\"Float64\" Name=\"TIME\" NumberOfTuples=\"1\">" << fmt_g(t)
          << "</DataArray>\n"
          << "<DataArray type=\"Int32\" Name=\"CYCLE\" NumberOfTuples=\"1\">" << step
          << "</DataArray>\n"
          << "</FieldData>\n";
  piece << "<Piece NumberOfPoints=\"" << n_points << "\" NumberOfCells=\"" << n_owned
        << "\">\n"
        << "<Points>\n"
        << "<DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">"
        << pts.str() << "</DataArray>\n"
        << "</Points>\n"
        << "<Cells>\n"
        << "<DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">" << conn.str()
        << "</DataArray>\n"
        << "<DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">" << offs.str()
        << "</DataArray>\n"
        << "<DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">" << types.str()
        << "</DataArray>\n"
        << "</Cells>\n"
        << "<CellData>\n";
  for (int d = 0; d < 8; ++d)
    piece << "<DataArray type=\"Float64\" Name=\"" << cell_names[d] << "\" format=\"ascii\">"
          << cell_data[static_cast<size_t>(d)].str() << "</DataArray>\n";
  piece << "</CellData>\n"
        << "</Piece>\n";

  // each rank writes its piece to a temp file; rank 0 assembles
  const std::string tmp = path + ".r" + std::to_string(p.rank);
  {
    std::ofstream f(tmp);
    f << piece.str();
  }
  MPI_Barrier(MPI_COMM_WORLD);
  if (p.rank == 0) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    f << "<?xml version=\"1.0\"?>\n"
      << "<VTKFile type=\"UnstructuredGrid\" version=\"1.0\" byte_order=\"LittleEndian\">\n"
      << "<UnstructuredGrid>\n";
    for (int r = 0; r < p.nranks; ++r) {
      std::ifstream in(path + ".r" + std::to_string(r));
      f << in.rdbuf();
    }
    f << "</UnstructuredGrid>\n</VTKFile>\n";
    for (int r = 0; r < p.nranks; ++r)
      std::remove((path + ".r" + std::to_string(r)).c_str());
  }
  MPI_Barrier(MPI_COMM_WORLD);
}

void write_checkpoint(const Solver& s, const std::string& case_id, int step, double time) {
  const Partition& p = s.part;
  const int n_owned = p.num_cells_owned;
  const bool transient = s.cfg.transient;
  // gather owned states (owned cells are in ascending global-id order)
  std::vector<int> counts(static_cast<size_t>(p.nranks));
  MPI_Gather(&n_owned, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  std::vector<double> local_u(static_cast<size_t>(n_owned) * NVAR);
  for (int i = 0; i < n_owned; ++i)
    for (int c = 0; c < NVAR; ++c)
      local_u[static_cast<size_t>(i) * NVAR + c] = s.U[static_cast<size_t>(i) * NVAR + c];

  std::vector<double> all_u;
  std::vector<int> displs(static_cast<size_t>(p.nranks));
  std::vector<int> recvcounts(static_cast<size_t>(p.nranks));
  if (p.rank == 0) {
    int total = 0;
    for (int r = 0; r < p.nranks; ++r) {
      displs[static_cast<size_t>(r)] = total;
      recvcounts[static_cast<size_t>(r)] = counts[static_cast<size_t>(r)] * NVAR;
      total += recvcounts[static_cast<size_t>(r)];
    }
    all_u.resize(static_cast<size_t>(p.num_cells_global) * NVAR);
  }
  MPI_Gatherv(local_u.data(), n_owned * NVAR, MPI_DOUBLE, all_u.data(), recvcounts.data(),
              displs.data(), MPI_DOUBLE, 0, MPI_COMM_WORLD);

  if (p.rank == 0) {
    const std::string path = std::filesystem::path(s.out_dir) / "restart_last.bin";
    std::ofstream f(path, std::ios::binary);
    f.write("CFD_RST1", 8);
    const int32_t ncells = p.num_cells_global;
    const int32_t nhist = transient ? 2 : 1;
    f.write(reinterpret_cast<const char*>(&ncells), 4);
    f.write(reinterpret_cast<const char*>(&nhist), 4);
    f.write(reinterpret_cast<const char*>(&step), 4);
    f.write(reinterpret_cast<const char*>(&time), 8);
    f.write(reinterpret_cast<const char*>(all_u.data()),
            static_cast<std::streamsize>(all_u.size() * sizeof(double)));
    // for transient runs store the histories too (same as current state at
    // checkpoint time after commit)
    std::vector<double> hist(static_cast<size_t>(p.num_cells_global) * NVAR, 0.0);
    for (int i = 0; i < n_owned; ++i)
      for (int c = 0; c < NVAR; ++c)
        hist[static_cast<size_t>(i) * NVAR + c] = s.U[static_cast<size_t>(i) * NVAR + c];
    f.write(reinterpret_cast<const char*>(hist.data()),
            static_cast<std::streamsize>(hist.size() * sizeof(double)));
    std::ofstream jf(std::filesystem::path(s.out_dir) / "restart_last.json");
    jf << "{\"case_id\":\"" << case_id << "\",\"step\":" << step
       << ",\"physical_time\":" << fmt_g(time) << ",\"num_cells_global\":" << ncells
       << ",\"transient\":" << (transient ? "true" : "false") << "}\n";
  }
}

bool read_restart(Solver& s, const std::string& path, std::string& err) {
  const Partition& p = s.part;
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    err = "cannot open restart file " + path;
    return false;
  }
  char magic[8];
  f.read(magic, 8);
  if (std::memcmp(magic, "CFD_RST1", 8) != 0) {
    err = "invalid restart file (bad magic): " + path;
    return false;
  }
  int32_t ncells = 0, nhist = 0, step = 0;
  double time = 0.0;
  f.read(reinterpret_cast<char*>(&ncells), 4);
  f.read(reinterpret_cast<char*>(&nhist), 4);
  f.read(reinterpret_cast<char*>(&step), 4);
  f.read(reinterpret_cast<char*>(&time), 8);
  if (ncells != p.num_cells_global) {
    err = "restart cell count mismatch: " + std::to_string(ncells) + " != " +
          std::to_string(p.num_cells_global);
    return false;
  }
  std::vector<double> all_u(static_cast<size_t>(ncells) * NVAR);
  f.read(reinterpret_cast<char*>(all_u.data()),
         static_cast<std::streamsize>(all_u.size() * sizeof(double)));
  std::vector<double> hist(static_cast<size_t>(ncells) * NVAR, 0.0);
  if (nhist >= 2)
    f.read(reinterpret_cast<char*>(hist.data()),
           static_cast<std::streamsize>(hist.size() * sizeof(double)));

  // broadcast to all ranks
  MPI_Bcast(all_u.data(), static_cast<int>(all_u.size()), MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast(hist.data(), static_cast<int>(hist.size()), MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast(&step, 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

  for (int i = 0; i < p.num_cells_owned; ++i) {
    const int gid = p.cells[static_cast<size_t>(i)].global_id;
    for (int c = 0; c < NVAR; ++c)
      s.U[static_cast<size_t>(i) * NVAR + c] = all_u[static_cast<size_t>(gid) * NVAR + c];
  }
  s.restart_step = step;
  s.restart_time = time;
  return true;
}

void write_final_outputs(const Solver& s, const std::string& out_dir,
                         double wall_time_seconds, const std::string& command_line) {
  const Partition& p = s.part;
  const int rank = p.rank;

  if (rank == 0)
    std::filesystem::create_directories(out_dir);
  MPI_Barrier(MPI_COMM_WORLD);

  // ---- partition diagnostics ----
  {
    std::ostringstream row;
    row << p.rank << "," << p.num_cells_owned << "," << p.num_cells_ghost << ","
        << p.bfaces.size() << "," << p.neighbors.size() << ",";
    bool first = true;
    int send_cells = 0, recv_cells = 0;
    for (const auto& nb : p.neighbors) {
      if (!first) row << ";";
      row << nb.rank;
      first = false;
      send_cells += static_cast<int>(nb.send_local.size());
      recv_cells += static_cast<int>(nb.recv_local.size());
    }
    row << "," << send_cells << "," << recv_cells;
    const auto rows = gather_strings(row.str());
    if (rank == 0) {
      std::vector<std::string> lines;
      for (const auto& r : rows) lines.push_back(r);
      write_csv(std::filesystem::path(out_dir) / "partition_diagnostics.csv",
                "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,"
                "neighbor_ranks,send_cells,recv_cells",
                lines);
    }
  }

  // ---- residuals.csv, forces.csv ----
  if (rank == 0) {
    std::vector<std::string> rrows;
    for (const auto& r : s.residual_history) {
      std::ostringstream os;
      os << r.step << "," << fmt_g(r.physical_time) << "," << r.inner_iter << ","
         << fmt_g(r.cfl) << "," << fmt_g(r.dt) << "," << fmt_g(r.comp[0]) << ","
         << fmt_g(r.comp[1]) << "," << fmt_g(r.comp[2]) << "," << fmt_g(r.comp[3]) << ","
         << fmt_g(r.l2) << "," << fmt_g(r.linf);
      rrows.push_back(os.str());
    }
    write_csv(std::filesystem::path(out_dir) / "residuals.csv",
              "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf",
              rrows);

    std::vector<std::string> frows;
    for (size_t k = 0; k < s.force_history.size(); ++k) {
      const ForceRow& f = s.force_history[k];
      const double t = (k < s.residual_history.size()) ? s.residual_history[k].physical_time
                                                       : 0.0;
      std::ostringstream os;
      os << (k + 1) << "," << fmt_g(t) << "," << fmt_g(f.cl) << "," << fmt_g(f.cd) << ","
         << fmt_g(f.cmz) << "," << fmt_g(f.pressure_drag) << "," << fmt_g(f.viscous_drag)
         << "," << fmt_g(f.pressure_lift) << "," << fmt_g(f.viscous_lift);
      frows.push_back(os.str());
    }
    write_csv(std::filesystem::path(out_dir) / "forces.csv",
              "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift",
              frows);
  }

  // ---- surface.csv ----
  {
    std::ostringstream local_rows;
    for (size_t k = 0; k < p.bfaces.size(); ++k) {
      const BFace& bf = p.bfaces[k];
      if (bf.type == BcType::Farfield) continue;
      const int loc = p.global_to_local(bf.cell);
      if (loc < 0) continue;
      double p_f = 0.0, ub = 0.0, vb = 0.0;
      wall_boundary_values(s, loc, bf, p_f, ub, vb);
      const double* u = &s.U[static_cast<size_t>(loc) * NVAR];
      const double rho = u[0];
      const double pr_cell = s.gas.pressure(u[0], u[1], u[2], u[3]);
      const double a = std::sqrt(s.cfg.gamma * pr_cell / rho);
      const double mach = std::sqrt(ub * ub + vb * vb) / a;
      const double cp = (p_f - s.fs.p) / s.qinf;
      double cf = 0.0;
      if (!s.cfg.inviscid && bf.type == BcType::NoSlipAdiabaticWall) {
        const double* g = &s.grads_[static_cast<size_t>(loc) * 8];
        double tx = 0.0, ty = 0.0, tt = 0.0;
        std::array<double, 6> g6;
        const double inv = 1.0 / (rho * s.gas.R);
        g6[0] = g[2];
        g6[1] = g[3];
        g6[2] = g[4];
        g6[3] = g[5];
        g6[4] = (g[6] - (pr_cell / rho) * g[0]) * inv;
        g6[5] = (g[7] - (pr_cell / rho) * g[1]) * inv;
        std::array<double, 4> Fv;
        const std::array<double, 4> qw{rho, ub, vb, pr_cell};
        viscous_wall_flux(qw, g6, bf.nx, bf.ny, s.mu, s.cfg.gamma, Fv, tt, &tx, &ty);
        // skin-friction coefficient: shear stress on the body (positive
        // along the flow) = -(tau . n_fluid) . t
        cf = -tt / s.qinf;
      }
      std::ostringstream os;
      os << fmt_g(bf.fx) << "," << fmt_g(bf.fy) << "," << fmt_g(bf.nx) << ","
         << fmt_g(bf.ny) << "," << fmt_g(p_f) << "," << fmt_g(cp) << "," << fmt_g(cf)
         << "," << fmt_g(rho) << "," << fmt_g(ub) << "," << fmt_g(vb) << "," << fmt_g(mach)
         << "," << bf.family;
      local_rows << p.bface_gbf[k] << "|" << os.str() << "\n";
    }
    const auto rows = gather_strings(local_rows.str());
    if (rank == 0) {
      // sort by global boundary face index
      std::vector<std::pair<int, std::string>> entries;
      for (const auto& r : rows) {
        std::istringstream ls(r);
        std::string line;
        while (std::getline(ls, line)) {
          if (line.empty()) continue;
          const size_t pos = line.find('|');
          if (pos == std::string::npos) continue;
          entries.emplace_back(std::stoi(line.substr(0, pos)), line.substr(pos + 1));
        }
      }
      std::sort(entries.begin(), entries.end());
      std::vector<std::string> lines;
      for (const auto& e : entries) lines.push_back(e.second);
      write_csv(std::filesystem::path(out_dir) / "surface.csv",
                "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag", lines);
    }
  }

  // ---- field_final.vtu ----
  write_field_file(s, "field_final.vtu", s.stats.final_physical_time, s.stats.final_step);

  // ---- restart_final ----
  write_checkpoint(s, s.cfg.case_id, s.stats.final_step, s.stats.final_physical_time);
  if (rank == 0) {
    std::filesystem::rename(std::filesystem::path(out_dir) / "restart_last.bin",
                            std::filesystem::path(out_dir) / "restart_final.bin");
    std::filesystem::rename(std::filesystem::path(out_dir) / "restart_last.json",
                            std::filesystem::path(out_dir) / "restart_final.json");
  }
  MPI_Barrier(MPI_COMM_WORLD);

  // ---- metadata.json ----
  {
    std::ostringstream os;
    os << "{\n";
    os << "  \"case_id\": \"" << s.cfg.case_id << "\",\n";
    os << "  \"solver_name\": \"cfd_solver\",\n";
    os << "  \"solver_version\": \"1.0.0\",\n";
    const char* rev = std::getenv("CFD_GIT_REVISION");
    os << "  \"git_revision\": " << (rev ? ("\"" + std::string(rev) + "\"") : "null") << ",\n";
    os << "  \"mpi_ranks\": " << p.nranks << ",\n";
    os << "  \"mesh_file\": \"" << s.cfg.mesh_file << "\",\n";
    os << "  \"num_cells_global\": " << p.num_cells_global << ",\n";
    os << "  \"num_faces_global\": " << p.num_faces_global << ",\n";
    os << "  \"num_cells_owned_local\": " << p.num_cells_owned << ",\n";
    os << "  \"num_cells_ghost_local\": " << p.num_cells_ghost << ",\n";
    os << "  \"partitioner\": \"metis_kway\",\n";
    os << "  \"partition_edge_cut\": " << p.edgecut << ",\n";
    os << "  \"halo_exchange\": \"neighbor_isend_irecv\",\n";
    os << "  \"full_state_replication_during_iterations\": false,\n";
    os << "  \"full_mesh_replication_during_iterations\": false,\n";
    os << "  \"equation_set\": \"compressible_navier_stokes_2d\",\n";
    os << "  \"inviscid_flux\": \"rusanov_llf\",\n";
    os << "  \"entropy_fix\": null,\n";
    os << "  \"viscous_flux\": \"laminar_constant_mu_newtonian_fourier\",\n";
    os << "  \"time_integrator\": \"" << (s.cfg.transient ? "bdf2" : "steady_pseudo_time")
       << "\",\n";
    os << "  \"implicit_solver\": \"lusgs\",\n";
    os << "  \"reconstruction\": \"weighted_lsq_second_order\",\n";
    os << "  \"limiter\": \"barth_jespersen\",\n";
    os << "  \"spatial_order_claimed\": 2,\n";
    os << "  \"positivity_preservation\": \"barth_jespersen_bounds_plus_update_scaling\",\n";
    os << "  \"wall_boundary_output_semantics\": \"boundary_value\",\n";
    os << "  \"typical_inner_iterations\": " << fmt_g(s.stats.mean_inner) << ",\n";
    os << "  \"min_inner_iterations\": " << s.cfg.min_inner_iterations << ",\n";
    os << "  \"max_inner_iterations\": " << s.cfg.max_inner_iterations << ",\n";
    os << "  \"observed_min_inner_iterations\": " << s.stats.min_inner << ",\n";
    os << "  \"observed_max_inner_iterations\": " << s.stats.max_inner << ",\n";
    os << "  \"inner_residual_reduction_target\": " << s.cfg.inner_residual_reduction_target
       << ",\n";
    os << "  \"inner_target_misses\": " << s.stats.target_misses << ",\n";
    os << "  \"inner_target_converged_fraction\": "
       << fmt_g(s.stats.n_steps > 0
                    ? static_cast<double>(s.stats.converged_steps) /
                          static_cast<double>(s.stats.n_steps)
                    : 0.0)
       << ",\n";
    os << "  \"last_inner_residual_ratio\": " << fmt_g(s.stats.last_inner_ratio) << ",\n";
    os << "  \"start_time_utc\": \"" << s.start_time_utc << "\",\n";
    os << "  \"end_time_utc\": \"" << iso8601_now() << "\",\n";
    os << "  \"completed\": "
       << (s.stats.convergence_status == "converged" ||
                   s.stats.convergence_status == "statistically_periodic"
               ? "true"
               : "false")
       << ",\n";
    os << "  \"convergence_status\": \"" << s.stats.convergence_status << "\"\n";
    if (s.cfg.transient) {
      os << "  ,\"true_bdf2_inner_loop\": true\n";
    }
    os << "}\n";
    if (rank == 0) {
      std::ofstream f(std::filesystem::path(out_dir) / "metadata.json");
      f << os.str();
    }
  }

  // ---- run_status.json ----
  if (rank == 0) {
    std::ostringstream os;
    os << "{\n";
    os << "  \"case_id\": \"" << s.cfg.case_id << "\",\n";
    os << "  \"command\": \"" << command_line << "\",\n";
    os << "  \"mpi_ranks\": " << p.nranks << ",\n";
    os << "  \"wall_time_seconds\": " << fmt_g(wall_time_seconds) << ",\n";
    os << "  \"final_step\": " << s.stats.final_step << ",\n";
    os << "  \"final_physical_time\": " << fmt_g(s.stats.final_physical_time) << ",\n";
    os << "  \"convergence_status\": \"" << s.stats.convergence_status << "\",\n";
    os << "  \"residual_reduction_orders\": " << fmt_g(s.stats.residual_reduction_orders)
       << ",\n";
    os << "  \"notes\": \"" << s.stats.notes << "\"\n";
    os << "}\n";
    std::ofstream f(std::filesystem::path(out_dir) / "run_status.json");
    f << os.str();
  }
}

}  // namespace cfd
