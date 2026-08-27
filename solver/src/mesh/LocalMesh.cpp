#include "mesh/LocalMesh.hpp"

#include <algorithm>
#include <unordered_map>

#include "core/Exception.hpp"
#include "mesh/Geometry.hpp"

namespace cfd {
namespace {
inline std::uint64_t edgeKey(Index a, Index b) {
  const std::uint64_t lo = static_cast<std::uint64_t>(std::min(a, b));
  const std::uint64_t hi = static_cast<std::uint64_t>(std::max(a, b));
  return (hi << 32) | lo;
}
}  // namespace

void LocalMesh::buildTopologyAndGeometry(const std::vector<std::array<Index, 3>>& boundary_edges) {
  const Index nt = numTotalCells();
  CFD_CHECK(nt > 0, "rank-local mesh has no cells (try fewer MPI ranks)");

  // ---- cell geometry (owned + ghost) ----
  cell_volume.assign(nt, 0.0);
  cell_center.assign(nt, Vec2{0.0, 0.0});
  for (Index c = 0; c < nt; ++c) {
    const int n = cellSize(c);
    Index* nodes = &cell_nodes[cell_node_offset[c]];
    Real a = geom::signedArea(x.data(), y.data(), nodes, n);
    if (a < 0.0) {  // keep every polygon counter-clockwise
      std::reverse(nodes, nodes + n);
      a = -a;
    }
    CFD_CHECK(a > 0.0, "degenerate local cell " << c << " (zero area)");
    cell_volume[c] = a;
    cell_center[c] = geom::centroid(x.data(), y.data(), nodes, n);
  }

  // ---- boundary edge lookup ----
  std::unordered_map<std::uint64_t, Index> patch_of_edge;
  patch_of_edge.reserve(boundary_edges.size() * 2 + 8);
  for (const auto& be : boundary_edges) patch_of_edge[edgeKey(be[0], be[1])] = be[2];

  // ---- rebuild faces from the local cell list ----
  struct FaceRec { Index n0, n1, cl, cr; };
  std::unordered_map<std::uint64_t, Index> face_of_edge;
  face_of_edge.reserve(2 * cell_nodes.size());
  std::vector<FaceRec> recs;
  recs.reserve(2 * nt);
  for (Index c = 0; c < nt; ++c) {
    const int n = cellSize(c);
    const Index* nodes = cellNodePtr(c);
    for (int i = 0; i < n; ++i) {
      const Index a = nodes[i], b = nodes[(i + 1) % n];
      const std::uint64_t k = edgeKey(a, b);
      auto it = face_of_edge.find(k);
      if (it == face_of_edge.end()) {
        face_of_edge.emplace(k, static_cast<Index>(recs.size()));
        recs.push_back({a, b, c, -1});
      } else {
        CFD_CHECK(recs[it->second].cr < 0, "local edge shared by more than two cells");
        recs[it->second].cr = c;
      }
    }
  }

  face_l.clear(); face_r.clear(); face_normal.clear(); face_area.clear(); face_center.clear();
  bface_cell.clear(); bface_patch.clear(); bface_normal.clear(); bface_area.clear();
  bface_center.clear(); bface_n0.clear(); bface_n1.clear();

  for (const auto& r : recs) {
    const bool l_owned = r.cl < num_owned;
    if (r.cr >= 0) {
      const bool r_owned = r.cr < num_owned;
      if (!l_owned && !r_owned) continue;   // ghost-ghost face: not needed here
      face_l.push_back(r.cl);
      face_r.push_back(r.cr);
      face_normal.push_back(geom::edgeNormal(x[r.n0], y[r.n0], x[r.n1], y[r.n1]));
      face_area.push_back(geom::edgeLength(x[r.n0], y[r.n0], x[r.n1], y[r.n1]));
      face_center.push_back({0.5 * (x[r.n0] + x[r.n1]), 0.5 * (y[r.n0] + y[r.n1])});
    } else {
      if (!l_owned) continue;               // dangling ghost edge: owned by another rank
      auto it = patch_of_edge.find(edgeKey(r.n0, r.n1));
      CFD_CHECK(it != patch_of_edge.end(),
                "rank-local mesh has an untagged boundary edge on owned cell " << r.cl);
      bface_cell.push_back(r.cl);
      bface_patch.push_back(it->second);
      bface_normal.push_back(geom::edgeNormal(x[r.n0], y[r.n0], x[r.n1], y[r.n1]));
      bface_area.push_back(geom::edgeLength(x[r.n0], y[r.n0], x[r.n1], y[r.n1]));
      bface_center.push_back({0.5 * (x[r.n0] + x[r.n1]), 0.5 * (y[r.n0] + y[r.n1])});
      bface_n0.push_back(r.n0);
      bface_n1.push_back(r.n1);
    }
  }

  // Face normals come from the counter-clockwise edge traversal of the "left"
  // cell, so by construction they point out of cell_l (i.e. from l towards r)
  // and out of the domain on boundary faces.  checkGeometry() verifies this
  // against the divergence theorem.
}

Real LocalMesh::checkGeometry() const {
  // Divergence-theorem check: for every owned cell,
  //     V = 1/2 * sum_f (x_f . n_f) S_f
  // holds exactly for a closed polygon whose face normals all point outward.
  // Any missing face, wrong normal orientation or wrong face length shows up
  // as a relative volume error.
  // The face centroids are taken relative to the cell centre so that the
  // check is not polluted by cancellation when the mesh is far from the
  // origin (the farfield of these meshes reaches |x| ~ 200).
  std::vector<Real> vcheck(static_cast<std::size_t>(num_owned), 0.0);
  auto accumulate = [&](Index c, const Vec2& xf, const Vec2& n, Real area, Real sign) {
    if (c < 0 || c >= num_owned) return;
    vcheck[c] += sign * 0.5 * dot(xf - cell_center[c], n) * area;
  };
  for (Index f = 0; f < numFaces(); ++f) {
    accumulate(face_l[f], face_center[f], face_normal[f], face_area[f], +1.0);
    accumulate(face_r[f], face_center[f], face_normal[f], face_area[f], -1.0);
  }
  for (Index b = 0; b < numBoundaryFaces(); ++b) {
    accumulate(bface_cell[b], bface_center[b], bface_normal[b], bface_area[b], +1.0);
  }
  Real worst = 0.0;
  for (Index c = 0; c < num_owned; ++c) {
    worst = std::max(worst, std::abs(vcheck[c] - cell_volume[c]) / cell_volume[c]);
  }
  return worst;
}

}  // namespace cfd
