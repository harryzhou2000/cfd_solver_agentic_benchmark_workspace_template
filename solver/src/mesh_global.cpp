#include "mesh_global.hpp"

#include <cgnslib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace cfd {
namespace {

struct EdgeKey {
  int a, b;  // sorted node ids
  bool operator<(const EdgeKey& o) const {
    return (a < o.a) || (a == o.a && b < o.b);
  }
};

// CGNS 4.x uses enums; map element types to nodes-per-element.
int nodes_per_element(int elem_type) {
  switch (elem_type) {
    case BAR_2: return 2;
    case TRI_3: return 3;
    case QUAD_4: return 4;
    default: return -1;
  }
}

void check_cgns(int ierr, const char* what) {
  if (ierr != CG_OK) {
    throw std::runtime_error(std::string("CGNS error (code ") +
                             std::to_string(ierr) + ") in " + what);
  }
}

// Map of rounded coordinates -> global node id (merges nodes across zones).
struct NodeMerger {
  std::map<std::pair<int64_t, int64_t>, int> map;
  std::vector<double> x, y;

  int add(double px, double py) {
    const int64_t qx = static_cast<int64_t>(std::llround(px * 1e9));
    const int64_t qy = static_cast<int64_t>(std::llround(py * 1e9));
    auto key = std::make_pair(qx, qy);
    auto it = map.find(key);
    if (it != map.end()) return it->second;
    int id = static_cast<int>(x.size());
    map[key] = id;
    x.push_back(px);
    y.push_back(py);
    return id;
  }
};

struct RawCell {
  std::vector<int> nodes;
  int zone;
  int64_t element_index;
};

struct RawBoundaryElem {
  int n0, n1;
  int64_t element_index;
  int zone;
};

}  // namespace

