// Phase 3/4/5 output implementation (see output.h).

#include "output.h"

#include <mpi.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "physics.h"

namespace cfd {

namespace {

// Full-precision double formatting (%.16g keeps CSV values round-trippable).
std::string fmt(double v) {
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.16g", v);
  return buf;
}

// Append a single row; report failures to stderr but never throw.
void append_row(const std::string& path, const std::string& row) {
  std::ofstream out(path, std::ios::app);
  if (!out) {
    std::fprintf(stderr, "cfd_solver: warning: cannot append to %s\n",
                 path.c_str());
    return;
  }
  out << row << '\n';
}

// Create (truncate) the file and write the header; report failures but
// never throw.
void write_header(const std::string& path, const std::string& header) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    std::fprintf(stderr, "cfd_solver: warning: cannot create %s\n",
                 path.c_str());
    return;
  }
  out << header << '\n';
}

// Write a JSON object to a file (rank 0 only; pretty-printed, valid JSON).
// Report failures to stderr but never throw.
void write_json_file(const std::string& path, const nlohmann::json& j) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    std::fprintf(stderr, "cfd_solver: warning: cannot create %s\n",
                 path.c_str());
    return;
  }
  out << j.dump(2) << '\n';
}

// Seconds since the Unix epoch -> ISO-8601 UTC string.
std::string iso_time_utc(double t_sec) {
  if (!(t_sec > 0.0)) {
    return "";
  }
  const std::time_t tt = static_cast<std::time_t>(t_sec);
  std::tm tmv{};
  gmtime_r(&tt, &tmv);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
  return buf;
}

}  // namespace

void write_residual_header(const std::string& path) {
  write_header(path,
               "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,"
               "residual_l2,residual_linf");
}

void write_residual_row(const std::string& path, const SolverStats& s) {
  std::string row = std::to_string(s.step) + "," +
                    fmt(s.physical_time) + "," +
                    std::to_string(s.inner_iter) + "," + fmt(s.cfl) + "," +
                    fmt(s.dt) + "," + fmt(s.residual_rho) + "," +
                    fmt(s.residual_rhou) + "," + fmt(s.residual_rhov) + "," +
                    fmt(s.residual_rhoE) + "," + fmt(s.residual_l2) + "," +
                    fmt(s.residual_linf);
  append_row(path, row);
}

void write_force_header(const std::string& path) {
  write_header(path,
               "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,"
               "pressure_lift,viscous_lift");
}

void write_force_row(const std::string& path, const SolverStats& s) {
  std::string row = std::to_string(s.step) + "," +
                    fmt(s.physical_time) + "," + fmt(s.cl) + "," +
                    fmt(s.cd) + "," + fmt(s.cmz) + "," +
                    fmt(s.pressure_drag) + "," + fmt(s.viscous_drag) + "," +
                    fmt(s.pressure_lift) + "," + fmt(s.viscous_lift);
  append_row(path, row);
}

// ---------------------------------------------------------------------------
// Phase 5: metadata.json
// ---------------------------------------------------------------------------

