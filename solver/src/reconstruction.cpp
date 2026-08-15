// Second-order reconstruction (Phase 3b): least-squares gradients of the
// primitive variables, linear reconstruction to face states with positivity
// fallback, and the Barth-Jespersen slope limiter.

#include "reconstruction.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cfd {

namespace {

bool is_owned(const LocalMesh& local_mesh, cgsize_t global_cell) {
    return std::binary_search(local_mesh.owned_cells.begin(),
                              local_mesh.owned_cells.end(), global_cell);
}

// Solves the 2x2 least-squares normal system for one variable: given
// A^T A = [[a11, a12], [a12, a22]] and A^T b = (b1, b2), returns the
// gradient (gx, gy) via Cramer's rule. Returns false (and leaves the
// gradient at 0) when the system is (near) singular.
bool solve_lsq_2x2(double a11, double a12, double a22, double b1, double b2,
                   double& gx, double& gy) {
    const double det = a11 * a22 - a12 * a12;
    const double scale = a11 + a22;
    if (scale <= 0.0 || std::abs(det) <= 1e-14 * scale * scale) {
        gx = 0.0;
        gy = 0.0;
        return false;
    }
    gx = (b1 * a22 - b2 * a12) / det;
    gy = (a11 * b2 - a12 * b1) / det;
    return true;
}

}  // namespace

std::vector<std::vector<cgsize_t>> build_cell_neighbor_list(const Mesh& mesh) {
    std::vector<std::vector<cgsize_t>> neighbors(
        static_cast<size_t>(mesh.n_cells));
    for (cgsize_t f = 0; f < mesh.n_faces; ++f) {
        const size_t fi = static_cast<size_t>(f);
        const cgsize_t l = mesh.face_left[fi];
        const cgsize_t r = mesh.face_right[fi];
        if (l >= 0 && l < mesh.n_cells && r >= 0 && r < mesh.n_cells &&
            l != r) {
            neighbors[static_cast<size_t>(l)].push_back(r);
            neighbors[static_cast<size_t>(r)].push_back(l);
        }
    }
    return neighbors;
}

CellConnectivity build_cell_connectivity(const Mesh& mesh,
                                         const LocalMesh& local_mesh) {
    CellConnectivity conn;
    const size_t n_owned = static_cast<size_t>(local_mesh.n_owned);
    conn.cell_neighbors.resize(n_owned);
    conn.cell_boundary_faces.resize(n_owned);
    conn.n_owned = local_mesh.n_owned;

    for (cgsize_t f = 0; f < mesh.n_faces; ++f) {
        const size_t fi = static_cast<size_t>(f);
        const cgsize_t gl = mesh.face_left[fi];
        const cgsize_t gr = mesh.face_right[fi];
        if (is_owned(local_mesh, gl)) {
            const size_t li = static_cast<size_t>(local_mesh.global_to_local.at(gl));
            conn.cell_neighbors[li].push_back(
                {local_mesh.global_to_local.at(gr), f});
        }
        if (is_owned(local_mesh, gr)) {
            const size_t ri = static_cast<size_t>(local_mesh.global_to_local.at(gr));
            conn.cell_neighbors[ri].push_back(
                {local_mesh.global_to_local.at(gl), f});
        }
    }
    for (cgsize_t b = 0; b < mesh.n_boundary_faces; ++b) {
        const size_t bi = static_cast<size_t>(b);
        const cgsize_t gc = mesh.bface_cell[bi];
        if (is_owned(local_mesh, gc)) {
            const size_t li = static_cast<size_t>(local_mesh.global_to_local.at(gc));
            conn.cell_boundary_faces[li].push_back(b);
        }
    }
    return conn;
}