GlobalMesh read_cgns_mesh(const std::string& path, const Case& c) {
  int fn;
  check_cgns(cg_open(path.c_str(), CG_MODE_READ, &fn), "cg_open");

  GlobalMesh m;
  NodeMerger merger;
  std::vector<RawCell> raw_cells;
  std::vector<RawBoundaryElem> raw_boundary;
  // element index -> boco/family name (from boco point ranges)
  std::map<int64_t, std::string> elem_bc_name;

  int nbases = 0;
  check_cgns(cg_nbases(fn, &nbases), "cg_nbases");
  if (nbases < 1) throw std::runtime_error("mesh has no CGNS base");

  for (int b = 1; b <= nbases; ++b) {
    char bname[33] = {0};
    int cell_dim = 0, phys_dim = 0;
    check_cgns(cg_base_read(fn, b, bname, &cell_dim, &phys_dim), "cg_base_read");
    if (cell_dim != 2)
      throw std::runtime_error("unsupported CGNS cell dimension " +
                               std::to_string(cell_dim) + " (2-D required)");

    int nzones = 0;
    check_cgns(cg_nzones(fn, b, &nzones), "cg_nzones");
    if (nzones < 1) throw std::runtime_error("mesh base has no zones");

    for (int z = 1; z <= nzones; ++z) {
      char zname[33] = {0};
      cgsize_t sizes[9] = {0};
      check_cgns(cg_zone_read(fn, b, z, zname, sizes), "cg_zone_read");
      const cgsize_t n_nodes = sizes[0];
      const cgsize_t n_cells = sizes[1];

      // Nodes
      std::vector<double> xs(n_nodes), ys(n_nodes);
      cgsize_t rmin[1] = {1};
      cgsize_t rmax[1] = {n_nodes};
      check_cgns(cg_coord_read(fn, b, z, "CoordinateX", RealDouble, rmin, rmax,
                               xs.data()),
                 "cg_coord_read X");
      check_cgns(cg_coord_read(fn, b, z, "CoordinateY", RealDouble, rmin, rmax,
                               ys.data()),
                 "cg_coord_read Y");
      std::vector<int> zone_node_map(n_nodes, -1);
      for (cgsize_t i = 0; i < n_nodes; ++i) {
        zone_node_map[i] = merger.add(xs[i], ys[i]);
      }

      int nsections = 0;
      check_cgns(cg_nsections(fn, b, z, &nsections), "cg_nsections");

      for (int s = 1; s <= nsections; ++s) {
        char sname[33] = {0};
        CGNS_ENUMT(ElementType_t) etype;
        cgsize_t rstart = 0, rend = 0;
        int nbndry = 0, parent_flag = 0;
        check_cgns(cg_section_read(fn, b, z, s, sname, &etype, &rstart, &rend,
                                   &nbndry, &parent_flag),
                   "cg_section_read");
        const cgsize_t nelem = rend - rstart + 1;
        const cgsize_t npe = (etype == MIXED) ? 0 : nodes_per_element(etype);
        const bool is_boundary_section = (etype == BAR_2);

        if (etype != MIXED && npe < 0 && !is_boundary_section)
          throw std::runtime_error("unsupported CGNS element type " +
                                   std::to_string(static_cast<int>(etype)) +
                                   " in section " + sname);

        if (is_boundary_section) {
          std::vector<cgsize_t> conn(nelem * 2);
          check_cgns(cg_elements_read(fn, b, z, s, conn.data(), nullptr),
                     "cg_elements_read (boundary)");
          for (cgsize_t e = 0; e < nelem; ++e) {
            RawBoundaryElem be;
            be.n0 = zone_node_map[conn[2 * e] - 1];
            be.n1 = zone_node_map[conn[2 * e + 1] - 1];
            be.element_index = rstart + e;
            be.zone = z;
            raw_boundary.push_back(be);
          }
          continue;
        }

        // Cell section (TRI_3 / QUAD_4 / MIXED)
        std::vector<cgsize_t> conn;
        if (etype == MIXED) {
          // count total integers: per element 1 type tag + nodes
          cgsize_t total = 0;
          std::vector<cgsize_t> tags(nelem);
          // Read element type tags: for MIXED the connectivity stream starts
          // with the element type of each element.
          std::vector<cgsize_t> tmp(nelem);
          check_cgns(cg_elements_read(fn, b, z, s, tmp.data(), nullptr),
                     "cg_elements_read (mixed tags)");
          for (cgsize_t e = 0; e < nelem; ++e) {
            tags[e] = tmp[e];
            int np = nodes_per_element(static_cast<int>(tags[e]));
            if (np < 0)
              throw std::runtime_error("unsupported element type in MIXED section");
            total += 1 + np;
          }
          conn.resize(total);
          check_cgns(cg_elements_read(fn, b, z, s, conn.data(), nullptr),
                     "cg_elements_read (mixed)");
          cgsize_t pos = 0;
          for (cgsize_t e = 0; e < nelem; ++e) {
            int np = nodes_per_element(static_cast<int>(conn[pos++]));
            RawCell rc;
            rc.zone = z;
            rc.element_index = rstart + e;
            for (int k = 0; k < np; ++k) {
              rc.nodes.push_back(zone_node_map[conn[pos++] - 1]);
            }
            raw_cells.push_back(std::move(rc));
          }
        } else {
          conn.resize(nelem * npe);
          check_cgns(cg_elements_read(fn, b, z, s, conn.data(), nullptr),
                     "cg_elements_read");
          for (cgsize_t e = 0; e < nelem; ++e) {
            RawCell rc;
            rc.zone = z;
            rc.element_index = rstart + e;
            for (cgsize_t k = 0; k < npe; ++k) {
              rc.nodes.push_back(zone_node_map[conn[e * npe + k] - 1]);
            }
            raw_cells.push_back(std::move(rc));
          }
        }
      }

      // Boundary condition point ranges -> boundary element names.
      int nbocos = 0;
      check_cgns(cg_nbocos(fn, b, z, &nbocos), "cg_nbocos");
      for (int bc = 1; bc <= nbocos; ++bc) {
        char bcname[33] = {0};
        CGNS_ENUMT(BCType_t) bctype;
        CGNS_ENUMT(PointSetType_t) pstype;
        cgsize_t npnts = 0;
        int normal_index = 0;
        cgsize_t normal_list_size = 0;
        CGNS_ENUMT(DataType_t) ndata;
        int ndataset = 0;
        check_cgns(cg_boco_info(fn, b, z, bc, bcname, &bctype, &pstype, &npnts,
                                &normal_index, &normal_list_size, &ndata,
                                &ndataset),
                   "cg_boco_info");
        if (pstype == PointRange && npnts == 2) {
          cgsize_t range[2] = {0};
          check_cgns(cg_boco_read(fn, b, z, bc, range, nullptr), "cg_boco_read");
          for (int64_t ei = range[0]; ei <= range[1]; ++ei)
            elem_bc_name[ei] = bcname;
        } else if (pstype == PointList) {
          std::vector<cgsize_t> plist(npnts);
          check_cgns(cg_boco_read(fn, b, z, bc, plist.data(), nullptr),
                     "cg_boco_read (point list)");
          for (cgsize_t k = 0; k < npnts; ++k)
            elem_bc_name[plist[k]] = bcname;
        } else {
          throw std::runtime_error("unsupported CGNS BC point-set type");
        }
      }
    }
  }

  check_cgns(cg_close(fn), "cg_close");

  if (raw_cells.empty())
    throw std::runtime_error("mesh contains no cells");

  // Build global cells.
  m.node_x = std::move(merger.x);
  m.node_y = std::move(merger.y);
  m.cells.resize(raw_cells.size());
  for (size_t i = 0; i < raw_cells.size(); ++i) {
    auto& cell = m.cells[i];
    cell.nodes = raw_cells[i].nodes;
    cell.zone = raw_cells[i].zone;
    // Area and centroid via the shoelace formula.
    const int npe = static_cast<int>(cell.nodes.size());
    double twice_area = 0.0, cx = 0.0, cy = 0.0;
    for (int k = 0; k < npe; ++k) {
      const int a = cell.nodes[k];
      const int b = cell.nodes[(k + 1) % npe];
      const double cross = m.node_x[a] * m.node_y[b] - m.node_y[a] * m.node_x[b];
      twice_area += cross;
      cx += (m.node_x[a] + m.node_x[b]) * cross;
      cy += (m.node_y[a] + m.node_y[b]) * cross;
    }
    if (std::abs(twice_area) < 1e-30)
      throw std::runtime_error("degenerate cell " + std::to_string(i));
    cell.vol = 0.5 * std::abs(twice_area);
    cx /= (3.0 * twice_area);
    cy /= (3.0 * twice_area);
    cell.cx = cx;
    cell.cy = cy;
  }

  // Build edges -> faces.
  std::map<EdgeKey, std::vector<int>> edge_cells;
  for (size_t ci = 0; ci < m.cells.size(); ++ci) {
    const auto& cell = m.cells[ci];
    const int npe = static_cast<int>(cell.nodes.size());
    for (int k = 0; k < npe; ++k) {
      int a = cell.nodes[k];
      int b = cell.nodes[(k + 1) % npe];
      if (a > b) std::swap(a, b);
      edge_cells[{a, b}].push_back(static_cast<int>(ci));
    }
  }

  // Boundary element lookup: (sorted node pair) -> boco name.
  std::map<EdgeKey, std::string> boundary_names;
  for (const auto& be : raw_boundary) {
    EdgeKey key{be.n0, be.n1};
    if (key.a > key.b) std::swap(key.a, key.b);
    auto it = elem_bc_name.find(be.element_index);
    if (it != elem_bc_name.end()) {
      auto res = boundary_names.emplace(key, it->second);
      if (!res.second && res.first->second != it->second)
        throw std::runtime_error("boundary face claimed by two BCs");
    }
  }

  // Faces.
  for (const auto& kv : edge_cells) {
    const auto& cells_on_edge = kv.second;
    GlobalMesh::Face f;
    f.n0 = kv.first.a;
    f.n1 = kv.first.b;
    f.fx = 0.5 * (m.node_x[f.n0] + m.node_x[f.n1]);
    f.fy = 0.5 * (m.node_y[f.n0] + m.node_y[f.n1]);
    const double ex = m.node_x[f.n1] - m.node_x[f.n0];
    const double ey = m.node_y[f.n1] - m.node_y[f.n0];
    f.area = std::sqrt(ex * ex + ey * ey);

    if (cells_on_edge.size() == 1) {
      // Boundary face: must carry a BC.
      auto bit = boundary_names.find(kv.first);
      if (bit == boundary_names.end())
        throw std::runtime_error("boundary face has no boundary condition");
      auto cit = c.boundary_conditions.find(bit->second);
      if (cit == c.boundary_conditions.end())
        throw std::runtime_error("boundary family '" + bit->second +
                                 "' not mapped in case file boundary_conditions");
      f.c0 = cells_on_edge[0];
      f.c1 = -1;
      f.bc = cit->second;
      f.bc_name = bit->second;
    } else if (cells_on_edge.size() == 2) {
      f.c0 = cells_on_edge[0];
      f.c1 = cells_on_edge[1];
      f.bc = BCType::Interior;
    } else {
      throw std::runtime_error("non-manifold edge (more than 2 cells)");
    }
    m.faces.push_back(f);
  }

  // Orient face normals outward from c0 and validate cell closure.
  for (auto& f : m.faces) {
    const double ex = m.node_x[f.n1] - m.node_x[f.n0];
    const double ey = m.node_y[f.n1] - m.node_y[f.n0];
    const double len = std::sqrt(ex * ex + ey * ey);
    // Candidate outward normal for a CCW edge (p0 -> p1): (ey, -ex)
    double nx = ey / len, ny = -ex / len;
    const double dx = f.fx - m.cells[f.c0].cx;
    const double dy = f.fy - m.cells[f.c0].cy;
    if (nx * dx + ny * dy < 0.0) {
      nx = -nx;
      ny = -ny;
    }
    f.nx = nx;
    f.ny = ny;
  }

  // Validate: each cell's faces must all point outward (dot with centroid
  // vector positive) and the face set must close the cell.
  std::map<EdgeKey, std::vector<int>> cell_face_index;
  for (size_t fi = 0; fi < m.faces.size(); ++fi) {
    const auto& f = m.faces[fi];
    EdgeKey key{f.n0, f.n1};
    if (key.a > key.b) std::swap(key.a, key.b);
    cell_face_index[key].push_back(static_cast<int>(fi));
  }
  for (size_t ci = 0; ci < m.cells.size(); ++ci) {
    const auto& cell = m.cells[ci];
    const int npe = static_cast<int>(cell.nodes.size());
    std::vector<int> cell_faces;
    for (int k = 0; k < npe; ++k) {
      int a = cell.nodes[k];
      int b = cell.nodes[(k + 1) % npe];
      if (a > b) std::swap(a, b);
      auto it = cell_face_index.find({a, b});
      if (it == cell_face_index.end())
        throw std::runtime_error("cell face missing from face list");
      for (int fi : it->second) {
        const auto& f = m.faces[fi];
        if (f.c0 == static_cast<int>(ci) || f.c1 == static_cast<int>(ci)) {
          const double dx = f.fx - cell.cx;
          const double dy = f.fy - cell.cy;
          const int other = (f.c0 == static_cast<int>(ci)) ? f.c1 : f.c0;
          const double dot = f.nx * dx + f.ny * dy;
          if (other == -1) {
            // boundary: outward normal must point away from the cell
            if (dot <= 0.0)
              throw std::runtime_error("boundary face normal not outward");
          } else if (f.c0 == static_cast<int>(ci)) {
            if (dot <= 0.0)
              throw std::runtime_error("interior face normal not outward from c0");
          } else {
            if (dot >= 0.0)
              throw std::runtime_error("interior face normal points toward c1");
          }
          cell_faces.push_back(fi);
        }
      }
    }
    if (cell_faces.size() != static_cast<size_t>(npe))
      throw std::runtime_error("cell face count mismatch (mixed topology)");
  }

  return m;
}