void write_metadata(const std::string& path, const RunConfig& run_cfg,
                    const LocalMesh& lm, int mpi_ranks, int n_cells_global,
                    int n_faces_global, int edge_cut,
                    const std::string& solver_name,
                    const std::string& solver_version,
                    const SolverConfig& scfg, double start_time,
                    double end_time, bool completed,
                    const std::string& convergence_status,
                    const std::string& case_id, const std::string& mesh_file,
                    int obs_min_inner, int obs_max_inner,
                    double obs_mean_inner, int inner_target_misses,
                    double inner_converged_fraction,
                    double last_inner_residual_ratio, bool is_transient) {
  nlohmann::json m;
  m["case_id"] = case_id;
  m["solver_name"] = solver_name;
  m["solver_version"] = solver_version;
  m["git_revision"] = nullptr;
  m["mpi_ranks"] = mpi_ranks;
  m["mesh_file"] = mesh_file;
  m["num_cells_global"] = n_cells_global;
  m["num_faces_global"] = n_faces_global;
  m["num_cells_owned_local"] = lm.n_owned;
  m["num_cells_ghost_local"] = lm.n_ghost;
  m["partitioner"] = "metis_kway";
  m["partition_edge_cut"] = edge_cut;
  m["halo_exchange"] = "neighbor_isend_irecv";
  m["full_state_replication_during_iterations"] = false;
  m["full_mesh_replication_during_iterations"] = false;
  m["equation_set"] =
      run_cfg.equations.empty() ? "compressible_navier_stokes_2d"
                                : run_cfg.equations + "_2d";
  // The method fields report the ACTUAL implementation, not the case file's
  // requirement strings ("approximate_riemann" / "required" / ...).
  m["inviscid_flux"] = "rusanov";
  m["entropy_fix"] = "none";
  m["viscous_flux"] = scfg.viscous ? "laminar_navier_stokes" : "none";
  m["time_integrator"] = is_transient ? "bdf2" : "pseudo_time_implicit";
  m["implicit_solver"] = "lusgs";
  m["reconstruction"] = "least_squares";
  m["limiter"] = "venkatakrishnan";
  m["spatial_order_claimed"] = scfg.spatial_order;
  m["positivity_preservation"] = "positivity_fallback";
  m["wall_boundary_output_semantics"] = "boundary_value";
  m["true_bdf2_inner_loop"] = is_transient;
  m["typical_inner_iterations"] =
      static_cast<int>(std::lround(obs_mean_inner));
  m["min_inner_iterations"] = std::max(1, scfg.min_inner_iterations);
  m["max_inner_iterations"] = std::max(1, scfg.max_inner_iterations);
  m["observed_min_inner_iterations"] = obs_min_inner;
  m["observed_max_inner_iterations"] = obs_max_inner;
  m["inner_residual_reduction_target"] = scfg.inner_residual_reduction_target;
  m["inner_target_misses"] = inner_target_misses;
  m["inner_target_converged_fraction"] = inner_converged_fraction;
  m["last_inner_residual_ratio"] = last_inner_residual_ratio;
  m["start_time_utc"] = iso_time_utc(start_time);
  m["end_time_utc"] = iso_time_utc(end_time);
  m["completed"] = completed;
  m["convergence_status"] = convergence_status;
  write_json_file(path, m);
}

// ---------------------------------------------------------------------------
// Phase 5: surface.csv
// ---------------------------------------------------------------------------

