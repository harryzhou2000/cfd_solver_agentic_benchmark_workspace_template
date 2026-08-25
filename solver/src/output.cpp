#include "output.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <sys/stat.h>
#include <unordered_map>
#include "physics.hpp"

namespace cfd {

namespace {

void mkdirs(const std::string& p) {
  std::string cur;
  for (char ch : p) {
    cur += ch;
    if (ch == '/') mkdir(cur.c_str(), 0755);
  }
  mkdir(p.c_str(), 0755);
}

// Gathers variable-length byte buffers to rank 0. Returns concatenated
// buffer and per-rank offsets (valid on rank 0 only).
void gather_bytes(const std::string& local, int rank, int n_ranks, MPI_Comm comm,
                  std::vector<char>& all, std::vector<int>& counts) {
  int n = static_cast<int>(local.size());
  counts.assign(n_ranks, 0);
  MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
  std::vector<int> disp(n_ranks, 0);
  if (rank == 0) {
    for (int r = 1; r < n_ranks; ++r) disp[r] = disp[r - 1] + counts[r - 1];
    all.resize(disp.back() + counts.back());
  }
  MPI_Gatherv(local.data(), n, MPI_CHAR, all.data(), counts.data(), disp.data(),
              MPI_CHAR, 0, comm);
}

template <typename T>
void append_pod(std::string& buf, const T& v) {
  buf.append(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <typename T>
void append_vec(std::string& buf, const std::vector<T>& v) {
  buf.append(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T));
}
template <typename T>
T read_pod(const char*& p) {
  T v;
  std::memcpy(&v, p, sizeof(T));
  p += sizeof(T);
  return v;
}

}  // namespace

void OutputContext::log(const std::string& msg) {
  if (rank != 0) return;
  std::cout << msg << std::endl;
  if (log_file) log_file << msg << std::endl;
}

void open_output(OutputContext& oc, const std::string& dir) {
  oc.dir = dir;
  if (oc.rank == 0) {
    mkdirs(dir);
    oc.log_file.open(dir + "/stdout.log", std::ios::out | std::ios::trunc);
  }
}

void write_residual_row(OutputContext& oc, long step, double time, int inner_iter,
                        double cfl, double dt, const double per_eq[4], double l2,
                        double linf) {
  if (oc.rank != 0) return;
  if (!oc.res_file) {
    oc.res_file.open(oc.dir + "/residuals.csv", std::ios::out | std::ios::trunc);
    oc.res_file << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,"
                   "residual_l2,residual_linf\n";
  }
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "%ld,%.17e,%d,%.6f,%.6e,%.6e,%.6e,%.6e,%.6e,%.6e,%.6e\n",
                step, time, inner_iter, cfl, dt, per_eq[0], per_eq[1], per_eq[2],
                per_eq[3], l2, linf);
  oc.res_file << buf;
  if (step % 200 == 0) oc.res_file.flush();
}

void write_force_row(OutputContext& oc, long step, double time, double cl, double cd,
                     double cmz, double pdrag, double vdrag, double plift,
                     double vlift) {
  if (oc.rank != 0) return;
  if (!oc.force_file) {
    oc.force_file.open(oc.dir + "/forces.csv", std::ios::out | std::ios::trunc);
    oc.force_file << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,"
                     "pressure_lift,viscous_lift\n";
  }
  char buf[512];
  std::snprintf(buf, sizeof(buf), "%ld,%.17e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e\n",
                step, time, cl, cd, cmz, pdrag, vdrag, plift, vlift);
  oc.force_file << buf;
  if (step % 200 == 0) oc.force_file.flush();
}

void write_surface_csv(OutputContext& oc, const LocalMesh& m,
                       const std::vector<SurfaceRow>& local_rows) {
  // Serialize local rows.
  std::string buf;
  append_pod<int>(buf, static_cast<int>(local_rows.size()));
  for (const auto& r : local_rows) {
    append_pod(buf, r.x); append_pod(buf, r.y);
    append_pod(buf, r.nx); append_pod(buf, r.ny);
    append_pod(buf, r.pressure); append_pod(buf, r.cp); append_pod(buf, r.cf);
    append_pod(buf, r.rho); append_pod(buf, r.u); append_pod(buf, r.v);
    append_pod(buf, r.mach); append_pod(buf, r.family);
  }
  std::vector<char> all;
  std::vector<int> counts;
  gather_bytes(buf, oc.rank, oc.n_ranks, oc.comm, all, counts);
  if (oc.rank != 0) return;

  std::vector<SurfaceRow> rows;
  const char* p = all.data();
  for (int r = 0; r < oc.n_ranks; ++r) {
    const char* end = p + counts[r];
    int n = read_pod<int>(p);
    for (int i = 0; i < n; ++i) {
      SurfaceRow row;
      row.x = read_pod<double>(p); row.y = read_pod<double>(p);
      row.nx = read_pod<double>(p); row.ny = read_pod<double>(p);
      row.pressure = read_pod<double>(p); row.cp = read_pod<double>(p);
      row.cf = read_pod<double>(p); row.rho = read_pod<double>(p);
      row.u = read_pod<double>(p); row.v = read_pod<double>(p);
      row.mach = read_pod<double>(p); row.family = read_pod<int>(p);
      rows.push_back(row);
    }
    p = end;
  }
  // Sort by family then angle around the body centroid for readable output.
  std::sort(rows.begin(), rows.end(), [](const SurfaceRow& a, const SurfaceRow& b) {
    if (a.family != b.family) return a.family < b.family;
    double aa = std::atan2(a.y, a.x), bb = std::atan2(b.y, b.x);
    if (aa != bb) return aa < bb;
    return a.x < b.x;
  });
  std::ofstream f(oc.dir + "/surface.csv", std::ios::out | std::ios::trunc);
  f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
  for (const auto& r : rows) {
    char line[640];
    std::snprintf(line, sizeof(line),
                  "%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%s\n",
                  r.x, r.y, r.nx, r.ny, r.pressure, r.cp, r.cf, r.rho, r.u, r.v,
                  r.mach, m.bc_names[r.family].c_str());
    f << line;
  }
}

void write_field_vtu(OutputContext& oc, const LocalMesh& m, const CaseFile& cs,
                     const FlowState& s, const std::string& filename) {
  // Per-rank payload: nodes (global ids + coords), owned cells (connectivity
  // in global node ids), and 8 cell data arrays.
  std::string ibuf, dbuf;
  const int nv = 8;
  append_pod<int>(ibuf, m.n_nodes);
  append_pod<int>(ibuf, m.n_owned);
  int nconn = m.cell_node_start[m.n_owned];
  append_pod<int>(ibuf, nconn);
  append_vec(ibuf, m.node_global);
  for (int i = 0; i < m.n_owned; ++i) {
    int nn = m.cell_node_start[i + 1] - m.cell_node_start[i];
    append_pod<int>(ibuf, nn);
  }
  for (int i = 0; i < m.n_owned; ++i)
    for (int k = m.cell_node_start[i]; k < m.cell_node_start[i + 1]; ++k)
      append_pod<int>(ibuf, m.node_global[m.cell_node_list[k]]);
  append_vec(dbuf, m.node_x);
  append_vec(dbuf, m.node_y);
  std::vector<double> data(static_cast<size_t>(m.n_owned) * nv);
  for (int i = 0; i < m.n_owned; ++i) {
    const Vec4& W = s.W[i];
    double a = sound_speed(W[0], W[3], cs.gas);
    double vmag = std::hypot(W[1], W[2]);
    double vort = s.gradW[i][4] - s.gradW[i][3];  // dv/dx - du/dy
    data[i * nv + 0] = W[0];
    data[i * nv + 1] = W[1];
    data[i * nv + 2] = W[2];
    data[i * nv + 3] = W[3];
    data[i * nv + 4] = vmag / a;
    data[i * nv + 5] = W[3] / (W[0] * cs.gas.R);
    data[i * nv + 6] = vort;
    data[i * nv + 7] = oc.rank;
  }
  append_vec(dbuf, data);

  std::vector<char> iall, dall;
  std::vector<int> icounts, dcounts;
  gather_bytes(ibuf, oc.rank, oc.n_ranks, oc.comm, iall, icounts);
  gather_bytes(dbuf, oc.rank, oc.n_ranks, oc.comm, dall, dcounts);
  if (oc.rank != 0) return;

  // Assemble the global unstructured grid.
  std::unordered_map<int, int> gn2out;
  std::vector<double> px, py;
  struct CellOut { int gid; std::vector<int> conn; };
  std::vector<CellOut> cells;
  std::vector<std::array<double, nv>> cdata_all;
  {
    const char* ip = iall.data();
    const char* dp = dall.data();
    for (int r = 0; r < oc.n_ranks; ++r) {
      const char* iend = ip + icounts[r];
      int n_nodes = read_pod<int>(ip);
      int n_owned = read_pod<int>(ip);
      int nconn = read_pod<int>(ip);
      const int* ng = reinterpret_cast<const int*>(ip);
      ip += sizeof(int) * n_nodes;
      std::vector<int> nno(n_owned);
      std::memcpy(nno.data(), ip, sizeof(int) * n_owned);
      ip += sizeof(int) * n_owned;
      const int* conn = reinterpret_cast<const int*>(ip);
      ip += sizeof(int) * nconn;
      const double* nx = reinterpret_cast<const double*>(dp);
      dp += sizeof(double) * n_nodes;
      const double* ny = reinterpret_cast<const double*>(dp);
      dp += sizeof(double) * n_nodes;
      const double* cdata = reinterpret_cast<const double*>(dp);
      dp += sizeof(double) * n_owned * nv;
      (void)iend;

      std::vector<int> node_map(n_nodes);
      for (int i = 0; i < n_nodes; ++i) {
        auto it = gn2out.find(ng[i]);
        if (it == gn2out.end()) {
          int oid = static_cast<int>(px.size());
          gn2out[ng[i]] = oid;
          px.push_back(nx[i]);
          py.push_back(ny[i]);
          node_map[i] = oid;
        } else {
          node_map[i] = it->second;
        }
      }
      // Owned cells arrive in ascending global-id order per rank; the
      // global id sequence is implicit in the data order of state arrays,
      // so recover gids by position is not possible here. We instead rely on
      // the caller passing cells whose global ids we reconstruct from the
      // partition: conn order is per-rank local order, so attach gid later.
      int coff = 0;
      for (int i = 0; i < n_owned; ++i) {
        CellOut co;
        co.gid = -1;
        for (int k = 0; k < nno[i]; ++k) co.conn.push_back(node_map[conn[coff + k]]);
        coff += nno[i];
        cells.push_back(std::move(co));
        std::array<double, nv> row;
        for (int v = 0; v < nv; ++v) row[v] = cdata[i * nv + v];
        cdata_all.push_back(row);
      }
    }
  }

  std::ofstream f(oc.dir + "/" + filename, std::ios::out | std::ios::trunc);
  const size_t np = px.size(), nc = cells.size();
  f << "<?xml version=\"1.0\"?>\n";
  f << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
  f << "<UnstructuredGrid>\n<Piece NumberOfPoints=\"" << np
    << "\" NumberOfCells=\"" << nc << "\">\n";
  f << "<Points><DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
  for (size_t i = 0; i < np; ++i)
    f << px[i] << " " << py[i] << " 0\n";
  f << "</DataArray></Points>\n<Cells>\n";
  f << "<DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
  for (const auto& c : cells)
    for (int nd : c.conn) f << nd << " ";
  f << "\n</DataArray>\n";
  f << "<DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
  {
    size_t off = 0;
    for (const auto& c : cells) {
      off += c.conn.size();
      f << off << " ";
    }
  }
  f << "\n</DataArray>\n";
  f << "<DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
  for (const auto& c : cells) f << (c.conn.size() == 3 ? 5 : 9) << " ";
  f << "\n</DataArray>\n</Cells>\n";
  static const char* names[nv] = {"rho", "u", "v", "pressure",
                                  "mach", "temperature", "vorticity", "partition_rank"};
  f << "<CellData>\n";
  for (int v = 0; v < nv; ++v) {
    f << "<DataArray type=\"Float64\" Name=\"" << names[v] << "\" format=\"ascii\">\n";
    for (const auto& row : cdata_all) f << row[v] << " ";
    f << "\n</DataArray>\n";
  }
  f << "</CellData>\n</Piece>\n</UnstructuredGrid>\n</VTKFile>\n";
}

void write_restart(OutputContext& oc, const LocalMesh& m, const std::string& filename,
                   long step, double time, const std::vector<Vec4>& U,
                   const std::vector<Vec4>& U_n, const std::vector<Vec4>& U_nm1,
                   int n_states) {
  // Serialize owned states with global ids.
  std::string buf;
  append_pod<int>(buf, m.n_owned);
  buf.append(reinterpret_cast<const char*>(m.cell_global.data()),
             static_cast<size_t>(m.n_owned) * sizeof(int));  // owned gids
  auto push_state = [&](const std::vector<Vec4>& S) {
    for (int i = 0; i < m.n_owned; ++i) {
      append_pod(buf, S[i][0]); append_pod(buf, S[i][1]);
      append_pod(buf, S[i][2]); append_pod(buf, S[i][3]);
    }
  };
  push_state(U);
  if (n_states >= 3) { push_state(U_n); push_state(U_nm1); }

  std::vector<char> all;
  std::vector<int> counts;
  gather_bytes(buf, oc.rank, oc.n_ranks, oc.comm, all, counts);
  if (oc.rank != 0) return;

  // Rank 0: total cells from gathered per-rank payloads.
  const char* p = all.data();
  int64_t total = 0;
  {
    const char* q = p;
    for (int r = 0; r < oc.n_ranks; ++r) {
      int n = 0;
      std::memcpy(&n, q, sizeof(int));
      total += n;
      q += counts[r];
    }
  }
  std::vector<double> states(static_cast<size_t>(total) * 4 * n_states);
  for (int r = 0; r < oc.n_ranks; ++r) {
    int n = read_pod<int>(p);
    const int* gid = reinterpret_cast<const int*>(p);
    p += sizeof(int) * n;
    const double* vals = reinterpret_cast<const double*>(p);
    p += sizeof(double) * n * 4 * n_states;
    for (int s = 0; s < n_states; ++s)
      for (int i = 0; i < n; ++i)
        for (int k = 0; k < 4; ++k)
          states[(static_cast<size_t>(s) * total + gid[i]) * 4 + k] =
              vals[(static_cast<size_t>(s) * n + i) * 4 + k];
  }

  std::ofstream f(oc.dir + "/" + filename, std::ios::binary | std::ios::trunc);
  char magic[16] = "CFDRST01";
  f.write(magic, 16);
  int64_t step64 = step, nc64 = total;
  int32_t ns32 = n_states;
  f.write(reinterpret_cast<const char*>(&step64), 8);
  f.write(reinterpret_cast<const char*>(&time), 8);
  f.write(reinterpret_cast<const char*>(&nc64), 8);
  f.write(reinterpret_cast<const char*>(&ns32), 4);
  f.write(reinterpret_cast<const char*>(states.data()),
          static_cast<std::streamsize>(states.size() * sizeof(double)));
}

bool read_restart(const std::string& filename, int n_cells_global, long& step,
                  double& time, std::vector<Vec4>& U, std::vector<Vec4>& U_n,
                  std::vector<Vec4>& U_nm1, int& n_states) {
  std::ifstream f(filename, std::ios::binary);
  if (!f) return false;
  char magic[16];
  f.read(magic, 16);
  if (std::strncmp(magic, "CFDRST01", 8) != 0) return false;
  int64_t step64, nc64;
  int32_t ns32;
  f.read(reinterpret_cast<char*>(&step64), 8);
  f.read(reinterpret_cast<char*>(&time), 8);
  f.read(reinterpret_cast<char*>(&nc64), 8);
  f.read(reinterpret_cast<char*>(&ns32), 4);
  if (nc64 != n_cells_global)
    throw std::runtime_error("restart cell count mismatch");
  step = step64;
  n_states = ns32;
  std::vector<double> states(static_cast<size_t>(nc64) * 4 * n_states);
  f.read(reinterpret_cast<char*>(states.data()),
         static_cast<std::streamsize>(states.size() * sizeof(double)));
  if (!f) return false;
  return true;
}

void write_partition_diagnostics(OutputContext& oc, const LocalMesh& m,
                                 const PartitionInfo& info) {
  std::string row = partition_diagnostics_csv(m, info);
  std::vector<char> all;
  std::vector<int> counts;
  gather_bytes(row, oc.rank, oc.n_ranks, oc.comm, all, counts);
  if (oc.rank != 0) return;
  std::ofstream f(oc.dir + "/partition_diagnostics.csv", std::ios::out | std::ios::trunc);
  f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,"
       "neighbor_ranks,send_cells,recv_cells\n";
  const char* p = all.data();
  for (int r = 0; r < oc.n_ranks; ++r) {
    f.write(p, counts[r]);
    f << "\n";
    p += counts[r];
  }
  // JSON summary with global load-balance statistics.
  std::ofstream jf(oc.dir + "/partition_diagnostics.json", std::ios::out | std::ios::trunc);
  jf << "{\n  \"num_cells_global\": " << info.n_cells_global
     << ",\n  \"num_faces_global\": " << info.n_faces_global
     << ",\n  \"partition_edge_cut\": " << info.edge_cut
     << ",\n  \"mpi_ranks\": " << oc.n_ranks << "\n}\n";
}

void write_json_file(const std::string& path, const std::string& json_text) {
  std::ofstream f(path, std::ios::out | std::ios::trunc);
  f << json_text;
}

}  // namespace cfd
