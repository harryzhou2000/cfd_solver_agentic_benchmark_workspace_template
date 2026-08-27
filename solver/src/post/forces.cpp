#include "post/forces.h"

#include <cmath>

#include "numerics/viscous_flux.h"
#include "physics/boundary_conditions.h"

namespace cns2d {

ForceResult computeForces(const DistributedMesh &mesh, const FlowContext &flow,
                          const ResidualAssembler &assembler, const StateField &U, MPI_Comm comm) {
  const auto &faces = mesh.faces();
  const auto &cells = mesh.cells();
  const StateField &W = assembler.primitives();
  const GradientField &grad = assembler.gradients();
  const Real p_inf = flow.freestream_prim[kPrimP];

  // Local accumulators: [pdx, pdy, vdx, vdy, moment, wall length]
  Real acc[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

  for (const Index face_id : mesh.boundaryFaces()) {
    const LocalFace &f = faces[static_cast<std::size_t>(face_id)];
    const BCType bc = mesh.boundaryTypeOfTag(f.boundary_tag);
    if (!isWall(bc)) continue;
    // Only integrate faces whose adjacent cell is owned, so a partition-boundary
    // wall face is counted exactly once globally.
    if (f.left >= mesh.numOwned()) continue;

    const Index c = f.left;
    const Real area = f.geom.area;
    // Face normals are oriented away from the adjacent (left) cell, so on a wall
    // face 'n' points OUT OF THE FLUID AND INTO THE BODY.
    //
    // Cauchy traction with m the normal pointing from the body into the fluid
    // (m = -n) gives the force per unit area exerted by the fluid on the body:
    //   t = sigma . m = (-p I + tau) . (-n) = p n - tau . n
    // so the pressure part acts along +n (pressure pushes the surface) and the
    // viscous part is MINUS the viscous traction tau . n.
    const Vec2 n = f.geom.normal;

    // --- pressure force ---------------------------------------------------
    // Reconstruct the wall pressure from the adjacent cell so the integrand is
    // second-order accurate, consistent with the flux evaluation.
    const Real *wc = W.cell(c);
    const Real *gc = grad.cell(c);
    const Vec2 delta = f.geom.centroid - cells[static_cast<std::size_t>(c)].centroid;
    Real p_wall = wc[kPrimP] + gc[kPrimP * kDim + 0] * delta.x + gc[kPrimP * kDim + 1] * delta.y;
    if (!(p_wall > 0.0)) p_wall = wc[kPrimP];

    // Pressure force on the body, referenced to the freestream pressure so a
    // uniform ambient pressure contributes nothing on a closed body.
    const Real dpx = (p_wall - p_inf) * n.x * area;
    const Real dpy = (p_wall - p_inf) * n.y * area;
    acc[0] += dpx;
    acc[1] += dpy;

    // --- viscous (tangential) force --------------------------------------
    Real dvx = 0.0;
    Real dvy = 0.0;
    if (flow.viscous && bc == BCType::kNoSlipAdiabaticWall) {
      ViscousGradients vg;
      const PrimGrad g = grad.getAll(c);
      vg.grad_u = g[kPrimU];
      vg.grad_v = g[kPrimV];
      vg.grad_T = {0.0, 0.0};

      // Correct the wall-normal derivative using the imposed zero wall velocity,
      // which is what makes the wall shear reflect the actual near-wall profile
      // rather than the cell-average gradient.
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
      // Tangential traction only: normal viscous traction is excluded.
      const Vec2 shear = wallShearTraction(mu, vg, n);
      // Force on the body is minus the viscous traction evaluated with the
      // fluid-outward normal (see the Cauchy sign discussion above).  For a flat
      // plate with du/dy > 0 this correctly yields a downstream skin-friction
      // drag rather than thrust.
      dvx = -shear.x * area;
      dvy = -shear.y * area;
      acc[2] += dvx;
      acc[3] += dvy;
    }

    // --- moment about the reference centre (positive counter-clockwise) ----
    const Vec2 r = f.geom.centroid - flow.moment_center;
    const Real fx = dpx + dvx;
    const Real fy = dpy + dvy;
    acc[4] += r.x * fy - r.y * fx;

    acc[5] += area;
  }

  Real global[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  MPI_Allreduce(acc, global, 6, MPI_DOUBLE, MPI_SUM, comm);

  // Nondimensionalise.  Lift/drag are taken along and normal to the freestream
  // direction so a nonzero angle of attack is handled correctly.
  const Real denom = flow.dynamic_pressure * flow.ref_area;
  const Real inv_denom = (denom > 0.0) ? 1.0 / denom : 0.0;
  const Vec2 d = flow.flow_direction;             // drag direction
  const Vec2 l{-flow.flow_direction.y, flow.flow_direction.x};  // lift direction

  ForceResult out;
  out.pressure_drag = (global[0] * d.x + global[1] * d.y) * inv_denom;
  out.pressure_lift = (global[0] * l.x + global[1] * l.y) * inv_denom;
  out.viscous_drag = (global[2] * d.x + global[3] * d.y) * inv_denom;
  out.viscous_lift = (global[2] * l.x + global[3] * l.y) * inv_denom;
  out.cd = out.pressure_drag + out.viscous_drag;
  out.cl = out.pressure_lift + out.viscous_lift;
  out.cmz = global[4] * inv_denom / std::max(flow.ref_length, kTiny);
  out.wall_length = global[5];
  return out;
}

}  // namespace cns2d
