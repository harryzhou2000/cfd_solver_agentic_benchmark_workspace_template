#include "solver/time_integrator.hpp"

#include "solver/flux.hpp"             // conservative_to_primitive, speed_of_sound
#include "solver/output_writer.hpp"    // step CSV logs
#include "solver/partition_types.hpp"  // HaloBuffers, init/start/finish_halo_exchange

#include <mpi.h>
#include <fmt/core.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>
#include <vector>

namespace solver {

namespace {

constexpr double kMinDensity = 1e-10;

// Linear ramp of the pseudo-time CFL: cfl_initial -> cfl_max over
// pseudo_cfl_ramp_steps steps; fixed at cfl_max afterwards (and immediately
// when the ramp length is zero).
double ramp_cfl(const RunControl& rc, int step) {
    if (rc.pseudo_cfl_ramp_steps <= 0) return rc.cfl_max;
    if (step < rc.pseudo_cfl_ramp_steps) {
        const double frac =
            static_cast<double>(step) / rc.pseudo_cfl_ramp_steps;
        return rc.cfl_initial + frac * (rc.cfl_max - rc.cfl_initial);
    }
    return rc.cfl_max;
}

// Jacobian of the inviscid flux in the face-normal direction, dF/dU * n,
// evaluated at the primitive state [rho, u, v, p] (conservative variables).
void euler_normal_jacobian(const double prim[4], double nx, double ny,
                           double gamma, double J[4][4]) {
    const double gm1 = gamma - 1.0;
    const double rho = prim[0], u = prim[1], v = prim[2], p = prim[3];
    const double un = u * nx + v * ny;
    const double k2 = 0.5 * (u * u + v * v);
    const double H = gamma * p / (gm1 * rho) + k2;  // total enthalpy

    J[0][0] = 0.0;
    J[0][1] = nx;
    J[0][2] = ny;
    J[0][3] = 0.0;

    J[1][0] = -u * un + nx * gm1 * k2;
    J[1][1] = un + u * nx * (2.0 - gamma);
    J[1][2] = u * ny - gm1 * v * nx;
    J[1][3] = gm1 * nx;

    J[2][0] = -v * un + ny * gm1 * k2;
    J[2][1] = v * nx - gm1 * u * ny;
    J[2][2] = un + v * ny * (2.0 - gamma);
    J[2][3] = gm1 * ny;

    J[3][0] = un * (-H + gm1 * k2);
    J[3][1] = nx * H - gm1 * u * un;
    J[3][2] = ny * H - gm1 * v * un;
    J[3][3] = gamma * un;
}

// Keep the state physically valid after an update: clamp the density and
// reset cells with non-positive (or NaN) pressure to the freestream state.
void enforce_positivity(std::vector<double>& state, int num_owned,
                        const CaseConfig& config) {
    const double gamma = config.gas.gamma;
    const double rho_inf = config.freestream.rho;
    const double u_inf = config.freestream.u_inf;
    const double v_inf = config.freestream.v_inf;
    const double p_inf = config.freestream.pressure;
    const double e_inf =
        p_inf / (gamma - 1.0) + 0.5 * rho_inf * (u_inf * u_inf + v_inf * v_inf);

    for (int c = 0; c < num_owned; ++c) {
        double* U = &state[static_cast<size_t>(c) * kStateSize];
        if (!(U[0] > kMinDensity)) U[0] = kMinDensity;  // catches NaN too
        const Vec4 W =
            conservative_to_primitive(Vec4(U[0], U[1], U[2], U[3]), gamma);
        if (!(W(3) > 0.0)) {  // negative or NaN pressure: reset to freestream
            U[0] = rho_inf;
            U[1] = rho_inf * u_inf;
            U[2] = rho_inf * v_inf;
            U[3] = e_inf;
        }
    }
}

// Halo exchange followed by the spatial residual assembly.
ResidualNorms compute_spatial_residual(DistributedMesh& mesh,
                                       std::vector<double>& state,
                                       std::vector<double>& residual,
                                       HaloBuffers& bufs,
                                       const CaseConfig& config,
                                       const std::string& flux_type,
                                       double diss_scale) {
    start_halo_exchange(state, mesh, bufs);
    finish_halo_exchange(state, mesh, bufs);
    return compute_residual(mesh, state, residual, config, flux_type,
                            diss_scale);
}

// Invert a 4x4 matrix into Minv (flat, row-major, 16 entries) using Gaussian
// elimination with partial pivoting. Returns false if singular.
bool invert_4x4(const double M[4][4], double* Minv) {
    double A[4][8];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) A[i][j] = M[i][j];
        for (int j = 0; j < 4; ++j) A[i][4 + j] = (i == j) ? 1.0 : 0.0;
    }
    for (int col = 0; col < 4; ++col) {
        // Partial pivoting.
        int piv = col;
        for (int row = col + 1; row < 4; ++row) {
            if (std::abs(A[row][col]) > std::abs(A[piv][col])) piv = row;
        }
        if (std::abs(A[piv][col]) < 1e-30) return false;
        if (piv != col) {
            for (int j = 0; j < 8; ++j) std::swap(A[col][j], A[piv][j]);
        }
        const double inv = 1.0 / A[col][col];
        for (int j = 0; j < 8; ++j) A[col][j] *= inv;
        for (int row = 0; row < 4; ++row) {
            if (row == col) continue;
            const double f = A[row][col];
            if (f == 0.0) continue;
            for (int j = 0; j < 8; ++j) A[row][j] -= f * A[col][j];
        }
    }
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) Minv[i * 4 + j] = A[i][4 + j];
    }
    return true;
}

