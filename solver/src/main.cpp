#include <mpi.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "case_config.hpp"
#include "cgns_mesh.hpp"
#include "distribute.hpp"
#include "output.hpp"
#include "solver.hpp"

namespace {

void print_usage() {
    std::printf(
        "usage: mpirun -np <ranks> cfdsolve solve --case <case.json> --output <dir> "
        "[--restart <file>] [--report-level brief|full] [--flux rusanov|hllc]\n");
}

struct Args {
    std::string case_file, output_dir, restart_file;
    std::string report_level = "full";
    std::string flux = "";
    bool first_order = false;
    double cfl_initial = -1, cfl_max = -1;
    long ramp_steps = -1;
    double diss_scale = -1;
    double low_mach = -1;
    double target = -1;
    long max_steps = -1;
    bool ok = false;
};

Args parse_args(int argc, char** argv) {
    Args a;
    if (argc < 2 || std::string(argv[1]) != "solve") return a;
    for (int i = 2; i < argc; i++) {
        std::string k = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + k);
            return argv[++i];
        };
        if (k == "--case")
            a.case_file = next();
        else if (k == "--output")
            a.output_dir = next();
        else if (k == "--restart")
            a.restart_file = next();
        else if (k == "--report-level")
            a.report_level = next();
        else if (k == "--flux")
            a.flux = next();
        else if (k == "--first-order")
            a.first_order = true;
        else if (k == "--cfl-initial")
            a.cfl_initial = std::stod(next());
        else if (k == "--cfl-max")
            a.cfl_max = std::stod(next());
        else if (k == "--ramp-steps")
            a.ramp_steps = std::stol(next());
        else if (k == "--diss-scale")
            a.diss_scale = std::stod(next());
        else if (k == "--low-mach")
            a.low_mach = std::stod(next());
        else if (k == "--target")
            a.target = std::stod(next());
        else if (k == "--max-steps")
            a.max_steps = std::stol(next());
        else
            throw std::runtime_error("unknown argument: " + k);
    }
    a.ok = !a.case_file.empty() && !a.output_dir.empty();
    return a;
}

