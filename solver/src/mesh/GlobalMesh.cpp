#include "mesh/GlobalMesh.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>

#include "core/Exception.hpp"
#include "mesh/Geometry.hpp"

namespace cfd {
namespace {

// 64-bit key for an undirected edge between two node indices.
inline std::uint64_t edgeKey(Index a, Index b) {
  const std::uint64_t lo = static_cast<std::uint64_t>(std::min(a, b));
  const std::uint64_t hi = static_cast<std::uint64_t>(std::max(a, b));
  return (hi << 32) | lo;
}

}  // namespace

void GlobalMesh::orientCells() {
  const Index nc = numCells();
  Index flipped = 0;
  for (Index c = 0; c < nc; ++c) {
    const int n = cellSize(c);
    Index* nodes = &cell_nodes[cell_node_offset[c]];
    const Real a = geom::signedArea(x.data(), y.data(), nodes, n);
    CFD_CHECK(std::abs(a) > 0.0, "degenerate cell " << c << " with zero area");
    if (a < 0.0) {
      std::reverse(nodes, nodes + n);
      ++flipped;
    }
  }
  (void)flipped;
}

void GlobalMesh::buildTopology(
    const std::vector<std::vector<std::pair<Index, Index>>>& patch_edges) {
  const Index nc = numCells();
  CFD_CHECK(nc > 0, "mesh contains no cells");
  CFD_CHECK(patch_edges.size() == patches.size(), "internal: patch/edge list size mismatch");

  // Map boundary edges to their patch index.
  std::unordered_map<std::uint64_t, Index> boundary_patch_of;
  boundary_patch_of.reserve(1024);
  for (std::size_t p = 0; p < patch_edges.size(); ++p) {
    for (const auto& e : patch_edges[p]) {
      const std::uint64_t k = edgeKey(e.first, e.second);
      auto it = boundary_patch_of.find(k);
      CFD_CHECK(it == boundary_patch_of.end() || it->second == static_cast<Index>(p),
                "boundary edge (" << e.first << "," << e.second
                << ") is claimed by two different boundary patches ('"
                << patches[it->second].name << "' and '" << patches[p].name << "')");
      boundary_patch_of[k] = static_cast<Index>(p);
    }
  }

  // Collect faces from cell edges.
  std::unordered_map<std::uint64_t, Index> face_of_edge;
  face_of_edge.reserve(static_cast<std::size_t>(2 * cell_nodes.size()));
  face_n0.clear(); face_n1.clear(); face_cell_l.clear(); face_cell_r.clear(); face_patch.clear();
  face_n0.reserve(2 * nc); face_n1.reserve(2 * nc);
  face_cell_l.reserve(2 * nc); face_cell_r.reserve(2 * nc); face_patch.reserve(2 * nc);

  for (Index c = 0; c < nc; ++c) {
    const int n = cellSize(c);
    const Index* nodes = cellNodePtr(c);
    for (int i = 0; i < n; ++i) {
      const Index a = nodes[i];
      const Index b = nodes[(i + 1) % n];
      const std::uint64_t k = edgeKey(a, b);
      auto it = face_of_edge.find(k);
      if (it == face_of_edge.end()) {
        const Index f = static_cast<Index>(face_n0.size());
        face_of_edge.emplace(k, f);
        face_n0.push_back(a);
        face_n1.push_back(b);
        face_cell_l.push_back(c);
        face_cell_r.push_back(-1);
        face_patch.push_back(-1);
      } else {
        const Index f = it->second;
        CFD_CHECK(face_cell_r[f] < 0,
                  "edge (" << a << "," << b << ") is shared by more than two cells; "
                  "the mesh is not a valid 2-D manifold");
        face_cell_r[f] = c;
      }
    }
  }

  // Tag boundary faces and check every one is covered by a declared patch.
  Index untagged = 0;
  Index first_untagged = -1;
  for (Index f = 0; f < numFaces(); ++f) {
    if (face_cell_r[f] >= 0) continue;
    auto it = boundary_patch_of.find(edgeKey(face_n0[f], face_n1[f]));
    if (it == boundary_patch_of.end()) {
      ++untagged;
      if (first_untagged < 0) first_untagged = f;
      continue;
    }
    face_patch[f] = it->second;
    patches[it->second].num_faces++;
  }
  if (untagged > 0) {
    const Index f = first_untagged;
    CFD_THROW(untagged << " boundary face(s) are not covered by any CGNS boundary condition; "
              << "first at nodes (" << face_n0[f] << "," << face_n1[f] << ") = ("
              << x[face_n0[f]] << "," << y[face_n0[f]] << ")-(" << x[face_n1[f]] << ","
              << y[face_n1[f]] << "). If the mesh has multiple zones this usually means an "
              << "inter-zone 1-to-1 connection was not merged.");
  }

  // Cell adjacency graph over interior faces (CSR).
  std::vector<Index> counts(nc, 0);
  for (Index f = 0; f < numFaces(); ++f) {
    if (face_cell_r[f] < 0) continue;
    counts[face_cell_l[f]]++;
    counts[face_cell_r[f]]++;
  }
  adj_offset.assign(nc + 1, 0);
  for (Index c = 0; c < nc; ++c) adj_offset[c + 1] = adj_offset[c] + counts[c];
  adj_cells.assign(adj_offset[nc], -1);
  adj_faces.assign(adj_offset[nc], -1);
  std::vector<Index> fill(adj_offset.begin(), adj_offset.end() - 1);
  for (Index f = 0; f < numFaces(); ++f) {
    if (face_cell_r[f] < 0) continue;
    const Index l = face_cell_l[f], r = face_cell_r[f];
    adj_cells[fill[l]] = r; adj_faces[fill[l]] = f; fill[l]++;
    adj_cells[fill[r]] = l; adj_faces[fill[r]] = f; fill[r]++;
  }
}

void GlobalMesh::release() {
  std::vector<Real>().swap(x);
  std::vector<Real>().swap(y);
  std::vector<Index>().swap(cell_node_offset);
  std::vector<Index>().swap(cell_nodes);
  std::vector<std::int8_t>().swap(cell_zone);
  std::vector<Index>().swap(face_n0);
  std::vector<Index>().swap(face_n1);
  std::vector<Index>().swap(face_cell_l);
  std::vector<Index>().swap(face_cell_r);
  std::vector<Index>().swap(face_patch);
  std::vector<Index>().swap(adj_offset);
  std::vector<Index>().swap(adj_cells);
  std::vector<Index>().swap(adj_faces);
}

std::string GlobalMesh::summary() const {
  Index nbound = 0;
  for (Index f = 0; f < numFaces(); ++f) if (face_cell_r[f] < 0) ++nbound;
  std::ostringstream os;
  os << "global mesh: " << numNodes() << " nodes, " << numCells() << " cells, "
     << numFaces() << " faces (" << nbound << " boundary)";
  for (const auto& p : patches) {
    os << "\n  patch '" << p.name << "' (CGNS BC '" << p.bc_name << "'): "
       << p.num_faces << " faces";
  }
  return os.str();
}

}  // namespace cfd