void compute_gradients_least_squares(
    const Mesh& mesh, const std::vector<PrimitiveState>& prim_local,
    const LocalMesh& local_mesh, const CellConnectivity& conn,
    const GasParams& gas, std::vector<Gradients>& grad_local) {
    (void)mesh;
    const size_t n_local = static_cast<size_t>(local_mesh.n_owned +
                                               local_mesh.n_ghost);
    grad_local.assign(n_local, Gradients{});

    for (cgsize_t i = 0; i < local_mesh.n_owned; ++i) {
        const size_t li = static_cast<size_t>(i);
        const double xi = local_mesh.cell_center_x_local[li];
        const double yi = local_mesh.cell_center_y_local[li];
        const PrimitiveState& Wi = prim_local[li];

        // Normal-matrix entries (shared by all four variables).
        double a11 = 0.0, a12 = 0.0, a22 = 0.0;
        double b_u1 = 0.0, b_u2 = 0.0, b_v1 = 0.0, b_v2 = 0.0;
        double b_p1 = 0.0, b_p2 = 0.0, b_T1 = 0.0, b_T2 = 0.0;

        for (const auto& [j, f] : conn.cell_neighbors[li]) {
            (void)f;
            const size_t lj = static_cast<size_t>(j);
            const double dx =
                local_mesh.cell_center_x_local[lj] - xi;
            const double dy =
                local_mesh.cell_center_y_local[lj] - yi;
            const PrimitiveState& Wj = prim_local[lj];

            a11 += dx * dx;
            a12 += dx * dy;
            a22 += dy * dy;
            b_u1 += dx * (Wj.u - Wi.u);
            b_u2 += dy * (Wj.u - Wi.u);
            b_v1 += dx * (Wj.v - Wi.v);
            b_v2 += dy * (Wj.v - Wi.v);
            b_p1 += dx * (Wj.p - Wi.p);
            b_p2 += dy * (Wj.p - Wi.p);
            const double Tj = Wj.p / (Wj.rho * gas.R);
            const double Ti = Wi.p / (Wi.rho * gas.R);
            b_T1 += dx * (Tj - Ti);
            b_T2 += dy * (Tj - Ti);
        }

        Gradients& g = grad_local[li];
        solve_lsq_2x2(a11, a12, a22, b_u1, b_u2, g.du_dx, g.du_dy);
        solve_lsq_2x2(a11, a12, a22, b_v1, b_v2, g.dv_dx, g.dv_dy);
        solve_lsq_2x2(a11, a12, a22, b_p1, b_p2, g.dp_dx, g.dp_dy);
        solve_lsq_2x2(a11, a12, a22, b_T1, b_T2, g.dT_dx, g.dT_dy);
    }
}

void exchange_halo_gradients(const HaloExchangePlan& plan,
                             std::vector<Gradients>& grad_local,
                             MPI_Comm comm) {
    const size_t np = plan.size();
    if (np == 0) return;

    std::vector<std::vector<double>> send_bufs(np), recv_bufs(np);
    std::vector<MPI_Request> reqs;
    reqs.reserve(2 * np);

    for (size_t i = 0; i < np; ++i) {
        const HaloMap& m = plan[i];
        const int n_send = static_cast<int>(m.send_cell_ids_local.size());
        const int n_recv = static_cast<int>(m.recv_cell_ids_local.size());

        // Pack: each Gradients is 8 contiguous doubles.
        send_bufs[i].resize(static_cast<size_t>(8) * m.send_cell_ids_local.size());
        for (size_t j = 0; j < m.send_cell_ids_local.size(); ++j) {
            const Gradients& g =
                grad_local[static_cast<size_t>(m.send_cell_ids_local[j])];
            double* p = send_bufs[i].data() + 8 * j;
            p[0] = g.du_dx; p[1] = g.du_dy;
            p[2] = g.dv_dx; p[3] = g.dv_dy;
            p[4] = g.dp_dx; p[5] = g.dp_dy;
            p[6] = g.dT_dx; p[7] = g.dT_dy;
        }
        recv_bufs[i].resize(static_cast<size_t>(8) * m.recv_cell_ids_local.size());

        MPI_Request rr = MPI_REQUEST_NULL, sr = MPI_REQUEST_NULL;
        if (n_recv > 0) {
            if (MPI_Irecv(recv_bufs[i].data(), 8 * n_recv, MPI_DOUBLE, m.rank,
                          0, comm, &rr) != MPI_SUCCESS) {
                throw std::runtime_error(
                    "exchange_halo_gradients: MPI_Irecv failed");
            }
        }
        if (n_send > 0) {
            if (MPI_Isend(send_bufs[i].data(), 8 * n_send, MPI_DOUBLE, m.rank,
                          0, comm, &sr) != MPI_SUCCESS) {
                throw std::runtime_error(
                    "exchange_halo_gradients: MPI_Isend failed");
            }
        }
        reqs.push_back(rr);
        reqs.push_back(sr);
    }

    if (!reqs.empty() &&
        MPI_Waitall(static_cast<int>(reqs.size()), reqs.data(),
                    MPI_STATUSES_IGNORE) != MPI_SUCCESS) {
        throw std::runtime_error("exchange_halo_gradients: MPI_Waitall failed");
    }

    for (size_t i = 0; i < np; ++i) {
        const HaloMap& m = plan[i];
        for (size_t j = 0; j < m.recv_cell_ids_local.size(); ++j) {
            const double* p = recv_bufs[i].data() + 8 * j;
            Gradients& g =
                grad_local[static_cast<size_t>(m.recv_cell_ids_local[j])];
            g.du_dx = p[0]; g.du_dy = p[1];
            g.dv_dx = p[2]; g.dv_dy = p[3];
            g.dp_dx = p[4]; g.dp_dy = p[5];
            g.dT_dx = p[6]; g.dT_dy = p[7];
        }
    }
}