// Build the frozen LU-SGS operator from the current state:
//   prim[c]      : primitive state [rho, u, v, p] of every owned+ghost cell
//   rA[f]        : face spectral radius (|un| + a) * A (+ viscous estimate)
//   dinv[c]      : inverse of the 4x4 diagonal block
//                  V/dtau*I + sum_faces 0.5*A_f*(r_f*I + A_n_f)
//                  (+ phys_diag_factor * V * I for BDF2)
// with dtau = CFL * V / sum_faces rA (local pseudo-time step) and A_n_f the
// inviscid flux Jacobian at the cell's own state in the outward face-normal
// direction.
void build_lusgs_operator(const DistributedMesh& mesh,
                          const std::vector<double>& state, double cfl,
                          double gamma, double Pr, double mu, bool viscous,
                          double phys_diag_factor,
                          std::vector<double>& prim,
                          std::vector<double>& rA,
                          std::vector<double>& dinv,
                          double* dt_mean_out = nullptr) {
    const int num_owned = static_cast<int>(mesh.owned_cells.size());
    const int num_ghost = static_cast<int>(mesh.ghost_cells.size());
    const int num_total = num_owned + num_ghost;
    const int num_faces = static_cast<int>(mesh.local_faces.size());

    prim.resize(static_cast<size_t>(num_total) * 4);
    for (int c = 0; c < num_total; ++c) {
        const size_t base = static_cast<size_t>(c) * kStateSize;
        const Vec4 W = conservative_to_primitive(
            Vec4(state[base], state[base + 1], state[base + 2],
                 state[base + 3]),
            gamma);
        prim[4 * c + 0] = W(0);
        prim[4 * c + 1] = W(1);
        prim[4 * c + 2] = W(2);
        prim[4 * c + 3] = W(3);
    }

    rA.assign(num_faces, 0.0);
    for (int f = 0; f < num_faces; ++f) {
        const Face& face = mesh.local_faces[f];
        const int l = mesh.face_left[f];
        const int r = mesh.face_right[f];
        const double nx = face.normal.x();
        const double ny = face.normal.y();
        const double A = face.area;

        // Face-averaged normal velocity and sound speed (interior faces);
        // left-cell values on boundary faces.
        double un, a, rho;
        if (r >= 0) {
            un = 0.5 * ((prim[4 * l + 1] + prim[4 * r + 1]) * nx +
                        (prim[4 * l + 2] + prim[4 * r + 2]) * ny);
            a = 0.5 * (speed_of_sound(prim[4 * l + 0], prim[4 * l + 3],
                                      gamma) +
                       speed_of_sound(prim[4 * r + 0], prim[4 * r + 3],
                                      gamma));
            rho = 0.5 * (prim[4 * l + 0] + prim[4 * r + 0]);
        } else {
            un = prim[4 * l + 1] * nx + prim[4 * l + 2] * ny;
            a = speed_of_sound(prim[4 * l + 0], prim[4 * l + 3], gamma);
            rho = prim[4 * l + 0];
        }

        double rad = (std::abs(un) + a) * A;
        if (viscous) {
            // Face-to-cell distance (mean of the two cells; left cell on
            // boundary faces).
            double dist;
            const Vec2 fc = face.centroid;
            const Vec2& xl =
                (l < num_owned) ? mesh.owned_cells[l].centroid
                                : mesh.ghost_cells[l - num_owned].centroid;
            if (r >= 0) {
                const Vec2& xr =
                    (r < num_owned) ? mesh.owned_cells[r].centroid
                                    : mesh.ghost_cells[r - num_owned].centroid;
                dist = 0.5 * ((fc - xl).norm() + (fc - xr).norm());
            } else {
                dist = (fc - xl).norm();
            }
            rad += std::max(4.0 / 3.0, gamma / Pr) *
                   (mu / std::max(rho, 1e-15)) * A / std::max(dist, 1e-30);
        }
        rA[f] = rad;
    }

    dinv.assign(static_cast<size_t>(num_owned) * 16, 0.0);
    double J[4][4];
    double dt_sum = 0.0;
    for (int c = 0; c < num_owned; ++c) {
        double lambda = 0.0;
        double D[4][4];
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) D[i][j] = 0.0;
        }
        for (int fid : mesh.owned_cells[c].face_ids) {
            const Face& face = mesh.local_faces[fid];
            const double sgn = (mesh.face_left[fid] == c) ? 1.0 : -1.0;
            lambda += rA[fid];
            euler_normal_jacobian(&prim[4 * c], sgn * face.normal.x(),
                                  sgn * face.normal.y(), gamma, J);
            const double halfA = 0.5 * face.area;
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    D[i][j] += halfA * J[i][j];
                }
            }
        }
        const double vol = mesh.owned_cells[c].volume;
        const double dtau = cfl * vol / std::max(lambda, 1e-15);
        const double scalar = vol / dtau + 0.5 * lambda + phys_diag_factor * vol;
        for (int i = 0; i < 4; ++i) D[i][i] += scalar;

        double* Dinv = &dinv[static_cast<size_t>(c) * 16];
        if (!invert_4x4(D, Dinv)) {
            // Degenerate block: fall back to a scalar diagonal.
            const double inv = 1.0 / std::max(scalar, 1e-15);
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    Dinv[i * 4 + j] = (i == j) ? inv : 0.0;
                }
            }
        }
        dt_sum += dtau;
    }
    if (dt_mean_out != nullptr) {
        *dt_mean_out = num_owned > 0 ? dt_sum / num_owned : 0.0;
    }
}

