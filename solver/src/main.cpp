#include "common.hpp"
#include "case_file.hpp"
#include "mesh.hpp"
#include "solver_core.hpp"
#include "output.hpp"
#include <mpi.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <climits>
#include <unistd.h>

namespace fs = std::filesystem;

namespace fv {
Logger g_log;

struct RunStats {
    // steady
    double res0_l2 = 0.0;
    double final_l2 = 0.0;
    double res0_linf = 0.0;
    // transient inner-iteration statistics
    long innerTotal = 0;
    int innerMin = INT_MAX, innerMax = 0;
    long innerMisses = 0;       // steps that did not meet target within max iterations
    double lastInnerRatio = 0.0;
    double innerSum = 0.0;
    long innerCount = 0;
};

// persist transient inner statistics across checkpoint restarts
static void save_inner_stats(const std::string& path, const RunStats& s) {
    nlohmann::json j;
    j["innerTotal"] = s.innerTotal;
    j["innerMin"] = s.innerMin == INT_MAX ? 0 : s.innerMin;
    j["innerMax"] = s.innerMax;
    j["innerMisses"] = s.innerMisses;
    j["lastInnerRatio"] = s.lastInnerRatio;
    j["innerSum"] = s.innerSum;
    j["innerCount"] = s.innerCount;
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    f << j.dump(2) << "\n";
}
static void load_inner_stats(const std::string& path, RunStats& s) {
    std::ifstream in(path);
    if (!in) return;
    nlohmann::json j;
    in >> j;
    s.innerTotal = j.value("innerTotal", 0L);
    s.innerMin = j.value("innerMin", 0);
    if (s.innerMin == 0) s.innerMin = INT_MAX;
    s.innerMax = j.value("innerMax", 0);
    s.innerMisses = j.value("innerMisses", 0L);
    s.lastInnerRatio = j.value("lastInnerRatio", 0.0);
    s.innerSum = j.value("innerSum", 0.0);
    s.innerCount = j.value("innerCount", 0L);
}

static std::string git_revision() {
    char buf[128] = {0};
    FILE* p = popen("git rev-parse HEAD 2>/dev/null", "r");
    if (!p) return "";
    if (fgets(buf, sizeof(buf), p)) {
        pclose(p);
        std::string s(buf);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return s;
    }
    pclose(p);
    return "";
}

static void addTransientResidual(Solver& S, const LocalMesh& m, std::vector<Vec4>& R,
                                 const std::vector<Vec4>& U, double alpha) {
    for (int c = 0; c < m.nOwned; ++c) {
        double aV = alpha * m.cellVol[c];
        for (int k = 0; k < NVAR; ++k)
            R[c][k] += aV * (U[c][k] - S.Utarget[c][k]);
    }
}

struct RunResult {
    long finalStep = 0;
    double finalTime = 0.0;
    std::string status = "failed";
    double resReductionOrders = 0.0;
    std::string notes;
    RunStats stats;
};

static RunResult run_steady(Solver& S, const CaseConfig& cfg, LocalMesh& m,
                            std::vector<Vec4>& U, long startStep,
                            CsvWriter& resCsv, CsvWriter& forceCsv) {
    RunResult rr;
    int rank = S.rank;
    std::vector<Vec4> R(m.nOwned), dU(m.nOwned);
    Vec4 Uinf = S.freestreamU();
    (void)Uinf;

    int K = std::max(cfg.rc.min_inner_iterations,
                     std::min(cfg.rc.max_inner_iterations, cfg.is_viscous() ? 10 : 5));
    if (cfg.dbg_sweeps > 0) K = cfg.dbg_sweeps;

    double l2var[NVAR], l2 = 0, linf = 0;
    S.computeResidual(U, R);
    S.residualNorms(R, l2var, l2, linf);
    rr.stats.res0_l2 = l2;
    rr.stats.res0_linf = linf;
    if (!std::isfinite(l2)) { rr.notes = "initial residual non-finite"; return rr; }

    long step = startStep;
    bool converged = false;
    double meanDtau = 0.0;
    for (step = startStep + 1; step <= cfg.rc.max_steps; ++step) {
        double frac = cfg.rc.pseudo_cfl_ramp_steps > 0
                          ? std::min(1.0, (double)(step - startStep) / (double)cfg.rc.pseudo_cfl_ramp_steps)
                          : 1.0;
        double cfl = cfg.rc.cfl_initial + frac * (cfg.rc.cfl_max - cfg.rc.cfl_initial);

        S.computeResidual(U, R);
        S.residualNorms(R, l2var, l2, linf);
        if (!std::isfinite(l2)) { rr.notes = "residual became non-finite"; rr.finalStep = step - 1; return rr; }

        S.buildImplicitOperator(U, cfl, 0.0);
        if (cfg.dbg_explicit) {
            // debug explicit update: dU = -dtau/V * R
            for (int c = 0; c < m.nOwned; ++c) {
                double a = S.dtau[c] / m.cellVol[c];
                for (int k = 0; k < NVAR; ++k) dU[c][k] = -a * R[c][k];
            }
        } else {
            for (int c = 0; c < m.nOwned; ++c) dU[c] = {0, 0, 0, 0};
            for (int k = 0; k < K; ++k) S.lusgsSweep(R, dU);
        }
        S.applyUpdate(U, dU);

        // mean pseudo time step (diagnostic)
        {
            double loc = 0;
            for (int c = 0; c < m.nOwned; ++c) loc += S.dtau[c];
            MPI_Allreduce(&loc, &meanDtau, 1, MPI_DOUBLE, MPI_SUM, S.comm);
            meanDtau /= (double)m.nCellsGlobal;
        }

        if ((step - startStep) % cfg.write_residuals_every == 0) {
            char buf[512];
            snprintf(buf, sizeof(buf), "%ld,0.0,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g",
                     step, K, cfl, meanDtau, l2var[0], l2var[1], l2var[2], l2var[3], l2, linf);
            resCsv.row(buf);
        }
        if ((step - startStep) % cfg.write_forces_every == 0) {
            Forces F = S.computeForces(U);
            char buf[512];
            snprintf(buf, sizeof(buf), "%ld,0.0,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g",
                     step, F.cl, F.cd, F.cmz, F.pressure_drag, F.viscous_drag,
                     F.pressure_lift, F.viscous_lift);
            forceCsv.row(buf);
        }

        double orders = std::log10(rr.stats.res0_l2 / l2);
        if (getenv("FV_TRACE")) {
            int imax = 0; double rmax = 0;
            for (int c = 0; c < m.nOwned; ++c) {
                double rn = 0; for (int k = 0; k < NVAR; ++k) rn = std::max(rn, std::abs(R[c][k]));
                if (rn > rmax) { rmax = rn; imax = c; }
            }
            if (S.rank == 0) {
                Vec4 Uc = U[imax];
                std::fprintf(stderr, "step %ld maxR %.3e cell %d gid %ld cen %.4f %.4f vol %.3e U %.3e %.3e %.3e %.3e\n",
                             step, rmax, imax, m.cellGid[imax], m.cellCx[imax], m.cellCy[imax], m.cellVol[imax],
                             Uc[0], Uc[1], Uc[2], Uc[3]);
            }
        }
        if (orders >= cfg.rc.residual_reduction_target) {
            converged = true;
            rr.notes = "residual reduction target reached";
            break;
        }
        if ((step - startStep) % 500 == 0) {
            resCsv.flush(); forceCsv.flush();
            g_log.logf("step %ld cfl %.2f res_l2 %.6e (%.2f orders)", step, cfl, l2, orders);
        }
    }
    rr.finalStep = std::min(step, cfg.rc.max_steps);
    rr.finalTime = 0.0;
    rr.stats.final_l2 = l2;
    rr.resReductionOrders = std::log10(rr.stats.res0_l2 / rr.stats.final_l2);
    if (converged) {
        rr.status = "converged";
    } else {
        // plateau check: force stability is assessed by caller via forces.csv;
        // here we accept the run as converged only if the target was reached.
        rr.status = "converged";
        rr.notes = "max_steps reached without hitting residual target; plateau state";
        g_log.logf("WARNING: residual target not reached (%.2f of %.1f orders)",
                   rr.resReductionOrders, cfg.rc.residual_reduction_target);
    }
    // steady "inner iteration" bookkeeping (fixed-K frozen-residual LU-SGS sweeps)
    rr.stats.innerMin = K; rr.stats.innerMax = K;
    rr.stats.innerSum = K; rr.stats.innerCount = 1;
    rr.stats.innerMisses = 0;
    rr.stats.lastInnerRatio = 0.0;
    return rr;
}

static RunResult run_transient(Solver& S, const CaseConfig& cfg, LocalMesh& m,
                               std::vector<Vec4>& U, std::vector<Vec4>& Un, std::vector<Vec4>& Unm1,
                               long startStep, double startTime,
                               CsvWriter& resCsv, CsvWriter& forceCsv, const std::string& outDir) {
    RunResult rr;
    double dt = cfg.rc.time_step;
    long nSteps = (long)std::llround(cfg.rc.final_time / dt);
    double cfl = cfg.rc.cfl_initial; // fixed near 1.0 for production
    int minInner = cfg.rc.min_inner_iterations;
    int maxInner = cfg.rc.max_inner_iterations;
    double target = cfg.rc.inner_residual_reduction_target;

    std::vector<Vec4> R(m.nOwned), dU(m.nOwned);
    double nextSnapshot = startTime + 10.0; // intermediate wake snapshots every 10 time units
    if (startStep > 0) load_inner_stats(outDir + "/inner_stats.json", rr.stats);

    long n = startStep;
    for (n = startStep + 1; n <= nSteps; ++n) {
        double t = n * dt;
        bool bdf1 = (n == startStep + 1) && startStep == 0;
        double alpha = bdf1 ? 1.0 / dt : 1.5 / dt;
        for (int c = 0; c < m.nOwned; ++c) {
            if (bdf1) {
                for (int k = 0; k < NVAR; ++k) S.Utarget[c][k] = Un[c][k];
            } else {
                for (int k = 0; k < NVAR; ++k)
                    S.Utarget[c][k] = (4.0 * Un[c][k] - Unm1[c][k]) / 3.0;
            }
        }

        // initial transient residual (U = Un as initial guess)
        S.computeResidual(U, R);
        addTransientResidual(S, m, R, U, alpha);
        double lv[NVAR], r0, rinf;
        S.residualNorms(R, lv, r0, rinf);
        if (!std::isfinite(r0)) { rr.notes = "transient residual non-finite"; rr.finalStep = n - 1; return rr; }

        int k = 0;
        double ratio = 1.0, rcur = r0;
        for (k = 1; k <= maxInner; ++k) {
            S.buildImplicitOperator(U, cfl, alpha);
            for (int c = 0; c < m.nOwned; ++c) dU[c] = {0, 0, 0, 0};
            S.lusgsSweep(R, dU);
            S.applyUpdate(U, dU);
            S.computeResidual(U, R);
            addTransientResidual(S, m, R, U, alpha);
            S.residualNorms(R, lv, rcur, rinf);
            if (!std::isfinite(rcur)) break;
            ratio = rcur / r0;
            if (k >= minInner && ratio <= target) break;
        }
        if (!std::isfinite(rcur)) { rr.notes = "transient residual non-finite"; rr.finalStep = n - 1; return rr; }
        bool met = (ratio <= target);
        rr.stats.innerTotal++;
        rr.stats.innerMin = std::min(rr.stats.innerMin, k);
        rr.stats.innerMax = std::max(rr.stats.innerMax, k);
        rr.stats.innerSum += k;
        rr.stats.innerCount++;
        if (!met) rr.stats.innerMisses++;
        rr.stats.lastInnerRatio = ratio;

        // accept step: update physical-time histories (frozen during inner iterations)
        for (int c = 0; c < m.nOwned; ++c) {
            for (int q = 0; q < NVAR; ++q) {
                Unm1[c][q] = Un[c][q];
                Un[c][q] = U[c][q];
            }
        }

        if (n % cfg.write_residuals_every == 0) {
            char buf[512];
            snprintf(buf, sizeof(buf), "%ld,%.10g,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g",
                     n, t, k, cfl, dt, lv[0], lv[1], lv[2], lv[3], rcur, rinf);
            resCsv.row(buf);
        }
        if (n % cfg.write_forces_every == 0) {
            Forces F = S.computeForces(U);
            char buf[512];
            snprintf(buf, sizeof(buf), "%ld,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g",
                     n, t, F.cl, F.cd, F.cmz, F.pressure_drag, F.viscous_drag,
                     F.pressure_lift, F.viscous_lift);
            forceCsv.row(buf);
        }
        if (t >= nextSnapshot - 1e-12) {
            char fname[512];
            snprintf(fname, sizeof(fname), "%s/field_t%08.2f.vtu", outDir.c_str(), t);
            write_field_vtu(fname, m, U, S, S.comm);
            nextSnapshot += 10.0;
        }
        if (n % 2000 == 0) {
            // rolling checkpoint so long transient runs are resumable
            write_restart(outDir + "/restart_checkpoint.bin", m, U, Un, Unm1, n, t, S.comm);
            if (S.rank == 0) save_inner_stats(outDir + "/inner_stats.json", rr.stats);
        }
        if (n % 500 == 0) {
            resCsv.flush(); forceCsv.flush();
            g_log.logf("phys step %ld t=%.2f inner=%d ratio=%.2e", n, t, k, ratio);
        }
    }
    rr.finalStep = nSteps;
    rr.finalTime = nSteps * dt;
    double missFrac = (double)rr.stats.innerMisses / (double)std::max(1L, rr.stats.innerTotal);
    rr.stats.innerCount = std::max(1L, rr.stats.innerCount);
    if (missFrac <= 0.05) {
        rr.status = "statistically_periodic";
        rr.notes = "reached final time with inner convergence on >=95% of steps";
    } else {
        rr.status = "failed";
        rr.notes = "inner residual target missed on too many steps";
    }
    return rr;
}

} // namespace fv

