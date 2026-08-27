#include "post/Forces.hpp"

#include <algorithm>

#include "parallel/Comm.hpp"
#include "physics/BoundaryCondition.hpp"

namespace cfd {
namespace {

// Unit surface tangent oriented along the freestream direction, so that a
// positive cf means wall shear acting downstream (attached flow) and a
// negative cf marks reversed / separated flow.
inline Vec2 orientedTangent(const Vec2& n, const Vec2& dhat) {
  Vec2 t{-n[1], n[0]};
  if (dot(t, dhat) < 0.0) { t[0] = -t[0]; t[1] = -t[1]; }
  return t;
}

}  // namespace

ForceReport computeForces(const SpatialOperator& op) {
  const LocalMesh& m = op.mesh();
  const CaseConfig& cfg = op.config();
  const Vec2 vinf = cfg.freestreamVelocity();
  const Real vmag = std::max(norm(vinf), 1e-30);
  const Vec2 dhat{vinf[0] / vmag, vinf[1] / vmag};      // drag direction
  const Vec2 lhat{-dhat[1], dhat[0]};                   // lift direction
  const Real pinf = cfg.freestream.pressure;
  const Real q = cfg.dynamicPressure();
  const Real aref = cfg.reference.area;
  const Real lref = cfg.reference.length;
  const Vec2& xref = cfg.reference.moment_center;

  Real acc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  for (Index b = 0; b < m.numBoundaryFaces(); ++b) {
    const BcType bc = op.patchBc()[m.bface_patch[b]];
    if (!isWall(bc)) continue;
    const Vec2& n = m.bface_normal[b];
    const Real s = m.bface_area[b];
    const Real pw = op.boundaryPrimitive()[b * kNVar + 3];

    const Vec2 fp{(pw - pinf) * n[0] * s, (pw - pinf) * n[1] * s};
    const Vec2 tv{op.boundaryShear()[b * kDim + 0], op.boundaryShear()[b * kDim + 1]};
    const Real tvn = dot(tv, n);
    const Vec2 tv_n{tvn * n[0], tvn * n[1]};
    const Vec2 tv_t{tv[0] - tv_n[0], tv[1] - tv_n[1]};
    const Vec2 fv_t{-tv_t[0] * s, -tv_t[1] * s};
    const Vec2 fv_n{-tv_n[0] * s, -tv_n[1] * s};
    const Vec2 ftot{fp[0] + fv_t[0] + fv_n[0], fp[1] + fv_t[1] + fv_n[1]};

    acc[0] += dot(fp, dhat);
    acc[1] += dot(fp, lhat);
    acc[2] += dot(fv_t, dhat);
    acc[3] += dot(fv_t, lhat);
    acc[4] += dot(fv_n, dhat);
    acc[5] += dot(fv_n, lhat);
    const Vec2 r = m.bface_center[b] - xref;
    acc[6] += cross(r, ftot);
    acc[7] += s;
  }
  globalSumArray(acc, 8, op.comm());

  const Real inv = 1.0 / (q * aref);
  ForceReport f;
  f.pressure_drag = acc[0] * inv;
  f.pressure_lift = acc[1] * inv;
  f.viscous_drag = acc[2] * inv;
  f.viscous_lift = acc[3] * inv;
  f.normal_viscous_drag = acc[4] * inv;
  f.normal_viscous_lift = acc[5] * inv;
  f.cd = f.pressure_drag + f.viscous_drag + f.normal_viscous_drag;
  f.cl = f.pressure_lift + f.viscous_lift + f.normal_viscous_lift;
  f.cmz = acc[6] * inv / lref;
  f.wall_area = acc[7];
  return f;
}

std::vector<SurfaceRow> collectSurfaceRows(const SpatialOperator& op) {
  const LocalMesh& m = op.mesh();
  const CaseConfig& cfg = op.config();
  const PerfectGas& gas = op.gas();
  const Vec2 vinf = cfg.freestreamVelocity();
  const Real vmag = std::max(norm(vinf), 1e-30);
  const Vec2 dhat{vinf[0] / vmag, vinf[1] / vmag};
  const Real pinf = cfg.freestream.pressure;
  const Real q = cfg.dynamicPressure();

  std::vector<SurfaceRow> rows;
  for (Index b = 0; b < m.numBoundaryFaces(); ++b) {
    const BcType bc = op.patchBc()[m.bface_patch[b]];
    if (!isWall(bc)) continue;
    SurfaceRow r;
    const Vec2& n = m.bface_normal[b];
    r.x = m.bface_center[b][0];
    r.y = m.bface_center[b][1];
    r.nx = n[0];
    r.ny = n[1];
    const Real* wb = &op.boundaryPrimitive()[b * kNVar];
    r.rho = wb[0];
    r.u = wb[1];
    r.v = wb[2];
    r.pressure = wb[3];
    r.cp = (wb[3] - pinf) / q;
    const Real a = gas.soundSpeed(std::max(wb[0], 1e-30), std::max(wb[3], 1e-30));
    r.mach = std::sqrt(wb[1] * wb[1] + wb[2] * wb[2]) / a;

    const Vec2 tv{op.boundaryShear()[b * kDim + 0], op.boundaryShear()[b * kDim + 1]};
    const Real tvn = dot(tv, n);
    const Vec2 tv_t{tv[0] - tvn * n[0], tv[1] - tvn * n[1]};
    const Vec2 that = orientedTangent(n, dhat);
    // Traction on the body is -tau.n; its tangential part projected on that.
    r.cf = -dot(tv_t, that) / q;

    const Index c = m.bface_cell[b];
    r.cell_u = op.W()[c * kNVar + 1];
    r.cell_v = op.W()[c * kNVar + 2];
    r.cell_pressure = op.W()[c * kNVar + 3];
    r.patch = m.bface_patch[b];
    r.cell_gid = m.cell_gid[c];
    rows.push_back(r);
  }
  return rows;
}

}  // namespace cfd
