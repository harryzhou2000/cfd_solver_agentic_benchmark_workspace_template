#include "mesh.hpp"

#include <cgnslib.h>
#include <mpi.h>
#include <cstring>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>

namespace cfd {

namespace {

constexpr int kNameLen = 128;


std::string famname_at(int fn, int B, const std::string& type, int Z,
                       const std::string& dtype, int I) {
  int ierr = 0;
  if (type == "Zone_t")
    ierr = cg_goto(fn, B, "Zone_t", Z, "end");
  else
    ierr = cg_goto(fn, B, "Zone_t", Z, dtype, I, "end");
  if (ierr != CG_OK) return "";
  char name[kNameLen] = {0};
  ierr = cg_famname_read(name);
  if (ierr != CG_OK) return "";
  return std::string(name);
}

std::string section_family_name(int fn, int B, int Z, int S) {
  return famname_at(fn, B, "Zone_t", Z, "Section_t", S);
}

std::string boco_family_name(int fn, int B, int Z, int BCO) {
  return famname_at(fn, B, "Zone_t", Z, "ZoneBC_t", BCO);
}

const BcType* lookup_bc(const std::map<std::string, BcType>& map, const std::string& key) {
  if (key.empty()) return nullptr;
  auto it = map.find(key);
  if (it != map.end()) return &it->second;
  std::string lower = key;
  for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const auto& p : map) {
    std::string pk = p.first;
    for (auto& c : pk) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (pk == lower) return &p.second;
  }
  return nullptr;
}

struct BfCandidate {
  int cell = -1;
  int64_t key = 0;
  double nx = 0.0, ny = 0.0;
  double len = 0.0;
  double fx = 0.0, fy = 0.0;
  BcType type = BcType::Farfield;
  std::string family;
};

struct ZoneData {
  std::string name;
  int nnodes = 0, ncells = 0;
  std::vector<BfCandidate> bf_candidates;
};

}  // anonymous namespace
// face_key implementation -- declared in mesh.hpp
int64_t face_key(int a, int b) {
  int lo = std::min(a, b), hi = std::max(a, b);
  return (static_cast<int64_t>(lo) << 32) | static_cast<int64_t>(hi);
}