PrimitiveState reconstruct_state(const PrimitiveState& center,
                                 const Gradients& grad, double cx, double cy,
                                 double fx, double fy, const GasParams& gas) {
    // MUSCL-style under-extrapolation: the face state is evaluated at
    // alpha*(face - center) with alpha = 1/4 instead of the full
    // extrapolation. The full one-sided extrapolation (alpha = 1) is
    // anti-diffusive at long wavelengths (the reconstruction's correction
    // exceeds the Rusanov dissipation), which leaves a slowly growing
    // smooth mode that prevents steady convergence. With alpha = 1/4 the
    // net dissipation is positive and the scheme remains second-order
    // accurate (the face value error is O(dx^2)).
    constexpr double alpha = 0.25;
    const double dx = alpha * (fx - cx);
    const double dy = alpha * (fy - cy);
    PrimitiveState face;
    face.u = center.u + grad.du_dx * dx + grad.du_dy * dy;
    face.v = center.v + grad.dv_dx * dx + grad.dv_dy * dy;
    face.p = center.p + grad.dp_dx * dx + grad.dp_dy * dy;
    const double T = center.p / (center.rho * gas.R) +
                     grad.dT_dx * dx + grad.dT_dy * dy;
    // Density from the reconstructed pressure/temperature (EOS-consistent).
    face.rho = face.p / (gas.R * T);

    // Positivity fallback: non-physical face state -> first-order.
    const double eps = 1e-10;
    if (!(face.rho > eps) || !(face.p > eps) || !(T > eps)) {
        return center;
    }
    return face;
}

FaceRecon reconstruct_face(cgsize_t cell_left, cgsize_t cell_right,
                           const std::vector<PrimitiveState>& prim_local,
                           const std::vector<Gradients>& grad_local,
                           const Mesh& mesh, const LocalMesh& local_mesh,
                           const GasParams& gas, cgsize_t face_idx) {
    const size_t fi = static_cast<size_t>(face_idx);
    const size_t li = static_cast<size_t>(cell_left);
    const size_t ri = static_cast<size_t>(cell_right);
    const double fx = mesh.face_center_x[fi];
    const double fy = mesh.face_center_y[fi];

    FaceRecon fr;
    fr.left = reconstruct_state(prim_local[li], grad_local[li],
                                local_mesh.cell_center_x_local[li],
                                local_mesh.cell_center_y_local[li], fx, fy,
                                gas);
    fr.right = reconstruct_state(prim_local[ri], grad_local[ri],
                                 local_mesh.cell_center_x_local[ri],
                                 local_mesh.cell_center_y_local[ri], fx, fy,
                                 gas);
    return fr;
}

PrimitiveState reconstruct_boundary_state(
    cgsize_t cell_local, const std::vector<PrimitiveState>& prim_local,
    const std::vector<Gradients>& grad_local, const Mesh& mesh,
    const LocalMesh& local_mesh, const GasParams& gas, cgsize_t bface_idx) {
    const size_t bi = static_cast<size_t>(bface_idx);
    const size_t li = static_cast<size_t>(cell_local);
    return reconstruct_state(prim_local[li], grad_local[li],
                             local_mesh.cell_center_x_local[li],
                             local_mesh.cell_center_y_local[li],
                             mesh.bface_center_x[bi], mesh.bface_center_y[bi],
                             gas);
}