void write_surface_csv(const std::string& path, const LocalMesh& lm,
                       const std::vector<ConsState>& U_local,
                       const GasConfig& gas, const Freestream& fs,
                       const SolverConfig& scfg) {
  constexpr int kTag = 4344;  // distinct from the partition-diagnostics tag
  const double q = dynamic_pressure(fs);

  // One row per boundary face. Wall faces (slip / no-slip) report
  // boundary-state values; non-wall boundary faces (farfield) report the
  // adjacent cell-center velocity.
  std::vector<std::string> rows;
  rows.reserve(static_cast<size_t>(lm.n_boundary_faces));
  for (const LocalMesh::LocalFace& face : lm.faces) {
    if (face.right != -1) {
      continue;  // boundary faces only
    }
    const int li = face.left;
    const ConsState& U = U_local[li];
    const Vec2 n_hat =
        face.area > 0.0 ? Vec2(face.normal.x / face.area, face.normal.y / face.area)
                        : Vec2();
    const double p = pressure_from_cons(U, gas);
    const double cp = q > 0.0 ? (p - fs.pressure) / q : 0.0;

    double u = 0.0;
    double v = 0.0;
    double mach = 0.0;
    double cf = 0.0;
    if (face.bc_type == BCType::NoSlipAdiabaticWall) {
      // No-slip wall: u = v = mach = 0 at the wall. Skin friction for
      // laminar runs: tangential traction from the one-sided wall gradient,
      // normalized by the freestream dynamic pressure (same sign convention
      // as the viscous force integration in compute_forces).
      if (scfg.viscous && scfg.mu > 0.0) {
        const double dist = (face.centroid - lm.cells[li].centroid).norm();
        if (dist > 0.0) {
          const PrimState P = cons_to_prim(U, gas);
          const double gn_u = -P.u / dist;
          const double gn_v = -P.v / dist;
          const double du_dx = gn_u * n_hat.x;
          const double du_dy = gn_u * n_hat.y;
          const double dv_dx = gn_v * n_hat.x;
          const double dv_dy = gn_v * n_hat.y;
          const double div = du_dx + dv_dy;
          const double tau_xx =
              scfg.mu * (2.0 * du_dx - (2.0 / 3.0) * div);
          const double tau_yy =
              scfg.mu * (2.0 * dv_dy - (2.0 / 3.0) * div);
          const double tau_xy = scfg.mu * (du_dy + dv_dx);
          const double traction_x = tau_xx * n_hat.x + tau_xy * n_hat.y;
          const double traction_y = tau_xy * n_hat.x + tau_yy * n_hat.y;
          const double t_hat_x = -n_hat.y;
          const double t_hat_y = n_hat.x;
          const double shear =
              traction_x * t_hat_x + traction_y * t_hat_y;
          cf = q > 0.0 ? shear / q : 0.0;
        }
      }
    } else if (face.bc_type == BCType::SlipWall) {
      // Slip wall: remove the normal velocity so the wall sees zero normal
      // flow; the tangential component is preserved.
      const PrimState P = cons_to_prim(U, gas);
      const double vn = P.u * n_hat.x + P.v * n_hat.y;
      u = P.u - vn * n_hat.x;
      v = P.v - vn * n_hat.y;
      const double a = speed_of_sound(U, gas);
      mach = a > 0.0 ? std::sqrt(u * u + v * v) / a : 0.0;
    } else {
      // Non-wall boundary (farfield etc.): cell-center values.
      const PrimState P = cons_to_prim(U, gas);
      u = P.u;
      v = P.v;
      mach = P.mach;
    }

    rows.push_back(fmt(face.centroid.x) + "," + fmt(face.centroid.y) + "," +
                   fmt(n_hat.x) + "," + fmt(n_hat.y) + "," + fmt(p) + "," +
                   fmt(cp) + "," + fmt(cf) + "," + fmt(U.rho) + "," + fmt(u) +
                   "," + fmt(v) + "," + fmt(mach) + "," + face.bc_family);
  }

  // Gather all rows on rank 0 (each wall face lives on exactly one rank, so
  // the rows are disjoint) and write the single file once.
  if (lm.rank == 0) {
    for (int r = 1; r < lm.nranks; ++r) {
      int len = 0;
      MPI_Recv(&len, 1, MPI_INT, r, kTag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      std::string buf(static_cast<size_t>(len), '\0');
      if (len > 0) {
        MPI_Recv(buf.data(), len, MPI_CHAR, r, kTag, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
      }
      // The payload is the concatenation of that rank's rows; split it back.
      size_t pos = 0;
      while (pos < buf.size()) {
        const size_t nl = buf.find('\n', pos);
        if (nl == std::string::npos) {
          break;
        }
        rows.push_back(buf.substr(pos, nl - pos));
        pos = nl + 1;
      }
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
      std::fprintf(stderr, "cfd_solver: warning: cannot create %s\n",
                   path.c_str());
      return;
    }
    out << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    for (const std::string& row : rows) {
      out << row << '\n';
    }
  } else {
    // Serialize this rank's rows as one payload (newline-terminated).
    std::string payload;
    for (const std::string& row : rows) {
      payload += row;
      payload += '\n';
    }
    const int len = static_cast<int>(payload.size());
    MPI_Send(&len, 1, MPI_INT, 0, kTag, MPI_COMM_WORLD);
    if (len > 0) {
      MPI_Send(payload.data(), len, MPI_CHAR, 0, kTag, MPI_COMM_WORLD);
    }
  }
}

// ---------------------------------------------------------------------------
// Phase 5: final field (field_final.pvtu + per-rank .vtu pieces)
// ---------------------------------------------------------------------------

void write_field_pvtu(const std::string& path, int nranks) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    std::fprintf(stderr, "cfd_solver: warning: cannot create %s\n",
                 path.c_str());
    return;
  }
  out << "<?xml version=\"1.0\"?>\n";
  out << "<VTKFile type=\"PUnstructuredGrid\" version=\"0.1\" "
         "byte_order=\"LittleEndian\">\n";
  out << "  <PUnstructuredGrid GhostLevel=\"0\">\n";
  out << "    <PPoints>\n";
  out << "      <PDataArray type=\"Float64\" Name=\"Points\" "
         "NumberOfComponents=\"3\"/>\n";
  out << "    </PPoints>\n";
  out << "    <PCellData>\n";
  const char* names[] = {"density",   "velocity_x", "velocity_y", "pressure",
                         "mach",      "temperature", "rank"};
  const char* types[] = {"Float64", "Float64", "Float64", "Float64",
                         "Float64", "Float64", "Int32"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    out << "      <PDataArray type=\"" << types[i] << "\" Name=\"" << names[i]
        << "\"/>\n";
  }
  out << "    </PCellData>\n";
  for (int r = 0; r < nranks; ++r) {
    out << "    <Piece Source=\"field_final_p" << r << ".vtu\"/>\n";
  }
  out << "  </PUnstructuredGrid>\n";
  out << "</VTKFile>\n";
}