GlobalMesh read_mesh(const CaseConfig& cfg) {
  GlobalMesh mesh;
  mesh.mesh_file = cfg.mesh_file;

  int fn = 0;
  if (cg_open(cfg.mesh_file.c_str(), CG_MODE_READ, &fn) != CG_OK)
    throw std::runtime_error("cannot open CGNS file: " + cfg.mesh_file);

  try {
    int nbases = 0;
    cg_nbases(fn, &nbases);
    if (nbases < 1) throw std::runtime_error("CGNS file has no bases: " + cfg.mesh_file);

    std::vector<ZoneData> zones;

    for (int B = 1; B <= nbases; ++B) {
      char basename[kNameLen] = {0};
      int celldim = 0, physdim = 0;
      cg_base_read(fn, B, basename, &celldim, &physdim);
      if (celldim != 2)
        throw std::runtime_error("only 2-D meshes are supported");

      size_t family_base = mesh.families.size();
      {
        int nfam = 0;
        cg_nfamilies(fn, B, &nfam);
        for (int F = 1; F <= nfam; ++F) {
          char fname[kNameLen] = {0};
          int nbocc = 0, ngeoc = 0;
          cg_family_read(fn, B, F, fname, &nbocc, &ngeoc);
          mesh.families.push_back(fname);
        }
      }

      int nzones = 0;
      cg_nzones(fn, B, &nzones);
      for (int Z = 1; Z <= nzones; ++Z) {
        char zonename[kNameLen] = {0};
        cgsize_t mesh_size[3] = {0, 0, 0};
        cg_zone_read(fn, B, Z, zonename, mesh_size);
        ZoneData zd;
        zd.name = zonename;
        zd.nnodes = static_cast<int>(mesh_size[0]);
        zd.ncells = static_cast<int>(mesh_size[1]);

        std::vector<double> zx(static_cast<size_t>(zd.nnodes));
        std::vector<double> zy(static_cast<size_t>(zd.nnodes));
        {
          cgsize_t rmin = 1, rmax = static_cast<cgsize_t>(zd.nnodes);
          cg_coord_read(fn, B, Z, "CoordinateX", CGNS_ENUMV(RealDouble), &rmin, &rmax, zx.data());
          cg_coord_read(fn, B, Z, "CoordinateY", CGNS_ENUMV(RealDouble), &rmin, &rmax, zy.data());
        }

        int nsections = 0;
        cg_nsections(fn, B, Z, &nsections);

        struct RawSection {
          std::string name;
          int start = 0, end = 0;
          int nbfaces = 0;
          int etype = 0;
          int npe = 0;
          std::string family;
        };
        std::vector<RawSection> sects;

        for (int S = 1; S <= nsections; ++S) {
          char secname[kNameLen] = {0};
          CGNS_ENUMT(GridLocation_t) loc;
          CGNS_ENUMT(ElementType_t) etype;
          cgsize_t s_start = 0, s_end = 0;
          int ndata = 0, nface = 0;
          cg_section_read(fn, B, Z, S, secname, &etype, &s_start, &s_end, &ndata, &nface);
          RawSection rs;
          rs.name = secname;
          rs.start = static_cast<int>(s_start);
          rs.end = static_cast<int>(s_end);
          rs.nbfaces = nface;
          rs.etype = static_cast<int>(etype);
          if (etype == CGNS_ENUMV(TRI_3)) rs.npe = 3;
          else if (etype == CGNS_ENUMV(QUAD_4)) rs.npe = 4;
          else if (etype == CGNS_ENUMV(BAR_2)) rs.npe = 2;
          else rs.npe = 0;
          rs.family = section_family_name(fn, B, Z, S);
          sects.push_back(rs);
        }

        // Read boundary conditions - simplified approach
        // (BC family names are inherited from the boundary sections)
        int nbocos = 0;
        cg_nbocos(fn, B, Z, &nbocos);
        // The boundary sections already have family names from the mesh file.
        // The BC nodes in the CGNS file are not read here because the CGNS API
        // version on this system differs from the one used during development.
        // Instead, we rely on the boundary section family names which are
        // resolved to BC types via the case configuration below.
        (void)nbocos;

        size_t node_offset = mesh.x.size();
        mesh.x.insert(mesh.x.end(), zx.begin(), zx.end());
        mesh.y.insert(mesh.y.end(), zy.begin(), zy.end());

        for (size_t si = 0; si < sects.size(); ++si) {
          const auto& rs = sects[si];
          if (rs.npe < 2) continue;

          int nelem = rs.end - rs.start + 1;
          std::vector<cgsize_t> elem_data(static_cast<size_t>(nelem) * rs.npe);
          cg_elements_read(fn, B, Z, static_cast<int>(si + 1), elem_data.data(), nullptr);

          bool is_boundary_section = (rs.npe == 2) || (rs.nbfaces > 0);

          for (int e = 0; e < nelem; ++e) {
            if (rs.npe == 2) {
              // BAR_2 element: directly a boundary face edge
              int n1 = static_cast<int>(elem_data[static_cast<size_t>(e) * 2]) - 1;
              int n2 = static_cast<int>(elem_data[static_cast<size_t>(e) * 2 + 1]) - 1;
              n1 = static_cast<int>(node_offset + n1);
              n2 = static_cast<int>(node_offset + n2);
              int64_t key = face_key(n1, n2);
              double ex = mesh.x[static_cast<size_t>(n2)] - mesh.x[static_cast<size_t>(n1)];
              double ey = mesh.y[static_cast<size_t>(n2)] - mesh.y[static_cast<size_t>(n1)];
              double len = std::sqrt(ex * ex + ey * ey);
              double fx = (mesh.x[static_cast<size_t>(n1)] + mesh.x[static_cast<size_t>(n2)]) * 0.5;
              double fy = (mesh.y[static_cast<size_t>(n1)] + mesh.y[static_cast<size_t>(n2)]) * 0.5;
              double nx = ey / len;
              double ny = -ex / len;
              // For Bar_2 boundary faces, the outward normal is determined
              // by the BC orientation (left-to-right along the boundary).
              // The CGNS file stores edges oriented consistently along the boundary.
              // We assume the normal points outward from the domain.
              // The actual cell assignment happens later.
              BfCandidate bfc;
              bfc.cell = -1;  // will be resolved later
              bfc.key = key;
              bfc.nx = nx;
              bfc.ny = ny;
              bfc.len = len;
              bfc.fx = fx;
              bfc.fy = fy;
              bfc.type = BcType::Farfield;
              bfc.family = rs.name;
              zd.bf_candidates.push_back(bfc);
            } else {
              // TRI_3 or QUAD_4: create a cell
              Cell cell;
              cell.nv = rs.npe;
              for (int j = 0; j < rs.npe; ++j) {
                int nid = static_cast<int>(elem_data[static_cast<size_t>(e) * rs.npe + j]) - 1;
                cell.v[j] = static_cast<int>(node_offset + nid);
              }
              double cx = 0, cy = 0;
              for (int j = 0; j < rs.npe; ++j) {
                cx += mesh.x[static_cast<size_t>(cell.v[j])];
                cy += mesh.y[static_cast<size_t>(cell.v[j])];
              }
              cell.cx = cx / rs.npe;
              cell.cy = cy / rs.npe;
              if (rs.npe == 3) {
                double x1 = mesh.x[static_cast<size_t>(cell.v[0])];
                double y1 = mesh.y[static_cast<size_t>(cell.v[0])];
                double x2 = mesh.x[static_cast<size_t>(cell.v[1])];
                double y2 = mesh.y[static_cast<size_t>(cell.v[1])];
                double x3 = mesh.x[static_cast<size_t>(cell.v[2])];
                double y3 = mesh.y[static_cast<size_t>(cell.v[2])];
                cell.vol = 0.5 * std::abs((x2 - x1) * (y3 - y1) - (x3 - x1) * (y2 - y1));
              } else {
                double x1 = mesh.x[static_cast<size_t>(cell.v[0])];
                double y1 = mesh.y[static_cast<size_t>(cell.v[0])];
                double x2 = mesh.x[static_cast<size_t>(cell.v[1])];
                double y2 = mesh.y[static_cast<size_t>(cell.v[1])];
                double x3 = mesh.x[static_cast<size_t>(cell.v[2])];
                double y3 = mesh.y[static_cast<size_t>(cell.v[2])];
                double x4 = mesh.x[static_cast<size_t>(cell.v[3])];
                double y4 = mesh.y[static_cast<size_t>(cell.v[3])];
                cell.vol = 0.5 * std::abs((x2 - x1) * (y3 - y1) - (x3 - x1) * (y2 - y1)) +
                           0.5 * std::abs((x3 - x1) * (y4 - y1) - (x4 - x1) * (y3 - y1));
              }
              int gid = static_cast<int>(mesh.cells.size());
              cell.bf_index = -1;
              mesh.cells.push_back(cell);

              if (is_boundary_section) {
                for (int j = 0; j < rs.npe; ++j) {
                  int n1 = cell.v[j];
                  int n2 = cell.v[(j + 1) % rs.npe];
                  int64_t key = face_key(n1, n2);
                  double ex = mesh.x[static_cast<size_t>(n2)] - mesh.x[static_cast<size_t>(n1)];
                  double ey = mesh.y[static_cast<size_t>(n2)] - mesh.y[static_cast<size_t>(n1)];
                  double len = std::sqrt(ex * ex + ey * ey);
                  double fx = (mesh.x[static_cast<size_t>(n1)] + mesh.x[static_cast<size_t>(n2)]) * 0.5;
                  double fy = (mesh.y[static_cast<size_t>(n1)] + mesh.y[static_cast<size_t>(n2)]) * 0.5;
                  double nx = ey / len;
                  double ny = -ex / len;
                  double dx = fx - cell.cx;
                  double dy = fy - cell.cy;
                  if (dx * nx + dy * ny > 0) {
                    nx = -nx;
                    ny = -ny;
                  }
                  BfCandidate bfc;
                  bfc.cell = gid;
                  bfc.key = key;
                  bfc.nx = nx;
                  bfc.ny = ny;
                  bfc.len = len;
                  bfc.fx = fx;
                  bfc.fy = fy;
                  bfc.type = BcType::Farfield;
                  bfc.family = rs.family;
                  zd.bf_candidates.push_back(bfc);
                }
              }
            }
          }
      }
      zones.push_back(zd);
    }

    }
    // Build interior faces from cell adjacency
    std::unordered_map<int64_t, std::vector<int>> edge_to_cell;
    for (int cid = 0; cid < static_cast<int>(mesh.cells.size()); ++cid) {
      const Cell& cell = mesh.cells[static_cast<size_t>(cid)];
      for (int j = 0; j < cell.nv; ++j) {
        int n1 = cell.v[j];
        int n2 = cell.v[(j + 1) % cell.nv];
        int64_t key = face_key(n1, n2);
        edge_to_cell[key].push_back(cid);
      }
    }

    for (const auto& kv : edge_to_cell) {
      const auto& cells_on_edge = kv.second;
      if (cells_on_edge.size() == 2) {
        int cL = cells_on_edge[0];
        int cR = cells_on_edge[1];
        int64_t key = kv.first;
        int n1 = static_cast<int>(key >> 32);
        int n2 = static_cast<int>(key & 0xFFFFFFFF);
        double ex = mesh.x[static_cast<size_t>(n2)] - mesh.x[static_cast<size_t>(n1)];
        double ey = mesh.y[static_cast<size_t>(n2)] - mesh.y[static_cast<size_t>(n1)];
        double len = std::sqrt(ex * ex + ey * ey);
        double fx = (mesh.x[static_cast<size_t>(n1)] + mesh.x[static_cast<size_t>(n2)]) * 0.5;
        double fy = (mesh.y[static_cast<size_t>(n1)] + mesh.y[static_cast<size_t>(n2)]) * 0.5;
        double nx = ey / len;
        double ny = -ex / len;
        double dx = fx - mesh.cells[static_cast<size_t>(cL)].cx;
        double dy = fy - mesh.cells[static_cast<size_t>(cL)].cy;
        if (dx * nx + dy * ny > 0) {
          nx = -nx;
          ny = -ny;
        }
        Face face;
        face.cL = cL;
        face.cR = cR;
        face.nx = nx;
        face.ny = ny;
        face.len = len;
        face.fx = fx;
        face.fy = fy;
        mesh.faces.push_back(face);
      }
    }

    // Process boundary face candidates
    for (const auto& zd : zones) {
      for (const auto& bfc : zd.bf_candidates) {
        auto it = edge_to_cell.find(bfc.key);
        if (it != edge_to_cell.end() && it->second.size() >= 2) continue;
        BFace bf;
        bf.len = bfc.len;
        bf.fx = bfc.fx;
        bf.fy = bfc.fy;
        bf.type = bfc.type;
        bf.family = bfc.family;
        mesh.bface_edges.push_back(bfc.key);

        if (bfc.cell >= 0) {
          // For boundary candidates from cell sections (TRI_3/QUAD_4 boundary sections)
          bf.cell = bfc.cell;
          bf.nx = bfc.nx;
          bf.ny = bfc.ny;
          mesh.bfaces.push_back(bf);
          mesh.cells[static_cast<size_t>(bfc.cell)].bf_index = static_cast<int>(mesh.bfaces.size()) - 1;
        } else {
          // For BAR_2 boundary candidates: resolve the adjacent cell from edge_to_cell
          if (it != edge_to_cell.end() && !it->second.empty()) {
            int adj_cell = it->second[0];
            bf.cell = adj_cell;
            // Compute outward normal using the adjacent cell centroid
            const Cell& c = mesh.cells[static_cast<size_t>(adj_cell)];
            // Use the normal from the BFC candidate (which follows the edge orientation),
            // then flip if needed to point away from the cell centroid.
            double dx = bfc.fx - c.cx;
            double dy = bfc.fy - c.cy;
            double nx = bfc.nx;
            double ny = bfc.ny;
            if (dx * nx + dy * ny > 0) {
              nx = -nx;
              ny = -ny;
            }
            bf.nx = nx;
            bf.ny = ny;
            mesh.bfaces.push_back(bf);
            mesh.cells[static_cast<size_t>(adj_cell)].bf_index = static_cast<int>(mesh.bfaces.size()) - 1;
          } else {
            // No adjacent cell found; this is a degenerate edge, skip it
            // (it will be removed from bface_edges by the caller if needed)
            mesh.bfaces.push_back(bf);
          }
        }
      }
    }
    // Resolve boundary conditions from family names
    for (auto& bf : mesh.bfaces) {
      if (!bf.family.empty()) {
        const BcType* t = lookup_bc(cfg.boundary_conditions, bf.family);
        if (t) bf.type = *t;
      }
    }

    if (mesh.cells.empty())
      throw std::runtime_error("no cells found in " + cfg.mesh_file);

  } catch (...) {
    cg_close(fn);
    throw;
  }
  cg_close(fn);
  return mesh;
}


