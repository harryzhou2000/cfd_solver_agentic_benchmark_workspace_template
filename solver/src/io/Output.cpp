#include "io/Output.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <sstream>

namespace cfds {

namespace {

std::string json_str(const std::string& s) {
  nlohmann::json j = s;
  return j.dump();
}

}  // namespace

std::string utc_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}

void write_csv_headers(const CaseConfig& cfg) {
  std::FILE* f = std::fopen("residuals.csv", "w");
  if (!f) fatal("cannot create residuals.csv");
  std::fprintf(f, "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n");
  std::fclose(f);
  f = std::fopen("forces.csv", "w");
  if (!f) fatal("cannot create forces.csv");
  std::fprintf(f, "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n");
  std::fclose(f);
  (void)cfg;
}

bool read_restart(const std::string& path, const DistributedMesh& mesh,
                  std::vector<ConsVec>& U_owned, int& step, double& time,
                  MPI_Comm comm) {
  int rank = 0, nranks = 0;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nranks);
  std::vector<double> global_U;
  int n_cells = 0;
  bool ok = false;
  if (rank == 0) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
      std::fprintf(stderr, "[cfds] restart file not found: %s\n", path.c_str());
    } else {
      char magic[8] = {0};
      std::fread(magic, 1, 8, f);
      if (std::memcmp(magic, "CFDSRST1", 8) != 0) {
        std::fprintf(stderr, "[cfds] invalid restart magic in %s\n", path.c_str());
      } else {
        int32_t version = 0;
        std::fread(&version, sizeof(version), 1, f);
        std::fread(&n_cells, sizeof(n_cells), 1, f);
        std::fread(&step, sizeof(step), 1, f);
        std::fread(&time, sizeof(time), 1, f);
        global_U.resize(static_cast<size_t>(n_cells) * 4);
        std::fread(global_U.data(), sizeof(double), global_U.size(), f);
        ok = true;
      }
      std::fclose(f);
    }
  }
  MPI_Bcast(&ok, 1, MPI_C_BOOL, 0, comm);
  if (!ok) return false;
  MPI_Bcast(&n_cells, 1, MPI_INT, 0, comm);
  MPI_Bcast(&step, 1, MPI_INT, 0, comm);
  MPI_Bcast(&time, 1, MPI_DOUBLE, 0, comm);
  if (rank != 0) global_U.resize(static_cast<size_t>(n_cells) * 4);
  MPI_Bcast(global_U.data(), static_cast<int>(global_U.size()), MPI_DOUBLE, 0, comm);

  // Distribute owned slices by global cell id.
  const std::vector<int>& gids = mesh.owned_global_ids;
  const int no = mesh.n_owned;
  std::vector<int> counts(nranks), displ(nranks + 1, 0);
  MPI_Allgather(&no, 1, MPI_INT, counts.data(), 1, MPI_INT, comm);
  for (int r = 0; r < nranks; ++r) displ[r + 1] = displ[r] + counts[r];
  std::vector<int> all_gids(displ[nranks]);
  MPI_Gatherv(gids.data(), no, MPI_INT, all_gids.data(), counts.data(),
              displ.data(), MPI_INT, 0, comm);
  U_owned.resize(no);
  if (rank == 0) {
    for (int r = 1; r < nranks; ++r) {
      std::vector<double> slice(static_cast<size_t>(counts[r]) * 4);
      for (int k = 0; k < counts[r]; ++k) {
        const int gid = all_gids[displ[r] + k];
        for (int c = 0; c < 4; ++c) slice[4 * k + c] = global_U[4 * gid + c];
      }
      MPI_Send(slice.data(), static_cast<int>(slice.size()), MPI_DOUBLE, r, 77, comm);
    }
    for (int k = 0; k < counts[0]; ++k) {
      const int gid = all_gids[k];
      for (int c = 0; c < 4; ++c) U_owned[k][c] = global_U[4 * gid + c];
    }
  } else {
    std::vector<double> slice(static_cast<size_t>(no) * 4);
    MPI_Recv(slice.data(), static_cast<int>(slice.size()), MPI_DOUBLE, 0, 77,
             comm, MPI_STATUS_IGNORE);
    for (int k = 0; k < no; ++k)
      for (int c = 0; c < 4; ++c) U_owned[k][c] = slice[4 * k + c];
  }
  return true;
}