void write_field_vtu_piece(const std::string& path, const LocalMesh& lm,
                           const std::vector<ConsState>& U_local,
                           const GasConfig& gas, int rank) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    std::fprintf(stderr, "[rank %d] warning: cannot create %s\n", rank,
                 path.c_str());
    return;
  }
  out.precision(16);
  const int n_points = static_cast<int>(lm.nodes.size());
  const int n_cells = lm.n_owned;

  out << "<?xml version=\"1.0\"?>\n";
  out << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" "
         "byte_order=\"LittleEndian\">\n";
  out << "  <UnstructuredGrid>\n";
  out << "    <Piece NumberOfPoints=\"" << n_points
      << "\" NumberOfCells=\"" << n_cells << "\">\n";
  out << "      <Points>\n";
  out << "        <DataArray type=\"Float64\" Name=\"Points\" "
         "NumberOfComponents=\"3\" format=\"ascii\">\n";
  for (const Vec2& p : lm.nodes) {
    out << p.x << ' ' << p.y << " 0.0\n";
  }
  out << "        </DataArray>\n";
  out << "      </Points>\n";
  out << "      <Cells>\n";
  out << "        <DataArray type=\"Int64\" Name=\"connectivity\" "
         "format=\"ascii\">\n";
  for (int i = 0; i < n_cells; ++i) {
    const Cell& c = lm.cells[i];
    for (uint8_t k = 0; k < c.n_nodes; ++k) {
      out << c.nodes[k] << ' ';
    }
    out << '\n';
  }
  out << "        </DataArray>\n";
  out << "        <DataArray type=\"Int64\" Name=\"offsets\" "
         "format=\"ascii\">\n";
  int offset = 0;
  for (int i = 0; i < n_cells; ++i) {
    offset += lm.cells[i].n_nodes;
    out << offset << '\n';
  }
  out << "        </DataArray>\n";
  out << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
  for (int i = 0; i < n_cells; ++i) {
    out << (lm.cells[i].is_tri() ? 5 : 9) << '\n';  // VTK_TRIANGLE / VTK_QUAD
  }
  out << "        </DataArray>\n";
  out << "      </Cells>\n";
  out << "      <CellData>\n";
  out << "        <DataArray type=\"Float64\" Name=\"density\" "
         "format=\"ascii\">\n";
  for (int i = 0; i < n_cells; ++i) {
    out << U_local[i].rho << '\n';
  }
  out << "        </DataArray>\n";
  out << "        <DataArray type=\"Float64\" Name=\"velocity_x\" "
         "format=\"ascii\">\n";
  for (int i = 0; i < n_cells; ++i) {
    out << U_local[i].rhou / U_local[i].rho << '\n';
  }
  out << "        </DataArray>\n";
  out << "        <DataArray type=\"Float64\" Name=\"velocity_y\" "
         "format=\"ascii\">\n";
  for (int i = 0; i < n_cells; ++i) {
    out << U_local[i].rhov / U_local[i].rho << '\n';
  }
  out << "        </DataArray>\n";
  out << "        <DataArray type=\"Float64\" Name=\"pressure\" "
         "format=\"ascii\">\n";
  for (int i = 0; i < n_cells; ++i) {
    out << pressure_from_cons(U_local[i], gas) << '\n';
  }
  out << "        </DataArray>\n";
  out << "        <DataArray type=\"Float64\" Name=\"mach\" "
         "format=\"ascii\">\n";
  for (int i = 0; i < n_cells; ++i) {
    out << mach_number(U_local[i], gas) << '\n';
  }
  out << "        </DataArray>\n";
  out << "        <DataArray type=\"Float64\" Name=\"temperature\" "
         "format=\"ascii\">\n";
  for (int i = 0; i < n_cells; ++i) {
    out << temperature(U_local[i], gas) << '\n';
  }
  out << "        </DataArray>\n";
  out << "        <DataArray type=\"Int32\" Name=\"rank\" format=\"ascii\">\n";
  for (int i = 0; i < n_cells; ++i) {
    out << rank << '\n';
  }
  out << "        </DataArray>\n";
  out << "      </CellData>\n";
  out << "    </Piece>\n";
  out << "  </UnstructuredGrid>\n";
  out << "</VTKFile>\n";
}