// One LU-SGS sweep pair (forward + backward) on the frozen linearized system
//   D_i * dU_i + sum_f 0.5*A_f*(A_n - r_f*I) * dU_j = -R_i
// with the full Euler flux Jacobian A_n in the off-diagonal (signed normal
// from i to j) and the (inverted) 4x4 diagonal block dinv[i]. The dU array
// is refined in place (callers zero it before the first sweep). Ghost-cell
// dU is left at zero, giving block-Jacobi coupling across partition
// boundaries. Returns the RMS change of dU over the sweep pair (0.0 when
// compute_rms is false, which also skips the collective MPI_Allreduce).
double lusgs_sweep_pair(const DistributedMesh& mesh,
                        const std::vector<double>& residual,
                        const std::vector<double>& prim,
                        const std::vector<double>& rA,
                        const std::vector<double>& dinv, double gamma,
                        std::vector<double>& dU, bool compute_rms = false) {
    const int num_owned = static_cast<int>(mesh.owned_cells.size());
    const MPI_Comm comm = mesh.comm;

    double sumSq = 0.0;
    double J[4][4];
    for (int dir = 0; dir < 2; ++dir) {
        for (int i = dir == 0 ? 0 : num_owned - 1;
             dir == 0 ? (i < num_owned) : (i >= 0);
             i += dir == 0 ? 1 : -1) {
            const size_t ib = static_cast<size_t>(i) * kStateSize;
            double rhs[4] = {-residual[ib], -residual[ib + 1],
                             -residual[ib + 2], -residual[ib + 3]};

            for (int fid : mesh.owned_cells[i].face_ids) {
                const int j = (mesh.face_left[fid] == i)
                                  ? mesh.face_right[fid]
                                  : mesh.face_left[fid];
                if (j < 0) continue;  // boundary face: no off-diagonal
                const Face& face = mesh.local_faces[fid];
                const double sgn =
                    (mesh.face_left[fid] == i) ? 1.0 : -1.0;
                euler_normal_jacobian(&prim[4 * j], sgn * face.normal.x(),
                                      sgn * face.normal.y(), gamma, J);
                const double w = 0.5 * rA[fid];
                const double A = face.area;
                const size_t jb = static_cast<size_t>(j) * kStateSize;
                for (int k = 0; k < 4; ++k) {
                    double aDu = 0.0;
                    for (int l = 0; l < 4; ++l) {
                        aDu += J[k][l] * dU[jb + l];
                    }
                    rhs[k] -= 0.5 * A * aDu - w * dU[jb + k];
                }
            }

            // dU_i = Dinv_i * rhs (4x4 block matvec).
            const double* Dinv = &dinv[static_cast<size_t>(i) * 16];
            double newdU[4];
            for (int k = 0; k < 4; ++k) {
                newdU[k] = 0.0;
                for (int l = 0; l < 4; ++l) {
                    newdU[k] += Dinv[k * 4 + l] * rhs[l];
                }
                const double delta = newdU[k] - dU[ib + k];
                dU[ib + k] = newdU[k];
                sumSq += delta * delta;
            }
        }
    }

    if (!compute_rms) return 0.0;  // skip the global reduction (transient path)

    double globalSum = 0.0;
    MPI_Allreduce(&sumSq, &globalSum, 1, MPI_DOUBLE, MPI_SUM, comm);
    return std::sqrt(globalSum / (4.0 * static_cast<double>(num_owned)));
}

} // namespace