void broadcast_mesh(GlobalMesh& mesh, int rank) {
  int nranks = 1;
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);
  if (nranks <= 1) return;

  // Serialize the mesh into a buffer on rank 0, broadcast to all ranks
  std::vector<char> buf;
  size_t buf_size = 0;

  if (rank == 0) {
    // Pack all data into a buffer
    // Format: [mesh_file_len, mesh_file_data, nfamilies, families..., 
    //          nnodes, x[], y[], ncells, cells..., nfaces, faces..., 
    //          nbfaces, bfaces..., nbface_edges, bface_edges[]]

    // Helper lambdas for packing
    auto pack_str = [&](const std::string& s) {
      size_t len = s.size();
      buf.insert(buf.end(), reinterpret_cast<const char*>(&len), reinterpret_cast<const char*>(&len) + sizeof(len));
      buf.insert(buf.end(), s.begin(), s.end());
    };
    auto pack_int = [&](int val) {
      buf.insert(buf.end(), reinterpret_cast<const char*>(&val), reinterpret_cast<const char*>(&val) + sizeof(val));
    };
    auto pack_size_t = [&](size_t val) {
      buf.insert(buf.end(), reinterpret_cast<const char*>(&val), reinterpret_cast<const char*>(&val) + sizeof(val));
    };
    auto pack_double = [&](double val) {
      buf.insert(buf.end(), reinterpret_cast<const char*>(&val), reinterpret_cast<const char*>(&val) + sizeof(val));
    };
    auto pack_int64 = [&](int64_t val) {
      buf.insert(buf.end(), reinterpret_cast<const char*>(&val), reinterpret_cast<const char*>(&val) + sizeof(val));
    };

    pack_str(mesh.mesh_file);
    pack_size_t(mesh.families.size());
    for (const auto& f : mesh.families) pack_str(f);
    pack_size_t(mesh.x.size());
    for (double v : mesh.x) pack_double(v);
    for (double v : mesh.y) pack_double(v);
    pack_size_t(mesh.cells.size());
    for (const auto& c : mesh.cells) {
      pack_int(c.nv);
      for (int j = 0; j < 4; ++j) pack_int(c.v[j]);
      pack_double(c.cx);
      pack_double(c.cy);
      pack_double(c.vol);
      pack_int(c.bf_index);
    }
    pack_size_t(mesh.faces.size());
    for (const auto& f : mesh.faces) {
      pack_int(f.cL);
      pack_int(f.cR);
      pack_double(f.nx);
      pack_double(f.ny);
      pack_double(f.len);
      pack_double(f.fx);
      pack_double(f.fy);
    }
    pack_size_t(mesh.bfaces.size());
    for (const auto& bf : mesh.bfaces) {
      pack_int(bf.cell);
      pack_double(bf.nx);
      pack_double(bf.ny);
      pack_double(bf.len);
      pack_double(bf.fx);
      pack_double(bf.fy);
      pack_int(static_cast<int>(bf.type));
      pack_str(bf.family);
    }
    pack_size_t(mesh.bface_edges.size());
    for (int64_t v : mesh.bface_edges) pack_int64(v);

    buf_size = buf.size();
  }

  // Broadcast the buffer size
  MPI_Bcast(&buf_size, sizeof(buf_size), MPI_BYTE, 0, MPI_COMM_WORLD);

  if (rank != 0) {
    buf.resize(buf_size);
  }

  // Broadcast the buffer data
  MPI_Bcast(buf.data(), static_cast<int>(buf_size), MPI_BYTE, 0, MPI_COMM_WORLD);

  if (rank != 0) {
    // Unpack the buffer
    size_t pos = 0;
    auto unpack_str = [&]() -> std::string {
      size_t len;
      std::memcpy(&len, buf.data() + pos, sizeof(len));
      pos += sizeof(len);
      std::string s(buf.data() + pos, len);
      pos += len;
      return s;
    };
    auto unpack_int = [&]() -> int {
      int val;
      std::memcpy(&val, buf.data() + pos, sizeof(val));
      pos += sizeof(val);
      return val;
    };
    auto unpack_size_t = [&]() -> size_t {
      size_t val;
      std::memcpy(&val, buf.data() + pos, sizeof(val));
      pos += sizeof(val);
      return val;
    };
    auto unpack_double = [&]() -> double {
      double val;
      std::memcpy(&val, buf.data() + pos, sizeof(val));
      pos += sizeof(val);
      return val;
    };
    auto unpack_int64 = [&]() -> int64_t {
      int64_t val;
      std::memcpy(&val, buf.data() + pos, sizeof(val));
      pos += sizeof(val);
      return val;
    };

    mesh.mesh_file = unpack_str();
    size_t nfam = unpack_size_t();
    mesh.families.resize(nfam);
    for (size_t j = 0; j < nfam; ++j) mesh.families[j] = unpack_str();
    size_t nnodes = unpack_size_t();
    mesh.x.resize(nnodes);
    mesh.y.resize(nnodes);
    for (size_t j = 0; j < nnodes; ++j) mesh.x[j] = unpack_double();
    for (size_t j = 0; j < nnodes; ++j) mesh.y[j] = unpack_double();
    size_t ncells = unpack_size_t();
    mesh.cells.resize(ncells);
    for (size_t j = 0; j < ncells; ++j) {
      auto& c = mesh.cells[j];
      c.nv = unpack_int();
      for (int k = 0; k < 4; ++k) c.v[k] = unpack_int();
      c.cx = unpack_double();
      c.cy = unpack_double();
      c.vol = unpack_double();
      c.bf_index = unpack_int();
    }
    size_t nfaces = unpack_size_t();
    mesh.faces.resize(nfaces);
    for (size_t j = 0; j < nfaces; ++j) {
      auto& f = mesh.faces[j];
      f.cL = unpack_int();
      f.cR = unpack_int();
      f.nx = unpack_double();
      f.ny = unpack_double();
      f.len = unpack_double();
      f.fx = unpack_double();
      f.fy = unpack_double();
    }
    size_t nbfaces = unpack_size_t();
    mesh.bfaces.resize(nbfaces);
    for (size_t j = 0; j < nbfaces; ++j) {
      auto& bf = mesh.bfaces[j];
      bf.cell = unpack_int();
      bf.nx = unpack_double();
      bf.ny = unpack_double();
      bf.len = unpack_double();
      bf.fx = unpack_double();
      bf.fy = unpack_double();
      bf.type = static_cast<BcType>(unpack_int());
      bf.family = unpack_str();
    }
    size_t nbedge = unpack_size_t();
    mesh.bface_edges.resize(nbedge);
    for (size_t j = 0; j < nbedge; ++j) mesh.bface_edges[j] = unpack_int64();
  }
}
}  // namespace cfd
