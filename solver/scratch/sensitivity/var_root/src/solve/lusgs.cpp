#include "solve/lusgs.h"

#include <algorithm>
#include <cmath>

#include "core/exceptions.h"
#include "physics/boundary_conditions.h"

namespace cns2d {

std::string implicitSolverName(ImplicitSolverType t) {
  switch (t) {
    case ImplicitSolverType::kLuSgs:
      return "lu_sgs_simplified_jacobian";
    case ImplicitSolverType::kJacobi:
      return "block_jacobi_simplified_jacobian";
  }
  return "unknown";
}

ImplicitSolverType parseImplicitSolverType(const std::string &name) {
  if (name == "lusgs" || name == "lu_sgs") return ImplicitSolverType::kLuSgs;
  if (name == "jacobi" || name == "block_jacobi") return ImplicitSolverType::kJacobi;
  throw CnsError("unknown implicit solver '" + name + "' (supported: lusgs, jacobi)");
}

ConsVec fluxJacobianTimesVector(const PerfectGas &gas, const ConsVec &U, Vec2 n,
                                const ConsVec &dU) {
  // Exact Jacobian-vector product for the 2-D Euler normal flux.
  //
  // With q = (rho, rho u, rho v, rho E), the normal flux is
  //   F.n = [ rho un, rho un u + p nx, rho un v + p ny, un (rho E + p) ].
  // Differentiating and contracting with dU gives the expressions below.  This
  // is written as a matrix-free product so no 4x4 block is ever stored.
  const Real gamma = gas.gamma();
  const Real gm1 = gamma - 1.0;

  const Real rho = U[kRho];
  const Real inv_rho = 1.0 / rho;
  const Real u = U[kRhoU] * inv_rho;
  const Real v = U[kRhoV] * inv_rho;
  const Real E = U[kRhoE] * inv_rho;      // total energy per unit mass
  const Real q2 = u * u + v * v;
  const Real p = gm1 * rho * (E - 0.5 * q2);
  const Real H = E + p / rho;             // total enthalpy
  const Real un = u * n.x + v * n.y;

  // Primitive-variable increments implied by dU.
  const Real drho = dU[kRho];
  const Real du = (dU[kRhoU] - u * drho) * inv_rho;
  const Real dv = (dU[kRhoV] - v * drho) * inv_rho;
  const Real dE = (dU[kRhoE] - E * drho) * inv_rho;
  const Real dun = du * n.x + dv * n.y;
  // dp = (gamma-1) * ( d(rho E) - 0.5 |u|^2 drho - rho (u du + v dv) )
  const Real dp = gm1 * (dU[kRhoE] - 0.5 * q2 * drho - rho * (u * du + v * dv));
  const Real d_rho_un = drho * un + rho * dun;

  ConsVec out{};
  out[kRho] = d_rho_un;
  out[kRhoU] = d_rho_un * u + rho * un * du + dp * n.x;
  out[kRhoV] = d_rho_un * v + rho * un * dv + dp * n.y;
  // d[ un (rho E + p) ] = dun (rho E + p) + un ( d(rho E) + dp )
  out[kRhoE] = dun * (rho * E + p) + un * (dU[kRhoE] + dp);
  (void)H;
  (void)dE;
  return out;
}

ImplicitSolver::ImplicitSolver(const DistributedMesh &mesh, const FlowContext &flow,
                               ImplicitSolverType type)
    : mesh_(mesh), flow_(flow), type_(type) {
  diagonal_.assign(static_cast<std::size_t>(mesh_.numOwned()), 0.0);
  work_.resize(mesh_.numLocal());
}

void ImplicitSolver::buildDiagonal(const StateField &U, const std::vector<Real> &diag_scale,
                                   const std::vector<Real> &conv_radius,
                                   const std::vector<Real> &visc_radius) {
  const Index num_owned = mesh_.numOwned();
  for (Index c = 0; c < num_owned; ++c) {
    // Diagonal = pseudo-time/physical-time term + 0.5 * sum |lambda| * area
    // (the scalar-dissipation part of the flux linearisation), plus the viscous
    // spectral radius when the run is viscous.  The 0.5 factor is the upwind
    // split of the face dissipation between the two adjacent cells.
    Real d = diag_scale[static_cast<std::size_t>(c)];
    d += 0.5 * conv_radius[static_cast<std::size_t>(c)];
    if (!visc_radius.empty()) {
      d += visc_radius[static_cast<std::size_t>(c)];
    }
    diagonal_[static_cast<std::size_t>(c)] = (d > 0.0) ? d : 1.0;
  }
}

ConsVec ImplicitSolver::offDiagonalAction(const StateField &U, const StateField &dU, Index face,
                                          Index c, Index other) const {
  const LocalFace &f = mesh_.faces()[static_cast<std::size_t>(face)];
  // Outward normal of cell c across this face.
  Vec2 n = f.geom.normal;
  if (f.left != c) n = -1.0 * n;

  const ConsVec u_other = U.get(other);
  const ConsVec du_other = dU.get(other);

  // Scalar-dissipation upwind approximation of the off-diagonal block:
  //   O_ij dU_j = 0.5 * ( A_j.n dU_j - |lambda| dU_j ) * area
  const ConsVec jac = fluxJacobianTimesVector(flow_.gas, u_other, n, du_other);

  const PrimVec w = flow_.gas.primFromCons(u_other);
  const Real a = flow_.gas.soundSpeed(std::max(w[kPrimRho], kTiny), std::max(w[kPrimP], kTiny));
  const Real un = w[kPrimU] * n.x + w[kPrimV] * n.y;
  Real lambda = std::abs(un) + a;

  // Viscous coupling strengthens the off-diagonal; include it so the implicit
  // operator stays consistent with the diagonal on viscous meshes.
  if (flow_.viscous) {
    const Real T = flow_.gas.temperatureFromRhoP(std::max(w[kPrimRho], kTiny),
                                                 std::max(w[kPrimP], kTiny));
    const Real mu = flow_.transport.viscosity(T);
    const Vec2 d = mesh_.cells()[static_cast<std::size_t>(other)].centroid -
                   mesh_.cells()[static_cast<std::size_t>(c)].centroid;
    const Real dist = std::max(norm(d), kTiny);
    lambda += 2.0 * mu / (std::max(w[kPrimRho], kTiny) * dist);
  }

  ConsVec out{};
  const Real area = f.geom.area;
  for (int k = 0; k < kNumVars; ++k) {
    out[k] = 0.5 * area * (jac[k] - lambda * du_other[k]);
  }
  return out;
}

void ImplicitSolver::solve(const StateField &U, const StateField &rhs,
                           const std::vector<Real> &diag_scale,
                           const std::vector<Real> &conv_radius,
                           const std::vector<Real> &visc_radius, int num_sweeps, StateField &dU,
                           HaloExchange &halo, bool reset_increment) {
  buildDiagonal(U, diag_scale, conv_radius, visc_radius);

  const Index num_owned = mesh_.numOwned();
  const auto &ranges = mesh_.cellFaceRanges();
  const auto &cell_faces = mesh_.cellFaces();
  const auto &faces = mesh_.faces();

  // dU = 0 is the natural initial guess for a fresh nonlinear step, but when the
  // driver is adding sweeps to an already partially solved system the existing
  // increment must be kept, otherwise the extra sweeps achieve nothing.
  if (reset_increment) dU.setZero();

  for (int sweep = 0; sweep < num_sweeps; ++sweep) {
    // Refresh ghost increments once per sweep.  Between refreshes the off-rank
    // coupling is frozen, which is exactly block-Jacobi across ranks.
    halo.exchange(dU.data(), kNumVars, kNumVars);

    if (type_ == ImplicitSolverType::kJacobi) {
      // Jacobi: compute all updates from the previous iterate.
      for (Index c = 0; c < num_owned; ++c) {
        ConsVec acc = rhs.get(c);
        const DistributedMesh::CellFaceRange range = ranges[static_cast<std::size_t>(c)];
        for (Index k = 0; k < range.count; ++k) {
          const Index face_id = cell_faces[static_cast<std::size_t>(range.begin + k)];
          const LocalFace &f = faces[static_cast<std::size_t>(face_id)];
          if (f.kind == FaceKind::kBoundary) continue;
          const Index other = (f.left == c) ? f.right : f.left;
          const ConsVec contribution = offDiagonalAction(U, dU, face_id, c, other);
          for (int v = 0; v < kNumVars; ++v) acc[v] -= contribution[v];
        }
        const Real inv_d = 1.0 / diagonal_[static_cast<std::size_t>(c)];
        ConsVec next{};
        for (int v = 0; v < kNumVars; ++v) next[v] = acc[v] * inv_d;
        work_.set(c, next);
      }
      for (Index c = 0; c < num_owned; ++c) dU.set(c, work_.get(c));
      continue;
    }

    // --- LU-SGS forward sweep (ascending cell index) --------------------
    for (Index c = 0; c < num_owned; ++c) {
      ConsVec acc = rhs.get(c);
      const DistributedMesh::CellFaceRange range = ranges[static_cast<std::size_t>(c)];
      for (Index k = 0; k < range.count; ++k) {
        const Index face_id = cell_faces[static_cast<std::size_t>(range.begin + k)];
        const LocalFace &f = faces[static_cast<std::size_t>(face_id)];
        if (f.kind == FaceKind::kBoundary) continue;
        const Index other = (f.left == c) ? f.right : f.left;
        const ConsVec contribution = offDiagonalAction(U, dU, face_id, c, other);
        for (int v = 0; v < kNumVars; ++v) acc[v] -= contribution[v];
      }
      const Real inv_d = 1.0 / diagonal_[static_cast<std::size_t>(c)];
      ConsVec next{};
      for (int v = 0; v < kNumVars; ++v) next[v] = acc[v] * inv_d;
      dU.set(c, next);
    }

    // --- LU-SGS backward sweep (descending cell index) ------------------
    for (Index c = num_owned - 1; c >= 0; --c) {
      ConsVec acc = rhs.get(c);
      const DistributedMesh::CellFaceRange range = ranges[static_cast<std::size_t>(c)];
      for (Index k = 0; k < range.count; ++k) {
        const Index face_id = cell_faces[static_cast<std::size_t>(range.begin + k)];
        const LocalFace &f = faces[static_cast<std::size_t>(face_id)];
        if (f.kind == FaceKind::kBoundary) continue;
        const Index other = (f.left == c) ? f.right : f.left;
        const ConsVec contribution = offDiagonalAction(U, dU, face_id, c, other);
        for (int v = 0; v < kNumVars; ++v) acc[v] -= contribution[v];
      }
      const Real inv_d = 1.0 / diagonal_[static_cast<std::size_t>(c)];
      ConsVec next{};
      for (int v = 0; v < kNumVars; ++v) next[v] = acc[v] * inv_d;
      dU.set(c, next);
    }
  }
}

Real ImplicitSolver::linearResidualRatio(const StateField &U, const StateField &rhs,
                                         const std::vector<Real> &diag_scale,
                                         const std::vector<Real> &conv_radius,
                                         const std::vector<Real> &visc_radius, StateField &dU,
                                         HaloExchange &halo) {
  buildDiagonal(U, diag_scale, conv_radius, visc_radius);
  // Ghost increments must be current for the off-diagonal action to be correct.
  halo.exchange(dU.data(), kNumVars, kNumVars);

  const Index num_owned = mesh_.numOwned();
  const auto &ranges = mesh_.cellFaceRanges();
  const auto &cell_faces = mesh_.cellFaces();
  const auto &faces = mesh_.faces();
  const auto &cells = mesh_.cells();

  Real local_num = 0.0;
  Real local_den = 0.0;
  for (Index c = 0; c < num_owned; ++c) {
    const Real *r = rhs.cell(c);
    const ConsVec du_c = dU.get(c);
    // Diagonal action.
    ConsVec Adu{};
    for (int v = 0; v < kNumVars; ++v) {
      Adu[static_cast<std::size_t>(v)] = diagonal_[static_cast<std::size_t>(c)] * du_c[v];
    }
    // Off-diagonal action from the neighbours.
    const DistributedMesh::CellFaceRange range = ranges[static_cast<std::size_t>(c)];
    for (Index k = 0; k < range.count; ++k) {
      const Index face_id = cell_faces[static_cast<std::size_t>(range.begin + k)];
      const LocalFace &f = faces[static_cast<std::size_t>(face_id)];
      if (f.kind == FaceKind::kBoundary) continue;
      const Index other = (f.left == c) ? f.right : f.left;
      const ConsVec contribution = offDiagonalAction(U, dU, face_id, c, other);
      for (int v = 0; v < kNumVars; ++v) Adu[static_cast<std::size_t>(v)] += contribution[v];
    }
    // Volume-normalised so the ratio is mesh-size independent, matching the
    // convention used for the nonlinear residual norms.
    const Real inv_v = cells[static_cast<std::size_t>(c)].inv_volume;
    for (int v = 0; v < kNumVars; ++v) {
      const Real defect = (r[v] - Adu[static_cast<std::size_t>(v)]) * inv_v;
      const Real reference = r[v] * inv_v;
      local_num += defect * defect;
      local_den += reference * reference;
    }
  }

  Real sums[2] = {local_num, local_den};
  Real global[2] = {0.0, 0.0};
  MPI_Allreduce(sums, global, 2, MPI_DOUBLE, MPI_SUM, mesh_.comm());
  return (global[1] > 0.0) ? std::sqrt(global[0] / global[1]) : 0.0;
}

}  // namespace cns2d
