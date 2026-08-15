/// @file reconstruction.cpp
/// Implementation of least-squares gradient computation, reconstruction,
/// and Barth-Jespersen limiter.

#include "reconstruction.hpp"
#include "flux.hpp"
#include <cmath>
#include <algorithm>
#include <Eigen/Dense>

namespace cfd {

// ============================================================================
// Least-squares gradient for a scalar field
// ============================================================================

void compute_scalar_gradient(const Mesh& mesh,
                             const std::vector<Real>& phi,
                             std::vector<Vec2>& grad_phi) {
    const std::size_t ncells = mesh.n_cells();
    grad_phi.resize(ncells);
    for (auto& g : grad_phi) g.setZero();

    for (std::size_t ic = 0; ic < ncells; ++ic) {
        const Cell& cell = mesh.cells[ic];
        const Vec2& xc = cell.centroid;

        // Least-squares: minimize Σ w_j (φ_j − φ_i − ∇φ·Δx_j)²
        // Normal equations: AᵀW A ∇φ = AᵀW b
        // where A = [Δx, Δy] for each neighbor, b = φ_j − φ_i

        // Build 2x2 matrix and 2x1 RHS
        Real Axx = 0.0, Axy = 0.0, Ayy = 0.0;
        Real bx = 0.0, by = 0.0;

        const std::size_t nneighbors = cell.neighbors.size();
        if (nneighbors == 0) {
            grad_phi[ic].setZero();
            continue;
        }

        for (std::size_t neighbor_idx : cell.neighbors) {
            const Cell& nb = mesh.cells[neighbor_idx];
            const Vec2 dr = nb.centroid - xc;
            const Real dx = dr(0);
            const Real dy = dr(1);
            const Real dist2 = dx * dx + dy * dy;
            const Real w = 1.0 / std::max(dist2, EPS * EPS);  // distance-based weighting

            const Real dphi = phi[neighbor_idx] - phi[ic];

            Axx += w * dx * dx;
            Axy += w * dx * dy;
            Ayy += w * dy * dy;
            bx  += w * dx * dphi;
            by  += w * dy * dphi;
        }

        // Solve 2x2 system: [Axx Axy; Axy Ayy] * grad = [bx; by]
        const Real det = Axx * Ayy - Axy * Axy;
        if (std::abs(det) > EPS) {
            grad_phi[ic](0) = (Ayy * bx - Axy * by) / det;
            grad_phi[ic](1) = (Axx * by - Axy * bx) / det;
        } else {
            // Fallback: simple Green-Gauss using face averages
            grad_phi[ic].setZero();
            Real sum_area = 0.0;
            for (std::size_t fi : cell.faces) {
                const Face& face = mesh.faces[fi];
                const Vec2& xf = face.centroid;
                const Vec2 dr = xf - xc;
                Real phi_f = phi[ic]; // first-order fallback
                if (!face.is_boundary) {
                    std::size_t nb_idx = (face.left_cell == ic) ? face.right_cell : face.left_cell;
                    phi_f = 0.5 * (phi[ic] + phi[nb_idx]);
                }
                // Use face normal direction for the Green-Gauss
                const Vec2 n_out = (face.left_cell == ic) ? face.normal : -face.normal;
                grad_phi[ic] += phi_f * n_out * face.area;
                sum_area += face.area;
            }
            if (sum_area > EPS) {
                grad_phi[ic] /= cell.volume;
            }
        }
    }
}

// ============================================================================
// Primitive variable gradients
// ============================================================================

void compute_primitive_gradients(const Mesh& mesh,
                                 const std::vector<Vec4>& U,
                                 Real gamma,
                                 std::vector<Vec2>& grad_rho,
                                 std::vector<Vec2>& grad_u,
                                 std::vector<Vec2>& grad_v,
                                 std::vector<Vec2>& grad_p) {
    const std::size_t ncells = mesh.n_cells();

    std::vector<Real> rho(ncells), u(ncells), v(ncells), p(ncells);
    for (std::size_t i = 0; i < ncells; ++i) {
        Vec4 prim = conservative_to_primitive(U[i], gamma);
        rho[i] = prim(0);
        u[i]   = prim(1);
        v[i]   = prim(2);
        p[i]   = prim(3);
    }

    compute_scalar_gradient(mesh, rho, grad_rho);
    compute_scalar_gradient(mesh, u,   grad_u);
    compute_scalar_gradient(mesh, v,   grad_v);
    compute_scalar_gradient(mesh, p,   grad_p);
}

// ============================================================================
// Reconstruction
// ============================================================================

static Vec4 reconstruct_state(const Mesh& mesh, std::size_t cell, std::size_t face_idx,
                              const std::vector<Vec4>& U,
                              const std::vector<Vec2>& grad_rho,
                              const std::vector<Vec2>& grad_u,
                              const std::vector<Vec2>& grad_v,
                              const std::vector<Vec2>& grad_p,
                              const Vec4& limiter,
                              Real gamma, Real sign) {
    const Vec4 prim_c = conservative_to_primitive(U[cell], gamma);
    const Vec2& xc = mesh.cells[cell].centroid;
    const Vec2& xf = mesh.faces[face_idx].centroid;
    const Vec2 dr = xf - xc;

    // Linearly extrapolate primitive variables, limited per component
    Real rho_f = prim_c(0) + limiter(0) * sign * grad_rho[cell].dot(dr);
    Real u_f   = prim_c(1) + limiter(1) * sign * grad_u[cell].dot(dr);
    Real v_f   = prim_c(2) + limiter(2) * sign * grad_v[cell].dot(dr);
    Real p_f   = prim_c(3) + limiter(3) * sign * grad_p[cell].dot(dr);

    // Positivity checks
    rho_f = std::max(rho_f, EPS);
    p_f   = std::max(p_f, EPS);

    // Build conservative state
    const Real rhoE_f = p_f / (gamma - 1.0) + 0.5 * rho_f * (u_f * u_f + v_f * v_f);
    Vec4 Uf;
    Uf << rho_f, rho_f * u_f, rho_f * v_f, rhoE_f;
    return Uf;
}

Vec4 reconstruct_left(const Mesh& mesh, std::size_t cell, std::size_t face,
                      const std::vector<Vec4>& U,
                      const std::vector<Vec2>& grad_rho,
                      const std::vector<Vec2>& grad_u,
                      const std::vector<Vec2>& grad_v,
                      const std::vector<Vec2>& grad_p,
                      const Vec4& limiter,
                      Real gamma) {
    return reconstruct_state(mesh, cell, face, U,
                             grad_rho, grad_u, grad_v, grad_p, limiter, gamma, +1.0);
}

Vec4 reconstruct_right(const Mesh& mesh, std::size_t cell, std::size_t face,
                       const std::vector<Vec4>& U,
                       const std::vector<Vec2>& grad_rho,
                       const std::vector<Vec2>& grad_u,
                       const std::vector<Vec2>& grad_v,
                       const std::vector<Vec2>& grad_p,
                       const Vec4& limiter,
                       Real gamma) {
    return reconstruct_state(mesh, cell, face, U,
                             grad_rho, grad_u, grad_v, grad_p, limiter, gamma, -1.0);
}

// ============================================================================
// Barth-Jespersen limiter
// ============================================================================

Vec4 barth_jespersen_limiter(const Mesh& mesh, std::size_t cell,
                              const std::vector<Vec4>& U,
                              const std::vector<Vec2>& grad_rho,
                              const std::vector<Vec2>& grad_u,
                              const std::vector<Vec2>& grad_v,
                              const std::vector<Vec2>& grad_p,
                              Real gamma) {
    const Cell& c = mesh.cells[cell];
    const Vec2& xc = c.centroid;
    const Vec4 prim_c = conservative_to_primitive(U[cell], gamma);

    // Initialize limiter to 1.0 (no limiting)
    Vec4 phi_lim = Vec4::Ones();

    // Find min and max of each primitive variable among cell and neighbors
    Real rho_min = prim_c(0), rho_max = prim_c(0);
    Real u_min   = prim_c(1), u_max   = prim_c(1);
    Real v_min   = prim_c(2), v_max   = prim_c(2);
    Real p_min   = prim_c(3), p_max   = prim_c(3);

    for (std::size_t nb_idx : c.neighbors) {
        const Vec4 prim_nb = conservative_to_primitive(U[nb_idx], gamma);
        rho_min = std::min(rho_min, prim_nb(0)); rho_max = std::max(rho_max, prim_nb(0));
        u_min   = std::min(u_min,   prim_nb(1)); u_max   = std::max(u_max,   prim_nb(1));
        v_min   = std::min(v_min,   prim_nb(2)); v_max   = std::max(v_max,   prim_nb(2));
        p_min   = std::min(p_min,   prim_nb(3)); p_max   = std::max(p_max,   prim_nb(3));
    }

    // Iterate over faces to apply Barth-Jespersen limiter
    for (std::size_t fi : c.faces) {
        const Face& face = mesh.faces[fi];
        const Vec2 dr = face.centroid - xc;

        // Reconstructed values at this face (unlimited)
        const Real rho_rc = prim_c(0) + grad_rho[cell].dot(dr);
        const Real u_rc   = prim_c(1) + grad_u[cell].dot(dr);
        const Real v_rc   = prim_c(2) + grad_v[cell].dot(dr);
        const Real p_rc   = prim_c(3) + grad_p[cell].dot(dr);

        // Barth-Jespersen limiter for each component
        auto limit_component = [](Real val, Real val_c, Real val_min, Real val_max) -> Real {
            const Real delta = val - val_c;
            if (std::abs(delta) < EPS) return 1.0;

            if (delta > 0.0) {
                const Real phi = (val_max - val_c) / delta;
                return std::min(1.0, std::max(0.0, phi));
            } else {
                const Real phi = (val_min - val_c) / delta;
                return std::min(1.0, std::max(0.0, phi));
            }
        };

        phi_lim(0) = std::min(phi_lim(0), limit_component(rho_rc, prim_c(0), rho_min, rho_max));
        phi_lim(1) = std::min(phi_lim(1), limit_component(u_rc,   prim_c(1), u_min,   u_max));
        phi_lim(2) = std::min(phi_lim(2), limit_component(v_rc,   prim_c(2), v_min,   v_max));
        phi_lim(3) = std::min(phi_lim(3), limit_component(p_rc,   prim_c(3), p_min,   p_max));
    }

    return phi_lim;
}

} // namespace cfd
