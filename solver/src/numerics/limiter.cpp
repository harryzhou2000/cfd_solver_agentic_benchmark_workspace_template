#include "numerics/limiter.h"

#include <algorithm>
#include <cmath>

#include "core/exceptions.h"
#include "physics/boundary_conditions.h"

namespace cns2d {

std::string limiterName(LimiterType t) {
  switch (t) {
    case LimiterType::kNone:
      return "none";
    case LimiterType::kBarthJespersen:
      return "barth_jespersen";
    case LimiterType::kVenkatakrishnan:
      return "venkatakrishnan";
  }
  return "unknown";
}

LimiterType parseLimiterType(const std::string &name) {
  if (name == "none") return LimiterType::kNone;
  if (name == "barth_jespersen" || name == "barth") return LimiterType::kBarthJespersen;
  if (name == "venkatakrishnan" || name == "venkat") return LimiterType::kVenkatakrishnan;
  throw CnsError("unknown limiter '" + name +
                 "' (supported: barth_jespersen, venkatakrishnan, none)");
}

void computeLimiter(const DistributedMesh &mesh, const FlowContext &ctx, LimiterType type,
                    const StateField &W, const GradientField &grad, Real venkat_k,
                    LimiterField &phi) {
  const Index num_owned = mesh.numOwned();
  const auto &faces = mesh.faces();
  const auto &cells = mesh.cells();
  const auto &ranges = mesh.cellFaceRanges();
  const auto &cell_faces = mesh.cellFaces();

  if (type == LimiterType::kNone) {
    phi.fill(1.0);
    return;
  }

  for (Index c = 0; c < num_owned; ++c) {
    const Real *wc = W.cell(c);
    const Real *gc = grad.cell(c);
    Real *pc = phi.cell(c);

    // Neighbour min/max per variable, including boundary-face states so a
    // wall-adjacent cell is not falsely flagged as an extremum.
    Real wmin[kNumVars];
    Real wmax[kNumVars];
    for (int v = 0; v < kNumVars; ++v) {
      wmin[v] = wc[v];
      wmax[v] = wc[v];
    }

    const DistributedMesh::CellFaceRange range = ranges[static_cast<std::size_t>(c)];
    for (Index k = 0; k < range.count; ++k) {
      const Index face_id = cell_faces[static_cast<std::size_t>(range.begin + k)];
      const LocalFace &f = faces[static_cast<std::size_t>(face_id)];
      PrimVec wn{};
      if (f.kind == FaceKind::kBoundary) {
        // Physical boundary state, so the min/max envelope reflects the real
        // wall value rather than an artificial mirrored extremum.
        const ConsVec ub = boundaryFaceState(mesh.boundaryTypeOfTag(f.boundary_tag),
                                             ctx.gas.consFromPrim({wc[0], wc[1], wc[2], wc[3]}),
                                             f.geom.normal, ctx);
        wn = ctx.gas.primFromCons(ub);
      } else {
        const Index other = (f.left == c) ? f.right : f.left;
        const Real *p = W.cell(other);
        for (int v = 0; v < kNumVars; ++v) wn[static_cast<std::size_t>(v)] = p[v];
      }
      for (int v = 0; v < kNumVars; ++v) {
        wmin[v] = std::min(wmin[v], wn[static_cast<std::size_t>(v)]);
        wmax[v] = std::max(wmax[v], wn[static_cast<std::size_t>(v)]);
      }
    }

    // Venkatakrishnan smoothing threshold: eps^2 = (K h)^3, with h the cell
    // length scale.  Below this amplitude the limiter is switched off, which is
    // what removes limiter chattering in smooth regions and lets the steady
    // residual keep converging.
    const Real h = std::sqrt(cells[static_cast<std::size_t>(c)].volume);
    const Real eps2 = (type == LimiterType::kVenkatakrishnan)
                          ? std::pow(std::max(venkat_k, 0.0) * h, 3.0)
                          : 0.0;

    for (int v = 0; v < kNumVars; ++v) pc[v] = 1.0;

    for (Index k = 0; k < range.count; ++k) {
      const Index face_id = cell_faces[static_cast<std::size_t>(range.begin + k)];
      const LocalFace &f = faces[static_cast<std::size_t>(face_id)];
      const Vec2 delta = f.geom.centroid - cells[static_cast<std::size_t>(c)].centroid;

      for (int v = 0; v < kNumVars; ++v) {
        // Unlimited extrapolation increment towards this face.
        const Real d = gc[v * kDim + 0] * delta.x + gc[v * kDim + 1] * delta.y;
        if (std::abs(d) < 1.0e-14 * (std::abs(wc[v]) + 1.0e-14)) continue;

        const Real dmax = wmax[v] - wc[v];
        const Real dmin = wmin[v] - wc[v];
        const Real bound = (d > 0.0) ? dmax : dmin;

        Real factor;
        if (type == LimiterType::kBarthJespersen) {
          // phi = min(1, bound / d): the largest factor that keeps the face
          // value inside the neighbour min/max envelope.
          factor = std::min(1.0, bound / d);
        } else {
          // Venkatakrishnan: smooth rational function of y = bound / d that
          // tends to 1 for smooth data and to the Barth-Jespersen value at
          // sharp extrema.
          const Real y = bound / d;
          const Real d2 = d * d;
          factor = (y * y + 2.0 * y + eps2 / std::max(d2, kTiny)) /
                   (y * y + y + 2.0 + eps2 / std::max(d2, kTiny));
          factor = std::min(1.0, std::max(0.0, factor));
        }
        pc[v] = std::min(pc[v], std::max(0.0, factor));
      }
    }
  }
}

bool reconstructPrimitive(const Real *w_cell, const Real *grad_cell, const Real *phi_cell,
                          Vec2 delta, PrimVec &out) {
  for (int v = 0; v < kNumVars; ++v) {
    const Real d = grad_cell[v * kDim + 0] * delta.x + grad_cell[v * kDim + 1] * delta.y;
    out[static_cast<std::size_t>(v)] = w_cell[v] + phi_cell[v] * d;
  }

  // Positivity fallback.  If the limited reconstruction still produces a
  // non-positive density or pressure, progressively shrink the whole increment
  // towards the cell average; the first-order state is always admissible, so
  // this terminates.  Reporting the fallback lets the run log and metadata state
  // honestly how often first order was used.
  if (out[kPrimRho] > 0.0 && out[kPrimP] > 0.0) return true;

  Real scale = 0.5;
  for (int attempt = 0; attempt < 8; ++attempt) {
    for (int v = 0; v < kNumVars; ++v) {
      const Real d = grad_cell[v * kDim + 0] * delta.x + grad_cell[v * kDim + 1] * delta.y;
      out[static_cast<std::size_t>(v)] = w_cell[v] + scale * phi_cell[v] * d;
    }
    if (out[kPrimRho] > 0.0 && out[kPrimP] > 0.0) return false;
    scale *= 0.5;
  }
  // Full first-order fallback.
  for (int v = 0; v < kNumVars; ++v) out[static_cast<std::size_t>(v)] = w_cell[v];
  return false;
}

}  // namespace cns2d
