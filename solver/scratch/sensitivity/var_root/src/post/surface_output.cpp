#include "post/surface_output.h"

#include <algorithm>
#include <cmath>

#include "numerics/viscous_flux.h"
#include "physics/boundary_conditions.h"

namespace cns2d {

std::vector<SurfaceRow> collectSurfaceRows(const DistributedMesh &mesh, const FlowContext &flow,
                                          const ResidualAssembler &assembler, const StateField &U,
                                          MPI_Comm comm, std::vector<std::string> &tag_names) {
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  tag_names = mesh.boundaryNames();

  const auto &faces = mesh.faces();
  const auto &cells = mesh.cells();
  const StateField &W = assembler.primitives();
  const GradientField &grad = assembler.gradients();
  const Real p_inf = flow.freestream_prim[kPrimP];
  const Real q_inf = flow.dynamic_pressure;
  const Real inv_q = (q_inf > 0.0) ? 1.0 / q_inf : 0.0;

  std::vector<SurfaceRow> local;
  for (const Index face_id : mesh.boundaryFaces()) {
    const LocalFace &f = faces[static_cast<std::size_t>(face_id)];
    const BCType bc = mesh.boundaryTypeOfTag(f.boundary_tag);
    if (!isWall(bc)) continue;
    if (f.left >= mesh.numOwned()) continue;  // owned-once rule

    const Index c = f.left;
    const Real *wc = W.cell(c);
    const Real *gc = grad.cell(c);
    const Vec2 n = f.geom.normal;
    const Vec2 delta = f.geom.centroid - cells[static_cast<std::size_t>(c)].centroid;

    // Reconstruct the interior state at the face, then apply the boundary
    // condition to obtain the state the BC actually imposes.
    PrimVec w_face{};
    {
      const Real phi_one[kNumVars] = {1.0, 1.0, 1.0, 1.0};
      const Real *phi = assembler.limiterFactors().cell(c);
      (void)phi_one;
      if (!reconstructPrimitive(wc, gc, phi, delta, w_face)) {
        for (int v = 0; v < kNumVars; ++v) w_face[static_cast<std::size_t>(v)] = wc[v];
      }
    }
    // Report the PHYSICAL boundary state, which is what the output contract
    // asks for: exactly zero velocity on a no-slip wall, zero normal velocity
    // with retained tangential velocity on a slip wall.
    const ConsVec u_boundary = boundaryFaceState(bc, flow.gas.consFromPrim(w_face), n, flow);
    const PrimVec w_boundary = flow.gas.primFromCons(u_boundary);

    SurfaceRow row;
    row.x = f.geom.centroid.x;
    row.y = f.geom.centroid.y;
    row.nx = n.x;
    row.ny = n.y;
    row.tag = f.boundary_tag;

    row.rho = w_boundary[kPrimRho];
    row.pressure = w_boundary[kPrimP];
    row.cp = (row.pressure - p_inf) * inv_q;

    if (bc == BCType::kNoSlipAdiabaticWall) {
      // Report the imposed wall values: identically zero velocity.
      row.u = 0.0;
      row.v = 0.0;
      row.mach = 0.0;
    } else {
      // Slip wall: the face state already has zero normal velocity.
      row.u = w_boundary[kPrimU];
      row.v = w_boundary[kPrimV];
      const Real speed = std::sqrt(row.u * row.u + row.v * row.v);
      row.mach = (row.rho > 0.0 && row.pressure > 0.0)
                     ? speed / flow.gas.soundSpeed(row.rho, row.pressure)
                     : 0.0;
    }

    // Adjacent cell-centre values, for transparency in the report.
    row.cell_u = wc[kPrimU];
    row.cell_v = wc[kPrimV];
    {
      const Real speed = std::sqrt(wc[kPrimU] * wc[kPrimU] + wc[kPrimV] * wc[kPrimV]);
      row.cell_mach = (wc[kPrimRho] > 0.0 && wc[kPrimP] > 0.0)
                          ? speed / flow.gas.soundSpeed(wc[kPrimRho], wc[kPrimP])
                          : 0.0;
    }

    // Skin friction from the TANGENTIAL wall shear only.
    if (flow.viscous && bc == BCType::kNoSlipAdiabaticWall) {
      ViscousGradients vg;
      const PrimGrad g = grad.getAll(c);
      vg.grad_u = g[kPrimU];
      vg.grad_v = g[kPrimV];
      vg.grad_T = {0.0, 0.0};
      const Real dn = dot(delta, n);
      if (std::abs(dn) > 0.0) {
        auto correct = [&](Vec2 gr, Real value_wall, Real value_cell) {
          const Real expected = (value_wall - value_cell) / dn;
          const Real actual = dot(gr, n);
          return gr + (expected - actual) * n;
        };
        vg.grad_u = correct(vg.grad_u, 0.0, wc[kPrimU]);
        vg.grad_v = correct(vg.grad_v, 0.0, wc[kPrimV]);
      }
      const Real T_wall = flow.gas.temperatureFromRhoP(wc[kPrimRho], wc[kPrimP]);
      const Real mu = flow.transport.viscosity(T_wall);
      const Vec2 shear = wallShearTraction(mu, vg, n);
      // Wall shear stress acting ON THE BODY is minus the viscous traction taken
      // with the fluid-outward normal 'n' (same Cauchy convention as the force
      // integration).  Projecting it on the surface tangent gives a signed skin
      // friction that is positive for downstream-directed shear.
      const Vec2 t{-n.y, n.x};
      const Vec2 shear_on_body{-shear.x, -shear.y};
      row.tangential_shear = dot(shear_on_body, t);
      row.cf = row.tangential_shear * inv_q;
    }

    local.push_back(row);
  }

  // Gather all rows on rank 0.
  std::vector<int> counts(static_cast<std::size_t>(size), 0);
  const int my_bytes = static_cast<int>(local.size() * sizeof(SurfaceRow));
  MPI_Gather(&my_bytes, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
  std::vector<int> displs(static_cast<std::size_t>(size), 0);
  int total_bytes = 0;
  if (rank == 0) {
    for (int r = 0; r < size; ++r) {
      displs[static_cast<std::size_t>(r)] = total_bytes;
      total_bytes += counts[static_cast<std::size_t>(r)];
    }
  }
  std::vector<SurfaceRow> all;
  if (rank == 0) all.resize(static_cast<std::size_t>(total_bytes) / sizeof(SurfaceRow));
  MPI_Gatherv(local.data(), my_bytes, MPI_BYTE, rank == 0 ? all.data() : nullptr, counts.data(),
              displs.data(), MPI_BYTE, 0, comm);

  if (rank != 0) return {};

  // Sort for reproducible, plot-friendly output: by boundary tag, then by the
  // angle around the body centroid so the rows trace the surface in order.
  Real cx = 0.0;
  Real cy = 0.0;
  if (!all.empty()) {
    for (const SurfaceRow &r : all) {
      cx += r.x;
      cy += r.y;
    }
    cx /= static_cast<Real>(all.size());
    cy /= static_cast<Real>(all.size());
  }
  std::sort(all.begin(), all.end(), [&](const SurfaceRow &a, const SurfaceRow &b) {
    if (a.tag != b.tag) return a.tag < b.tag;
    const Real ta = std::atan2(a.y - cy, a.x - cx);
    const Real tb = std::atan2(b.y - cy, b.x - cx);
    if (ta != tb) return ta < tb;
    if (a.x != b.x) return a.x < b.x;
    return a.y < b.y;
  });
  return all;
}

}  // namespace cns2d