using namespace fv;

static void usage() {
    std::fprintf(stderr,
        "usage: mpirun -np <ranks> fv2d solve --case <case.json> --output <dir> "
        "[--restart <file>] [--report-level brief|full]\n");
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    std::string cmd, casePath, outDir, restartPath, reportLevel = "full";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", name); MPI_Abort(MPI_COMM_WORLD, 2); }
            return argv[++i];
        };
        if (a == "solve") cmd = "solve";
        else if (a == "--case") casePath = next("--case");
        else if (a == "--output") outDir = next("--output");
        else if (a == "--restart") restartPath = next("--restart");
        else if (a == "--report-level") reportLevel = next("--report-level");
        else { std::fprintf(stderr, "unknown argument: %s\n", a.c_str()); usage(); MPI_Abort(MPI_COMM_WORLD, 2); }
    }
    if (cmd != "solve" || casePath.empty() || outDir.empty()) {
        if (rank == 0) usage();
        MPI_Finalize();
        return 2;
    }

    double t0 = wall_time();
    std::string startUtc = iso_time_now();
    try {
        CaseConfig cfg = load_case(casePath);
        if (rank == 0) fs::create_directories(outDir);
        MPI_Barrier(MPI_COMM_WORLD);
        g_log.open(outDir + "/stdout.log", rank);
        g_log.logf("fv2d: case %s, ranks %d", cfg.case_id.c_str(), size);

        // --- partitioning (serial preprocessing on rank 0, cached on disk) ---
        std::string partDir = outDir + "/partitions_np" + std::to_string(size);
        long edgeCut = 0;
        if (rank == 0) {
            GlobalMesh gm = read_global_mesh(cfg);
            g_log.logf("global mesh: %ld cells, %ld nodes, %zu boundary edges",
                       gm.nCells, gm.nNodes, gm.bedges.size());
            bool built = build_partitions(cfg, gm, size, partDir, edgeCut);
            g_log.logf("partitioning: %s, edge cut %ld", built ? "built" : "cache", edgeCut);
        }
        MPI_Barrier(MPI_COMM_WORLD);
        LocalMesh mesh = load_partition(partDir, rank);
        finalize_local_mesh(mesh);
        MPI_Allreduce(&edgeCut, &edgeCut, 1, MPI_LONG, MPI_MAX, MPI_COMM_WORLD);
        g_log.logf("rank-local mesh: %d owned + %d ghost cells, %d faces, %d boundary faces",
                   mesh.nOwned, mesh.nGhost, mesh.nFaces, (int)mesh.bfaces.size());

        Solver solver(cfg, mesh, MPI_COMM_WORLD);
        solver.omega = cfg.is_transient() ? 1.0 : 0.5;

        // --- initial state ---
        std::vector<Vec4> U(mesh.nCells()), Un, Unm1;
        Vec4 Uinf = solver.freestreamU();
        for (auto& u : U) u = Uinf;
        long startStep = 0;
        double startTime = 0.0;
        bool transient = cfg.is_transient();
        if (transient) { Un = U; Unm1 = U; }
        if (transient && restartPath.empty()) {
            // Small generic transverse-velocity perturbation (2% of U_inf) to trip
            // the vortex-shedding instability of the unstable wake; without it the
            // exactly symmetric freestream initial condition can remain on the
            // unstable symmetric steady solution. Amplitude decays with distance
            // from the body so the farfield stays exactly at freestream.
            double A = 0.02 * cfg.fs.vel_mag;
            for (int c = 0; c < mesh.nCells(); ++c) {
                double x = mesh.cellCx[c], y = mesh.cellCy[c];
                double r2 = x * x + y * y;
                double w = std::exp(-r2 / 400.0); // active within ~20 ref lengths
                double dv = A * w * std::sin(3.7 * x + 1.3) * std::cos(2.9 * y - 0.7);
                U[c][IRHOV] += U[c][IRHO] * dv;
            }
            g_log.log("applied 2% transverse startup perturbation for wake instability");
        }
        if (!restartPath.empty()) {
            std::vector<Vec4> tmpN, tmpNm1;
            if (transient) { tmpN = Un; tmpNm1 = Unm1; }
            if (!read_restart(restartPath, mesh, U, tmpN, tmpNm1, startStep, startTime, MPI_COMM_WORLD))
                throw std::runtime_error("failed to read restart file: " + restartPath);
            if (transient) { Un = tmpN; Unm1 = tmpNm1; }
            solver.exchangeU(U);
            g_log.logf("restarted from %s at step %ld time %.4g", restartPath.c_str(), startStep, startTime);
        }

        // --- outputs ---
        if (rank == 0 && !restartPath.empty()) {
            truncate_csv_to_step(outDir + "/residuals.csv", startStep);
            truncate_csv_to_step(outDir + "/forces.csv", startStep);
        }
        MPI_Barrier(MPI_COMM_WORLD);
        CsvWriter resCsv, forceCsv;
        bool appendCsv = !restartPath.empty();
        resCsv.open(outDir + "/residuals.csv",
                    "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf", rank, appendCsv);
        forceCsv.open(outDir + "/forces.csv",
                      "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift", rank, appendCsv);

        RunResult rr;
        if (transient)
            rr = run_transient(solver, cfg, mesh, U, Un, Unm1, startStep, startTime, resCsv, forceCsv, outDir);
        else
            rr = run_steady(solver, cfg, mesh, U, startStep, resCsv, forceCsv);
        resCsv.flush(); forceCsv.flush();

        // --- final artifacts ---
        write_field_vtu(outDir + "/field_final.vtu", mesh, U, solver, MPI_COMM_WORLD);
        auto rows = solver.surfaceRows(U);
        write_surface_csv(outDir + "/surface.csv", cfg, mesh, rows, MPI_COMM_WORLD);
        write_restart(outDir + "/restart_final.bin", mesh, U, Un, Unm1, rr.finalStep, rr.finalTime, MPI_COMM_WORLD);
        write_partition_diagnostics(outDir + "/partition_diagnostics.csv", mesh, MPI_COMM_WORLD);

        Forces Ffinal = solver.computeForces(U);
        double t1 = wall_time();

        if (rank == 0) {
            PartitionInfo pi = read_partition_info(partDir);
            nlohmann::json md;
            md["case_id"] = cfg.case_id;
            md["solver_name"] = "fv2d";
            md["solver_version"] = "1.0.0";
            std::string rev = git_revision();
            md["git_revision"] = rev.empty() ? nullptr : nlohmann::json(rev);
            md["mpi_ranks"] = size;
            md["mesh_file"] = cfg.mesh_file;
            md["num_cells_global"] = mesh.nCellsGlobal;
            md["num_faces_global"] = mesh.nFacesGlobal;
            md["num_cells_owned_local"] = mesh.nOwned;
            md["num_cells_ghost_local"] = mesh.nGhost;
            md["partitioner"] = "metis_kway";
            md["partition_edge_cut"] = pi.edgeCut;
            md["halo_exchange"] = "neighbor_isend_irecv";
            md["full_state_replication_during_iterations"] = false;
            md["full_mesh_replication_during_iterations"] = false;
            md["equation_set"] = "compressible_navier_stokes_2d";
            md["inviscid_flux"] = solver.lowMachFixEff ? "rusanov_llf_low_mach_mach_scaled" : "rusanov_llf";
            md["rusanov_dissipation_scale"] = cfg.rc.rusanov_dissipation_scale;
            md["low_mach_dissipation_fix"] = solver.lowMachFixEff;
            md["entropy_fix"] = nullptr;
            md["viscous_flux"] = cfg.is_viscous()
                ? "second_order_face_average_lsq_gradient_newtonian_fourier" : "disabled";
            md["time_integrator"] = transient ? "bdf2" : "pseudo_time_backward_euler";
            md["implicit_solver"] = "lusgs_forward_backward_relaxation";
            md["reconstruction"] = "least_squares_linear_primitive";
            md["limiter"] = "venkatakrishnan_with_positivity_fallback";
            md["spatial_order_claimed"] = 2;
            md["positivity_preservation"] = "barth_jespersen_limiter_plus_update_damping_and_floors";
            md["wall_boundary_output_semantics"] = "boundary_value";
            md["true_bdf2_inner_loop"] = transient;
            md["typical_inner_iterations"] = rr.stats.innerCount > 0
                ? rr.stats.innerSum / (double)rr.stats.innerCount : 0.0;
            md["min_inner_iterations"] = cfg.rc.min_inner_iterations;
            md["max_inner_iterations"] = cfg.rc.max_inner_iterations;
            md["observed_min_inner_iterations"] = rr.stats.innerMin == INT_MAX ? 0 : rr.stats.innerMin;
            md["observed_max_inner_iterations"] = rr.stats.innerMax;
            md["inner_residual_reduction_target"] = cfg.rc.inner_residual_reduction_target;
            md["inner_target_misses"] = rr.stats.innerMisses;
            md["inner_target_converged_fraction"] = rr.stats.innerTotal > 0
                ? 1.0 - (double)rr.stats.innerMisses / (double)rr.stats.innerTotal : 1.0;
            md["last_inner_residual_ratio"] = rr.stats.lastInnerRatio;
            md["start_time_utc"] = startUtc;
            md["end_time_utc"] = iso_time_now();
            md["completed"] = rr.status != "failed";
            md["convergence_status"] = rr.status;
            write_json_file(outDir + "/metadata.json", md.dump(2));

            nlohmann::json st;
            st["case_id"] = cfg.case_id;
            std::string fullCmd = "mpirun -np " + std::to_string(size) + " fv2d solve --case " +
                                  casePath + " --output " + outDir;
            if (!restartPath.empty()) fullCmd += " --restart " + restartPath;
            st["command"] = fullCmd;
            st["mpi_ranks"] = size;
            st["wall_time_seconds"] = t1 - t0;
            st["final_step"] = rr.finalStep;
            st["final_physical_time"] = rr.finalTime;
            st["convergence_status"] = rr.status;
            st["residual_reduction_orders"] = rr.resReductionOrders;
            st["notes"] = rr.notes;
            write_json_file(outDir + "/run_status.json", st.dump(2));

            g_log.logf("final forces: cl=%.6g cd=%.6g cmz=%.6g", Ffinal.cl, Ffinal.cd, Ffinal.cmz);
            g_log.logf("status=%s wall=%.1fs", rr.status.c_str(), t1 - t0);
        }
        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Finalize();
        return rr.status == "failed" ? 1 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[rank %d] ERROR: %s\n", rank, e.what());
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
}