void write_global_mesh_bin(const GlobalMesh& m, const std::string& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) throw std::runtime_error("cannot write " + path);
  auto put = [&](const char* data, size_t n) {
    out.write(data, static_cast<std::streamsize>(n));
  };
  const char magic[8] = {'C', 'F', 'D', 'G', 'M', 'B', '1', 0};
  put(magic, 8);
  const uint64_t nn = m.node_x.size();
  const uint64_t nc = m.cells.size();
  const uint64_t nf = m.faces.size();
  put(reinterpret_cast<const char*>(&nn), 8);
  put(reinterpret_cast<const char*>(&nc), 8);
  put(reinterpret_cast<const char*>(&nf), 8);
  put(reinterpret_cast<const char*>(m.node_x.data()), nn * 8);
  put(reinterpret_cast<const char*>(m.node_y.data()), nn * 8);
  for (const auto& cell : m.cells) {
    uint32_t npe = cell.nodes.size();
    put(reinterpret_cast<const char*>(&npe), 4);
    put(reinterpret_cast<const char*>(cell.nodes.data()), npe * 4);
  }
  for (const auto& f : m.faces) {
    int32_t data[5] = {f.n0, f.n1, f.c0, f.c1, static_cast<int32_t>(f.bc)};
    put(reinterpret_cast<const char*>(data), 20);
    uint32_t nlen = f.bc_name.size();
    put(reinterpret_cast<const char*>(&nlen), 4);
    put(f.bc_name.data(), nlen);
    double geo[5] = {f.area, f.nx, f.ny, f.fx, f.fy};
    put(reinterpret_cast<const char*>(geo), 40);
  }
}