void write_outputs(const OutputContext& ctx) {
  const int rank = ctx.mesh.rank;

  // -------------------------------------------------------------------------
  // partition_diagnostics.csv (all ranks contribute; rank 0 writes).
  // -------------------------------------------------------------------------
  {
    // Gather per-rank row strings.
    std::string row = std::to_string(ctx.mesh.rank) + "," +
        std::to_string(ctx.mesh.n_owned) + "," +
        std::to_string(ctx.mesh.n_ghost) + "," +
        std::to_string(ctx.mesh.num_boundary_faces) + "," +
        std::to_string(ctx.mesh.neighbor_ranks.size()) + ",";
    for (size_t i = 0; i < ctx.mesh.neighbor_ranks.size(); ++i) {
      if (i) row += ";";
      row += std::to_string(ctx.mesh.neighbor_ranks[i]);
    }
    row += "," + std::to_string(ctx.mesh.send_count) + "," +
           std::to_string(ctx.mesh.recv_count);

    const int len = static_cast<int>(row.size());
    MPI_Comm comm = ctx.comm;
    int nranks = 0;
    MPI_Comm_size(comm, &nranks);
    std::vector<int> lens(nranks);
    MPI_Allgather(&len, 1, MPI_INT, lens.data(), 1, MPI_INT, comm);
    std::vector<int> displ(nranks + 1, 0);
    for (int r = 0; r < nranks; ++r) displ[r + 1] = displ[r] + lens[r];
    std::vector<char> all(displ[nranks]);
    MPI_Gatherv(row.data(), len, MPI_CHAR, all.data(), lens.data(), displ.data(),
                MPI_CHAR, 0, comm);
    if (rank == 0) {
      std::FILE* f = std::fopen("partition_diagnostics.csv", "w");
      if (!f) fatal("cannot create partition_diagnostics.csv");
      std::fprintf(f, "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n");
      for (int r = 0; r < nranks; ++r) {
        std::fwrite(all.data() + displ[r], 1, lens[r], f);
        std::fputc('\n', f);
      }
      std::fclose(f);
    }
  }

  // -------------------------------------------------------------------------
  // surface.csv: gather wall-face rows from every rank.
  // -------------------------------------------------------------------------
  {
    std::ostringstream rows;
    const double q_inf = 0.5 * ctx.cfg.rho_inf *
        (ctx.cfg.u_inf * ctx.cfg.u_inf + ctx.cfg.v_inf * ctx.cfg.v_inf);
    const Vec2 flow_dir{ctx.cfg.u_inf, ctx.cfg.v_inf};
    const double umag = std::sqrt(flow_dir[0] * flow_dir[0] + flow_dir[1] * flow_dir[1]);
    const int no = ctx.mesh.n_owned;
    for (const auto& f : ctx.mesh.faces) {
      if (f.cellR >= 0) continue;
      const BcType bc = static_cast<BcType>(f.bc);
      if (bc != BcType::SlipWall && bc != BcType::NoSlipAdiabaticWall) continue;
      const int L = f.cellL;
      const Primitive pc = ctx.gas.to_primitive(ctx.solver.state()[L]);
      // Wall pressure from the cell-average state, consistent with the wall
      // boundary flux used in the residual.
      const double pw = pc.p;
      const double cp = (pw - ctx.cfg.p_inf) / q_inf;
      // Body normal (into the fluid).
      const Vec2 nb{-f.normal[0], -f.normal[1]};
      double u_b, v_b, mach_b;
      double cf = 0.0;
      if (bc == BcType::NoSlipAdiabaticWall) {
        u_b = 0.0;
        v_b = 0.0;
        mach_b = 0.0;
        if (ctx.gas.viscous) {
          Vec2 tangent;
          const double dx = f.centroid[0] - ctx.mesh.cell_centroid[L][0];
          const double dy = f.centroid[1] - ctx.mesh.cell_centroid[L][1];
          const double d = std::fabs(dx * f.normal[0] + dy * f.normal[1]);
          const double d_eff = std::max(
              d, 0.5 * std::sqrt(std::max(ctx.mesh.cell_volume[L], 1e-30)));
          cf = wall_shear_coefficient(ctx.gas, pc, nb, d_eff, flow_dir,
                                      tangent) / q_inf;
        }
      } else {
        // Slip wall: tangential velocity only.
        const double vn = pc.u * nb[0] + pc.v * nb[1];
        u_b = pc.u - vn * nb[0];
        v_b = pc.v - vn * nb[1];
        mach_b = std::sqrt(u_b * u_b + v_b * v_b) / ctx.gas.sound_speed(pc);
      }
      const std::string tag = ctx.mesh.family_names[f.family_id];
      rows << f.centroid[0] << "," << f.centroid[1] << "," << nb[0] << "," << nb[1]
           << "," << pw << "," << cp << "," << cf << "," << pc.rho << ","
           << u_b << "," << v_b << "," << mach_b << "," << tag << "\n";
    }
    const std::string payload = rows.str();
    const int len = static_cast<int>(payload.size());
    int nranks = 0;
    MPI_Comm_size(ctx.comm, &nranks);
    std::vector<int> lens(nranks), displ(nranks + 1, 0);
    MPI_Allgather(&len, 1, MPI_INT, lens.data(), 1, MPI_INT, ctx.comm);
    for (int r = 0; r < nranks; ++r) displ[r + 1] = displ[r] + lens[r];
    std::vector<char> all(displ[nranks]);
    MPI_Gatherv(payload.data(), len, MPI_CHAR, all.data(), lens.data(),
                displ.data(), MPI_CHAR, 0, ctx.comm);
    if (rank == 0) {
      std::FILE* f = std::fopen("surface.csv", "w");
      if (!f) fatal("cannot create surface.csv");
      std::fprintf(f, "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n");
      std::fwrite(all.data(), 1, all.size(), f);
      std::fclose(f);
    }
  }

  // -------------------------------------------------------------------------
  // field_final.vtu: gather per-cell data into global cell order (rank 0).
  // -------------------------------------------------------------------------
  {
    const int no = ctx.mesh.n_owned;
    std::vector<double> local(no * 8);  // rho, u, v, p, mach, T, rank, psi
    for (int i = 0; i < no; ++i) {
      const Primitive pc = ctx.gas.to_primitive(ctx.solver.state()[i]);
      local[8 * i + 0] = pc.rho;
      local[8 * i + 1] = pc.u;
      local[8 * i + 2] = pc.v;
      local[8 * i + 3] = pc.p;
      local[8 * i + 4] = ctx.gas.mach(pc);
      local[8 * i + 5] = ctx.gas.temperature(pc);
      local[8 * i + 6] = static_cast<double>(ctx.mesh.rank);
      local[8 * i + 7] = ctx.solver.limiters()[i];
    }
    // Gather (global ids, data) pairs to rank 0.
    const std::vector<int>& gids = ctx.mesh.owned_global_ids;
    int nranks = 0;
    MPI_Comm_size(ctx.comm, &nranks);
    std::vector<int> counts(nranks), displ(nranks + 1, 0);
    MPI_Allgather(&no, 1, MPI_INT, counts.data(), 1, MPI_INT, ctx.comm);
    for (int r = 0; r < nranks; ++r) displ[r + 1] = displ[r] + counts[r];
    std::vector<int> all_gids(displ[nranks]);
    MPI_Gatherv(gids.data(), no, MPI_INT, all_gids.data(), counts.data(),
                displ.data(), MPI_INT, 0, ctx.comm);
    std::vector<int> d7(nranks + 1, 0);
    std::vector<int> c7(nranks);
    for (int r = 0; r < nranks; ++r) d7[r + 1] = d7[r] + 8 * counts[r];
    for (int r = 0; r < nranks; ++r) c7[r] = 8 * counts[r];
    std::vector<double> all_data7(d7[nranks]);
    MPI_Gatherv(local.data(), no * 8, MPI_DOUBLE, all_data7.data(),
                c7.data(), d7.data(), MPI_DOUBLE, 0, ctx.comm);
    if (rank == 0) {
      // Reorder by global cell id.
      const int nc = static_cast<int>(ctx.global->cells.size());
      std::vector<double> by_id(nc * 8, 0.0);
      for (int r = 0; r < nranks; ++r) {
        for (int k = 0; k < counts[r]; ++k) {
          const int gid = all_gids[displ[r] + k];
          for (int c = 0; c < 8; ++c)
            by_id[8 * gid + c] = all_data7[8 * (displ[r] + k) + c];
        }
      }
      // Write VTU (ASCII unstructured grid).
      const auto& gm = *ctx.global;
      std::FILE* f = std::fopen("field_final.vtu", "w");
      if (!f) fatal("cannot create field_final.vtu");
      std::fprintf(f, "<?xml version=\"1.0\"?>\n");
      std::fprintf(f, "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n");
      std::fprintf(f, "  <UnstructuredGrid>\n");
      std::fprintf(f, "    <Piece NumberOfPoints=\"%zu\" NumberOfCells=\"%zu\">\n",
                   gm.nodes.size(), gm.cells.size());
      std::fprintf(f, "      <Points>\n");
      std::fprintf(f, "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n");
      for (const auto& p : gm.nodes)
        std::fprintf(f, "%.10e %.10e 0\n", p[0], p[1]);
      std::fprintf(f, "        </DataArray>\n      </Points>\n");
      std::fprintf(f, "      <Cells>\n");
      std::fprintf(f, "        <DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n");
      for (const auto& c : gm.cells) {
        for (int nd : c.nodes) std::fprintf(f, "%d ", nd);
        std::fprintf(f, "\n");
      }
      std::fprintf(f, "        </DataArray>\n");
      std::fprintf(f, "        <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n");
      int off = 0;
      for (const auto& c : gm.cells) {
        off += static_cast<int>(c.nodes.size());
        std::fprintf(f, "%d\n", off);
      }
      std::fprintf(f, "        </DataArray>\n");
      std::fprintf(f, "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n");
      for (const auto& c : gm.cells)
        std::fprintf(f, "%d\n", c.nodes.size() == 3 ? 5 : 9);
      std::fprintf(f, "        </DataArray>\n      </Cells>\n");
      std::fprintf(f, "      <CellData>\n");
      auto write_array = [&](const char* name, int ncomp, int var) {
        std::fprintf(f, "        <DataArray type=\"Float64\" Name=\"%s\" NumberOfComponents=\"%d\" format=\"ascii\">\n",
                     name, ncomp);
        for (int i = 0; i < nc; ++i) {
          std::fprintf(f, "%.10e", by_id[8 * i + var]);
          if (ncomp == 2) std::fprintf(f, " %.10e", by_id[8 * i + var + 1]);
          std::fprintf(f, "\n");
        }
        std::fprintf(f, "        </DataArray>\n");
      };
      write_array("Density", 1, 0);
      write_array("Velocity", 2, 1);
      write_array("Pressure", 1, 3);
      write_array("Mach", 1, 4);
      write_array("Temperature", 1, 5);
      std::fprintf(f, "        <DataArray type=\"Float64\" Name=\"Limiter\" format=\"ascii\">\n");
      for (int i = 0; i < nc; ++i)
        std::fprintf(f, "%.10e\n", by_id[8 * i + 7]);
      std::fprintf(f, "        </DataArray>\n");
      std::fprintf(f, "        <DataArray type=\"Int32\" Name=\"rank_id\" format=\"ascii\">\n");
      for (int i = 0; i < nc; ++i)
        std::fprintf(f, "%d\n", static_cast<int>(std::lround(by_id[8 * i + 6])));
      std::fprintf(f, "        </DataArray>\n      </CellData>\n");
      std::fprintf(f, "    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n");
      std::fclose(f);
    }
  }

  // -------------------------------------------------------------------------
  // restart_final.bin (rank 0 gathers and writes a single binary file).
  // -------------------------------------------------------------------------
  {
    const int no = ctx.mesh.n_owned;
    const std::vector<int>& gids = ctx.mesh.owned_global_ids;
    int nranks = 0;
    MPI_Comm_size(ctx.comm, &nranks);
    std::vector<int> counts(nranks), displ(nranks + 1, 0);
    MPI_Allgather(&no, 1, MPI_INT, counts.data(), 1, MPI_INT, ctx.comm);
    for (int r = 0; r < nranks; ++r) displ[r + 1] = displ[r] + counts[r];
    std::vector<int> all_gids(displ[nranks]);
    std::vector<double> local(no * 4);
    for (int i = 0; i < no; ++i)
      for (int c = 0; c < 4; ++c) local[4 * i + c] = ctx.solver.state()[i][c];
    MPI_Gatherv(gids.data(), no, MPI_INT, all_gids.data(), counts.data(),
                displ.data(), MPI_INT, 0, ctx.comm);
    std::vector<int> d4(nranks + 1, 0);
    std::vector<int> c4(nranks);
    for (int r = 0; r < nranks; ++r) d4[r + 1] = d4[r] + 4 * counts[r];
    for (int r = 0; r < nranks; ++r) c4[r] = 4 * counts[r];
    std::vector<double> allU(d4[nranks]);
    MPI_Gatherv(local.data(), no * 4, MPI_DOUBLE, allU.data(), c4.data(),
                d4.data(), MPI_DOUBLE, 0, ctx.comm);
    if (rank == 0) {
      const int nc = static_cast<int>(ctx.global->cells.size());
      std::vector<double> by_id(nc * 4, 0.0);
      for (int r = 0; r < nranks; ++r)
        for (int k = 0; k < counts[r]; ++k) {
          const int gid = all_gids[displ[r] + k];
          for (int c = 0; c < 4; ++c) by_id[4 * gid + c] = allU[4 * (displ[r] + k) + c];
        }
      std::FILE* f = std::fopen("restart_final.bin", "wb");
      if (!f) fatal("cannot create restart_final.bin");
      const char magic[8] = {'C', 'F', 'D', 'S', 'R', 'S', 'T', '1'};
      std::fwrite(magic, 1, 8, f);
      int32_t version = 1;
      std::fwrite(&version, sizeof(version), 1, f);
      int32_t n = nc;
      std::fwrite(&n, sizeof(n), 1, f);
      int32_t step = ctx.stats.final_step;
      std::fwrite(&step, sizeof(step), 1, f);
      double time = ctx.stats.final_physical_time;
      std::fwrite(&time, sizeof(time), 1, f);
      std::fwrite(by_id.data(), sizeof(double), by_id.size(), f);
      std::fclose(f);
    }
  }

  // -------------------------------------------------------------------------
  // metadata.json and run_status.json (rank 0).
  // -------------------------------------------------------------------------
  if (rank == 0) {
    nlohmann::json md;
    md["case_id"] = ctx.cfg.case_id;
    md["solver_name"] = "cfd_solver";
    md["solver_version"] = "1.0.0";
    md["git_revision"] = ctx.git_revision.empty() ? nullptr : ctx.git_revision;
    md["mpi_ranks"] = ctx.mesh.n_owned ? ctx.mesh.rank + 1 : 0;
    int nranks = 0;
    MPI_Comm_size(ctx.comm, &nranks);
    md["mpi_ranks"] = nranks;
    md["mesh_file"] = ctx.cfg.mesh_file;
    md["num_cells_global"] = ctx.global ? static_cast<int>(ctx.global->cells.size()) : 0;
    md["num_faces_global"] = ctx.global ? static_cast<int>(ctx.global->faces.size()) : 0;
    md["num_cells_owned_local"] = ctx.mesh.n_owned;
    md["num_cells_ghost_local"] = ctx.mesh.n_ghost;
    md["partitioner"] = "metis_kway";
    md["partition_edge_cut"] = ctx.edge_cut;
    md["halo_exchange"] = "neighbor_isend_irecv";
    md["full_state_replication_during_iterations"] = false;
    md["full_mesh_replication_during_iterations"] = false;
    md["equation_set"] = "compressible_navier_stokes_2d";
    md["inviscid_flux"] = "rusanov_llf";
    md["entropy_fix"] = nullptr;
    md["viscous_flux"] = ctx.gas.viscous ? "laminar_newtonian_fourier" : "none";
    md["time_integrator"] = ctx.cfg.steady ? "implicit_euler_local_time_stepping"
                                           : "bdf2_dual_time";
    md["implicit_solver"] = "damped_block_jacobi_defect_correction";
    md["reconstruction"] = "weighted_least_squares_piecewise_linear";
    md["limiter"] = "barth_jespersen_with_positivity_fallback";
    md["spatial_order_claimed"] = 2;
    md["positivity_preservation"] = "barth_jespersen_limiter_with_face_positivity_clamp_and_update_bisection";
    md["wall_boundary_output_semantics"] = "boundary_value";
    if (!ctx.cfg.steady) {
      md["true_bdf2_inner_loop"] = true;
      md["min_inner_iterations"] = ctx.cfg.min_inner_iterations;
      md["max_inner_iterations"] = ctx.cfg.max_inner_iterations;
      md["observed_min_inner_iterations"] =
          ctx.stats.min_inner == INT_MAX ? 0 : ctx.stats.min_inner;
      md["observed_max_inner_iterations"] = ctx.stats.max_inner;
      md["observed_mean_inner_iterations"] = ctx.stats.mean_inner;
      md["inner_residual_reduction_target"] = ctx.cfg.inner_residual_reduction_target;
      md["inner_target_misses"] = ctx.stats.target_misses;
      md["inner_target_converged_fraction"] =
          ctx.stats.outer_steps_with_inner > 0
              ? static_cast<double>(ctx.stats.inner_target_converged_steps) /
                    ctx.stats.outer_steps_with_inner
              : 0.0;
      md["last_inner_residual_ratio"] = ctx.stats.last_inner_residual_ratio;
    } else {
      md["typical_inner_iterations"] = ctx.stats.mean_inner;
      md["min_inner_iterations"] = ctx.cfg.min_inner_iterations;
      md["max_inner_iterations"] = ctx.cfg.max_inner_iterations;
      md["observed_min_inner_iterations"] =
          ctx.stats.min_inner == INT_MAX ? 0 : ctx.stats.min_inner;
      md["observed_max_inner_iterations"] = ctx.stats.max_inner;
      md["inner_residual_reduction_target"] = ctx.cfg.inner_residual_reduction_target;
      md["inner_target_misses"] = ctx.stats.target_misses;
      md["inner_target_converged_fraction"] =
          ctx.stats.outer_steps_with_inner > 0
              ? static_cast<double>(ctx.stats.inner_target_converged_steps) /
                    ctx.stats.outer_steps_with_inner
              : 0.0;
      md["last_inner_residual_ratio"] = ctx.stats.last_inner_residual_ratio;
    }
    md["start_time_utc"] = ctx.start_utc;
    md["end_time_utc"] = ctx.end_utc;
    md["completed"] = true;
    md["convergence_status"] = ctx.stats.convergence_status;
    md["load_balance_ratio"] = ctx.summary.load_balance;
    md["min_owned_per_rank"] = ctx.summary.min_owned;
    md["max_owned_per_rank"] = ctx.summary.max_owned;
    md["mean_owned_per_rank"] = ctx.summary.mean_owned;
    md["total_boundary_faces"] = ctx.summary.total_boundary_faces;
    {
      std::FILE* f = std::fopen("metadata.json", "w");
      if (!f) fatal("cannot create metadata.json");
      std::fputs(md.dump(2).c_str(), f);
      std::fputc('\n', f);
      std::fclose(f);
    }

    nlohmann::json rs;
    rs["case_id"] = ctx.cfg.case_id;
    rs["command"] = ctx.command;
    rs["mpi_ranks"] = nranks;
    rs["wall_time_seconds"] = ctx.stats.wall_time_seconds;
    rs["final_step"] = ctx.stats.final_step;
    rs["final_physical_time"] = ctx.stats.final_physical_time;
    rs["convergence_status"] = ctx.stats.convergence_status;
    rs["residual_reduction_orders"] = ctx.stats.residual_reduction_orders;
    std::ostringstream notes;
    notes << "initial L2 residual " << ctx.stats.initial_residual_l2
          << ", final L2 residual " << ctx.stats.last_residual_l2
          << ", mean inner iterations " << ctx.stats.mean_inner
          << ", positivity fallbacks " << ctx.stats.positivity_fallbacks
          << ", first-order fallback cells " << ctx.stats.first_order_fallback_cells;
    rs["notes"] = notes.str();
    {
      std::FILE* f = std::fopen("run_status.json", "w");
      if (!f) fatal("cannot create run_status.json");
      std::fputs(rs.dump(2).c_str(), f);
      std::fputc('\n', f);
      std::fclose(f);
    }
  }
}

}  // namespace cfds
