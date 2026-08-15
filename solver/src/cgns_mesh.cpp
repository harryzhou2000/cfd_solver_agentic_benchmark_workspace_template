#include "cfd/mesh.hpp"

#include <cgnslib.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
#include <unordered_map>

namespace cfd {

namespace {

void check_cgns(int ier, const std::string& what) {
  if (ier != CG_OK) throw CfdError(what + ": " + cg_get_error());
}

bool is_volume_type(ElementType_t type) {
  return type == TRI_3 || type == QUAD_4;
}

bool is_bar_type(ElementType_t type) {
  return type == BAR_2;
}

struct NodeKey {
  long long ix{0};
  long long iy{0};

  bool operator==(const NodeKey& rhs) const { return ix == rhs.ix && iy == rhs.iy; }
};

struct NodeKeyHash {
  std::size_t operator()(const NodeKey& k) const {
    const std::uint64_t a = static_cast<std::uint64_t>(k.ix);
    const std::uint64_t b = static_cast<std::uint64_t>(k.iy);
    return static_cast<std::size_t>((a * 11400714819323198485ull) ^ (b + 0x9e3779b97f4a7c15ull));
  }
};

NodeKey make_key(Real x, Real y) {
  constexpr Real eps = 1.0e-12;
  return {static_cast<long long>(std::llround(x / eps)),
          static_cast<long long>(std::llround(y / eps))};
}

struct BoundaryEdge {
  int v0{-1};
  int v1{-1};
  std::string tag;
};

uint64_t edge_key(int a, int b) {
  const uint32_t lo = static_cast<uint32_t>(std::min(a, b));
  const uint32_t hi = static_cast<uint32_t>(std::max(a, b));
  return (static_cast<uint64_t>(lo) << 32u) | hi;
}

}  // namespace

GlobalMesh read_cgns_mesh(const std::string& file_name) {
  GlobalMesh mesh;
  std::vector<BoundaryEdge> boundary_edges;
  std::unordered_map<NodeKey, int, NodeKeyHash> node_lookup;

  int fn = 0;
  check_cgns(cg_open(file_name.c_str(), CG_MODE_READ, &fn), "cg_open " + file_name);
  int nbases = 0;
  check_cgns(cg_nbases(fn, &nbases), "cg_nbases");
  if (nbases < 1) throw CfdError("CGNS file has no bases: " + file_name);

  for (int ib = 1; ib <= nbases; ++ib) {
    char basename[33]{};
    int celldim = 0;
    int physdim = 0;
    check_cgns(cg_base_read(fn, ib, basename, &celldim, &physdim), "cg_base_read");
    if (celldim != 2) throw CfdError("expected 2-D CGNS base in " + file_name);

    int nzones = 0;
    check_cgns(cg_nzones(fn, ib, &nzones), "cg_nzones");
    for (int iz = 1; iz <= nzones; ++iz) {
      char zonename[33]{};
      cgsize_t size[9]{};
      check_cgns(cg_zone_read(fn, ib, iz, zonename, size), "cg_zone_read");
      ZoneType_t ztype{};
      check_cgns(cg_zone_type(fn, ib, iz, &ztype), "cg_zone_type");
      if (ztype != Unstructured) {
        throw CfdError("only unstructured CGNS zones are supported; zone " +
                       std::string(zonename));
      }
      const cgsize_t nverts = size[0];
      if (nverts <= 0) throw CfdError("zone has no vertices: " + std::string(zonename));

      std::vector<Real> x(static_cast<std::size_t>(nverts));
      std::vector<Real> y(static_cast<std::size_t>(nverts));
      cgsize_t rmin[3]{1, 1, 1};
      cgsize_t rmax[3]{nverts, 1, 1};
      check_cgns(cg_coord_read(fn, ib, iz, "CoordinateX", RealDouble, rmin, rmax, x.data()),
                 "cg_coord_read CoordinateX");
      check_cgns(cg_coord_read(fn, ib, iz, "CoordinateY", RealDouble, rmin, rmax, y.data()),
                 "cg_coord_read CoordinateY");

      std::vector<int> zone_to_global(static_cast<std::size_t>(nverts) + 1, -1);
      for (cgsize_t i = 1; i <= nverts; ++i) {
        const Real xx = x[static_cast<std::size_t>(i - 1)];
        const Real yy = y[static_cast<std::size_t>(i - 1)];
        const NodeKey key = make_key(xx, yy);
        auto it = node_lookup.find(key);
        if (it == node_lookup.end()) {
          const int gid = static_cast<int>(mesh.vertices.size());
          mesh.vertices.push_back({xx, yy});
          node_lookup.emplace(key, gid);
          zone_to_global[static_cast<std::size_t>(i)] = gid;
        } else {
          zone_to_global[static_cast<std::size_t>(i)] = it->second;
        }
      }

      int nsections = 0;
      check_cgns(cg_nsections(fn, ib, iz, &nsections), "cg_nsections");
      for (int is = 1; is <= nsections; ++is) {
        char sname[33]{};
        ElementType_t etype{};
        cgsize_t start = 0;
        cgsize_t end = 0;
        int nbndry = 0;
        int parent_flag = 0;
        check_cgns(cg_section_read(fn, ib, iz, is, sname, &etype, &start, &end, &nbndry,
                                   &parent_flag),
                   "cg_section_read");
        if (!is_volume_type(etype) && !is_bar_type(etype)) continue;

        cgsize_t data_size = 0;
        check_cgns(cg_ElementDataSize(fn, ib, iz, is, &data_size), "cg_ElementDataSize");
        std::vector<cgsize_t> conn(static_cast<std::size_t>(data_size));
        check_cgns(cg_elements_read(fn, ib, iz, is, conn.data(), nullptr), "cg_elements_read");

        int npe = 0;
        check_cgns(cg_npe(etype, &npe), "cg_npe");
        const int count = static_cast<int>(end - start + 1);
        if (static_cast<cgsize_t>(count * npe) != data_size) {
          throw CfdError("unexpected mixed/variable-size section in " + std::string(sname));
        }
        for (int e = 0; e < count; ++e) {
          std::vector<int> verts;
          verts.reserve(npe);
          for (int j = 0; j < npe; ++j) {
            const cgsize_t local_node = conn[static_cast<std::size_t>(e * npe + j)];
            if (local_node <= 0 || local_node > nverts) {
              throw CfdError("element node id out of range in " + std::string(sname));
            }
            verts.push_back(zone_to_global[static_cast<std::size_t>(local_node)]);
          }
          if (is_volume_type(etype)) {
            Cell c;
            c.global_id = static_cast<int>(mesh.cells.size());
            c.vertices = std::move(verts);
            mesh.cells.push_back(std::move(c));
          } else if (verts.size() == 2) {
            boundary_edges.push_back({verts[0], verts[1], sname});
          }
        }
      }
    }
  }

  check_cgns(cg_close(fn), "cg_close");
  build_geometry(mesh);

  std::unordered_map<uint64_t, std::string> edge_tags;
  for (const BoundaryEdge& be : boundary_edges) {
    edge_tags[edge_key(be.v0, be.v1)] = be.tag;
  }
  for (Face& f : mesh.faces) {
    if (f.right_cell >= 0) continue;
    const auto it = edge_tags.find(edge_key(f.v0, f.v1));
    f.tag = (it == edge_tags.end()) ? "UNMARKED" : it->second;
    mesh.boundary_face_counts[f.tag] += 1;
  }

  return mesh;
}

}  // namespace cfd