// ---------------------------------------------------------------------------
// Phase 5: restart file (collective MPI-IO)
// ---------------------------------------------------------------------------

void write_restart_binary(const std::string& path,
                          const std::vector<ConsState>& U_local,
                          const LocalMesh& lm, int mpi_ranks,
                          double physical_time) {
  MPI_File fh;
  const int rc =
      MPI_File_open(MPI_COMM_WORLD, path.c_str(),
                    MPI_MODE_CREATE | MPI_MODE_WRONLY, MPI_INFO_NULL, &fh);
  if (rc != MPI_SUCCESS) {
    std::fprintf(stderr, "[rank %d] warning: cannot create %s\n", lm.rank,
                 path.c_str());
    return;
  }

  // 32-byte header: int32 n_owned, int32 nranks, double physical_time,
  // double padding[2] (explicit field writes; no struct-layout assumptions).
  char header[32] = {0};
  if (lm.rank == 0) {
    const int32_t n_owned = static_cast<int32_t>(lm.n_owned);
    const int32_t nranks = static_cast<int32_t>(mpi_ranks);
    std::memcpy(header, &n_owned, sizeof(n_owned));
    std::memcpy(header + 4, &nranks, sizeof(nranks));
    std::memcpy(header + 8, &physical_time, sizeof(physical_time));
    MPI_File_write_at(fh, 0, header, 32, MPI_CHAR, MPI_STATUS_IGNORE);
  }

  // Disjoint per-rank data blocks: rank r's block starts after the header
  // plus the owned cells of all lower ranks.
  long long offset_cells = 0;
  {
    const long long local = lm.n_owned;
    MPI_Exscan(&local, &offset_cells, 1, MPI_LONG_LONG, MPI_SUM,
               MPI_COMM_WORLD);
  }
  std::vector<double> buf(static_cast<size_t>(lm.n_owned) * 4);
  for (int i = 0; i < lm.n_owned; ++i) {
    pack_cons_state(U_local[i], &buf[static_cast<size_t>(i) * 4]);
  }
  const MPI_Offset off =
      32 + offset_cells * 4LL * static_cast<MPI_Offset>(sizeof(double));
  MPI_File_write_at(fh, off, buf.data(), static_cast<int>(buf.size()),
                    MPI_DOUBLE, MPI_STATUS_IGNORE);
  MPI_File_close(&fh);
}

