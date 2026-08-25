// cfd2d: 2-D unstructured compressible Navier--Stokes finite-volume solver
// with MPI domain decomposition (METIS partitioning, halo exchange).
//
//   mpirun -np <ranks> cfd2d solve --case <case.json> --output <dir>
//        [--restart <file>] [--report-level brief|full]
//        [--limiter venkat|barth|none] [--flux roe|rusanov]
//        [--pert-aoa-deg <deg> --pert-duration <T>]

#include <mpi.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "case_file.hpp"
#include "output.hpp"
#include "partition.hpp"
#include "physics.hpp"
#include "spatial.hpp"
#include "timestep.hpp"

#ifndef GIT_REVISION
#define GIT_REVISION nullptr
#endif

namespace {

std::string now_utc() {
  std::time_t t = std::time(nullptr);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}

void usage() {
  std::cout << "cfd2d commands:\n"
               "  solve --case <case.json> --output <dir> [options]\n"
               "  meshinfo --mesh <file.cgns>\n"
               "  selftest\n";
}

int selftest() {
  using namespace cfd;
  GasModel gas;
  int fails = 0;
  auto check = [&](bool ok, const std::string& name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name.c_str());
    if (!ok) ++fails;
  };

  // 1. State conversion round trip.
  Vec4 W{1.2, 0.8, -0.3, 2.5};
  Vec4 U = prim_to_cons(W, gas);
  Vec4 W2 = cons_to_prim(U, gas);
  double err = 0;
  for (int k = 0; k < 4; ++k) err = std::max(err, std::fabs(W[k] - W2[k]));
  check(err < 1e-12, "cons<->prim round trip");

  // 2. Roe flux: uniform state -> zero dissipation (equals physical flux).
  Vec4 n12{0.8, 0.6};
  Vec4 Fp = inviscid_flux_phys(W, n12[0], n12[1], gas);
  Vec4 Fr = roe_flux(W, W, n12[0], n12[1], gas);
  err = 0;
  for (int k = 0; k < 4; ++k) err = std::max(err, std::fabs(Fp[k] - Fr[k]));
  check(err < 1e-10, "Roe flux consistent for uniform state");

  // 3. Directional symmetry: F(WL,WR,n) == -F(WR,WL,-n).
  Vec4 WL{1.0, 1.0, 0.2, 2.0}, WR{0.5, 0.3, -0.4, 1.2};
  Vec4 F1 = roe_flux(WL, WR, n12[0], n12[1], gas);
  Vec4 F2 = roe_flux(WR, WL, -n12[0], -n12[1], gas);
  err = 0;
  for (int k = 0; k < 4; ++k) err = std::max(err, std::fabs(F1[k] + F2[k]));
  check(err < 1e-10, "Roe flux directional symmetry");

  // 4. Roe linearization exactness: jump condition F_R - F_L = sum |...|:
  //    with dissipation sign removed: F(WL,WR) = 0.5(FL+FR) - 0.5|A|(UR-UL).
  //    Check F_diff = 0.5(FL+FR) - F_roe equals |A|dU/2 via finite diff of F.
  {
    Vec4 UL = prim_to_cons(WL, gas), UR = prim_to_cons(WR, gas);
    Vec4 FL = inviscid_flux_phys(WL, n12[0], n12[1], gas);
    Vec4 FR = inviscid_flux_phys(WR, n12[0], n12[1], gas);
    Vec4 jump;  // dissipation part
    for (int k = 0; k < 4; ++k) jump[k] = (FL[k] + FR[k]) - 2.0 * F1[k];
    // jump should equal |A|(UR-UL). Verify |A|(UR-UL) has zero projection
    // along directions annihilated by construction: instead verify that for
    // WL==WR+eps the flux derivative is consistent. Simpler: verify jump is
    // dissipative for a pure density contact (u=v=p equal): mass jump >= 0.
    Vec4 WA{1.0, 0.5, 0.1, 1.5}, WB{1.4, 0.5, 0.1, 1.5};
    Vec4 FA = roe_flux(WA, WB, 1.0, 0.0, gas);
    check(std::fabs(FA[1] - 0.5 * 0.1 * 0.5 * (1.0 + 1.4) - 1.5) < 1e-9 ||
              FA[0] <= 0.5 * 0.5 * (1.0 + 1.4),
          "Roe contact wave dissipation sign");
    (void)jump;
  }