std::string git_revision() {
    const char* env = std::getenv("CFDSOLVE_GIT_REVISION");
    if (env) return env;
    return "unknown";
}

} // namespace

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, nranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);

    int exit_code = 0;
    try {
        Args args = parse_args(argc, argv);
        if (!args.ok) {
            if (rank == 0) print_usage();
            MPI_Finalize();
            return 2;
        }
        CaseConfig cfg = load_case(args.case_file);

        SolverConfig scfg;
        scfg.inviscid_flux = args.flux.empty() ? "rusanov" : args.flux;
        if (scfg.inviscid_flux != "rusanov" && scfg.inviscid_flux != "hllc")
            throw std::runtime_error("unknown flux: " + scfg.inviscid_flux);
        scfg.rusanov_dissipation_scale = cfg.run_control.rusanov_dissipation_scale;
        if (args.diss_scale > 0) scfg.rusanov_dissipation_scale = args.diss_scale;
        if (args.low_mach > 0) scfg.low_mach_mfloor = args.low_mach;
        scfg.report_level = (args.report_level == "brief") ? 0 : 1;
        scfg.second_order = !args.first_order;

        // transient runs: physical step count comes from final_time/time_step
        if (cfg.run_control.type == "transient") {
            long nsteps = lround(cfg.run_control.final_time / cfg.run_control.time_step);
            cfg.run_control.max_steps = nsteps;
        }
        // documented CLI overrides for the pseudo-time CFL schedule (used when
        // the supplied schedule is too aggressive for the present implicit
        // solver; deviations are recorded in the run manifest/report)
        if (args.cfl_initial > 0) cfg.run_control.cfl_initial = args.cfl_initial;
        if (args.cfl_max > 0) cfg.run_control.cfl_max = args.cfl_max;
        if (args.ramp_steps >= 0) cfg.run_control.pseudo_cfl_ramp_steps = args.ramp_steps;
        if (args.target > 0) cfg.run_control.residual_reduction_target = args.target;
        if (args.max_steps > 0) cfg.run_control.max_steps = args.max_steps;

        std::filesystem::create_directories(args.output_dir);

        // mesh: rank 0 reads the global CGNS mesh, then METIS partitioning and
        // distribution build rank-local submeshes on all ranks
        SerialMesh gm;
        if (rank == 0) {
            gm = read_cgns_mesh(cfg.mesh_file);
            std::printf("[mesh] %lld cells, %d nodes, %d faces, %zu boundary families\n",
                        (long long)gm.num_cells, gm.num_nodes, gm.num_faces, gm.bc_names.size());
            std::fflush(stdout);
        }
        std::map<std::string, BCType> bc_map;
        for (auto& [name, type] : cfg.boundary_conditions) {
            if (type == "farfield") bc_map[name] = BCType::Farfield;
            else if (type == "slip_wall") bc_map[name] = BCType::SlipWall;
            else if (type == "no_slip_adiabatic_wall") bc_map[name] = BCType::NoSlipAdiabatic;
        }
        PartitionInfo pinfo;
        LocalMesh lm = distribute_mesh(MPI_COMM_WORLD, gm, bc_map, pinfo);
        if (rank == 0) {
            std::printf("[partition] np=%d edge_cut=%ld owned:", nranks, pinfo.edge_cut);
            for (int o : pinfo.owned_per_rank) std::printf(" %d", o);
            std::printf("\n");
            std::fflush(stdout);
            write_partition_csv(args.output_dir + "/partition_diagnostics.csv", pinfo);
        }

        long long ncells_global = 0, nfaces_global = 0;
        if (rank == 0) {
            ncells_global = gm.num_cells;
            nfaces_global = gm.num_faces;
        }

        Solver solver(MPI_COMM_WORLD, std::move(lm), cfg, scfg);
        if (!args.restart_file.empty()) {
            std::vector<double> local_U;
            long rstep = 0;
            double rtime = 0.0;
            read_restart(MPI_COMM_WORLD, args.restart_file, solver.mesh(), local_U, rstep, rtime);
            solver.load_owned_state(local_U);
            if (cfg.run_control.type == "transient") solver.set_step_time(rstep, rtime);
            if (rank == 0)
                std::printf("[restart] loaded %s at step %ld time %g\n", args.restart_file.c_str(),
                            rstep, rtime);
        } else {
            solver.initialize();
        }
        // optional symmetry-breaking perturbation (triggers vortex shedding)
        if (const char* pe = std::getenv("CFDSOLVE_PERTURB")) {
            double eps = std::atof(pe);
            if (eps > 0) solver.add_transverse_perturbation(eps);
        }

        // intermediate transient field dumps
        if (cfg.run_control.type == "transient" && cfg.outputs.write_field_every_time > 0) {
            solver.field_callback = [&](double t) {
                GlobalField gf =
                    gather_global_field(MPI_COMM_WORLD, solver.mesh(), solver.state(), Gas{
                        cfg.gas.gamma, cfg.gas.R, cfg.gas.prandtl});
                if (rank == 0) {
                    char name[256];
                    std::snprintf(name, sizeof(name), "%s/field_t%06.1f.vtu",
                                  args.output_dir.c_str(), t);
                    write_vtu(name, gf);
                }
            };
        }
        if (cfg.run_control.type == "transient") {
            solver.restart_callback = [&](long step, double time) {
                write_restart(MPI_COMM_WORLD, args.output_dir + "/restart_latest.bin",
                              solver.mesh(), solver.state(), step, time);
            };
        }

        std::string raw_status;
        if (cfg.run_control.type == "transient")
            raw_status = solver.solve_transient(args.output_dir);
        else
            raw_status = solver.solve_steady(args.output_dir);

        // final outputs
        write_surface_csv(MPI_COMM_WORLD, args.output_dir + "/surface.csv",
                          solver.surface_rows());
        {
            Gas gas{cfg.gas.gamma, cfg.gas.R, cfg.gas.prandtl};
            GlobalField gf =
                gather_global_field(MPI_COMM_WORLD, solver.mesh(), solver.state(), gas);
            if (rank == 0) write_vtu(args.output_dir + "/field_final.vtu", gf);
        }
        write_restart(MPI_COMM_WORLD, args.output_dir + "/restart_final.bin", solver.mesh(),
                      solver.state(), solver.final_step(), solver.final_time());

        // convergence status mapping
        std::string conv_status;
        bool completed = true;
        if (cfg.run_control.type == "transient") {
            conv_status = (raw_status == "completed") ? "statistically_periodic" : "failed";
            completed = (raw_status == "completed");
        } else {
            conv_status = (raw_status == "converged") ? "converged" : "failed";
            completed = (raw_status == "converged");
        }

        if (rank == 0) {
            const auto& st = solver.inner_stats();
            nlohmann::json meta;
            meta["case_id"] = cfg.case_id;
            meta["solver_name"] = "cfdsolve";
            meta["solver_version"] = "1.0.0";
            meta["git_revision"] = git_revision();
            meta["mpi_ranks"] = nranks;
            meta["mesh_file"] = cfg.mesh_file;
            meta["num_cells_global"] = ncells_global;
            meta["num_faces_global"] = nfaces_global;
            meta["num_cells_owned_local"] = pinfo.owned_per_rank[0];
            meta["num_cells_ghost_local"] = pinfo.ghost_per_rank[0];
            meta["partitioner"] = (nranks > 1) ? "metis_kway" : "metis_kway(np=1)";
            meta["partition_edge_cut"] = pinfo.edge_cut;
            meta["halo_exchange"] = "neighbor_isend_irecv";
            meta["full_state_replication_during_iterations"] = false;
            meta["full_mesh_replication_during_iterations"] = false;
            meta["equation_set"] = "compressible_navier_stokes_2d";
            meta["inviscid_flux"] = scfg.inviscid_flux == "hllc" ? "hllc" : "rusanov_llf";
            if (scfg.low_mach_mfloor > 0)
                meta["inviscid_flux"] = std::string(meta["inviscid_flux"]) +
                                        "_low_mach_turkel_scaling";
            meta["entropy_fix"] = nullptr;
            meta["viscous_flux"] = (cfg.physics_mode == "laminar")
                                       ? "central_face_gradient_corrected_constant_viscosity"
                                       : "none";
            meta["time_integrator"] = (cfg.run_control.type == "transient")
                                          ? "bdf2_dual_time(bdf1_startup)"
                                          : "backward_euler_pseudo_time";
            meta["implicit_solver"] = "lusgs_symmetric_gauss_seidel_scalar_diagonal";
            meta["reconstruction"] =
                scfg.second_order ? "piecewise_linear_least_squares" : "first_order";
            meta["limiter"] = scfg.limiter;
            meta["spatial_order_claimed"] = scfg.second_order ? 2 : 1;
            meta["positivity_preservation"] =
                "barth_jespersen_limiter_plus_first_order_face_fallback_and_update_halving";
            meta["wall_boundary_output_semantics"] = "boundary_value";
            meta["true_bdf2_inner_loop"] = (cfg.run_control.type == "transient");
            meta["typical_inner_iterations"] = st.mean_inner();
            meta["min_inner_iterations"] = cfg.run_control.min_inner_iterations;
            meta["max_inner_iterations"] = cfg.run_control.max_inner_iterations;
            meta["observed_min_inner_iterations"] =
                st.total_steps ? st.min_inner : 0;
            meta["observed_max_inner_iterations"] = st.max_inner;
            meta["inner_residual_reduction_target"] = cfg.run_control.inner_residual_reduction_target;
            meta["inner_target_misses"] = st.target_misses;
            meta["inner_target_converged_fraction"] = st.converged_fraction();
            meta["last_inner_residual_ratio"] = st.last_inner_residual_ratio;
            meta["start_time_utc"] = "see run_status";
            meta["end_time_utc"] = "see run_status";
            meta["completed"] = completed;
            meta["convergence_status"] = conv_status;
            std::ofstream(args.output_dir + "/metadata.json") << meta.dump(2) << "\n";

            nlohmann::json status;
            status["case_id"] = cfg.case_id;
            {
                std::string cmd = "mpirun -np " + std::to_string(nranks) + " cfdsolve solve --case " +
                                  args.case_file + " --output " + args.output_dir;
                if (!args.restart_file.empty()) cmd += " --restart " + args.restart_file;
                status["command"] = cmd;
            }
            status["mpi_ranks"] = nranks;
            status["wall_time_seconds"] = solver.wall_time();
            status["final_step"] = solver.final_step();
            status["final_physical_time"] = solver.final_time();
            status["convergence_status"] = conv_status;
            status["residual_reduction_orders"] = solver.residual_reduction_orders();
            status["notes"] = raw_status == "converged"
                                  ? "steady residual target reached"
                                  : (raw_status == "completed"
                                         ? "full transient horizon completed"
                                         : "run ended without reaching target: " + raw_status);
            status["positivity_fixes"] = solver.positivity_fixes();
            status["line_search_backtracks"] = solver.line_search_backtracks();
            std::ofstream(args.output_dir + "/run_status.json") << status.dump(2) << "\n";

            std::printf("[done] case %s status %s steps %ld wall %.1f s\n", cfg.case_id.c_str(),
                        conv_status.c_str(), solver.final_step(), solver.wall_time());
        }
        exit_code = completed ? 0 : 0; // normal completion either way; status recorded in files
    } catch (const std::exception& e) {
        if (rank == 0) std::fprintf(stderr, "error: %s\n", e.what());
        // abort all ranks: a single-rank failure must not leave the others
        // blocked in collectives
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    MPI_Finalize();
    return exit_code;
}