bool read_restart_binary(const std::string& path,
                         std::vector<ConsState>& U_local, const LocalMesh& lm,
                         double& physical_time) {
  MPI_File fh;
  const int rc = MPI_File_open(MPI_COMM_WORLD, path.c_str(), MPI_MODE_RDONLY,
                               MPI_INFO_NULL, &fh);
  if (rc != MPI_SUCCESS) {
    return false;
  }
  char header[32] = {0};
  MPI_Status st;
  MPI_File_read_at(fh, 0, header, 32, MPI_CHAR, &st);
  int32_t n_owned_hdr = 0;
  int32_t nranks_hdr = 0;
  double t_hdr = 0.0;
  std::memcpy(&n_owned_hdr, header, sizeof(n_owned_hdr));
  std::memcpy(&nranks_hdr, header + 4, sizeof(nranks_hdr));
  std::memcpy(&t_hdr, header + 8, sizeof(t_hdr));
  if (nranks_hdr != lm.nranks || n_owned_hdr < 0) {
    if (lm.rank == 0) {
      std::fprintf(stderr,
                   "cfd_solver: restart file '%s': rank-count mismatch "
                   "(file %d, run %d) or invalid header\n",
                   path.c_str(), nranks_hdr, lm.nranks);
    }
    MPI_File_close(&fh);
    return false;
  }
  // The header stores the WRITING rank 0's owned-cell count; the reading
  // rank 0 must see the same count, otherwise the per-rank data blocks (laid
  // out by the writer's partition) cannot be trusted. The verdict is
  // broadcast so every rank returns the same result.
  {
    int hdr_ok = 1;
    if (lm.rank == 0) {
      if (n_owned_hdr != lm.n_owned) {
        hdr_ok = 0;
        std::fprintf(stderr,
                     "cfd_solver: restart file '%s': owned-cell count "
                     "mismatch (file %d, rank 0 has %d); the restart file "
                     "was written by an incompatible partition\n",
                     path.c_str(), n_owned_hdr, lm.n_owned);
      }
    }
    MPI_Allreduce(&hdr_ok, &hdr_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    if (!hdr_ok) {
      MPI_File_close(&fh);
      return false;
    }
  }

  long long offset_cells = 0;
  {
    const long long local = lm.n_owned;
    MPI_Exscan(&local, &offset_cells, 1, MPI_LONG_LONG, MPI_SUM,
               MPI_COMM_WORLD);
  }
  std::vector<double> buf(static_cast<size_t>(lm.n_owned) * 4);
  const MPI_Offset off =
      32 + offset_cells * 4LL * static_cast<MPI_Offset>(sizeof(double));
  MPI_File_read_at(fh, off, buf.data(), static_cast<int>(buf.size()),
                   MPI_DOUBLE, &st);
  int count = 0;
  MPI_Get_count(&st, MPI_DOUBLE, &count);
  MPI_File_close(&fh);
  if (count != static_cast<int>(buf.size())) {
    return false;  // truncated block
  }
  for (int i = 0; i < lm.n_owned; ++i) {
    unpack_cons_state(&buf[static_cast<size_t>(i) * 4], U_local[i]);
  }
  physical_time = t_hdr;
  return true;
}

// ---------------------------------------------------------------------------
// Phase 5: run_status.json
// ---------------------------------------------------------------------------

void write_run_status(const std::string& path, const RunConfig& run_cfg,
                      int mpi_ranks, double wall_time, int final_step,
                      double final_physical_time,
                      const std::string& convergence_status,
                      double residual_reduction_orders,
                      const std::string& command, const std::string& notes) {
  nlohmann::json s;
  s["case_id"] = run_cfg.case_id;
  s["command"] = command;
  s["mpi_ranks"] = mpi_ranks;
  s["wall_time_seconds"] = wall_time;
  s["final_step"] = final_step;
  s["final_physical_time"] = final_physical_time;
  s["convergence_status"] = convergence_status;
  s["residual_reduction_orders"] = residual_reduction_orders;
  s["notes"] = notes;
  write_json_file(path, s);
}

}  // namespace cfd
