#include "numerics/gradients.h"

#include "physics/boundary_conditions.h"

namespace cns2d {

void computePrimitives(const DistributedMesh &mesh, const FlowContext &ctx, const StateField &U,
                       StateField &W) {
  const Index n = mesh.numLocal();
  for (Index c = 0; c < n; ++c) {
    const PrimVec w = ctx.gas.primFromCons(U.get(c));
    W.set(c, w);
  }
}

void computeGradients(const DistributedMesh &mesh, const FlowContext &ctx, const StateField &W,
                      const StateField &U, GradientField &grad) {
  const Index num_owned = mesh.numOwned();
  const auto &stencils = mesh.gradientStencils();
  const auto &entries = mesh.gradientEntries();
  const auto &faces = mesh.faces();

  for (Index c = 0; c < num_owned; ++c) {
    const DistributedMesh::GradientStencil st = stencils[static_cast<std::size_t>(c)];
    const Real *wc = W.cell(c);

    Real acc[kNumVars][kDim] = {};

    for (Index k = 0; k < st.count; ++k) {
      const DistributedMesh::StencilEntry &e = entries[static_cast<std::size_t>(st.begin + k)];

      PrimVec wn{};
      if (e.kind == DistributedMesh::StencilKind::kCell) {
        const Real *p = W.cell(e.index);
        for (int v = 0; v < kNumVars; ++v) wn[static_cast<std::size_t>(v)] = p[v];
      } else {
        // Boundary face: use the PHYSICAL state on the boundary (not the
        // mirrored ghost state), so a wall contributes its true wall value to
        // the least-squares fit.  Using the mirrored state here would double
        // every wall-normal gradient.
        const LocalFace &f = faces[static_cast<std::size_t>(e.index)];
        const ConsVec ub =
            boundaryFaceState(mesh.boundaryTypeOfTag(f.boundary_tag), U.get(c), f.geom.normal, ctx);
        wn = ctx.gas.primFromCons(ub);
      }

      for (int v = 0; v < kNumVars; ++v) {
        const Real dphi = wn[static_cast<std::size_t>(v)] - wc[v];
        acc[v][0] += e.weight.x * dphi;
        acc[v][1] += e.weight.y * dphi;
      }
    }

    Real *g = grad.cell(c);
    for (int v = 0; v < kNumVars; ++v) {
      g[v * kDim + 0] = acc[v][0];
      g[v * kDim + 1] = acc[v][1];
    }
  }
}

}  // namespace cns2d