SolverStats run_steady_solve(DistributedMesh& mesh,
                             std::vector<double>& state,
                             const CaseConfig& config,
                             const std::string& flux_type,
                             const std::string& output_dir) {
    const RunControl& rc = config.run_control;
    const double gamma = config.gas.gamma;
    const double mu = config.freestream.viscosity;
    const bool viscous = (config.physics.mode != "inviscid") && mu > 0.0;
    const double diss_scale =
        rc.rusanov_dissipation_scale > 0.0 ? rc.rusanov_dissipation_scale : 1.0;
    const double inner_target =
        std::max(rc.inner_residual_reduction_target, 1e-12);
    const int min_inner = std::max(rc.min_inner_iterations, 0);
    const int max_inner = std::max(rc.max_inner_iterations, 1);

    const int num_owned = static_cast<int>(mesh.owned_cells.size());
    const int num_ghost = static_cast<int>(mesh.ghost_cells.size());
    const MPI_Comm comm = mesh.comm;
    int rank = 0;
    MPI_Comm_rank(comm, &rank);

    StepLogFiles logs = open_step_logs(output_dir, rank);

    HaloBuffers bufs;
    init_halo_buffers(mesh, bufs);

    std::vector<double> residual(static_cast<size_t>(num_owned) * kStateSize,
                                 0.0);
    std::vector<double> prim, rA, dinv;
    // dU spans owned + ghost cells: the sweep reads dU of ghost neighbors
    // (kept at zero for block-Jacobi coupling across partitions).
    std::vector<double> dU(static_cast<size_t>(num_owned + num_ghost) *
                               kStateSize,
                           0.0);

    SolverStats stats;
    const double start_time = MPI_Wtime();

    // Initial residual.
    ResidualNorms norms =
        compute_spatial_residual(mesh, state, residual, bufs, config,
                                 flux_type, diss_scale);
    const double initial_res = norms.l2;
    if (rank == 0) fmt::print("Initial residual L2 = {:.6e}\n", initial_res);

    long total_inner = 0;
    int min_its = INT_MAX;
    int max_its = 0;
    int misses = 0;
    int met = 0;
    double last_ratio = 1.0;
    bool converged = false;
    int step = 0;

    // Residual already at machine-zero level: nothing to do.
    if (initial_res < 1e-14) converged = true;

    for (; step < rc.max_steps && !converged; ++step) {
        if (step > 0) {
            norms = compute_spatial_residual(mesh, state, residual, bufs,
                                             config, flux_type, diss_scale);
        }

        const double reduction = initial_res / std::max(norms.l2, 1e-15);
        if (reduction >= std::pow(10.0, rc.residual_reduction_target)) {
            converged = true;
            break;
        }
        if (step % 100 == 0 && rank == 0) {
            fmt::print("Step {}: L2 = {:.6e}, reduction = {:.2f}\n", step,
                       norms.l2, reduction);
        }

        const double cfl = ramp_cfl(rc, step);

        // Frozen implicit operator (local pseudo-time steps + block
        // diagonal + face spectral radii) for this outer step.
        double dt_mean = 0.0;
        build_lusgs_operator(mesh, state, cfl, gamma, config.gas.prandtl, mu,
                             viscous, 0.0, prim, rA, dinv, &dt_mean);

        // Inner iterations: LU-SGS sweeps on the linear system
        //   (V/dtau + dR/dU) * dU = -R
        // with the residual FROZEN at the current state. Each sweep pair
        // refines dU; the loop stops when the dU update drops below
        // inner_target relative to the first sweep (linear-solve
        // convergence) or after max_inner_iterations. One pseudo-time step
        // is taken per outer iteration; the CFL ramp drives the nonlinear
        // convergence.
        std::fill(dU.begin(), dU.end(), 0.0);
        int its = 0;
        bool inner_ok = false;
        double ratio = 1.0;
        double first_sweep = 0.0;
        for (int inner = 1; inner <= max_inner; ++inner) {
            const double upd = lusgs_sweep_pair(mesh, residual, prim, rA,
                                                dinv, gamma, dU, true);
            if (inner == 1) first_sweep = upd;
            its = inner;
            ratio = (first_sweep > 0.0) ? upd / first_sweep : 0.0;
            if (inner >= min_inner && first_sweep > 0.0 &&
                upd / first_sweep < inner_target) {
                inner_ok = true;
                break;
            }
        }


        // Apply the accumulated correction and restore positivity.
        for (int c = 0; c < num_owned; ++c) {
            const size_t base = static_cast<size_t>(c) * kStateSize;
            for (int v = 0; v < kStateSize; ++v) {
                state[base + v] += dU[base + v];
            }
        }
        enforce_positivity(state, num_owned, config);

        total_inner += its;
        min_its = std::min(min_its, its);
        max_its = std::max(max_its, its);
        last_ratio = ratio;
        if (inner_ok) {
            ++met;
        } else {
            ++misses;
        }

        // Per-step output: residual row (component L2 norms of the spatial
        // residual used for this step's update) and force row.
        double comp[4] = {0.0, 0.0, 0.0, 0.0};
        for (int c = 0; c < num_owned; ++c) {
            const size_t base = static_cast<size_t>(c) * kStateSize;
            for (int v = 0; v < kStateSize; ++v) {
                const double r = residual[base + v];
                comp[v] += r * r;
            }
        }
        double gcomp[4];
        MPI_Allreduce(comp, gcomp, 4, MPI_DOUBLE, MPI_SUM, comm);
        for (int v = 0; v < 4; ++v) gcomp[v] = std::sqrt(gcomp[v]);
        write_residual_row(logs, step + 1, 0.0, its, cfl, dt_mean, gcomp,
                           norms.l2, norms.linf);
        const ForceCoeffs f = compute_forces(mesh, state, config);
        write_force_row(logs, step + 1, 0.0, f);
    }

    // Refresh the residual at the final state for reporting.
    norms = compute_spatial_residual(mesh, state, residual, bufs, config,
                                     flux_type, diss_scale);

    if (converged) {
        stats.convergence_status = "converged";
        if (rank == 0) {
            fmt::print("Converged at step {}: reduction = {:.2f}\n", step,
                       initial_res / std::max(norms.l2, 1e-15));
        }
    } else if (rank == 0) {
        fmt::print("Steady solve did not converge within {} steps "
                   "(achieved {:.2f} orders)\n",
                   rc.max_steps,
                   std::log10(initial_res / std::max(norms.l2, 1e-15)));
    }

    const int processed = step;  // outer steps that ran the inner solve
    stats.total_steps = processed + (converged ? 1 : 0);
    stats.final_residual_l2 = norms.l2;
    stats.final_residual_linf = norms.linf;
    stats.residual_reduction_orders =
        std::log10(initial_res / std::max(norms.l2, 1e-15));
    stats.inner_iterations = static_cast<int>(total_inner);
    stats.min_inner_its = processed > 0 ? min_its : 0;
    stats.max_inner_its = max_its;
    stats.mean_inner_its =
        processed > 0 ? static_cast<double>(total_inner) / processed : 0.0;
    stats.inner_target_misses = misses;
    stats.converged_fraction =
        processed > 0 ? static_cast<double>(met) / processed : 0.0;
    stats.last_inner_residual_ratio = last_ratio;
    stats.wall_time_seconds = MPI_Wtime() - start_time;

    close_step_logs(logs);
    return stats;
}

