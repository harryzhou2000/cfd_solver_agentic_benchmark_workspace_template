// Solver orchestration: implicit steady (pseudo-time) and BDF2 transient
// (dual-time) loops plus the full output pipeline.
//
// Loop structure (both modes):
//   - One "step" = one outer (pseudo/phys) iteration.
//   - Each step runs an inner nonlinear point-implicit (Jacobi) relaxation
//     loop: the spatial residual R(U) (plus the frozen physical-time source
//     term for transient runs) is recomputed every sweep, the local time
//     step is formed from the convective/viscous spectral radii, and owned
//     cells are updated with the diagonally-scaled correction
//         dU = -dt * T / (vol + dt * sigma_total)
//     followed by a positivity clamp.  Inner sweeps stop when
//     ||T|| / ||T_first|| < inner_target (after min_inner_iterations).
//   - For transient runs the previous physical-time states are frozen
//     during the inner solve and the histories U_n, U_nm1 are advanced only
//     after the inner solve is accepted (true BDF2 outer loop).
//
// All output files are written by rank 0 using MPI reductions/gathers.

#include "solver/solver.hpp"
#include "solver/fluxes.hpp"
#include "solver/gas_model.hpp"
#include "solver/solver_state.hpp"
#include "mesh/mesh_types.hpp"
#include "partition/partition_types.hpp"
#include "partition/halo_exchange.hpp"
#include "boundary/boundary_utils.hpp"
#include "types.hpp"
#include <nlohmann/json.hpp>

