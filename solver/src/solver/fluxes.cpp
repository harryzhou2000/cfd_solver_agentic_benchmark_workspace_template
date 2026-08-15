#include "solver/fluxes.hpp"
#include <algorithm>
#include <cmath>

namespace cfd {

// ============================================================================
// Flux functions
// ============================================================================

Conserved rusanov_flux(const Conserved& UL, const Conserved& UR,
                       Real nx, Real ny,
                       const GasModel& gas,
                       Real dissipation_scale) {
    const Real gm1 = gas.gm1;

    // Physical flux through the face at each side: F*nx + G*ny
    const Conserved FL = inviscid_flux_vector(UL, nx, ny, gm1);
    const Conserved FR = inviscid_flux_vector(UR, nx, ny, gm1);

    // Wave speeds: |V.n| + a on each side
    const Primitive WL = conserved_to_primitive(UL, gm1);
    const Primitive WR = conserved_to_primitive(UR, gm1);
    const Real vnL = WL[1] * nx + WL[2] * ny;
    const Real vnR = WR[1] * nx + WR[2] * ny;
    const Real aL = speed_of_sound(WL, gas.gamma);
    const Real aR = speed_of_sound(WR, gas.gamma);
    const Real lambda_max = std::max(std::abs(vnL) + aL, std::abs(vnR) + aR);

    // Rusanov (local Lax-Friedrichs) flux: central part minus dissipation
    // Flux is THROUGH the face (area multiplication happens in the solver loop).
    return 0.5 * (FL + FR) - 0.5 * (lambda_max * dissipation_scale) * (UR - UL);
}

Conserved inviscid_flux_vector(const Conserved& U, Real nx, Real ny, Real gm1) {
    const Primitive W = conserved_to_primitive(U, gm1);
    const Real rho = W[0];
    const Real u = W[1];
    const Real v = W[2];
    const Real p = W[3];
    const Real rhoE = U[3];          // total energy per unit volume
    const Real vn = u * nx + v * ny; // normal velocity

    Conserved F;
    F[0] = rho * vn;                       // mass
    F[1] = rho * u * vn + p * nx;          // x-momentum
    F[2] = rho * v * vn + p * ny;          // y-momentum
    F[3] = (rhoE + p) * vn;                // energy
    return F;
}

Real max_wave_speed(const Conserved& UL, const Conserved& UR,
                    Real nx, Real ny, const GasModel& gas) {
    const Primitive WL = conserved_to_primitive(UL, gas.gm1);
    const Primitive WR = conserved_to_primitive(UR, gas.gm1);
    const Real lambdaL = std::abs(WL[1] * nx + WL[2] * ny) + speed_of_sound(WL, gas.gamma);
    const Real lambdaR = std::abs(WR[1] * nx + WR[2] * ny) + speed_of_sound(WR, gas.gamma);
    return std::max(lambdaL, lambdaR);
}

// ============================================================================
// Gradient and reconstruction
// ============================================================================

std::vector<Vec2> least_squares_gradient(
    const std::vector<Real>& values,
    const std::vector<Vec2>& cell_centroids,
    const std::vector<std::vector<Int>>& cell_neighbors) {
    const size_t n = values.size();
    std::vector<Vec2> gradients(n);

    for (size_t i = 0; i < n; ++i) {
        if (i >= cell_centroids.size() || i >= cell_neighbors.size()) {
            continue; // missing geometry: zero gradient
        }
        const Vec2& ci = cell_centroids[i];

        // Normal equations for the 2x2 least-squares system M * grad = b
        Real m00 = 0.0, m01 = 0.0, m11 = 0.0;
        Real b0 = 0.0, b1 = 0.0;
        Int count = 0;
        for (Int j : cell_neighbors[i]) {
            if (j < 0 || static_cast<size_t>(j) >= n) {
                continue; // skip sentinel/out-of-range indices
            }
            const Real dx = cell_centroids[static_cast<size_t>(j)][0] - ci[0];
            const Real dy = cell_centroids[static_cast<size_t>(j)][1] - ci[1];
            const Real dphi = values[static_cast<size_t>(j)] - values[i];
            m00 += dx * dx;
            m01 += dx * dy;
            m11 += dy * dy;
            b0 += dphi * dx;
            b1 += dphi * dy;
            ++count;
        }

        // Not enough neighbors or singular system: zero gradient
        if (count < 2) {
            continue;
        }
        const Real det = m00 * m11 - m01 * m01;
        if (det < 1e-12) {
            continue;
        }
        gradients[i][0] = (b0 * m11 - b1 * m01) / det;
        gradients[i][1] = (b1 * m00 - b0 * m01) / det;
    }
    return gradients;
}

PrimitiveGradients compute_primitive_gradients(
    const std::vector<Primitive>& W,
    const std::vector<Vec2>& cell_centroids,
    const std::vector<std::vector<Int>>& cell_neighbors) {
    const size_t n = W.size();
    std::vector<Real> rho(n), u(n), v(n), p(n);
    for (size_t i = 0; i < n; ++i) {
        rho[i] = W[i][0];
        u[i] = W[i][1];
        v[i] = W[i][2];
        p[i] = W[i][3];
    }
    PrimitiveGradients grads;
    grads.grad_rho = least_squares_gradient(rho, cell_centroids, cell_neighbors);
    grads.grad_u = least_squares_gradient(u, cell_centroids, cell_neighbors);
    grads.grad_v = least_squares_gradient(v, cell_centroids, cell_neighbors);
    grads.grad_p = least_squares_gradient(p, cell_centroids, cell_neighbors);
    return grads;
}

// ============================================================================
// Reconstruction and limiting
// ============================================================================

std::pair<Primitive, Primitive> reconstruct_face_states(
    const Primitive& W_cell,
    const Primitive& W_neighbor,
    const Vec2& grad_cell,
    const Vec2& grad_neighbor,
    const Vec2& face_centroid,
    const Vec2& cell_centroid,
    const Vec2& neighbor_centroid,
    bool limiter_active) {
    const Vec2 d_cell = face_centroid - cell_centroid;
    const Vec2 d_neigh = face_centroid - neighbor_centroid;

    Primitive WL, WR;
    for (int c = 0; c < 4; ++c) {
        // Unlimited piecewise-linear extrapolation to the face
        Real incL = grad_cell.dot(d_cell);
        Real incR = grad_neighbor.dot(d_neigh);
        if (limiter_active) {
            // Barth-Jespersen limiting using the face neighbor as the stencil
            const Real phiL = barth_jespersen_limiter(
                W_cell[c], grad_cell,
                std::vector<Real>{W_neighbor[c]},
                std::vector<Vec2>{neighbor_centroid}, cell_centroid);
            const Real phiR = barth_jespersen_limiter(
                W_neighbor[c], grad_neighbor,
                std::vector<Real>{W_cell[c]},
                std::vector<Vec2>{cell_centroid}, neighbor_centroid);
            incL *= phiL;
            incR *= phiR;
        }
        WL[c] = W_cell[c] + incL;
        WR[c] = W_neighbor[c] + incR;
    }
    return {WL, WR};
}

Real barth_jespersen_limiter(
    Real value_cell,
    const Vec2& grad,
    const std::vector<Real>& neighbor_values,
    const std::vector<Vec2>& neighbor_centroids,
    const Vec2& cell_centroid) {
    const size_t n = std::min(neighbor_values.size(), neighbor_centroids.size());

    // Bounds on the scalar over the cell and its neighbors
    Real max_bound = value_cell;
    Real min_bound = value_cell;
    for (size_t j = 0; j < n; ++j) {
        max_bound = std::max(max_bound, neighbor_values[j]);
        min_bound = std::min(min_bound, neighbor_values[j]);
    }

    Real phi = 1.0;
    for (size_t j = 0; j < n; ++j) {
        // Extrapolated value at the neighbor centroid
        const Vec2 d = neighbor_centroids[j] - cell_centroid;
        const Real diff = grad.dot(d); // val_j - value_cell
        Real phi_j = 1.0;
        if (diff > 0.0) {
            const Real denom = max_bound - value_cell;
            phi_j = (denom > 0.0) ? std::min(1.0, denom / diff) : 0.0;
        } else if (diff < 0.0) {
            const Real denom = min_bound - value_cell;
            phi_j = (denom < 0.0) ? std::min(1.0, denom / diff) : 0.0;
        }
        // diff == 0: phi_j stays 1.0
        phi = std::min(phi, phi_j);
    }
    return std::max(0.0, std::min(1.0, phi));
}

// ============================================================================
// Viscous flux
// ============================================================================

Conserved viscous_flux(const Primitive& WL, const Primitive& WR,
                       const PrimitiveGradients& gradL,
                       const PrimitiveGradients& gradR,
                       const Vec2& face_centroid,
                       const Vec2& cellL, const Vec2& cellR,
                       Real nx, Real ny, Real face_area,
                       const GasModel& gas, Real viscosity) {
    (void)face_centroid;
    (void)cellL;
    (void)cellR;

    // Face-centered gradients (average of left/right cell gradients).
    // The per-face convention is a single-entry container: [0] holds the
    // gradient vector of the respective cell.
    PrimitiveGradients grad_face;
    grad_face.grad_rho.resize(1);
    grad_face.grad_u.resize(1);
    grad_face.grad_v.resize(1);
    grad_face.grad_p.resize(1);
    grad_face.grad_rho[0] = 0.5 * (gradL.grad_rho[0] + gradR.grad_rho[0]);
    grad_face.grad_u[0] = 0.5 * (gradL.grad_u[0] + gradR.grad_u[0]);
    grad_face.grad_v[0] = 0.5 * (gradL.grad_v[0] + gradR.grad_v[0]);
    grad_face.grad_p[0] = 0.5 * (gradL.grad_p[0] + gradR.grad_p[0]);

    // Velocity gradient tensor
    const Real dudx = grad_face.grad_u[0][0];
    const Real dudy = grad_face.grad_u[0][1];
    const Real dvdx = grad_face.grad_v[0][0];
    const Real dvdy = grad_face.grad_v[0][1];
    const Real divV = dudx + dvdy;

    // Newtonian stress tensor
    const Real mu = viscosity;
    const Real tau_xx = 2.0 * mu * dudx - (2.0 / 3.0) * mu * divV;
    const Real tau_yy = 2.0 * mu * dvdy - (2.0 / 3.0) * mu * divV;
    const Real tau_xy = mu * (dudy + dvdx);

    // Temperature gradient via the chain rule T = p / (rho * R):
    //   dT/dx = (dp/dx * rho - p * drho/dx) / (rho^2 * R)
    const Real rho_face = 0.5 * (WL[0] + WR[0]);
    const Real p_face = 0.5 * (WL[3] + WR[3]);
    const Real drhodx = grad_face.grad_rho[0][0];
    const Real drhody = grad_face.grad_rho[0][1];
    const Real dpdx = grad_face.grad_p[0][0];
    const Real dpdy = grad_face.grad_p[0][1];
    Real dTdx = 0.0, dTdy = 0.0;
    if (rho_face > 1e-30) {
        const Real inv = 1.0 / (rho_face * rho_face * gas.R);
        dTdx = (dpdx * rho_face - p_face * drhodx) * inv;
        dTdy = (dpdy * rho_face - p_face * drhody) * inv;
    }

    // Heat flux: q = -k * grad(T), k = mu * cp / Pr
    const Real k = mu * gas.cp / gas.prandtl;
    const Real qx = -k * dTdx;
    const Real qy = -k * dTdy;

    // Face-averaged velocity
    const Real u_face = 0.5 * (WL[1] + WR[1]);
    const Real v_face = 0.5 * (WL[2] + WR[2]);

    Conserved Fv;
    Fv[0] = 0.0; // no viscous mass flux
    Fv[1] = (tau_xx * nx + tau_xy * ny) * face_area;
    Fv[2] = (tau_xy * nx + tau_yy * ny) * face_area;
    Fv[3] = ((tau_xx * u_face + tau_xy * v_face - qx) * nx +
             (tau_xy * u_face + tau_yy * v_face - qy) * ny) * face_area;
    return Fv;
}

// ============================================================================
// Boundary conditions
// ============================================================================

Conserved farfield_boundary_state(const Conserved& U_interior,
                                  const FreestreamConfig& freestream,
                                  const GasModel& gas) {
    const Real gm1 = gas.gm1;
    const Primitive W_inf{freestream.rho, freestream.u, freestream.v,
                          freestream.pressure};
    const Conserved U_inf = primitive_to_conserved(W_inf, gm1);

    // Characteristic-based treatment (no face normal available here, so the
    // inflow/outflow decision uses the interior flow direction):
    const Primitive W_i = conserved_to_primitive(U_interior, gm1);
    const Real m_i = mach_number(W_i, gas.gamma);

    if (m_i >= 1.0) {
        // Supersonic interior: decide inflow vs outflow by comparing the
        // interior velocity with the freestream direction.
        const Real dot = W_i[1] * W_inf[1] + W_i[2] * W_inf[2];
        if (dot < 0.0) {
            // Supersonic inflow: impose the freestream state
            return U_inf;
        }
        // Supersonic outflow: extrapolate the interior state
        return U_interior;
    }

    // Subsonic: impose the freestream state (simple, robust farfield BC)
    return U_inf;
}

Conserved slip_wall_boundary_state(const Conserved& U_interior,
                                   Real nx, Real ny,
                                   const GasModel& gas) {
    const Primitive W = conserved_to_primitive(U_interior, gas.gm1);
    const Real vn = W[1] * nx + W[2] * ny;

    Primitive W_ghost;
    W_ghost[0] = W[0];                                  // same density
    W_ghost[1] = W[1] - 2.0 * vn * nx;                  // mirror normal velocity
    W_ghost[2] = W[2] - 2.0 * vn * ny;
    W_ghost[3] = W[3];                                  // same pressure
    return primitive_to_conserved(W_ghost, gas.gm1);
}

Conserved no_slip_wall_boundary_state(const Conserved& U_interior,
                                      Real nx, Real ny,
                                      const GasModel& gas) {
    (void)nx;
    (void)ny;
    const Primitive W = conserved_to_primitive(U_interior, gas.gm1);

    // Adiabatic no-slip wall: reflect velocity (zero at wall by averaging),
    // same density and pressure (dT/dn = 0 -> T_ghost = T_interior)
    Primitive W_ghost;
    W_ghost[0] = W[0];
    W_ghost[1] = -W[1];
    W_ghost[2] = -W[2];
    W_ghost[3] = W[3];
    return primitive_to_conserved(W_ghost, gas.gm1);
}

// ============================================================================
// Force integration
// ============================================================================

FaceForce compute_boundary_face_force(
    const Conserved& U_face,
    const Primitive& W_face,
    const PrimitiveGradients& grad_face,
    Real nx, Real ny, Real face_area,
    const GasModel& gas, Real viscosity,
    BcType bc_type) {
    (void)U_face;
    (void)gas;

    FaceForce force;

    // Pressure force: F = p * n * area
    const Real p = W_face[3];
    force.pressure_force_x = p * nx * face_area;
    force.pressure_force_y = p * ny * face_area;

    // Viscous force (only on no-slip adiabatic walls; slip walls and
    // farfield boundaries carry no viscous contribution)
    if (bc_type == BcType::NoSlipAdiabaticWall) {
        const Real dudx = grad_face.grad_u[0][0];
        const Real dudy = grad_face.grad_u[0][1];
        const Real dvdx = grad_face.grad_v[0][0];
        const Real dvdy = grad_face.grad_v[0][1];
        const Real divV = dudx + dvdy;

        const Real mu = viscosity;
        const Real tau_xx = 2.0 * mu * dudx - (2.0 / 3.0) * mu * divV;
        const Real tau_yy = 2.0 * mu * dvdy - (2.0 / 3.0) * mu * divV;
        const Real tau_xy = mu * (dudy + dvdx);

        force.viscous_force_x = (tau_xx * nx + tau_xy * ny) * face_area;
        force.viscous_force_y = (tau_xy * nx + tau_yy * ny) * face_area;
    }

    return force;
}

} // namespace cfd