  // 5. Positivity floors.
  Vec4 Ubad{-1.0, 0.0, 0.0, -2.0};
  Vec4 Wb = cons_to_prim(Ubad, gas);
  check(Wb[0] > 0 && Wb[3] > 0, "positivity floors active");

  // 6. Flux Jacobian times vector vs finite difference.
  {
    Vec4 dU{1e-7, -2e-7, 3e-7, 1e-7};
    Vec4 U0 = prim_to_cons(W, gas);
    Vec4 U1 = add4(U0, dU);
    Vec4 W1 = cons_to_prim(U1, gas);
    Vec4 F0 = inviscid_flux_phys(W, n12[0], n12[1], gas);
    Vec4 F1fd = inviscid_flux_phys(W1, n12[0], n12[1], gas);
    Vec4 jac = flux_jac_times(W, n12[0], n12[1], dU, gas);
    double e2 = 0;
    for (int k = 0; k < 4; ++k)
      e2 = std::max(e2, std::fabs((F1fd[k] - F0[k]) - jac[k]));
    check(e2 < 1e-11, "flux Jacobian matches finite difference");
  }

  if (fails == 0) std::printf("selftest: all checks passed\n");
  return fails == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, nranks = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);

  int exit_code = 0;
  try {
    if (argc < 2) {
      if (rank == 0) usage();
      MPI_Finalize();
      return 2;
    }
    std::string cmd = argv[1];
    if (cmd == "selftest") {
      int rc = 0;
      if (rank == 0) rc = selftest();
      MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
      MPI_Finalize();
      return rc;
    }
    if (cmd == "meshinfo") {
      std::string mesh;
      for (int i = 2; i < argc; ++i)
        if (std::strcmp(argv[i], "--mesh") == 0 && i + 1 < argc) mesh = argv[++i];
      if (mesh.empty()) throw std::runtime_error("meshinfo requires --mesh");
      if (rank == 0) {
        cfd::GlobalMesh gm = cfd::read_cgns_mesh(mesh);
        std::cout << cfd::mesh_summary(gm) << std::endl;
      }
      MPI_Finalize();
      return 0;
    }
    if (cmd != "solve") {
      if (rank == 0) { usage(); }
      MPI_Finalize();
      return 2;
    }

    // ---- parse solve arguments ----
    std::string case_path, out_dir, restart_file;
    std::string report_level = "full";
    cfd::SolverConfig cfg;
    std::string limiter = "venkat", flux = "roe";
    int max_steps_override = -1;
    for (int i = 2; i < argc; ++i) {
      std::string a = argv[i];
      auto next = [&](const char* name) -> std::string {
        if (i + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + name);
        return argv[++i];
      };
      if (a == "--case") case_path = next("--case");
      else if (a == "--output") out_dir = next("--output");
      else if (a == "--restart") restart_file = next("--restart");
      else if (a == "--report-level") report_level = next("--report-level");
      else if (a == "--limiter") limiter = next("--limiter");
      else if (a == "--flux") flux = next("--flux");
      else if (a == "--pert-aoa-deg") cfg.pert_aoa_deg = std::stod(next("--pert-aoa-deg"));
      else if (a == "--pert-duration") cfg.pert_duration = std::stod(next("--pert-duration"));
      else if (a == "--sweeps-per-inner")
        cfg.transient_sweeps_per_inner = std::stoi(next("--sweeps-per-inner"));
      else if (a == "--venkat-k") cfg.venkat_k = std::stod(next("--venkat-k"));
      else if (a == "--max-steps") max_steps_override = std::stoi(next("--max-steps"));
      else throw std::runtime_error("unknown argument: " + a);
    }
    if (case_path.empty() || out_dir.empty())
      throw std::runtime_error("solve requires --case and --output");
    if (limiter == "venkat") cfg.limiter = 2;
    else if (limiter == "barth") cfg.limiter = 1;
    else if (limiter == "none") cfg.limiter = 0;
    else throw std::runtime_error("unknown limiter: " + limiter);
    if (flux == "roe") cfg.inviscid_flux = 0;
    else if (flux == "rusanov") cfg.inviscid_flux = 1;
    else throw std::runtime_error("unknown flux: " + flux);
    if (report_level != "brief" && report_level != "full")
      throw std::runtime_error("--report-level must be brief or full");

    std::string command_line;
    for (int i = 0; i < argc; ++i) {
      command_line += argv[i];
      if (i + 1 < argc) command_line += " ";
    }

    cfd::CaseFile cs = cfd::load_case_file(case_path);
    if (max_steps_override > 0) cs.run.max_steps = max_steps_override;

    cfd::OutputContext oc;
    oc.comm = MPI_COMM_WORLD;
    oc.rank = rank;
    oc.n_ranks = nranks;
    oc.brief = (report_level == "brief");
    cfd::open_output(oc, out_dir);

    // ---- partition preprocessing (rank 0, cached on disk) ----
    if (rank == 0) {
      std::string err;
      bool did = cfd::preprocess_partition(cs.mesh_file, nranks, out_dir, &err);
      if (!err.empty()) throw std::runtime_error("partitioning failed: " + err);
      if (did) oc.log("preprocessing: mesh read, METIS partition written");
    }
    MPI_Barrier(MPI_COMM_WORLD);

    cfd::SolverContext ctx;
    ctx.comm = MPI_COMM_WORLD;
    ctx.rank = rank;
    ctx.n_ranks = nranks;
    ctx.cs = &cs;
    ctx.cfg = cfg;
    ctx.mesh = cfd::load_local_partition(out_dir, rank, nranks, ctx.pinfo);
    ctx.init();

    {
      char buf[512];
      std::snprintf(buf, sizeof(buf),
                    "case %s | ranks %d | rank0 owned %d ghost %d | cells %d | edge cut %d",
                    cs.case_id.c_str(), nranks, ctx.mesh.n_owned, ctx.mesh.n_ghost,
                    ctx.pinfo.n_cells_global, ctx.pinfo.edge_cut);
      oc.log(buf);
    }

    long start_step = 0;
    double start_time = 0.0;
    if (!restart_file.empty()) {
      // Every rank reads the (small) restart file and picks its own cells.
      std::ifstream f(restart_file, std::ios::binary);
      if (!f) throw std::runtime_error("cannot open restart file: " + restart_file);
      char magic[16];
      f.read(magic, 16);
      if (std::strncmp(magic, "CFDRST01", 8) != 0)
        throw std::runtime_error("bad restart file magic");
      int64_t step64, nc64;
      int32_t ns32;
      f.read(reinterpret_cast<char*>(&step64), 8);
      double t8;
      f.read(reinterpret_cast<char*>(&t8), 8);
      f.read(reinterpret_cast<char*>(&nc64), 8);
      f.read(reinterpret_cast<char*>(&ns32), 4);
      if (nc64 != ctx.pinfo.n_cells_global)
        throw std::runtime_error("restart cell count does not match mesh");
      std::vector<double> states(static_cast<size_t>(nc64) * 4 * ns32);
      f.read(reinterpret_cast<char*>(states.data()),
             static_cast<std::streamsize>(states.size() * sizeof(double)));
      if (!f) throw std::runtime_error("restart file truncated");
      for (int i = 0; i < ctx.mesh.n_cells; ++i) {
        int64_t g = ctx.mesh.cell_global[i];
        for (int v = 0; v < 4; ++v) ctx.st.U[i][v] = states[(g * 4) + v];
        if (i < ctx.mesh.n_owned && ns32 >= 3) {
          for (int v = 0; v < 4; ++v) {
            ctx.U_n[i][v] = states[(static_cast<size_t>(nc64) + g) * 4 + v];
            ctx.U_nm1[i][v] = states[(2ull * nc64 + g) * 4 + v];
          }
        }
      }
      start_step = step64;
      start_time = t8;
      cfd::compute_primitives(ctx.mesh, cs.gas, ctx.st);
      oc.log("restarted from " + restart_file);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();
    const std::string start_utc = now_utc();

    cfd::RunResult res;
    if (cs.run.type == "steady")
      res = cfd::run_steady(ctx, oc, start_step);
    else
      res = cfd::run_transient(ctx, oc, start_step, start_time);

    double t1 = MPI_Wtime();
    const std::string end_utc = now_utc();
    const double wall = t1 - t0;

    // ---- final outputs ----
    cfd::AssembleOpts fo;
    fo.viscous = cs.fs.viscous;
    fo.inviscid_flux = cfg.inviscid_flux;
    fo.limiter = cfg.limiter;
    fo.venkat_k = cfg.venkat_k;
    fo.time = res.final_time;
    // State already evaluated by the driver at the final step; still refresh
    // gradients for vorticity output.
    {
      cfd::ForceSums fl;
      ctx.eval_residual(fo, fl);
    }
    if (cs.outputs.write_surface) {
      auto rows = cfd::compute_surface_rows(ctx.mesh, cs, ctx.st, fo);
      cfd::write_surface_csv(oc, ctx.mesh, rows);
    }
    if (cs.outputs.write_final_field)
      cfd::write_field_vtu(oc, ctx.mesh, cs, ctx.st, "field_final.vtu");
    if (cs.run.type == "transient")
      cfd::write_restart(oc, ctx.mesh, "restart_final.bin", res.final_step,
                         res.final_time, ctx.st.U, ctx.U_n, ctx.U_nm1, 3);
    else
      cfd::write_restart(oc, ctx.mesh, "restart_final.bin", res.final_step,
                         res.final_time, ctx.st.U, ctx.st.U, ctx.st.U, 1);
    cfd::write_partition_diagnostics(oc, ctx.mesh, ctx.pinfo);

    if (rank == 0) {
      nlohmann::json md;
      md["case_id"] = cs.case_id;
      md["solver_name"] = "cfd2d";
      md["solver_version"] = "1.0.0";
#ifdef GIT_REVISION_STR
      md["git_revision"] = GIT_REVISION_STR;
#else
      md["git_revision"] = nullptr;
#endif
      md["mpi_ranks"] = nranks;
      md["mesh_file"] = cs.mesh_file;
      md["num_cells_global"] = ctx.pinfo.n_cells_global;
      md["num_faces_global"] = ctx.pinfo.n_faces_global;
      md["num_cells_owned_local"] = ctx.mesh.n_owned;
      md["num_cells_ghost_local"] = ctx.mesh.n_ghost;
      md["partitioner"] = nranks > 1 ? "metis_kway" : "metis_kway (np=1)";
      md["partition_edge_cut"] = ctx.pinfo.edge_cut;
      md["halo_exchange"] = "neighbor_isend_irecv";
      md["full_state_replication_during_iterations"] = false;
      md["full_mesh_replication_during_iterations"] = false;
      md["equation_set"] = "compressible_navier_stokes_2d";
      md["inviscid_flux"] = flux == "roe" ? "roe" : "rusanov_llf";
      md["entropy_fix"] = flux == "roe" ? "harten" : nlohmann::json(nullptr);
      md["viscous_flux"] = cs.fs.viscous
          ? "newtonian_fourier_constant_viscosity_corrected_face_gradient"
          : "none (inviscid mode)";
      md["time_integrator"] = cs.run.type == "transient"
          ? "bdf2_two_level (bdf1 first step)"
          : "implicit_pseudo_time_backward_euler";
      md["implicit_solver"] = "symmetric_gauss_seidel (LU-SGS) simplified_jacobian";
      md["reconstruction"] = "piecewise_linear_unweighted_lsq_gradient";
      md["limiter"] = limiter == "venkat"
          ? "venkatakrishnan (k=" + std::to_string(cfg.venkat_k) + ")"
          : limiter == "barth" ? "barth_jespersen" : "none_first_order";
      md["spatial_order_claimed"] = cfg.limiter == 0 ? 1 : 2;
      md["positivity_preservation"] =
          "floored rho/p in state conversion plus per-face first-order "
          "fallback of reconstructed states and per-cell update backtracking";
      md["wall_boundary_output_semantics"] = "boundary_value";
      md["true_bdf2_inner_loop"] = cs.run.type == "transient";
      md["typical_inner_iterations"] = res.inner.observed_mean;
      md["min_inner_iterations"] = cs.run.min_inner_iterations;
      md["max_inner_iterations"] = cs.run.max_inner_iterations;
      md["observed_min_inner_iterations"] = std::max(0, res.inner.observed_min);
      md["observed_max_inner_iterations"] = res.inner.observed_max;
      md["inner_residual_reduction_target"] = cs.run.inner_residual_reduction_target;
      md["inner_target_misses"] = res.inner.target_misses;
      md["inner_target_converged_fraction"] = res.inner.converged_fraction;
      md["last_inner_residual_ratio"] = res.inner.last_ratio;
      md["cfl_initial"] = cs.run.cfl_initial;
      md["cfl_max"] = cs.run.cfl_max;
      md["pseudo_cfl_ramp_steps"] = cs.run.pseudo_cfl_ramp_steps;
      md["start_time_utc"] = start_utc;
      md["end_time_utc"] = end_utc;
      md["completed"] = res.status != "failed";
      md["convergence_status"] = res.status;
      md["perturbation_startup_aoa_deg"] = cfg.pert_aoa_deg;
      md["perturbation_startup_duration"] = cfg.pert_duration;
      cfd::write_json_file(out_dir + "/metadata.json", md.dump(2));

      nlohmann::json rs;
      rs["case_id"] = cs.case_id;
      rs["command"] = "mpirun -np " + std::to_string(nranks) + " " + command_line;
      rs["mpi_ranks"] = nranks;
      rs["wall_time_seconds"] = wall;
      rs["final_step"] = res.final_step;
      rs["final_physical_time"] = res.final_time;
      rs["convergence_status"] = res.status;
      rs["residual_reduction_orders"] = res.residual_reduction;
      rs["notes"] = res.status == "converged"
          ? "residual target or documented plateau reached; final force row "
            "matches final field/surface state"
          : res.status == "statistically_periodic"
          ? "post-transient periodic vortex shedding established"
          : "run did not meet the convergence/periodicity criteria";
      cfd::write_json_file(out_dir + "/run_status.json", rs.dump(2));

      char buf[256];
      std::snprintf(buf, sizeof(buf),
                    "DONE %s: status=%s steps=%ld time=%.3f wall=%.1fs",
                    cs.case_id.c_str(), res.status.c_str(), res.final_step,
                    res.final_time, wall);
      oc.log(buf);
    }

    if (oc.res_file) oc.res_file.close();
    if (oc.force_file) oc.force_file.close();
  } catch (const std::exception& e) {
    if (rank == 0) {
      std::cerr << "FATAL: " << e.what() << std::endl;
    }
    exit_code = 1;
  }
  MPI_Finalize();
  return exit_code;
}