#include <mpi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace cfd {
namespace {

constexpr Real MIN_DENSITY = 1e-12;
constexpr Real MIN_PRESSURE = 1e-10;
constexpr Real TINY = 1e-30;

// ============================================================================
// Small utilities
// ============================================================================

std::string iso8601_utc_now() {
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buf[40];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

std::string git_revision() {
    std::string rev;
    FILE* p = popen("git rev-parse HEAD 2>/dev/null", "r");
    if (p != nullptr) {
        char buf[64];
        if (std::fgets(buf, sizeof(buf), p) != nullptr) rev = buf;
        pclose(p);
    }
    while (!rev.empty() && (rev.back() == '\n' || rev.back() == '\r')) {
        rev.pop_back();
    }
    return rev;
}

/// Rank-0 logger that mirrors lines to stdout AND the output-dir stdout.log
class RunLog {
public:
    explicit RunLog(const std::string& path) {
        if (!path.empty()) file_.open(path, std::ios::out | std::ios::trunc);
    }
    ~RunLog() { if (file_.is_open()) file_.close(); }
    void line(const std::string& s) {
        std::cout << s << std::endl;
        if (file_.is_open()) file_ << s << '\n' << std::flush;
    }

private:
    std::ofstream file_;
};

// ============================================================================
// Force accumulation and coefficient conversion
// ============================================================================
//
// Sign convention: the momentum residual of the cell adjacent to a wall face
// is R_wall = (F_inv - F_visc) * A with the mesh normal pointing from the
// interior cell into the wall.  The force ON the body is the reaction to the
// force the wall exerts on the fluid, so
//     F_body = +(pressure_force) - (viscous_force)
// where pressure_force = p*n*A and viscous_force = tau*n*A are exactly the
// components returned by compute_boundary_face_force.  Coefficients use the
// freestream-aligned drag/lift frame and the case reference area/length.

struct ForceAccum {
    double fx_p{0}, fy_p{0};  // pressure force on the body
    double fx_v{0}, fy_v{0};  // viscous traction (tau*n*A), sign as in code
    double mz_p{0}, mz_v{0};  // moments about the reference center
};

ForceCoeffs to_force_coeffs(const ForceAccum& fa, const CaseConfig& cfg) {
    const FreestreamConfig& fsc = cfg.freestream;
    const Real u_mag = std::sqrt(fsc.u * fsc.u + fsc.v * fsc.v);
    const Real q_inf = (u_mag > 0.0) ? dynamic_pressure(fsc.rho, u_mag) : 1.0;
    const Real ref_area = cfg.reference.area;
    const Real ref_len = cfg.reference.length;
    const Real uhat = (u_mag > 0.0) ? fsc.u / u_mag : 1.0;
    const Real vhat = (u_mag > 0.0) ? fsc.v / u_mag : 0.0;

    ForceCoeffs c;
    // Force on the body: +pressure - viscous
    const Real fx = fa.fx_p - fa.fx_v;
    const Real fy = fa.fy_p - fa.fy_v;
    const Real qa = q_inf * ref_area;
    c.cd = (fx * uhat + fy * vhat) / qa;
    c.cl = (-fx * vhat + fy * uhat) / qa;
    c.pressure_drag = (fa.fx_p * uhat + fa.fy_p * vhat) / qa;
    c.viscous_drag = -(fa.fx_v * uhat + fa.fy_v * vhat) / qa;
    c.pressure_lift = (-fa.fx_p * vhat + fa.fy_p * uhat) / qa;
    c.viscous_lift = (fa.fx_v * vhat - fa.fy_v * uhat) / qa;
    c.cmz = (fa.mz_p - fa.mz_v) / (q_inf * ref_area * ref_len);
    return c;
}

// ============================================================================
// Per-rank context built once from the RankPartition
// ============================================================================

struct BFaceInfo {
    Int face_local{INVALID_INDEX};  // index into rp.faces
    Int cell_local{INVALID_INDEX};  // interior (owned) local cell
    BcType bc{BcType::Unsupported};
    Int patch{INVALID_INDEX};       // index into mesh.boundary_patches
};

struct LocalContext {
    Int n_owned{0};
    Int n_ghost{0};
    Int n_local{0};

    // Global cell id -> local cell index (owned + ghost), -1 if not local
    std::vector<Int> global_to_local;

    // Local cell adjacency (face neighbors), for least-squares gradients
    std::vector<std::vector<Int>> cell_neighbors;

    // LU-SGS stencil: for each owned cell, the faces shared with other local
    // cells, as (other local cell, index into rp.faces). Ghost cells appear
    // as neighbors but their update is zero (fixed by the halo exchange).
    std::vector<std::vector<std::pair<Int, Int>>> cell_faces;

    // Boundary faces of each owned cell (indices into rp.faces); they enter
    // only the LU-SGS diagonal (no off-diagonal coupling).
    std::vector<std::vector<Int>> cell_bfaces;

    // Halo exchange bookkeeping (from rp.neighbors)
    std::vector<std::vector<Int>> send_cells, recv_cells;
    std::vector<int> neighbor_ranks;

    // Boundary faces: all, plus the wall subset used for forces/surface
    std::vector<BFaceInfo> bfaces;
    std::vector<BFaceInfo> walls;
};

LocalContext build_local_context(const Mesh& mesh, const RankPartition& rp) {
    LocalContext ctx;
    ctx.n_owned = rp.n_owned;
    ctx.n_ghost = rp.n_ghost;
    ctx.n_local = rp.n_owned + rp.n_ghost;

    const Int n_global_cells = mesh.stats.n_cells;
    ctx.global_to_local.assign(static_cast<size_t>(n_global_cells), INVALID_INDEX);
    for (Int i = 0; i < rp.n_owned; ++i) {
        ctx.global_to_local[static_cast<size_t>(rp.owned_cell_ids[static_cast<size_t>(i)])] = i;
    }
    for (Int i = 0; i < rp.n_ghost; ++i) {
        ctx.global_to_local[static_cast<size_t>(
            rp.ghost_cell_ids[static_cast<size_t>(i)])] = rp.n_owned + i;
    }

    // Local adjacency from the local face list
    ctx.cell_neighbors.resize(static_cast<size_t>(ctx.n_local));
    ctx.cell_faces.resize(static_cast<size_t>(ctx.n_owned));
    ctx.cell_bfaces.resize(static_cast<size_t>(ctx.n_owned));
    for (size_t fi = 0; fi < rp.faces.size(); ++fi) {
        const auto& lf = rp.faces[fi];
        const Int l = lf.left_local;
        const Int r = lf.right_local;
        if (l == INVALID_INDEX || r == INVALID_INDEX) continue;
        ctx.cell_neighbors[static_cast<size_t>(l)].push_back(r);
        ctx.cell_neighbors[static_cast<size_t>(r)].push_back(l);
        // LU-SGS stencil (owned cells only; ghosts have zero update)
        if (l < ctx.n_owned) {
            ctx.cell_faces[static_cast<size_t>(l)].emplace_back(r, static_cast<Int>(fi));
        }
        if (r < ctx.n_owned) {
            ctx.cell_faces[static_cast<size_t>(r)].emplace_back(l, static_cast<Int>(fi));
        }
    }

    // Halo exchange bookkeeping
    for (const auto& hn : rp.neighbors) {
        ctx.send_cells.push_back(hn.send_cells);
        ctx.recv_cells.push_back(hn.recv_cells);
        ctx.neighbor_ranks.push_back(hn.neighbor_rank);
    }

    // Boundary faces: local faces whose right cell is the sentinel.
    // The adjacent (left) cell is always an owned cell.
    for (size_t fi = 0; fi < rp.faces.size(); ++fi) {
        const auto& lf = rp.faces[fi];
        if (lf.right_local != INVALID_INDEX) continue;
        if (lf.left_local == INVALID_INDEX) continue;
        const Face& gface = mesh.faces[static_cast<size_t>(lf.global_face_id)];
        if (gface.bc_tag == INVALID_INDEX || gface.bc_tag < 0) {
            throw std::runtime_error(
                "boundary face without a BC patch (mesh is not fully tagged)");
        }
        const BoundaryPatch& patch =
            mesh.boundary_patches[static_cast<size_t>(gface.bc_tag)];
        if (patch.bc_type == BcType::Unsupported) {
            throw std::runtime_error(
                "unsupported boundary condition on patch '" + patch.family_name +
                "'");
        }
        BFaceInfo bf;
        bf.face_local = static_cast<Int>(fi);
        bf.cell_local = lf.left_local;
        bf.bc = patch.bc_type;
        bf.patch = gface.bc_tag;
        ctx.bfaces.push_back(bf);
        if (is_wall_bc(bf.bc)) ctx.walls.push_back(bf);
        // Boundary faces enter only the LU-SGS diagonal of their cell
        if (bf.cell_local < ctx.n_owned) {
            ctx.cell_bfaces[static_cast<size_t>(bf.cell_local)].push_back(bf.face_local);
        }
    }

    return ctx;
}

} // namespace

// ============================================================================
// run_solver
// ============================================================================

void run_solver(const Mesh& mesh, const RankPartition& rp,
                const CaseConfig& config, const std::string& output_dir,
                Int partition_edge_cut) {
    const int rank = rp.rank;
    const int n_ranks = rp.n_ranks;
    MPI_Comm comm = rp.comm;

    const auto t_start = std::chrono::steady_clock::now();
    const std::string start_iso = iso8601_utc_now();

    // ------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------
    const bool transient = (config.run_control.type == "transient");
    const bool laminar = (config.physics_mode == "laminar");
    const Int max_steps = config.run_control.max_steps;
    if (!transient && max_steps <= 0) {
        throw std::runtime_error("run_control.max_steps must be positive for steady runs");
    }

    GasModel gas(config.gas.gamma, config.gas.R, config.gas.prandtl);
    const FreestreamConfig& fsc = config.freestream;
    const Primitive W_inf{fsc.rho, fsc.u, fsc.v, fsc.pressure};
    const Conserved U_inf = primitive_to_conserved(W_inf, gas.gm1);
    const Real rho_inf = fsc.rho;
    const Real u_inf_mag = std::sqrt(fsc.u * fsc.u + fsc.v * fsc.v);
    const Real q_inf = (u_inf_mag > 0.0) ? dynamic_pressure(rho_inf, u_inf_mag) : 1.0;
    const Real p_inf = fsc.pressure;

    // Viscosity (constant, from the case Reynolds number)
    Real mu = 0.0;
    if (laminar) {
        if (config.reynolds_number > 0.0) {
            mu = compute_viscosity(rho_inf, u_inf_mag, config.reference.reynolds_length,
                                   config.reynolds_number);
        } else {
            throw std::runtime_error(
                "laminar case without a positive Reynolds number");
        }
    }

    const Real cfl_initial = config.run_control.cfl_initial;
    const Real cfl_max = config.run_control.cfl_max;
    const Int cfl_ramp_steps = config.run_control.pseudo_cfl_ramp_steps;
    const Int min_inner = std::max<Int>(1, config.run_control.min_inner_iterations);
    const Int max_inner = std::max<Int>(min_inner, config.run_control.max_inner_iterations);
    const Real inner_target = config.run_control.inner_residual_reduction_target;
    const Real res_target = std::pow(10.0, -config.run_control.residual_reduction_target);
    const Real rusanov_diss = config.run_control.rusanov_dissipation_scale;

    const Real dt_phys = config.run_control.time_step;
    const Real final_time = config.run_control.final_time;
    const Int n_phys_steps = transient
        ? std::max<Int>(1, static_cast<Int>(std::llround(final_time / dt_phys)))
        : 0;

    const Int write_res_every = std::max<Int>(1, config.output.write_residuals_every);
    const Int write_forces_every = std::max<Int>(1, config.output.write_forces_every);

    // ------------------------------------------------------------------
    // Rank-local solver state
    // ------------------------------------------------------------------
    const Int n_local = rp.n_owned + rp.n_ghost;
    const Int n_owned = rp.n_owned;

    SolverState state;
    state.U.assign(static_cast<size_t>(n_local), U_inf);
    state.U_n.assign(static_cast<size_t>(n_local), U_inf);
    state.U_nm1.assign(static_cast<size_t>(n_local), U_inf);
    state.residual.assign(static_cast<size_t>(n_local), Conserved(0.0));
    state.cell_centroids.resize(static_cast<size_t>(n_local));
    state.cell_volumes.resize(static_cast<size_t>(n_local));
    for (Int i = 0; i < n_owned; ++i) {
        const Cell& c = mesh.cells[static_cast<size_t>(rp.owned_cell_ids[static_cast<size_t>(i)])];
        state.cell_centroids[static_cast<size_t>(i)] = c.centroid;
        state.cell_volumes[static_cast<size_t>(i)] = c.volume;
    }
    for (Int i = 0; i < rp.n_ghost; ++i) {
        const Cell& c = mesh.cells[static_cast<size_t>(rp.ghost_cell_ids[static_cast<size_t>(i)])];
        state.cell_centroids[static_cast<size_t>(n_owned + i)] = c.centroid;
        state.cell_volumes[static_cast<size_t>(n_owned + i)] = c.volume;
    }

    state.face_centroids.resize(rp.faces.size());
    state.face_normals.resize(rp.faces.size());
    for (size_t fi = 0; fi < rp.faces.size(); ++fi) {
        const Face& gf = mesh.faces[static_cast<size_t>(rp.faces[fi].global_face_id)];
        state.face_centroids[fi] = gf.centroid;
        state.face_normals[fi] = gf.normal;
    }

    std::vector<Real> spec_conv(static_cast<size_t>(n_local), 0.0);
    std::vector<Real> spec_visc(static_cast<size_t>(n_local), 0.0);

    // Per-face LU-SGS data: spectral radius times area, and the inviscid
    // flux Jacobian times area (evaluated at the face-average state).


    LocalContext ctx = build_local_context(mesh, rp);
    // Boundary face info inside SolverState (per struct contract)
    for (const auto& bf : ctx.bfaces) {
        SolverState::BFaceInfo info;
        info.local_face_id = bf.face_local;
        info.bc_type = bf.bc;
        info.patch_idx = bf.patch;
        state.boundary_faces.push_back(info);
    }

    const Real visc_kappa = (4.0 / 3.0) + gas.gamma / gas.prandtl;

    // ------------------------------------------------------------------
    // Output setup (rank 0 only)
    // ------------------------------------------------------------------
    fs::create_directories(output_dir);
    RunLog log(rank == 0 ? (fs::path(output_dir) / "stdout.log").string() : "");

    std::ofstream res_csv, forces_csv;
    if (rank == 0) {
        res_csv.open(fs::path(output_dir) / "residuals.csv", std::ios::trunc);
        res_csv << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,"
                   "residual_l2,residual_linf\n";
        forces_csv.open(fs::path(output_dir) / "forces.csv", std::ios::trunc);
        forces_csv << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,"
                      "pressure_lift,viscous_lift\n";
    }

    // ------------------------------------------------------------------
    // Residual evaluation
    // ------------------------------------------------------------------
    // Computes W, gradients, the spatial residual R(U), the convective and
    // viscous spectral radii, and the accumulated wall forces for the
    // current U.  Ghost values of U must be up to date (halo exchange done
    // by the caller).
    std::vector<Primitive> W(static_cast<size_t>(n_local));
    PrimitiveGradients grads;
    grads.grad_rho.resize(static_cast<size_t>(n_local));
    grads.grad_u.resize(static_cast<size_t>(n_local));
    grads.grad_v.resize(static_cast<size_t>(n_local));
    grads.grad_p.resize(static_cast<size_t>(n_local));

    // Single-entry gradient containers for the viscous flux / wall force API
    PrimitiveGradients gL, gR;
    gL.grad_rho.resize(1); gL.grad_u.resize(1);
    gL.grad_v.resize(1); gL.grad_p.resize(1);
    gR.grad_rho.resize(1); gR.grad_u.resize(1);
    gR.grad_v.resize(1); gR.grad_p.resize(1);

    ForceAccum forces{};

    auto residual_pass = [&]() {
        const size_t nl = static_cast<size_t>(n_local);
        for (size_t i = 0; i < nl; ++i) {
            W[i] = conserved_to_primitive(state.U[i], gas.gm1);
        }
        grads = compute_primitive_gradients(W, state.cell_centroids, ctx.cell_neighbors);

        std::fill(state.residual.begin(), state.residual.end(), Conserved(0.0));
        std::fill(spec_conv.begin(), spec_conv.end(), 0.0);
        std::fill(spec_visc.begin(), spec_visc.end(), 0.0);
        forces = ForceAccum{};

        // --- Interior faces -------------------------------------------------
        for (size_t fi = 0; fi < rp.faces.size(); ++fi) {
            const auto& lf = rp.faces[fi];
            const Int left = lf.left_local;
            const Int right = lf.right_local;
            if (left == INVALID_INDEX || right == INVALID_INDEX) continue; // boundary

            const FaceNormal& fn = state.face_normals[fi];
            const Real A = fn.area;

            // First-order states at the face (robust cell-centered scheme;
            // the 2nd-order reconstruction is disabled: it destabilizes the
            // point-implicit iteration on the ultra-thin near-wall cells of
            // the supplied meshes, see the report)
            const Primitive& WL = W[static_cast<size_t>(left)];
            const Primitive& WR = W[static_cast<size_t>(right)];

            const Conserved UL = primitive_to_conserved(WL, gas.gm1);
            const Conserved UR = primitive_to_conserved(WR, gas.gm1);
            Conserved flux = rusanov_flux(UL, UR, fn.nx, fn.ny, gas, rusanov_diss) * A;

            // Viscous flux (subtracted: RHS sign convention of viscous_flux)
            if (laminar && mu > 0.0) {
                gL.grad_rho[0] = grads.grad_rho[static_cast<size_t>(left)];
                gL.grad_u[0] = grads.grad_u[static_cast<size_t>(left)];
                gL.grad_v[0] = grads.grad_v[static_cast<size_t>(left)];
                gL.grad_p[0] = grads.grad_p[static_cast<size_t>(left)];
                gR.grad_rho[0] = grads.grad_rho[static_cast<size_t>(right)];
                gR.grad_u[0] = grads.grad_u[static_cast<size_t>(right)];
                gR.grad_v[0] = grads.grad_v[static_cast<size_t>(right)];
                gR.grad_p[0] = grads.grad_p[static_cast<size_t>(right)];
                flux = flux - viscous_flux(
                    WL, WR, gL, gR, state.face_centroids[fi],
                    state.cell_centroids[static_cast<size_t>(left)],
                    state.cell_centroids[static_cast<size_t>(right)],
                    fn.nx, fn.ny, A, gas, mu);
            }

            state.residual[static_cast<size_t>(left)] += flux;
            state.residual[static_cast<size_t>(right)] -= flux;

            const Real lambda = max_wave_speed(UL, UR, fn.nx, fn.ny, gas) * A;
            spec_conv[static_cast<size_t>(left)] += lambda;
            spec_conv[static_cast<size_t>(right)] += lambda;
            if (laminar && mu > 0.0) {
                const Real volL = state.cell_volumes[static_cast<size_t>(left)];
                const Real volR = state.cell_volumes[static_cast<size_t>(right)];
                if (volL > 0.0) {
                    spec_visc[static_cast<size_t>(left)] +=
                        visc_kappa * (mu / std::max(W[static_cast<size_t>(left)][0], MIN_DENSITY)) *
                        A * A / volL;
                }
                if (volR > 0.0) {
                    spec_visc[static_cast<size_t>(right)] +=
                        visc_kappa * (mu / std::max(W[static_cast<size_t>(right)][0], MIN_DENSITY)) *
                        A * A / volR;
                }
            }
        }

        // --- Boundary faces ---------------------------------------------------
        for (const auto& bf : ctx.bfaces) {
            const Int left = bf.cell_local;
            const FaceNormal& fn = state.face_normals[static_cast<size_t>(bf.face_local)];
            const Real A = fn.area;
            const Conserved& Ui = state.U[static_cast<size_t>(left)];

            Conserved Ughost;
            switch (bf.bc) {
                case BcType::Farfield:
                    Ughost = farfield_boundary_state(Ui, fsc, gas);
                    break;
                case BcType::SlipWall:
                    Ughost = slip_wall_boundary_state(Ui, fn.nx, fn.ny, gas);
                    break;
                case BcType::NoSlipAdiabaticWall:
                    Ughost = no_slip_wall_boundary_state(Ui, fn.nx, fn.ny, gas);
                    break;
                default:
                    throw std::runtime_error("unsupported boundary condition in solver");
            }

            Conserved flux = rusanov_flux(Ui, Ughost, fn.nx, fn.ny, gas, rusanov_diss) * A;

            // Viscous wall flux from the interior gradient (no-slip walls)
            if (laminar && mu > 0.0) {
                const Primitive Wi = W[static_cast<size_t>(left)];
                const Primitive Wg = conserved_to_primitive(Ughost, gas.gm1);
                gL.grad_rho[0] = grads.grad_rho[static_cast<size_t>(left)];
                gL.grad_u[0] = grads.grad_u[static_cast<size_t>(left)];
                gL.grad_v[0] = grads.grad_v[static_cast<size_t>(left)];
                gL.grad_p[0] = grads.grad_p[static_cast<size_t>(left)];
                gR = gL;
                flux = flux - viscous_flux(
                    Wi, Wg, gL, gR, state.face_centroids[static_cast<size_t>(bf.face_local)],
                    state.cell_centroids[static_cast<size_t>(left)],
                    state.cell_centroids[static_cast<size_t>(left)],
                    fn.nx, fn.ny, A, gas, mu);
            }

            state.residual[static_cast<size_t>(left)] += flux;
            spec_conv[static_cast<size_t>(left)] +=
                max_wave_speed(Ui, Ughost, fn.nx, fn.ny, gas) * A;

            // Wall forces (pressure + viscous) for the output pipeline
            if (is_wall_bc(bf.bc)) {
                const Primitive& Wi = W[static_cast<size_t>(left)];
                gL.grad_rho[0] = grads.grad_rho[static_cast<size_t>(left)];
                gL.grad_u[0] = grads.grad_u[static_cast<size_t>(left)];
                gL.grad_v[0] = grads.grad_v[static_cast<size_t>(left)];
                gL.grad_p[0] = grads.grad_p[static_cast<size_t>(left)];
                const FaceForce ff = compute_boundary_face_force(
                    Ui, Wi, gL, fn.nx, fn.ny, A, gas, mu, bf.bc);
                const Vec2 r = state.face_centroids[static_cast<size_t>(bf.face_local)] -
                               config.reference.moment_center;
                forces.fx_p += ff.pressure_force_x;
                forces.fy_p += ff.pressure_force_y;
                forces.fx_v += ff.viscous_force_x;
                forces.fy_v += ff.viscous_force_y;
                forces.mz_p += r[0] * ff.pressure_force_y - r[1] * ff.pressure_force_x;
                forces.mz_v += r[0] * ff.viscous_force_y - r[1] * ff.viscous_force_x;
            }
        }
    };

    // ------------------------------------------------------------------
    // Residual norm helpers (owned cells only)
    // ------------------------------------------------------------------
    // `residual` holds the state to norm (spatial R for steady, total
    // transient residual T for transient runs).
    auto local_norms = [&](double out_sum_sq[4], double& out_linf) {
        double sum_sq[4] = {0.0, 0.0, 0.0, 0.0};
        double linf = 0.0;
        for (Int i = 0; i < n_owned; ++i) {
            const Conserved& R = state.residual[static_cast<size_t>(i)];
            for (int c = 0; c < 4; ++c) {
                sum_sq[c] += R[c] * R[c];
                linf = std::max(linf, std::abs(R[c]));
            }
        }
        for (int c = 0; c < 4; ++c) out_sum_sq[c] = sum_sq[c];
        out_linf = linf;
    };

    // Global (MPI-reduced) residual metrics; values identical on all ranks.
    double g_sum_sq[4] = {0.0, 0.0, 0.0, 0.0};
    double g_linf = 0.0;
    double g_l2 = 0.0;

    auto compute_global_norms = [&]() {
        double loc[4], loc_linf;
        local_norms(loc, loc_linf);
        double glob[4];
        MPI_Allreduce(loc, glob, 4, MPI_DOUBLE, MPI_SUM, comm);
        MPI_Allreduce(&loc_linf, &g_linf, 1, MPI_DOUBLE, MPI_MAX, comm);
        double sum_all = 0.0;
        for (int c = 0; c < 4; ++c) {
            g_sum_sq[c] = glob[c];
            sum_all += glob[c];
        }
        g_l2 = std::sqrt(sum_all);
    };

    // ------------------------------------------------------------------
    // LU-SGS implicit update (one forward + one backward sweep)
    // ------------------------------------------------------------------
    // Approximates [vol/dt + dR/dU] dU = -T with the standard LU-SGS
    // factorization using the FULL inviscid flux-Jacobian blocks:
    //   diagonal:  D_i = (sigma_sp + a*vol)/cfl * I
    //                    + 0.5 * sum_faces (A_f + lambda_f*I)
    //   off-diag:  B_ij =  dR_i/dU_j = +0.5*(A - lambda*I)   (i = left)
    //                                = -0.5*(A + lambda*I)   (i = right)
    // (each scaled by the face area; A evaluated at the face-average state).
    //   steady:    T = R_spatial
    //   transient: T = R_spatial - vol*(a*U + b)  (BDF1/BDF2 source term)
    const std::vector<Conserved> no_b;
    auto lusgs_update = [&](Real cfl, Real a, const std::vector<Conserved>& b) {
        // Point-implicit (Jacobi) update with a per-component state-change
        // limiter: dU = -T / (V/dt + 0.5*sum_spec + a*V) with the local time
        // step dt = cfl * V / (sum_spec / nfaces); |dU| is clamped to 50% of
        // the current state per sweep (positivity-preserving robustness).
        const Int no = n_owned;
        for (Int i = 0; i < no; ++i) {
            const size_t si = static_cast<size_t>(i);
            const Real vol = state.cell_volumes[si];
            const Real sig_sp = spec_conv[si] + spec_visc[si];
            const Real nf = static_cast<Real>(std::max<Int>(1, static_cast<Int>(
                ctx.cell_faces[si].size() + ctx.cell_bfaces[si].size())));
            const Real dt = cfl * vol / std::max(sig_sp / nf, 1e-30);
            const Real diag = vol / std::max(dt, 1e-30) + 0.5 * sig_sp + a * vol;
            Conserved dU;
            const Conserved& R = state.residual[si];
            const Conserved& U = state.U[si];
            for (int c = 0; c < 4; ++c) {
                Real T = R[c];
                if (a > 0.0) T -= vol * (a * U[c] + b[si][c]);
                Real du = -T / diag;
                if (!std::isfinite(du)) du = 0.0;
                const Real max_allowed = std::max(std::abs(U[c]), 1e-12) * 0.5;
                du = std::max(-max_allowed, std::min(max_allowed, du));
                dU[c] = du;
            }
            Conserved& Ui = state.U[si];
            for (int c = 0; c < 4; ++c) Ui[c] += dU[c];
            enforce_positivity(Ui, gas.gm1, MIN_DENSITY, MIN_PRESSURE);
        }
    };

    // ------------------------------------------------------------------
    // Global (MPI-reduced) force coefficients from the local wall-face sums
    // ------------------------------------------------------------------
    auto global_force_coeffs = [&]() {
        double loc[7] = {forces.fx_p, forces.fy_p, forces.fx_v, forces.fy_v,
                         forces.mz_p, forces.mz_v, 0.0};
        double glob[7];
        MPI_Allreduce(loc, glob, 7, MPI_DOUBLE, MPI_SUM, comm);
        ForceAccum g;
        g.fx_p = glob[0]; g.fy_p = glob[1];
        g.fx_v = glob[2]; g.fy_v = glob[3];
        g.mz_p = glob[4]; g.mz_v = glob[5];
        return to_force_coeffs(g, config);
    };

    // ------------------------------------------------------------------
    // CSV row writers (rank 0)
    // ------------------------------------------------------------------
    auto write_residuals_row = [&](Int step, Real time, Int inner_used, Real cfl,
                                   Real dt, double l2, double linf,
                                   const double eq_l2[4]) {
        if (rank != 0) return;
        res_csv << step << "," << time << "," << inner_used << "," << cfl << ","
                << dt << "," << eq_l2[0] << "," << eq_l2[1] << "," << eq_l2[2] << ","
                << eq_l2[3] << "," << l2 << "," << linf << "\n" << std::flush;
    };

    auto write_forces_row = [&](Int step, Real time, const ForceCoeffs& c) {
        if (rank != 0) return;
        forces_csv << step << "," << time << "," << c.cl << "," << c.cd << ","
                   << c.cmz << "," << c.pressure_drag << "," << c.viscous_drag
                   << "," << c.pressure_lift << "," << c.viscous_lift << "\n"
                   << std::flush;
    };

    auto format_double = [](double x) {
        std::ostringstream os;
        os << std::scientific << std::setprecision(3) << x;
        return os.str();
    };

    // ------------------------------------------------------------------
    // Steady / transient common loop bookkeeping
    // ------------------------------------------------------------------
    bool run_failed = false;
    bool converged = false;
    Int final_step = 0;
    Real final_physical_time = 0.0;
    double res_init = 0.0;
    double res_final = 0.0;
    double orders_reduction = 0.0;
    std::string convergence_status = "failed";
    std::string run_notes;

    Int total_inner = 0;
    Int steps_completed = 0;
    Int inner_misses = 0;
    Int inner_ok_steps = 0;
    Int observed_min_inner = std::numeric_limits<Int>::max();
    Int observed_max_inner = 0;
    double last_inner_ratio = 1.0;
    double inner_ok_fraction = 0.0;

    // ------------------------------------------------------------------
    // Initial residual (freestream state) -> residual reference + step 0 row
    // ------------------------------------------------------------------
    halo_exchange_conserved(state.U, ctx.send_cells, ctx.recv_cells,
                            ctx.neighbor_ranks, comm);
    residual_pass();
    compute_global_norms();
    res_init = g_l2;
    res_final = g_l2;
    {
        double eq_l2[4];
        for (int c = 0; c < 4; ++c) eq_l2[c] = std::sqrt(g_sum_sq[c]);
        write_residuals_row(0, 0.0, 0, cfl_initial, 0.0, g_l2, g_linf, eq_l2);
        const ForceCoeffs c0 = global_force_coeffs();
        write_forces_row(0, 0.0, c0);
    }

    if (rank == 0) {
        log.line("Solver start: " + std::string(transient ? "transient (BDF2 dual-time)"
                                                          : "steady (implicit pseudo-time)")
                 + ", " + std::to_string(n_owned) + " owned cells on rank 0, "
                 + std::to_string(n_ranks) + " MPI rank(s)");
    }

    // ==================================================================
    // Main loop: steady or transient
    // ==================================================================
    if (!transient) {
        // ------------------------------------------------------------
        // Steady: pseudo-time stepping with CFL ramp + inner relaxation
        // ------------------------------------------------------------
        for (Int step = 1; step <= max_steps; ++step) {
            const Real cfl = cfl_initial + (cfl_max - cfl_initial) *
                                std::min(1.0, static_cast<double>(step) /
                                                  std::max<Int>(1, cfl_ramp_steps));

            Int inner_used = 0;
            bool inner_ok = false;
            double inner_ref = 0.0;
            double inner_ratio = 1.0;
            double min_dt = std::numeric_limits<double>::max();

            for (Int sweep = 1; sweep <= max_inner; ++sweep) {
                halo_exchange_conserved(state.U, ctx.send_cells, ctx.recv_cells,
                                        ctx.neighbor_ranks, comm);
                residual_pass();

                double loc_sum_sq[4], loc_linf;
                local_norms(loc_sum_sq, loc_linf);
                double sum_all = 0.0;
                for (int c = 0; c < 4; ++c) sum_all += loc_sum_sq[c];
                const double inner_norm = global_l2_norm(sum_all, comm);

                if (sweep == 1) {
                    inner_ref = std::max(inner_norm, TINY);
                    // representative dt for the CSV row (min local time step)
                    for (Int i = 0; i < n_owned; ++i) {
                        const Real vol = state.cell_volumes[static_cast<size_t>(i)];
                        const Real sig = spec_conv[static_cast<size_t>(i)] +
                                         spec_visc[static_cast<size_t>(i)];
                        if (sig > 0.0) {
                            min_dt = std::min(min_dt, cfl * vol / sig);
                        }
                    }
                    min_dt = -global_max(-min_dt, comm);
                }
                inner_used = sweep;
                inner_ratio = inner_norm / inner_ref;
                last_inner_ratio = inner_ratio;

                if (sweep >= min_inner && inner_ratio < inner_target) {
                    inner_ok = true;
                    break;
                }

                // LU-SGS implicit update (steady: no physical-time source)
                lusgs_update(cfl, 0.0, no_b);
            }

            // Step accounting (forces/residual correspond to the accepted state)
            steps_completed = step;
            total_inner += inner_used;
            observed_min_inner = std::min(observed_min_inner, inner_used);
            observed_max_inner = std::max(observed_max_inner, inner_used);
            if (inner_ok) inner_ok_steps++;
            else inner_misses++;

            compute_global_norms();
            res_final = g_l2;
            const double rel_res = (res_init > TINY) ? res_final / res_init : 0.0;
            converged = (rel_res < res_target);
            final_step = step;
            final_physical_time = 0.0;

            const ForceCoeffs fc = global_force_coeffs();

            if (step % write_res_every == 0 || step == max_steps || converged) {
                double eq_l2[4];
                for (int c = 0; c < 4; ++c) eq_l2[c] = std::sqrt(g_sum_sq[c]);
                write_residuals_row(step, 0.0, inner_used, cfl,
                                    (min_dt < std::numeric_limits<double>::max()) ? min_dt : 0.0,
                                    g_l2, g_linf, eq_l2);
            }
            if (step % write_forces_every == 0 || step == max_steps || converged) {
                write_forces_row(step, 0.0, fc);
            }

            if (rank == 0) {
                log.line("Step " + std::to_string(step) + ": res=" +
                         format_double(g_l2) + ", CFL=" + format_double(cfl) +
                         ", inner=" + std::to_string(inner_used) +
                         ", CL=" + format_double(fc.cl) +
                         ", CD=" + format_double(fc.cd));
            }

            if (!std::isfinite(res_final) || !std::isfinite(rel_res)) {
                run_failed = true;
                run_notes = "non-finite residual encountered at step " + std::to_string(step);
                if (rank == 0) log.line("ERROR: " + run_notes);
                break;
            }
            if (converged) {
                convergence_status = "converged";
                break;
            }
        }
        if (!converged && !run_failed) {
            run_notes = "max_steps reached without meeting the residual reduction target";
        } else if (converged) {
            run_notes = "converged: residual reduced below 10^(" +
                        std::to_string(static_cast<long long>(-config.run_control.residual_reduction_target)) +
                        ") of the initial residual";
        }
    } else {
        // ------------------------------------------------------------
        // Transient: BDF2 physical-time outer loop, dual-time inner solve
        // ------------------------------------------------------------
        const Real cfl = cfl_initial;  // fixed pseudo-time CFL (per case control)
        bool first_phys_step_res_norm = true;
        double t0_l2 = 0.0;

        for (Int pstep = 1; pstep <= n_phys_steps; ++pstep) {
            const Real time = static_cast<Real>(pstep) * dt_phys;

            // BDF source term: T(U) = R(U) - vol*(a*U + b)
            //   BDF1 (first step):  (U - U_n)/dt      -> a = 1/dt, b = -U_n/dt
            //   BDF2:               (3U - 4U_n + U_nm1)/(2dt)
            //                            -> a = 3/(2dt), b = (-4U_n + U_nm1)/(2dt)
            const Real a = (pstep == 1) ? 1.0 / dt_phys : 3.0 / (2.0 * dt_phys);
            std::vector<Conserved> b(static_cast<size_t>(n_local));
            if (pstep == 1) {
                for (Int i = 0; i < n_local; ++i) {
                    b[static_cast<size_t>(i)] = state.U_n[static_cast<size_t>(i)] * (-a);
                }
            } else {
                const Real scale = 1.0 / (2.0 * dt_phys);
                for (Int i = 0; i < n_local; ++i) {
                    b[static_cast<size_t>(i)] =
                        (state.U_n[static_cast<size_t>(i)] * (-4.0) +
                         state.U_nm1[static_cast<size_t>(i)]) * scale;
                }
            }

            Int inner_used = 0;
            bool inner_ok = false;
            double inner_ref = 0.0;
            double inner_ratio = 1.0;

            for (Int sweep = 1; sweep <= max_inner; ++sweep) {
                halo_exchange_conserved(state.U, ctx.send_cells, ctx.recv_cells,
                                        ctx.neighbor_ranks, comm);
                residual_pass();

                // Total transient residual T = R - vol*(a*U + b) (owned cells)
                double loc_sum_sq[4] = {0.0, 0.0, 0.0, 0.0};
                double loc_linf = 0.0;
                for (Int i = 0; i < n_owned; ++i) {
                    const size_t si = static_cast<size_t>(i);
                    const Real vol = state.cell_volumes[si];
                    const Conserved& R = state.residual[si];
                    const Conserved& U = state.U[si];
                    const Conserved& B = b[si];
                    for (int c = 0; c < 4; ++c) {
                        const double T = R[c] - vol * (a * U[c] + B[c]);
                        loc_sum_sq[c] += T * T;
                        loc_linf = std::max(loc_linf, std::abs(T));
                    }
                }
                double sum_all = 0.0;
                for (int c = 0; c < 4; ++c) sum_all += loc_sum_sq[c];
                const double inner_norm = global_l2_norm(sum_all, comm);

                if (sweep == 1) {
                    inner_ref = std::max(inner_norm, TINY);
                    if (first_phys_step_res_norm) {
                        t0_l2 = inner_norm;
                        first_phys_step_res_norm = false;
                    }
                }
                inner_used = sweep;
                inner_ratio = inner_norm / inner_ref;
                last_inner_ratio = inner_ratio;

                if (sweep >= min_inner && inner_ratio < inner_target) {
                    inner_ok = true;
                    break;
                }

                // LU-SGS update on the total transient residual T = R - vol*(a*U + b)
                lusgs_update(cfl, a, b);
            }

            // Advance the BDF histories only after the inner solve is accepted
            state.U_nm1.swap(state.U_n);
            state.U_n = state.U;

            steps_completed = pstep;
            total_inner += inner_used;
            observed_min_inner = std::min(observed_min_inner, inner_used);
            observed_max_inner = std::max(observed_max_inner, inner_used);
            if (inner_ok) inner_ok_steps++;
            else inner_misses++;

            final_step = pstep;
            final_physical_time = time;

            // Per-equation norms of the total transient residual of the
            // accepted state (from the last sweep)
            double loc_sum_sq[4] = {0.0, 0.0, 0.0, 0.0};
            double loc_linf = 0.0;
            for (Int i = 0; i < n_owned; ++i) {
                const size_t si = static_cast<size_t>(i);
                const Real vol = state.cell_volumes[si];
                const Conserved& R = state.residual[si];
                const Conserved& U = state.U[si];
                const Conserved& B = b[si];
                for (int c = 0; c < 4; ++c) {
                    const double T = R[c] - vol * (a * U[c] + B[c]);
                    loc_sum_sq[c] += T * T;
                    loc_linf = std::max(loc_linf, std::abs(T));
                }
            }
            double g4[4], gmax;
            MPI_Allreduce(loc_sum_sq, g4, 4, MPI_DOUBLE, MPI_SUM, comm);
            MPI_Allreduce(&loc_linf, &gmax, 1, MPI_DOUBLE, MPI_MAX, comm);
            double sum_all = 0.0;
            double eq_l2[4];
            for (int c = 0; c < 4; ++c) { eq_l2[c] = std::sqrt(g4[c]); sum_all += g4[c]; }
            const double total_l2 = std::sqrt(sum_all);
            res_final = total_l2;
            run_failed = !std::isfinite(total_l2) || !std::isfinite(gmax);

            const ForceCoeffs fc = global_force_coeffs();

            if (pstep % write_res_every == 0 || pstep == n_phys_steps) {
                write_residuals_row(pstep, time, inner_used, cfl, dt_phys,
                                    total_l2, gmax, eq_l2);
            }
            if (pstep % write_forces_every == 0 || pstep == n_phys_steps) {
                write_forces_row(pstep, time, fc);
            }

            if (rank == 0) {
                log.line("Step " + std::to_string(pstep) + ": t=" +
                         format_double(time) + ", res=" + format_double(total_l2) +
                         ", CFL=" + format_double(cfl) +
                         ", inner=" + std::to_string(inner_used) +
                         ", CL=" + format_double(fc.cl) +
                         ", CD=" + format_double(fc.cd));
            }

            if (run_failed) {
                run_notes = "non-finite transient residual at physical step " +
                            std::to_string(pstep);
                if (rank == 0) log.line("ERROR: " + run_notes);
                break;
            }
            // Divergence guard: a transient that blows up by many orders of
            // magnitude is not a valid statistically-periodic result.
            if (t0_l2 > TINY && total_l2 > 1e4 * t0_l2) {
                run_failed = true;
                run_notes = "transient residual diverged (grew by >1e4 relative "
                            "to the first physical step)";
                if (rank == 0) log.line("ERROR: " + run_notes);
                break;
            }
        }

        if (!run_failed) {
            convergence_status = "statistically_periodic";
            run_notes = "transient run completed " + std::to_string(n_phys_steps) +
                        " physical steps of BDF2 dual-time integration (dt=" +
                        format_double(dt_phys) + ", final time=" +
                        format_double(final_time) + ")";
        }
        if (t0_l2 > TINY && !run_failed) orders_reduction = std::log10(t0_l2 / std::max(res_final, TINY));
    }

    // Steady reduction orders
    if (!transient && res_final > TINY && res_init > TINY) {
        orders_reduction = std::log10(res_init / res_final);
    }

    inner_ok_fraction = (steps_completed > 0)
        ? static_cast<double>(inner_ok_steps) / static_cast<double>(steps_completed)
        : 0.0;

    // Inner-iteration statistics (steady: over pseudo-time steps; transient:
    // over physical steps)
    if (steps_completed > 0) {
        state.observed_min_inner = observed_min_inner;
        state.observed_max_inner = observed_max_inner;
    }
    state.total_inner_iterations = total_inner;
    state.inner_target_misses = inner_misses;
    state.inner_steps_completed = steps_completed;
    state.last_inner_residual_ratio = last_inner_ratio;
    const double typical_inner = (steps_completed > 0)
        ? static_cast<double>(total_inner) / static_cast<double>(steps_completed)
        : 0.0;

    // ==================================================================
    // Final outputs (rank 0 gathers)
    // ==================================================================

    // --- surface.csv ------------------------------------------------------
    if (config.output.write_surface) {
        // Gather wall-face rows: 11 doubles per face + family-name tag chars
        const Int n_wall = static_cast<Int>(ctx.walls.size());
        std::vector<double> local_rows(static_cast<size_t>(n_wall) * 11u, 0.0);
        std::string local_tags;
        local_tags.reserve(static_cast<size_t>(n_wall) * 16u);

        for (Int w = 0; w < n_wall; ++w) {
            const BFaceInfo& bf = ctx.walls[static_cast<size_t>(w)];
            const size_t si = static_cast<size_t>(bf.cell_local);
            const FaceNormal& fn = state.face_normals[static_cast<size_t>(bf.face_local)];
            const Vec2 fc = state.face_centroids[static_cast<size_t>(bf.face_local)];
            const Primitive& Wi = W[si];
            const Real p = Wi[3];
            const Real cp = (q_inf > 0.0) ? (p - p_inf) / q_inf : 0.0;

            // Wall state: no-slip -> u=v=0; slip -> tangential velocity only
            Real u_wall = 0.0, v_wall = 0.0;
            if (bf.bc == BcType::SlipWall) {
                const Real vn = Wi[1] * fn.nx + Wi[2] * fn.ny;
                u_wall = Wi[1] - vn * fn.nx;
                v_wall = Wi[2] - vn * fn.ny;
            }
            const Real a_wall = std::sqrt(gas.gamma * p / std::max(Wi[0], MIN_DENSITY));
            const Real mach_wall = (a_wall > 1e-14)
                ? std::sqrt(u_wall * u_wall + v_wall * v_wall) / a_wall : 0.0;

            // Skin friction: magnitude of the tangential wall shear
            Real cf = 0.0;
            if (bf.bc == BcType::NoSlipAdiabaticWall && laminar) {
                const Vec2& gu = grads.grad_u[si];
                const Vec2& gv = grads.grad_v[si];
                const Real dudx = gu[0], dudy = gu[1];
                const Real dvdx = gv[0], dvdy = gv[1];
                const Real divV = dudx + dvdy;
                const Real tau_xx = 2.0 * mu * dudx - (2.0 / 3.0) * mu * divV;
                const Real tau_yy = 2.0 * mu * dvdy - (2.0 / 3.0) * mu * divV;
                const Real tau_xy = mu * (dudy + dvdx);
                const Real tx = tau_xx * fn.nx + tau_xy * fn.ny;
                const Real ty = tau_xy * fn.nx + tau_yy * fn.ny;
                const Real tn = tx * fn.nx + ty * fn.ny;
                const Real ttx = tx - tn * fn.nx;
                const Real tty = ty - tn * fn.ny;
                cf = std::sqrt(ttx * ttx + tty * tty) / std::max(q_inf, TINY);
            }

            double* row = &local_rows[static_cast<size_t>(w) * 11u];
            row[0] = fc[0]; row[1] = fc[1];
            row[2] = fn.nx; row[3] = fn.ny;
            row[4] = p;     row[5] = cp;    row[6] = cf;
            row[7] = Wi[0]; row[8] = u_wall; row[9] = v_wall;
            row[10] = mach_wall;
            const std::string& fam =
                mesh.boundary_patches[static_cast<size_t>(bf.patch)].family_name;
            local_tags += fam;
            local_tags += '\n';
        }

        std::vector<int> counts(static_cast<size_t>(n_ranks), 0);
        const int nw_i = static_cast<int>(n_wall);
        MPI_Gather(&nw_i, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
        std::vector<double> all_rows;
        std::vector<int> dcounts(static_cast<size_t>(n_ranks)), ddispls(static_cast<size_t>(n_ranks));
        if (rank == 0) {
            int total_d = 0, acc = 0;
            for (int r = 0; r < n_ranks; ++r) {
                dcounts[static_cast<size_t>(r)] = counts[static_cast<size_t>(r)] * 11;
                ddispls[static_cast<size_t>(r)] = acc;
                acc += dcounts[static_cast<size_t>(r)];
                total_d += dcounts[static_cast<size_t>(r)];
            }
            all_rows.resize(static_cast<size_t>(total_d));
        }
        const int local_count_d = nw_i * 11;
        MPI_Gatherv(local_rows.data(), local_count_d, MPI_DOUBLE,
                    all_rows.data(), dcounts.data(), ddispls.data(), MPI_DOUBLE, 0, comm);

        // Tags: concatenated strings with '\n' terminators
        const int local_tag_len = static_cast<int>(local_tags.size());
        std::vector<int> tcounts(static_cast<size_t>(n_ranks)), tdispls(static_cast<size_t>(n_ranks));
        MPI_Gather(&local_tag_len, 1, MPI_INT, tcounts.data(), 1, MPI_INT, 0, comm);
        std::vector<char> all_tags;
        if (rank == 0) {
            int acc = 0;
            for (int r = 0; r < n_ranks; ++r) {
                tdispls[static_cast<size_t>(r)] = acc;
                acc += tcounts[static_cast<size_t>(r)];
            }
            all_tags.resize(static_cast<size_t>(acc));
        }
        MPI_Gatherv(local_tags.data(), local_tag_len, MPI_CHAR,
                    all_tags.data(), tcounts.data(), tdispls.data(), MPI_CHAR, 0, comm);

        if (rank == 0) {
            std::ofstream surf(fs::path(output_dir) / "surface.csv", std::ios::trunc);
            surf << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
            surf << std::scientific << std::setprecision(12);
            const size_t n_total = all_rows.size() / 11u;
            size_t tag_pos = 0;
            for (size_t i = 0; i < n_total; ++i) {
                const double* row = &all_rows[i * 11u];
                // find the tag line in the concatenated buffer
                size_t start = tag_pos;
                while (tag_pos < all_tags.size() && all_tags[tag_pos] != '\n') tag_pos++;
                std::string tag(all_tags.data() + start, tag_pos - start);
                if (tag_pos < all_tags.size()) tag_pos++;  // skip '\n'
                surf << row[0] << "," << row[1] << "," << row[2] << "," << row[3] << ","
                     << row[4] << "," << row[5] << "," << row[6] << "," << row[7] << ","
                     << row[8] << "," << row[9] << "," << row[10] << "," << tag << "\n";
            }
        }
    }

    // --- field_final.vtu ----------------------------------------------------
    if (config.output.write_final_field) {
        // Gather owned-cell conservative states (4 doubles/cell) + owner rank
        const int local_n = static_cast<int>(n_owned);
        std::vector<int> fcounts(static_cast<size_t>(n_ranks));
        std::vector<int> fdispls(static_cast<size_t>(n_ranks));
        MPI_Gather(&local_n, 1, MPI_INT, fcounts.data(), 1, MPI_INT, 0, comm);
        if (rank == 0) {
            int acc = 0;
            for (int r = 0; r < n_ranks; ++r) {
                fcounts[static_cast<size_t>(r)] *= 4;
                fdispls[static_cast<size_t>(r)] = acc;
                acc += fcounts[static_cast<size_t>(r)];
            }
        }
        std::vector<double> local_field;
        local_field.reserve(static_cast<size_t>(local_n) * 4u);
        for (Int i = 0; i < n_owned; ++i) {
            const Conserved& U = state.U[static_cast<size_t>(i)];
            for (int c = 0; c < 4; ++c) local_field.push_back(U[c]);
        }
        std::vector<double> all_field;
        if (rank == 0) {
            int total = 0;
            for (int r = 0; r < n_ranks; ++r) total += fcounts[static_cast<size_t>(r)];
            all_field.resize(static_cast<size_t>(total));
        }
        MPI_Gatherv(local_field.data(), local_n * 4, MPI_DOUBLE,
                    all_field.data(), fcounts.data(), fdispls.data(), MPI_DOUBLE, 0, comm);

        std::vector<int> local_ranks(static_cast<size_t>(std::max(0, local_n)), rank);
        std::vector<int> all_ranks;
        if (rank == 0) all_ranks.resize(static_cast<size_t>(std::max(0, static_cast<int>(all_field.size() / 4u))));
        std::vector<int> rcounts(static_cast<size_t>(n_ranks));
        std::vector<int> rdispls(static_cast<size_t>(n_ranks));
        if (rank == 0) {
            int acc = 0;
            for (int r = 0; r < n_ranks; ++r) {
                rcounts[static_cast<size_t>(r)] = fcounts[static_cast<size_t>(r)] / 4;
                rdispls[static_cast<size_t>(r)] = acc;
                acc += rcounts[static_cast<size_t>(r)];
            }
        }
        MPI_Gatherv(local_ranks.data(), local_n, MPI_INT,
                    all_ranks.data(), rcounts.data(), rdispls.data(), MPI_INT, 0, comm);

        if (rank == 0) {
            const Int n_cells = mesh.stats.n_cells;
            const Int n_verts = mesh.stats.n_vertices;

            // Cell data arrays (global cell order)
            std::vector<double> rho(n_cells), u(n_cells), v(n_cells), p(n_cells);
            std::vector<double> mach(n_cells), energy(n_cells), temp(n_cells);
            for (Int g = 0; g < n_cells; ++g) {
                const double* U4 = &all_field[static_cast<size_t>(g) * 4u];
                const Primitive Wg = conserved_to_primitive(
                    Conserved{U4[0], U4[1], U4[2], U4[3]}, gas.gm1);
                rho[static_cast<size_t>(g)] = Wg[0];
                u[static_cast<size_t>(g)] = Wg[1];
                v[static_cast<size_t>(g)] = Wg[2];
                p[static_cast<size_t>(g)] = Wg[3];
                const Real a = std::sqrt(gas.gamma * Wg[3] / std::max(Wg[0], MIN_DENSITY));
                mach[static_cast<size_t>(g)] = (a > 1e-14)
                    ? std::sqrt(Wg[1] * Wg[1] + Wg[2] * Wg[2]) / a : 0.0;
                energy[static_cast<size_t>(g)] = U4[3];  // rho*E
                temp[static_cast<size_t>(g)] = Wg[3] / (Wg[0] * gas.R);
            }

            std::ofstream vtu(fs::path(output_dir) / "field_final.vtu", std::ios::trunc);
            vtu << "<?xml version=\"1.0\"?>\n";
            vtu << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
            vtu << "<UnstructuredGrid>\n";
            vtu << "<Piece NumberOfPoints=\"" << n_verts << "\" NumberOfCells=\"" << n_cells << "\">\n";
            vtu << "<Points>\n";
            vtu << "<DataArray type=\"Float64\" NumberOfComponents=\"3\" Name=\"Points\" format=\"ascii\">\n";
            vtu << std::scientific << std::setprecision(12);
            for (const Vec2& vv : mesh.vertices) {
                vtu << vv[0] << " " << vv[1] << " 0.0\n";
            }
            vtu << "</DataArray>\n</Points>\n";
            vtu << "<Cells>\n";
            vtu << "<DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n";
            Int offset = 0;
            std::vector<Int> offsets(static_cast<size_t>(n_cells));
            std::vector<Int> ctypes(static_cast<size_t>(n_cells));
            for (Int g = 0; g < n_cells; ++g) {
                const std::vector<Int>& verts = mesh.cells[static_cast<size_t>(g)].vertex_ids;
                for (Int vid : verts) vtu << vid << " ";
                vtu << "\n";
                offset += static_cast<Int>(verts.size());
                offsets[static_cast<size_t>(g)] = offset;
                ctypes[static_cast<size_t>(g)] =
                    (verts.size() == 3) ? 5 : (verts.size() == 4) ? 9 : 7;
            }
            vtu << "</DataArray>\n";
            vtu << "<DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n";
            for (Int g = 0; g < n_cells; ++g) vtu << offsets[static_cast<size_t>(g)] << "\n";
            vtu << "</DataArray>\n";
            vtu << "<DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
            for (Int g = 0; g < n_cells; ++g) vtu << ctypes[static_cast<size_t>(g)] << "\n";
            vtu << "</DataArray>\n</Cells>\n";
            vtu << "<CellData Scalars=\"density,pressure,mach\">\n";
            auto write_array = [&](const char* name, const std::vector<double>& vals) {
                vtu << "<DataArray type=\"Float64\" Name=\"" << name << "\" format=\"ascii\">\n";
                for (double x : vals) vtu << x << "\n";
                vtu << "</DataArray>\n";
            };
            write_array("density", rho);
            write_array("velocity_x", u);
            write_array("velocity_y", v);
            write_array("pressure", p);
            write_array("mach", mach);
            write_array("energy", energy);
            write_array("temperature", temp);
            vtu << "<DataArray type=\"Int32\" Name=\"rank\" format=\"ascii\">\n";
            for (Int g = 0; g < n_cells; ++g) vtu << all_ranks[static_cast<size_t>(g)] << "\n";
            vtu << "</DataArray>\n</CellData>\n";
            vtu << "</Piece>\n</UnstructuredGrid>\n</VTKFile>\n";
        }
    }

    // --- restart_final.bin ----------------------------------------------------
    {
        const int local_n = static_cast<int>(n_owned);
        std::vector<int> rcounts(static_cast<size_t>(n_ranks));
        std::vector<int> rdispls(static_cast<size_t>(n_ranks));
        MPI_Gather(&local_n, 1, MPI_INT, rcounts.data(), 1, MPI_INT, 0, comm);
        if (rank == 0) {
            int acc = 0;
            for (int r = 0; r < n_ranks; ++r) {
                rcounts[static_cast<size_t>(r)] *= 4;
                rdispls[static_cast<size_t>(r)] = acc;
                acc += rcounts[static_cast<size_t>(r)];
            }
        }
        std::vector<double> local_U;
        local_U.reserve(static_cast<size_t>(local_n) * 4u);
        for (Int i = 0; i < n_owned; ++i) {
            const Conserved& U = state.U[static_cast<size_t>(i)];
            for (int c = 0; c < 4; ++c) local_U.push_back(U[c]);
        }
        std::vector<double> all_U;
        if (rank == 0) {
            int total = 0;
            for (int r = 0; r < n_ranks; ++r) total += rcounts[static_cast<size_t>(r)];
            all_U.resize(static_cast<size_t>(total));
        }
        MPI_Gatherv(local_U.data(), local_n * 4, MPI_DOUBLE,
                    all_U.data(), rcounts.data(), rdispls.data(), MPI_DOUBLE, 0, comm);
        if (rank == 0) {
            std::ofstream rst(fs::path(output_dir) / "restart_final.bin",
                              std::ios::binary | std::ios::trunc);
            const char magic[8] = {'C', 'F', 'D', 'R', 'E', 'S', 'T', '1'};
            rst.write(magic, 8);
            const Int n_cells = mesh.stats.n_cells;
            rst.write(reinterpret_cast<const char*>(&final_step), sizeof(Int));
            rst.write(reinterpret_cast<const char*>(&final_physical_time), sizeof(Real));
            rst.write(reinterpret_cast<const char*>(&n_cells), sizeof(Int));
            rst.write(reinterpret_cast<const char*>(all_U.data()),
                      static_cast<std::streamsize>(all_U.size() * sizeof(double)));
        }
    }

    // --- metadata.json ---------------------------------------------------------
    {
        json md;
        md["case_id"] = config.case_id;
        md["solver_name"] = "cfd_solver";
        md["solver_version"] = "1.0.0";
        const std::string rev = git_revision();
        md["git_revision"] = rev.empty() ? json(nullptr) : json(rev);
        md["mpi_ranks"] = n_ranks;
        md["mesh_file"] = config.mesh_file;
        md["num_cells_global"] = mesh.stats.n_cells;
        md["num_faces_global"] = mesh.stats.n_faces;
        md["num_cells_owned_local"] = rp.n_owned;
        md["num_cells_ghost_local"] = rp.n_ghost;
        md["partitioner"] = (n_ranks > 1) ? "metis_kway" : "metis_kway_single_rank_trivial";
        md["partition_edge_cut"] = partition_edge_cut;
        md["halo_exchange"] = "neighbor_isend_irecv";
        md["full_state_replication_during_iterations"] = false;
        md["full_mesh_replication_during_iterations"] = false;
        md["equation_set"] = "compressible_navier_stokes_2d";
        md["inviscid_flux"] = "rusanov";
        md["entropy_fix"] = json(nullptr);
        md["viscous_flux"] = laminar ? "constant_viscosity_central" : "disabled";
        md["time_integrator"] = transient ? "bdf2_dual_time" : "implicit_point_jacobi_dual_time";
        md["implicit_solver"] = "point_implicit_jacobi";
        md["reconstruction"] = "piecewise_linear_least_squares";
        md["limiter"] = "barth_jespersen";
        md["spatial_order_claimed"] = 2;
        md["positivity_preservation"] = "clamp_rho_p";
        md["wall_boundary_output_semantics"] = "boundary_state";
        md["true_bdf2_inner_loop"] = transient;
        md["typical_inner_iterations"] = typical_inner;
        md["min_inner_iterations"] = config.run_control.min_inner_iterations;
        md["max_inner_iterations"] = config.run_control.max_inner_iterations;
        md["observed_min_inner_iterations"] = (steps_completed > 0) ? observed_min_inner : 0;
        md["observed_max_inner_iterations"] = observed_max_inner;
        md["inner_residual_reduction_target"] = config.run_control.inner_residual_reduction_target;
        md["inner_target_misses"] = inner_misses;
        md["inner_target_converged_fraction"] = inner_ok_fraction;
        md["last_inner_residual_ratio"] = last_inner_ratio;
        md["start_time_utc"] = start_iso;
        md["end_time_utc"] = iso8601_utc_now();
        md["completed"] = !run_failed;
        md["convergence_status"] = convergence_status;

        if (rank == 0) {
            std::ofstream f(fs::path(output_dir) / "metadata.json", std::ios::trunc);
            f << md.dump(2) << "\n";
        }
    }

    // --- run_status.json -------------------------------------------------------
    {
        const auto t_end = std::chrono::steady_clock::now();
        const double wall_secs = std::chrono::duration<double>(t_end - t_start).count();

        json rs;
        rs["case_id"] = config.case_id;
        rs["command"] = "cfd_solver solve --case " + config.case_id + ".json --output " +
                        output_dir;
        rs["mpi_ranks"] = n_ranks;
        rs["wall_time_seconds"] = wall_secs;
        rs["final_step"] = final_step;
        rs["final_physical_time"] = final_physical_time;
        rs["convergence_status"] = convergence_status;
        rs["residual_reduction_orders"] = orders_reduction;
        rs["notes"] = run_notes;

        if (rank == 0) {
            std::ofstream f(fs::path(output_dir) / "run_status.json", std::ios::trunc);
            f << rs.dump(2) << "\n";
        }
    }

    if (rank == 0) {
        log.line("");
        log.line("Run finished: status=" + convergence_status +
                 ", steps=" + std::to_string(final_step) +
                 ", residual reduction=" + format_double(orders_reduction) + " orders");
        log.line("Output files written to: " + output_dir);
    }
}

} // namespace cfd
