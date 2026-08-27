#include "mesh/mesh_verification.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "core/exceptions.h"
#include "core/logging.h"
#include "physics/boundary_conditions.h"

namespace cns2d {

GeometryVerification verifyMeshGeometry(const DistributedMesh &mesh, const FlowContext &flow,
                                        ResidualAssembler &assembler, HaloExchange &halo) {
  GeometryVerification v;
  const Index num_owned = mesh.numOwned();
  const auto &faces = mesh.faces();
  const auto &cells = mesh.cells();
  const auto &ranges = mesh.cellFaceRanges();
  const auto &cell_faces = mesh.cellFaces();
  const auto &signs = mesh.cellFaceSign();

  // --- 1 & 2: per-cell face closure and divergence-theorem volume -----------
  Real local_max_closure = 0.0;
  Real local_max_volume_error = 0.0;
  Real local_volume = 0.0;

  for (Index c = 0; c < num_owned; ++c) {
    const DistributedMesh::CellFaceRange range = ranges[static_cast<std::size_t>(c)];
    Vec2 closure{0.0, 0.0};
    Real area_sum = 0.0;
    Real volume_estimate = 0.0;
    const Vec2 xc = cells[static_cast<std::size_t>(c)].centroid;

    for (Index k = 0; k < range.count; ++k) {
      const Index face_id = cell_faces[static_cast<std::size_t>(range.begin + k)];
      const LocalFace &f = faces[static_cast<std::size_t>(face_id)];
      const Real sign = static_cast<Real>(signs[static_cast<std::size_t>(range.begin + k)]);
      // Outward normal of THIS cell.
      const Vec2 n = sign * f.geom.normal;
      closure = closure + f.geom.area * n;
      area_sum += f.geom.area;
      // 2-D divergence theorem: V = 0.5 * sum (x_f - x_c) . n * |face|
      volume_estimate += 0.5 * dot(f.geom.centroid - xc, n) * f.geom.area;
    }

    if (area_sum > 0.0) {
      local_max_closure = std::max(local_max_closure, norm(closure) / area_sum);
    }
    const Real volume = cells[static_cast<std::size_t>(c)].volume;
    if (volume > 0.0) {
      local_max_volume_error =
          std::max(local_max_volume_error, std::abs(volume_estimate - volume) / volume);
    }
    local_volume += volume;
  }

  // --- 3: global area from the boundary alone ------------------------------
  // The signed area enclosed by the outer boundary is 0.5 * closed integral of
  // (x . n) ds over the boundary faces, which must equal the summed cell volumes.
  Real local_boundary_area = 0.0;
  for (const Index face_id : mesh.boundaryFaces()) {
    const LocalFace &f = faces[static_cast<std::size_t>(face_id)];
    if (f.left >= num_owned) continue;  // count each boundary face once
    local_boundary_area += 0.5 * dot(f.geom.centroid, f.geom.normal) * f.geom.area;
  }

  // --- 4: free-stream preservation on interior-only cells ------------------
  // A uniform state must produce an identically zero residual wherever the
  // stencil does not touch a boundary condition.  This isolates the interior
  // scheme (geometry, reconstruction, flux, scatter) from the boundary treatment.
  StateField U_uniform(mesh.numLocal());
  for (Index c = 0; c < mesh.numLocal(); ++c) U_uniform.set(c, flow.freestream_cons);
  halo.exchange(U_uniform.data(), kNumVars, kNumVars);

  StateField residual(mesh.numLocal());
  ResidualDiagnostics diag;
  assembler.evaluate(U_uniform, residual, halo, diag);

  // A cell is 'interior only' when neither it nor any of its face neighbours
  // touches a boundary face, so its residual involves no boundary condition.
  std::vector<bool> touches_boundary(static_cast<std::size_t>(mesh.numLocal()), false);
  for (const Index face_id : mesh.boundaryFaces()) {
    touches_boundary[static_cast<std::size_t>(faces[static_cast<std::size_t>(face_id)].left)] = true;
  }

  Real local_max_uniform = 0.0;
  Index local_interior_only = 0;
  for (Index c = 0; c < num_owned; ++c) {
    if (touches_boundary[static_cast<std::size_t>(c)]) continue;
    bool neighbour_on_boundary = false;
    const DistributedMesh::CellFaceRange range = ranges[static_cast<std::size_t>(c)];
    for (Index k = 0; k < range.count; ++k) {
      const Index face_id = cell_faces[static_cast<std::size_t>(range.begin + k)];
      const LocalFace &f = faces[static_cast<std::size_t>(face_id)];
      const Index other = (f.left == c) ? f.right : f.left;
      if (other < 0 || touches_boundary[static_cast<std::size_t>(other)]) {
        neighbour_on_boundary = true;
        break;
      }
    }
    if (neighbour_on_boundary) continue;
    ++local_interior_only;

    const Real inv_v = cells[static_cast<std::size_t>(c)].inv_volume;
    const Real *r = residual.cell(c);
    // Scale by the freestream flux magnitude so the number is dimensionless.
    const Real reference =
        std::max(flow.freestream_prim[kPrimRho] * flow.freestream_speed, kTiny);
    for (int k = 0; k < kNumVars; ++k) {
      local_max_uniform = std::max(local_max_uniform, std::abs(r[k] * inv_v) / reference);
    }
  }

  // --- 6: discrete global conservation --------------------------------------
  // Summing the residual over every owned cell must reproduce minus the net flux
  // through the outer boundary, because every interior face contributes with
  // opposite signs to its two cells and therefore telescopes away.  A nonzero
  // defect means the face-flux scatter is inconsistent.
  //
  // The boundary term MUST be recomputed with exactly the same face states the
  // residual used, including the second-order reconstruction; comparing a
  // reconstructed interior assembly against a first-order boundary term measures
  // the reconstruction, not the conservation, and produces a spurious defect.
  // The check therefore runs with reconstruction disabled, which isolates the
  // scatter itself: telescoping is a property of the assembly loop and is
  // independent of how the face states were obtained.
  //
  // The test uses a non-uniform state so the cancellation is nontrivial.
  {
    // Temporarily force first-order face states so the boundary flux below can be
    // reproduced exactly.
    SchemeOptions saved_scheme = assembler.scheme();
    SchemeOptions probe_scheme = saved_scheme;
    probe_scheme.second_order = false;
    assembler.setScheme(probe_scheme);

    StateField U_test(mesh.numLocal());
    for (Index c = 0; c < mesh.numLocal(); ++c) {
      const Vec2 x = cells[static_cast<std::size_t>(c)].centroid;
      // A smooth perturbation of the freestream, kept small enough to stay
      // firmly subsonic and positive.
      const Real bump = 0.05 * std::sin(0.7 * x.x) * std::cos(0.9 * x.y);
      PrimVec w = flow.freestream_prim;
      w[kPrimRho] *= (1.0 + bump);
      w[kPrimU] *= (1.0 + 0.5 * bump);
      w[kPrimV] += 0.05 * bump;
      w[kPrimP] *= (1.0 + 0.3 * bump);
      U_test.set(c, flow.gas.consFromPrim(w));
    }
    halo.exchange(U_test.data(), kNumVars, kNumVars);

    StateField R_test(mesh.numLocal());
    ResidualDiagnostics test_diag;
    assembler.evaluate(U_test, R_test, halo, test_diag);

    // Sum of the residual over owned cells.
    std::array<Real, kNumVars> local_sum{};
    for (Index c = 0; c < num_owned; ++c) {
      const Real *r = R_test.cell(c);
      for (int k = 0; k < kNumVars; ++k) local_sum[static_cast<std::size_t>(k)] += r[k];
    }

    // Net boundary flux, recomputed independently from the boundary faces.
    std::array<Real, kNumVars> local_boundary{};
    Real local_scale = 0.0;
    for (const Index face_id : mesh.boundaryFaces()) {
      const LocalFace &f = faces[static_cast<std::size_t>(face_id)];
      if (f.left >= num_owned) continue;
      const PrimVec wl = flow.gas.primFromCons(U_test.get(f.left));
      const ConsVec ub = boundaryGhostState(mesh.boundaryTypeOfTag(f.boundary_tag),
                                            flow.gas.consFromPrim(wl), f.geom.normal, flow);
      Real wave = 0.0;
      const ConsVec flux =
          riemannFlux(assembler.scheme().inviscid_flux, flow.gas, wl,
                      flow.gas.primFromCons(ub), f.geom.normal,
                      assembler.scheme().rusanov_dissipation_scale, wave);
      for (int k = 0; k < kNumVars; ++k) {
        local_boundary[static_cast<std::size_t>(k)] += flux[k] * f.geom.area;
        local_scale += std::abs(flux[k]) * f.geom.area;
      }
    }

    std::array<Real, 2 * kNumVars + 1> send{};
    for (int k = 0; k < kNumVars; ++k) {
      send[static_cast<std::size_t>(k)] = local_sum[static_cast<std::size_t>(k)];
      send[static_cast<std::size_t>(kNumVars + k)] = local_boundary[static_cast<std::size_t>(k)];
    }
    send[2 * kNumVars] = local_scale;
    std::array<Real, 2 * kNumVars + 1> recv{};
    MPI_Allreduce(send.data(), recv.data(), 2 * kNumVars + 1, MPI_DOUBLE, MPI_SUM, mesh.comm());

    Real defect = 0.0;
    for (int k = 0; k < kNumVars; ++k) {
      // Residual is defined as MINUS the flux sum, so residual_sum + boundary_flux
      // must vanish when the interior faces telescope.
      defect += std::abs(recv[static_cast<std::size_t>(k)] +
                         recv[static_cast<std::size_t>(kNumVars + k)]);
    }
    const Real scale = std::max(recv[2 * kNumVars], kTiny);
    v.conservation_defect = defect / scale;

    assembler.setScheme(saved_scheme);
  }

  // --- global reductions --------------------------------------------------
  Real send_max[3] = {local_max_closure, local_max_volume_error, local_max_uniform};
  Real recv_max[3] = {0.0, 0.0, 0.0};
  MPI_Allreduce(send_max, recv_max, 3, MPI_DOUBLE, MPI_MAX, mesh.comm());
  Real send_sum[2] = {local_volume, local_boundary_area};
  Real recv_sum[2] = {0.0, 0.0};
  MPI_Allreduce(send_sum, recv_sum, 2, MPI_DOUBLE, MPI_SUM, mesh.comm());
  long long send_count = local_interior_only;
  long long recv_count = 0;
  MPI_Allreduce(&send_count, &recv_count, 1, MPI_LONG_LONG, MPI_SUM, mesh.comm());

  v.max_face_closure_error = recv_max[0];
  v.max_volume_closure_error = recv_max[1];
  v.max_uniform_flow_residual = recv_max[2];
  v.total_volume = recv_sum[0];
  v.boundary_enclosed_area = recv_sum[1];
  v.num_interior_only_cells = static_cast<Index>(recv_count);
  v.area_mismatch = (v.total_volume > 0.0)
                        ? std::abs(v.boundary_enclosed_area - v.total_volume) / v.total_volume
                        : 0.0;

  // --- 5: least-squares gradient exactness on a linear field ---------------
  // Reconstructing a linear field must return its exact gradient.  The test
  // field is evaluated at cell centroids and at boundary-face centroids, which
  // is exactly how the stencil consumes data, so this validates the precomputed
  // weights including the boundary entries.
  {
    const Vec2 exact_gradient{0.37, -0.81};
    const Real offset = 2.5;
    auto linearField = [&](Vec2 x) { return offset + exact_gradient.x * x.x + exact_gradient.y * x.y; };

    const auto &stencils = mesh.gradientStencils();
    const auto &entries = mesh.gradientEntries();
    Real local_max_error = 0.0;
    for (Index c = 0; c < num_owned; ++c) {
      const DistributedMesh::GradientStencil st = stencils[static_cast<std::size_t>(c)];
      const Real phi_c = linearField(cells[static_cast<std::size_t>(c)].centroid);
      Vec2 g{0.0, 0.0};
      for (Index k = 0; k < st.count; ++k) {
        const DistributedMesh::StencilEntry &e = entries[static_cast<std::size_t>(st.begin + k)];
        const Vec2 x_n = (e.kind == DistributedMesh::StencilKind::kCell)
                             ? cells[static_cast<std::size_t>(e.index)].centroid
                             : faces[static_cast<std::size_t>(e.index)].geom.centroid;
        const Real dphi = linearField(x_n) - phi_c;
        g.x += e.weight.x * dphi;
        g.y += e.weight.y * dphi;
      }
      const Real err = norm(g - exact_gradient) / norm(exact_gradient);
      local_max_error = std::max(local_max_error, err);
    }
    Real global_error = 0.0;
    MPI_Allreduce(&local_max_error, &global_error, 1, MPI_DOUBLE, MPI_MAX, mesh.comm());
    v.max_linear_gradient_error = global_error;
  }

  // Hard geometric identities: a violation means the mesh or the metric
  // construction is wrong, and no amount of iteration will fix it.  The
  // tolerances are loose enough to absorb floating-point cancellation on the
  // smallest boundary-layer cells (whose areas reach 1e-9 here, so a relative
  // error near 1e-10 is pure round-off) but far tighter than any real error.
  if (v.max_face_closure_error > 1.0e-9) {
    throw CnsError(formatString(
        "mesh verification failed: face closure error %.3e exceeds 1e-9; cell face normals do not "
        "sum to zero, so the discretization is not conservative",
        v.max_face_closure_error));
  }
  if (v.max_volume_closure_error > 1.0e-8) {
    throw CnsError(formatString(
        "mesh verification failed: divergence-theorem cell volume differs from the polygon area by "
        "%.3e relative (tolerance 1e-8)",
        v.max_volume_closure_error));
  }
  if (v.area_mismatch > 1.0e-8) {
    throw CnsError(formatString(
        "mesh verification failed: summed cell volume %.10g disagrees with the boundary-enclosed "
        "area %.10g (%.3e relative)",
        v.total_volume, v.boundary_enclosed_area, v.area_mismatch));
  }
  // Free-stream preservation is a scheme property rather than a mesh identity, so
  // it is reported and warned about rather than being fatal.
  v.passed = v.max_uniform_flow_residual < 1.0e-12;
  if (!v.passed) {
    logWarn(formatString(
        "mesh verification: uniform-flow residual on interior-only cells is %.3e (expected < 1e-12);"
        " the interior scheme does not preserve a uniform state exactly",
        v.max_uniform_flow_residual));
  }
  return v;
}

std::string describeVerification(const GeometryVerification &v) {
  return formatString(
      "mesh verification: face-closure error %.3e, volume-closure error %.3e, total area %.10g "
      "(boundary integral %.10g, mismatch %.3e), linear-gradient error %.3e, conservation defect "
      "%.3e, uniform-flow residual %.3e on %lld interior-only cells -- %s",
      v.max_face_closure_error, v.max_volume_closure_error, v.total_volume,
      v.boundary_enclosed_area, v.area_mismatch, v.max_linear_gradient_error,
      v.conservation_defect, v.max_uniform_flow_residual,
      static_cast<long long>(v.num_interior_only_cells), v.passed ? "PASS" : "CHECK");
}

}  // namespace cns2d