double limit_gradient(const PrimitiveState& cell_center,
                      const Gradients& unlimited_grad,
                      const std::vector<PrimitiveState>& prim_local,
                      const CellConnectivity& conn, cgsize_t cell_local,
                      const Mesh& mesh, const LocalMesh& local_mesh,
                      const GasParams& gas, Gradients& limited_grad) {
    const size_t li = static_cast<size_t>(cell_local);
    const double cx = local_mesh.cell_center_x_local[li];
    const double cy = local_mesh.cell_center_y_local[li];

    // Cell and neighbor min/max for each limited variable (u, v, p, T).
    const double u0 = cell_center.u, v0 = cell_center.v;
    const double p0 = cell_center.p, T0 = cell_center.p / (cell_center.rho * gas.R);
    double u_min = u0, u_max = u0, v_min = v0, v_max = v0;
    double p_min = p0, p_max = p0, T_min = T0, T_max = T0;
    for (const auto& [j, f] : conn.cell_neighbors[li]) {
        (void)f;
        const PrimitiveState& W = prim_local[static_cast<size_t>(j)];
        const double T = W.p / (W.rho * gas.R);
        u_min = std::min(u_min, W.u); u_max = std::max(u_max, W.u);
        v_min = std::min(v_min, W.v); v_max = std::max(v_max, W.v);
        p_min = std::min(p_min, W.p); p_max = std::max(p_max, W.p);
        T_min = std::min(T_min, T);   T_max = std::max(T_max, T);
    }

    // Barth-Jespersen ratio for one variable at one face.
    const auto barth_phi = [](double w_i, double w_face, double w_min,
                              double w_max) -> double {
        const double delta = w_face - w_i;
        if (std::abs(delta) < 1e-300) return 1.0;
        if (delta > 0.0) {
            const double allowed = w_max - w_i;
            if (allowed <= 0.0) return 0.0;
            return std::min(1.0, allowed / delta);
        }
        const double allowed = w_i - w_min;
        if (allowed <= 0.0) return 0.0;
        return std::min(1.0, allowed / (-delta));
    };

    double phi = 1.0;
    const auto limit_face = [&](double fx, double fy) {
        const double dx = fx - cx, dy = fy - cy;
        const Gradients& g = unlimited_grad;
        const double uf = u0 + g.du_dx * dx + g.du_dy * dy;
        const double vf = v0 + g.dv_dx * dx + g.dv_dy * dy;
        const double pf = p0 + g.dp_dx * dx + g.dp_dy * dy;
        const double Tf = T0 + g.dT_dx * dx + g.dT_dy * dy;
        phi = std::min(phi, barth_phi(u0, uf, u_min, u_max));
        phi = std::min(phi, barth_phi(v0, vf, v_min, v_max));
        phi = std::min(phi, barth_phi(p0, pf, p_min, p_max));
        phi = std::min(phi, barth_phi(T0, Tf, T_min, T_max));
    };

    // Internal faces (evaluated at the face midpoints) and boundary faces.
    for (const auto& [j, f] : conn.cell_neighbors[li]) {
        (void)j;
        limit_face(mesh.face_center_x[static_cast<size_t>(f)],
                   mesh.face_center_y[static_cast<size_t>(f)]);
    }
    for (cgsize_t b : conn.cell_boundary_faces[li]) {
        limit_face(mesh.bface_center_x[static_cast<size_t>(b)],
                   mesh.bface_center_y[static_cast<size_t>(b)]);
    }

    phi = std::max(0.0, std::min(1.0, phi));
    limited_grad = unlimited_grad;
    limited_grad.du_dx *= phi; limited_grad.du_dy *= phi;
    limited_grad.dv_dx *= phi; limited_grad.dv_dy *= phi;
    limited_grad.dp_dx *= phi; limited_grad.dp_dy *= phi;
    limited_grad.dT_dx *= phi; limited_grad.dT_dy *= phi;
    return phi;
}

}  // namespace cfd