SolverStats run_transient_solve(DistributedMesh& mesh,
                                std::vector<double>& state,
                                const CaseConfig& config,
                                const std::string& flux_type,
                                const std::string& output_dir) {
    const RunControl& rc = config.run_control;
    const double gamma = config.gas.gamma;
    const double mu = config.freestream.viscosity;
    const bool viscous = (config.physics.mode != "inviscid") && mu > 0.0;
    const double diss_scale =
        rc.rusanov_dissipation_scale > 0.0 ? rc.rusanov_dissipation_scale : 1.0;
    const double inner_target =
        std::max(rc.inner_residual_reduction_target, 1e-12);
    const int min_inner = std::max(rc.min_inner_iterations, 0);
    const int max_inner = std::max(rc.max_inner_iterations, 1);
    const double dt_phys = rc.time_step;
    const int num_steps =
        (dt_phys > 0.0)
            ? static_cast<int>(std::lround(rc.final_time / dt_phys))
            : 0;

    const int num_owned = static_cast<int>(mesh.owned_cells.size());
    const int num_ghost = static_cast<int>(mesh.ghost_cells.size());
    const MPI_Comm comm = mesh.comm;
    int rank = 0;
    MPI_Comm_rank(comm, &rank);

    SolverStats stats;
    if (num_steps <= 0) {
        if (rank == 0) {
            fmt::print(stderr,
                       "run_transient_solve: invalid time_step/final_time "
                       "({}/{}); aborting\n",
                       rc.time_step, rc.final_time);
        }
        return stats;
    }

    StepLogFiles logs = open_step_logs(output_dir, rank);

    HaloBuffers bufs;
    init_halo_buffers(mesh, bufs);

    std::vector<double> residual(static_cast<size_t>(num_owned) * kStateSize,
                                 0.0);
    std::vector<double> prim, rA, dinv;
    // dU spans owned + ghost cells: the sweep reads dU of ghost neighbors
    // (kept at zero for block-Jacobi coupling across partitions).
    std::vector<double> dU(static_cast<size_t>(num_owned + num_ghost) *
                               kStateSize,
                           0.0);

    // BDF2 histories: U^n and U^{n-1}. Both are frozen during the inner
    // iterations for U^{n+1} and updated only after the inner solve of the
    // physical step is accepted.
    std::vector<double> U_n = state;
    std::vector<double> U_nm1 = state;

    const double start_time = MPI_Wtime();
    if (rank == 0) {
        fmt::print("Transient BDF2: {} physical steps, dt = {:.6f}\n",
                   num_steps, dt_phys);
    }

    long total_inner = 0;
    int min_its = INT_MAX;
    int max_its = 0;
    int misses = 0;
    int met = 0;
    double last_ratio = 1.0;
    double last_total_l2 = 0.0;
    double last_total_linf = 0.0;

    for (int n = 1; n <= num_steps; ++n) {
        const double phys_time = n * dt_phys;
        const double cfl = ramp_cfl(rc, n - 1);
        const double bdf2_diag = 1.5 / dt_phys;  // physical-time diagonal

        // ---- Inner (dual-time) iterations for U^{n+1} ----
        int its = 0;
        bool inner_ok = false;
        double inner_ref = 1.0;
        double total_l2 = 0.0;
        double total_linf = 0.0;
        double comp_l2[4] = {0.0, 0.0, 0.0, 0.0};
        for (int inner = 0; inner < max_inner; ++inner) {
            // Spatial residual at the current iterate, then add the frozen
            // BDF2 physical-time term:
            //   R_total = R_spatial + V/dt*(1.5 U^{n+1} - 2 U^n + 0.5 U^{n-1})
            start_halo_exchange(state, mesh, bufs);
            finish_halo_exchange(state, mesh, bufs);
            compute_residual(mesh, state, residual, config, flux_type,
                             diss_scale);

            double l2sum = 0.0;
            double linf = 0.0;
            double compsum[4] = {0.0, 0.0, 0.0, 0.0};
            for (int c = 0; c < num_owned; ++c) {
                const double fact = mesh.owned_cells[c].volume / dt_phys;
                const size_t base = static_cast<size_t>(c) * kStateSize;
                for (int v = 0; v < kStateSize; ++v) {
                    const double r =
                        residual[base + v] +
                        fact * (1.5 * state[base + v] -
                                2.0 * U_n[base + v] +
                                0.5 * U_nm1[base + v]);
                    residual[base + v] = r;
                    l2sum += r * r;
                    linf = std::max(linf, std::abs(r));
                    compsum[v] += r * r;
                }
            }
            MPI_Allreduce(&l2sum, &total_l2, 1, MPI_DOUBLE, MPI_SUM, comm);
            total_l2 = std::sqrt(total_l2);
            MPI_Allreduce(&linf, &total_linf, 1, MPI_DOUBLE, MPI_MAX, comm);
            MPI_Allreduce(compsum, comp_l2, 4, MPI_DOUBLE, MPI_SUM, comm);
            for (int v = 0; v < 4; ++v) comp_l2[v] = std::sqrt(comp_l2[v]);
            last_total_l2 = total_l2;
            last_total_linf = total_linf;

            if (inner == 0) inner_ref = std::max(total_l2, 1e-15);
            if (inner >= min_inner && total_l2 / inner_ref < inner_target) {
                inner_ok = true;
                break;
            }

            // One implicit step: rebuild the operator at the current state
            // (including the BDF2 diagonal) and take a single LU-SGS sweep
            // pair against the total residual.
            build_lusgs_operator(mesh, state, cfl, gamma, config.gas.prandtl,
                                 mu, viscous, bdf2_diag, prim, rA, dinv);
            std::fill(dU.begin(), dU.end(), 0.0);
            lusgs_sweep_pair(mesh, residual, prim, rA, dinv, gamma, dU);
            for (int c = 0; c < num_owned; ++c) {
                const size_t base = static_cast<size_t>(c) * kStateSize;
                for (int v = 0; v < kStateSize; ++v) {
                    state[base + v] += dU[base + v];
                }
            }
            enforce_positivity(state, num_owned, config);
            ++its;
        }

        // History update after the inner solve is accepted.
        U_nm1 = U_n;
        U_n = state;

        total_inner += its;
        min_its = std::min(min_its, its);
        max_its = std::max(max_its, its);
        last_ratio = total_l2 / inner_ref;
        if (inner_ok) {
            ++met;
        } else {
            ++misses;
        }

        // Sync halo before the force computation: when the inner loop exits
        // without convergence the last operation is a state update after the
        // last halo sync, so ghost data may be stale at partition boundaries.
        start_halo_exchange(state, mesh, bufs);
        finish_halo_exchange(state, mesh, bufs);

        // Sample forces every physical step (Strouhal extraction requires
        // a dense force history; ~5 samples per shedding period is too coarse).
        // compute_forces is collective (it reduces over the communicator), so
        // every rank must participate; only rank 0 prints the line.
        const ForceCoeffs f = compute_forces(mesh, state, config);
        if (rank == 0) {
            fmt::print("t={:.2f}: CL={:.4f}, CD={:.4f}, inner={}/{}\n",
                       phys_time, f.cl, f.cd, its, max_inner);
        }

        // Per-step output rows (total residual including the BDF2 term).
        write_residual_row(logs, n, phys_time, its, cfl, dt_phys, comp_l2,
                           total_l2, total_linf);
        write_force_row(logs, n, phys_time, f);
    }

    stats.convergence_status = "statistically_periodic";
    stats.total_steps = num_steps;
    stats.final_residual_l2 = last_total_l2;
    stats.final_residual_linf = last_total_linf;
    stats.inner_iterations = static_cast<int>(total_inner);
    stats.min_inner_its = min_its;
    stats.max_inner_its = max_its;
    stats.mean_inner_its = static_cast<double>(total_inner) / num_steps;
    stats.inner_target_misses = misses;
    stats.converged_fraction = static_cast<double>(met) / num_steps;
    stats.last_inner_residual_ratio = last_ratio;
    stats.wall_time_seconds = MPI_Wtime() - start_time;

    if (rank == 0) {
        fmt::print("Transient solve completed: {} steps in {:.2f}s\n",
                   num_steps, stats.wall_time_seconds);
    }
    close_step_logs(logs);
    return stats;
}

} // namespace solver