GlobalMesh read_global_mesh_bin(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + path);
  auto get = [&](void* data, size_t n) {
    in.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(n));
    if (!in) throw std::runtime_error("truncated file: " + path);
  };
  char magic[8] = {0};
  get(magic, 8);
  if (std::memcmp(magic, "CFDGMB1", 7) != 0)
    throw std::runtime_error("bad global mesh magic in " + path);
  uint64_t nn = 0, nc = 0, nf = 0;
  get(&nn, 8);
  get(&nc, 8);
  get(&nf, 8);
  GlobalMesh m;
  m.node_x.resize(nn);
  m.node_y.resize(nn);
  get(m.node_x.data(), nn * 8);
  get(m.node_y.data(), nn * 8);
  m.cells.resize(nc);
  for (auto& cell : m.cells) {
    uint32_t npe = 0;
    get(&npe, 4);
    cell.nodes.resize(npe);
    get(cell.nodes.data(), npe * 4);
  }
  m.faces.resize(nf);
  for (auto& f : m.faces) {
    int32_t data[5] = {0};
    get(data, 20);
    f.n0 = data[0];
    f.n1 = data[1];
    f.c0 = data[2];
    f.c1 = data[3];
    f.bc = static_cast<BCType>(data[4]);
    uint32_t nlen = 0;
    get(&nlen, 4);
    f.bc_name.resize(nlen);
    get(&f.bc_name[0], nlen);
    double geo[5] = {0};
    get(geo, 40);
    f.area = geo[0];
    f.nx = geo[1];
    f.ny = geo[2];
    f.fx = geo[3];
    f.fy = geo[4];
  }
  return m;
}

}  // namespace cfd
