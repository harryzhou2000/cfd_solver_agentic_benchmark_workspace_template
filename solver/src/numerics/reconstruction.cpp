#include "numerics/reconstruction.hpp"

#include <Eigen/Dense>

#include <cmath>
#include <stdexcept>

namespace cfd {

// ---------------------------------------------------------------------------
// Unified face index space
// ---------------------------------------------------------------------------
int n_unified_faces(const DistributedMesh& dmesh) {
  return static_cast<int>(dmesh.interior_faces.size() +
                          dmesh.boundary_faces.size() + dmesh.send_faces.size() +
                          dmesh.recv_faces.size());
}

const Face2D& get_local_face(const DistributedMesh& dmesh, int unified_face) {
  int u = unified_face;
  if (u < static_cast<int>(dmesh.interior_faces.size()))
    return dmesh.interior_faces[u];
  u -= static_cast<int>(dmesh.interior_faces.size());
  if (u < static_cast<int>(dmesh.boundary_faces.size()))
    return dmesh.boundary_faces[u];
  u -= static_cast<int>(dmesh.boundary_faces.size());
  if (u < static_cast<int>(dmesh.send_faces.size()))
    return dmesh.send_faces[u];
  u -= static_cast<int>(dmesh.send_faces.size());
  return dmesh.recv_faces[u];
}

int face_other_cell(const DistributedMesh& dmesh, int unified_face, int cell) {
  const int n_int = static_cast<int>(dmesh.interior_faces.size());
  const int n_bnd = static_cast<int>(dmesh.boundary_faces.size());
  const int n_snd = static_cast<int>(dmesh.send_faces.size());
  if (unified_face < n_int) {
    const Face2D& f = dmesh.interior_faces[unified_face];
    return f.left_cell == cell ? f.right_cell : f.left_cell;
  }
  if (unified_face < n_int + n_bnd) return -1;  // boundary: no neighbor
  if (unified_face < n_int + n_bnd + n_snd)
    return dmesh.send_faces[unified_face - n_int - n_bnd].right_cell;
  return dmesh.recv_faces[unified_face - n_int - n_bnd - n_snd].left_cell;
}

std::vector<std::vector<int>> build_cell_face_map(const DistributedMesh& dmesh) {
  const long long n_owned = dmesh.n_owned;
  std::vector<std::vector<int>> cfmap(static_cast<std::size_t>(n_owned));
  const int n_int = static_cast<int>(dmesh.interior_faces.size());
  const int n_bnd = static_cast<int>(dmesh.boundary_faces.size());
  const int n_snd = static_cast<int>(dmesh.send_faces.size());

  auto add = [&](int cell, int unified) {
    if (cell >= 0 && cell < static_cast<int>(n_owned))
      cfmap[static_cast<std::size_t>(cell)].push_back(unified);
  };

  for (int f = 0; f < n_int; ++f) {
    add(dmesh.interior_faces[f].left_cell, f);
    add(dmesh.interior_faces[f].right_cell, f);
  }
  for (int f = 0; f < n_bnd; ++f) {
    const Face2D& face = dmesh.boundary_faces[f];
    add(face.left_cell >= 0 ? face.left_cell : face.right_cell, n_int + f);
  }
  for (int f = 0; f < n_snd; ++f)
    add(dmesh.send_faces[f].left_cell, n_int + n_bnd + f);
  for (int f = 0; f < static_cast<int>(dmesh.recv_faces.size()); ++f)
    add(dmesh.recv_faces[f].right_cell, n_int + n_bnd + n_snd + f);

  return cfmap;
}

// ---------------------------------------------------------------------------
// Least-squares gradients (primitive variables)
// ---------------------------------------------------------------------------
void compute_gradients(const std::vector<double>& U,
                       const DistributedMesh& dmesh,
                       const std::vector<std::vector<int>>& cell_face_map,
                       std::vector<double>& gradients, double gamma) {
  const long long n_owned = dmesh.n_owned;
  const long long n_local = static_cast<long long>(dmesh.cells.size());
  // Size the array for ALL local cells (owned + ghosts) so the caller can
  // exchange ghost entries with halo_exchange(NVARS*2 components) without
  // an out-of-bounds write; ghost entries start zeroed.
  gradients.assign(static_cast<std::size_t>(n_local) * NVARS * 2, 0.0);

  for (long long c = 0; c < n_owned; ++c) {
    const PrimitiveState prim = cons_to_prim(
        U.data() + static_cast<std::size_t>(c) * NVARS, gamma, 1.0);
    const Vector3& center = dmesh.cells[static_cast<std::size_t>(c)].cell_center;
    const double phi_cell[NVARS] = {prim.rho, prim.u, prim.v, prim.p};

    Eigen::Matrix2d ATA = Eigen::Matrix2d::Zero();
    Eigen::Vector2d ATb[NVARS] = {Eigen::Vector2d::Zero(),
                                  Eigen::Vector2d::Zero(),
                                  Eigen::Vector2d::Zero(),
                                  Eigen::Vector2d::Zero()};

    for (int uf : cell_face_map[static_cast<std::size_t>(c)]) {
      // Boundary faces are excluded from the stencil: ghosting the cell
      // value to the face center would bias the fit at O(|grad|*h) (the
      // ghost point is off the local plane); the interior neighbors already
      // span both directions for well-shaped cells, and the singularity
      // check below keeps degenerate stencils first-order.
      const int nbr = face_other_cell(dmesh, uf, static_cast<int>(c));
      if (nbr < 0) continue;
      const PrimitiveState nbr_prim = cons_to_prim(
          U.data() + static_cast<std::size_t>(nbr) * NVARS, gamma, 1.0);
      const double phi_nbr[NVARS] = {nbr_prim.rho, nbr_prim.u, nbr_prim.v,
                                     nbr_prim.p};
      const Vector3 d =
          dmesh.cells[static_cast<std::size_t>(nbr)].cell_center - center;
      const double dist2 = d.x * d.x + d.y * d.y;
      if (!(dist2 > 0.0)) continue;
      const double w = 1.0 / std::sqrt(dist2);

      ATA(0, 0) += w * d.x * d.x;
      ATA(0, 1) += w * d.x * d.y;
      ATA(1, 1) += w * d.y * d.y;
      const double dphi[NVARS] = {phi_nbr[0] - phi_cell[0],
                                  phi_nbr[1] - phi_cell[1],
                                  phi_nbr[2] - phi_cell[2],
                                  phi_nbr[3] - phi_cell[3]};
      for (int k = 0; k < NVARS; ++k) {
        ATb[k](0) += w * d.x * dphi[k];
        ATb[k](1) += w * d.y * dphi[k];
      }
    }
    ATA(1, 0) = ATA(0, 1);

    // Singular (degenerate/one-sided stencil): stay first-order.
    const double det = ATA(0, 0) * ATA(1, 1) - ATA(0, 1) * ATA(1, 0);
    if (!(det > 1e-30)) continue;

    double* g = gradients.data() + static_cast<std::size_t>(c) * NVARS * 2;
    for (int k = 0; k < NVARS; ++k) {
      const Eigen::Vector2d grad = ATA.ldlt().solve(ATb[k]);
      g[k * 2 + 0] = grad(0);
      g[k * 2 + 1] = grad(1);
    }
  }
}

// ---------------------------------------------------------------------------
// Face reconstruction
// ---------------------------------------------------------------------------
void reconstruct_face(const double* U_int, const double* grad_int,
                      const Vector3& dx, double gamma, double* U_face) {
  const PrimitiveState prim = cons_to_prim(U_int, gamma, 1.0);
  double rho_f = prim.rho + grad_int[0] * dx.x + grad_int[1] * dx.y;
  double u_f = prim.u + grad_int[2] * dx.x + grad_int[3] * dx.y;
  double v_f = prim.v + grad_int[4] * dx.x + grad_int[5] * dx.y;
  double p_f = prim.p + grad_int[6] * dx.x + grad_int[7] * dx.y;

  // Positivity fallback: keep the cell-center state.
  if (!(rho_f > 0.0) || !(p_f > 0.0)) {
    rho_f = prim.rho;
    u_f = prim.u;
    v_f = prim.v;
    p_f = prim.p;
  }

  PrimitiveState pf;
  pf.rho = rho_f;
  pf.u = u_f;
  pf.v = v_f;
  pf.p = p_f;
  prim_to_cons(pf, gamma, U_face);
}

}  // namespace cfd
