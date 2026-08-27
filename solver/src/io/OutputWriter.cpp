#include "io/OutputWriter.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "core/Exception.hpp"
#include "core/Log.hpp"
#include "parallel/Comm.hpp"

namespace cfd {
namespace {

using json = nlohmann::json;

template <typename T>
std::vector<T> gatherv(const std::vector<T>& local, MPI_Datatype dt, MPI_Comm comm, int rank,
                       int size, std::vector<int>* counts_out = nullptr) {
  int n = static_cast<int>(local.size());
  std::vector<int> counts(size, 0), displs(size, 0);
  MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
  long long total = 0;
  if (rank == 0) {
    for (int r = 0; r < size; ++r) {
      CFD_CHECK(total <= static_cast<long long>(std::numeric_limits<int>::max()),
                "gather buffer exceeds the MPI 32-bit element count");
      displs[r] = static_cast<int>(total);
      total += counts[r];
    }
  }
  std::vector<T> all(static_cast<std::size_t>(std::max<long long>(total, 1)));
  MPI_Gatherv(local.data(), n, dt, all.data(), counts.data(), displs.data(), dt, 0, comm);
  if (rank == 0) all.resize(static_cast<std::size_t>(total));
  else all.clear();
  if (counts_out) *counts_out = counts;
  return all;
}

struct AppendedBlock {
  std::string name;
  std::string type;
  int ncomp;
  std::vector<char> bytes;
};

void appendRaw(std::vector<AppendedBlock>& blocks, const std::string& name, const std::string& type,
               int ncomp, const void* data, std::size_t nbytes) {
  AppendedBlock b;
  b.name = name;
  b.type = type;
  b.ncomp = ncomp;
  b.bytes.resize(nbytes);
  std::memcpy(b.bytes.data(), data, nbytes);
  blocks.push_back(std::move(b));
}

}  // namespace

void CsvStream::open(const std::string& path, const std::string& header, int rank) {
  if (rank != 0) return;
  file_ = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
  CFD_CHECK(file_->good(), "cannot open output file '" << path << "'");
  (*file_) << header << "\n";
}
void CsvStream::row(const std::string& line) { if (file_) (*file_) << line << "\n"; }
void CsvStream::flush() { if (file_) file_->flush(); }
void CsvStream::close() { if (file_) { file_->flush(); file_->close(); file_.reset(); } }

// ---------------------------------------------------------------------------
void writeVtu(const std::string& path, const SpatialOperator& op, int precision_bits) {
  const LocalMesh& m = op.mesh();
  MPI_Comm comm = op.comm();
  int rank = 0, size = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);
  const PerfectGas& gas = op.gas();

  // --- local pieces (owned cells only) ---
  std::vector<long long> conn;          // node gids
  std::vector<int> npts;
  std::vector<long long> node_gids;
  std::vector<double> node_xy;
  {
    std::unordered_map<GlobalIndex, int> seen;
    seen.reserve(m.num_owned * 4);
    for (Index c = 0; c < m.num_owned; ++c) {
      const int n = m.cellSize(c);
      npts.push_back(n);
      for (int k = 0; k < n; ++k) {
        const Index ln = m.cellNodePtr(c)[k];
        const GlobalIndex g = m.node_gid[ln];
        conn.push_back(g);
        if (seen.emplace(g, 1).second) {
          node_gids.push_back(g);
          node_xy.push_back(m.x[ln]);
          node_xy.push_back(m.y[ln]);
        }
      }
    }
  }

  const int nfields = 12;
  std::vector<double> celldata(static_cast<std::size_t>(m.num_owned) * nfields);
  const std::vector<Real> resmag = op.residualMagnitude();
  for (Index c = 0; c < m.num_owned; ++c) {
    const Real rho = op.W()[c * kNVar + 0];
    const Real u = op.W()[c * kNVar + 1];
    const Real v = op.W()[c * kNVar + 2];
    const Real p = op.W()[c * kNVar + 3];
    const Real a = gas.soundSpeed(std::max(rho, 1e-30), std::max(p, 1e-30));
    const Real T = gas.temperature(std::max(rho, 1e-30), std::max(p, 1e-30));
    const Real E = p / ((gas.gamma() - 1.0) * rho) + 0.5 * (u * u + v * v);
    const Real dvdx = op.gradients()[(c * kNVar + 2) * kDim + 0];
    const Real dudy = op.gradients()[(c * kNVar + 1) * kDim + 1];
    double* d = &celldata[static_cast<std::size_t>(c) * nfields];
    d[0] = rho;
    d[1] = u;
    d[2] = v;
    d[3] = std::sqrt(u * u + v * v);
    d[4] = p;
    d[5] = std::sqrt(u * u + v * v) / a;
    d[6] = T;
    d[7] = E;
    d[8] = dvdx - dudy;                 // vorticity (z component)
    d[9] = static_cast<double>(rank);
    d[10] = static_cast<double>(m.cell_gid[c]);
    d[11] = resmag[c];
  }

  std::vector<long long> all_conn = gatherv(conn, MPI_LONG_LONG, comm, rank, size);
  std::vector<int> all_npts = gatherv(npts, MPI_INT, comm, rank, size);
  std::vector<long long> all_ngid = gatherv(node_gids, MPI_LONG_LONG, comm, rank, size);
  std::vector<double> all_nxy = gatherv(node_xy, MPI_DOUBLE, comm, rank, size);
  std::vector<double> all_cd = gatherv(celldata, MPI_DOUBLE, comm, rank, size);
  if (rank != 0) return;

  // --- assemble a unique point list ---
  std::unordered_map<long long, int> pid;
  pid.reserve(all_ngid.size() * 2);
  std::vector<double> px, py;
  px.reserve(all_ngid.size());
  py.reserve(all_ngid.size());
  for (std::size_t i = 0; i < all_ngid.size(); ++i) {
    auto it = pid.find(all_ngid[i]);
    if (it != pid.end()) continue;
    pid.emplace(all_ngid[i], static_cast<int>(px.size()));
    px.push_back(all_nxy[2 * i]);
    py.push_back(all_nxy[2 * i + 1]);
  }
  const std::size_t npoints = px.size();
  const std::size_t ncells = all_npts.size();

  std::vector<double> points(npoints * 3, 0.0);
  for (std::size_t i = 0; i < npoints; ++i) { points[3 * i] = px[i]; points[3 * i + 1] = py[i]; }

  std::vector<long long> vtk_conn(all_conn.size());
  std::vector<long long> vtk_off(ncells);
  std::vector<unsigned char> vtk_types(ncells);
  {
    std::size_t pos = 0;
    for (std::size_t c = 0; c < ncells; ++c) {
      const int n = all_npts[c];
      for (int k = 0; k < n; ++k) {
        auto it = pid.find(all_conn[pos + k]);
        CFD_CHECK(it != pid.end(), "field output: unknown node gid in gathered connectivity");
        vtk_conn[pos + k] = it->second;
      }
      pos += n;
      vtk_off[c] = static_cast<long long>(pos);
      vtk_types[c] = (n == 3) ? 5 : (n == 4 ? 9 : 7);   // VTK_TRIANGLE / QUAD / POLYGON
    }
  }

  static const char* kNames[] = {"Density",  "VelocityX", "VelocityY", "VelocityMagnitude",
                                 "Pressure", "Mach",      "Temperature", "TotalEnergy",
                                 "Vorticity", "RankId",   "CellGlobalId",
                                 "ScaledResidual"};
  std::vector<AppendedBlock> blocks;
  appendRaw(blocks, "Points", "Float64", 3, points.data(), points.size() * sizeof(double));
  appendRaw(blocks, "connectivity", "Int64", 1, vtk_conn.data(), vtk_conn.size() * sizeof(long long));
  appendRaw(blocks, "offsets", "Int64", 1, vtk_off.data(), vtk_off.size() * sizeof(long long));
  appendRaw(blocks, "types", "UInt8", 1, vtk_types.data(), vtk_types.size());
  for (int fidx = 0; fidx < nfields; ++fidx) {
    if (precision_bits == 64) {
      std::vector<double> v(ncells);
      for (std::size_t c = 0; c < ncells; ++c) v[c] = all_cd[c * nfields + fidx];
      appendRaw(blocks, kNames[fidx], "Float64", 1, v.data(), v.size() * sizeof(double));
    } else {
      std::vector<float> v(ncells);
      for (std::size_t c = 0; c < ncells; ++c) v[c] = static_cast<float>(all_cd[c * nfields + fidx]);
      appendRaw(blocks, kNames[fidx], "Float32", 1, v.data(), v.size() * sizeof(float));
    }
  }

  std::vector<unsigned long long> offsets(blocks.size(), 0);
  unsigned long long running = 0;
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    offsets[i] = running;
    running += sizeof(unsigned long long) + blocks[i].bytes.size();
  }

  std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
  CFD_CHECK(out.good(), "cannot open field file '" << path << "'");
  out << "<?xml version=\"1.0\"?>\n"
      << "<VTKFile type=\"UnstructuredGrid\" version=\"1.0\" byte_order=\"LittleEndian\""
      << " header_type=\"UInt64\">\n"
      << "  <UnstructuredGrid>\n"
      << "    <Piece NumberOfPoints=\"" << npoints << "\" NumberOfCells=\"" << ncells << "\">\n"
      << "      <Points>\n"
      << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"appended\""
      << " offset=\"" << offsets[0] << "\"/>\n"
      << "      </Points>\n"
      << "      <Cells>\n";
  for (int i = 1; i <= 3; ++i) {
    out << "        <DataArray type=\"" << blocks[i].type << "\" Name=\"" << blocks[i].name
        << "\" format=\"appended\" offset=\"" << offsets[i] << "\"/>\n";
  }
  out << "      </Cells>\n      <CellData Scalars=\"Density\">\n";
  for (std::size_t i = 4; i < blocks.size(); ++i) {
    out << "        <DataArray type=\"" << blocks[i].type << "\" Name=\"" << blocks[i].name
        << "\" NumberOfComponents=\"1\" format=\"appended\" offset=\"" << offsets[i] << "\"/>\n";
  }
  out << "      </CellData>\n    </Piece>\n  </UnstructuredGrid>\n"
      << "  <AppendedData encoding=\"raw\">\n   _";
  for (const auto& b : blocks) {
    const unsigned long long nb = b.bytes.size();
    out.write(reinterpret_cast<const char*>(&nb), sizeof(nb));
    out.write(b.bytes.data(), static_cast<std::streamsize>(nb));
  }
  out << "\n  </AppendedData>\n</VTKFile>\n";
  CFD_CHECK(out.good(), "error while writing field file '" << path << "'");
}

// ---------------------------------------------------------------------------
void writeSurfaceCsv(const std::string& base_path, const SpatialOperator& op) {
  MPI_Comm comm = op.comm();
  int rank = 0, size = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  const std::vector<SurfaceRow> local = collectSurfaceRows(op);
  const int kCols = 15;
  std::vector<double> buf;
  buf.reserve(local.size() * kCols);
  for (const auto& r : local) {
    buf.insert(buf.end(), {r.x, r.y, r.nx, r.ny, r.pressure, r.cp, r.cf, r.rho, r.u, r.v, r.mach,
                           static_cast<double>(r.patch), r.cell_u, r.cell_v, r.cell_pressure});
  }
  std::vector<double> all = gatherv(buf, MPI_DOUBLE, comm, rank, size);
  if (rank != 0) return;

  const std::size_t nrows = all.size() / kCols;
  std::vector<std::size_t> idx(nrows);
  for (std::size_t i = 0; i < nrows; ++i) idx[i] = i;

  // Order rows patch by patch, counter-clockwise around each patch centroid so
  // that the surface distributions plot as a continuous curve.
  std::map<int, std::pair<double, double>> centroid;
  std::map<int, int> counts;
  for (std::size_t i = 0; i < nrows; ++i) {
    const int p = static_cast<int>(all[i * kCols + 11]);
    centroid[p].first += all[i * kCols + 0];
    centroid[p].second += all[i * kCols + 1];
    counts[p]++;
  }
  for (auto& kv : centroid) {
    kv.second.first /= std::max(1, counts[kv.first]);
    kv.second.second /= std::max(1, counts[kv.first]);
  }
  const std::map<int, std::pair<double, double>>& centroid_ref = centroid;
  std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
    const int pa = static_cast<int>(all[a * kCols + 11]);
    const int pb = static_cast<int>(all[b * kCols + 11]);
    if (pa != pb) return pa < pb;
    const auto& ca = centroid_ref.at(pa);
    const double aa = std::atan2(all[a * kCols + 1] - ca.second, all[a * kCols + 0] - ca.first);
    const double ab = std::atan2(all[b * kCols + 1] - ca.second, all[b * kCols + 0] - ca.first);
    return aa < ab;
  });

  std::ofstream out(base_path + "/surface.csv");
  CFD_CHECK(out.good(), "cannot open surface.csv");
  out << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
  out << std::setprecision(10);
  std::ofstream ex(base_path + "/surface_cellcenter.csv");
  ex << "x,y,nx,ny,tag,cell_u,cell_v,cell_pressure,wall_u,wall_v,wall_pressure\n";
  ex << std::setprecision(10);
  for (std::size_t k = 0; k < nrows; ++k) {
    const double* r = &all[idx[k] * kCols];
    const int p = static_cast<int>(r[11]);
    const std::string tag = op.mesh().patch_names[p];
    for (int c = 0; c < 11; ++c) out << r[c] << ",";
    out << tag << "\n";
    ex << r[0] << "," << r[1] << "," << r[2] << "," << r[3] << "," << tag << "," << r[12] << ","
       << r[13] << "," << r[14] << "," << r[8] << "," << r[9] << "," << r[4] << "\n";
  }
}

// ---------------------------------------------------------------------------
namespace {
constexpr char kRestartMagic[8] = {'C', 'F', 'D', 'R', 'S', 'T', '0', '2'};

std::vector<double> gatherGlobalOrdered(const std::vector<Real>& local, const SpatialOperator& op,
                                        int rank, int size) {
  const LocalMesh& m = op.mesh();
  std::vector<long long> gids(m.num_owned);
  std::vector<double> vals(static_cast<std::size_t>(m.num_owned) * kNVar);
  for (Index c = 0; c < m.num_owned; ++c) {
    gids[c] = m.cell_gid[c];
    for (int k = 0; k < kNVar; ++k) vals[c * kNVar + k] = local[c * kNVar + k];
  }
  std::vector<long long> all_g = gatherv(gids, MPI_LONG_LONG, op.comm(), rank, size);
  std::vector<double> all_v = gatherv(vals, MPI_DOUBLE, op.comm(), rank, size);
  if (rank != 0) return {};
  std::vector<double> out(static_cast<std::size_t>(m.global_num_cells) * kNVar, 0.0);
  for (std::size_t i = 0; i < all_g.size(); ++i) {
    for (int k = 0; k < kNVar; ++k) out[all_g[i] * kNVar + k] = all_v[i * kNVar + k];
  }
  return out;
}
}  // namespace

void writeRestart(const std::string& path, const SpatialOperator& op, Real time, long long step,
                  const std::vector<Real>* un, const std::vector<Real>* unm1) {
  int rank = 0, size = 1;
  MPI_Comm_rank(op.comm(), &rank);
  MPI_Comm_size(op.comm(), &size);
  const int levels = (un && unm1) ? 3 : 1;
  std::vector<std::vector<double>> data;
  data.push_back(gatherGlobalOrdered(op.U(), op, rank, size));
  if (levels == 3) {
    data.push_back(gatherGlobalOrdered(*un, op, rank, size));
    data.push_back(gatherGlobalOrdered(*unm1, op, rank, size));
  }
  if (rank != 0) return;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  CFD_CHECK(out.good(), "cannot open restart file '" << path << "'");
  const long long ncells = op.mesh().global_num_cells;
  const int nvar = kNVar;
  out.write(kRestartMagic, 8);
  out.write(reinterpret_cast<const char*>(&nvar), sizeof(int));
  out.write(reinterpret_cast<const char*>(&levels), sizeof(int));
  out.write(reinterpret_cast<const char*>(&ncells), sizeof(long long));
  out.write(reinterpret_cast<const char*>(&time), sizeof(double));
  out.write(reinterpret_cast<const char*>(&step), sizeof(long long));
  for (const auto& d : data)
    out.write(reinterpret_cast<const char*>(d.data()),
              static_cast<std::streamsize>(d.size() * sizeof(double)));
  CFD_CHECK(out.good(), "error while writing restart file '" << path << "'");
}

RestartInfo readRestart(const std::string& path, SpatialOperator& op, std::vector<Real>* un,
                        std::vector<Real>* unm1) {
  int rank = 0;
  MPI_Comm_rank(op.comm(), &rank);
  const LocalMesh& m = op.mesh();
  RestartInfo info;
  std::vector<std::vector<double>> global;
  long long header[2] = {0, 0};
  if (rank == 0) {
    std::ifstream in(path, std::ios::binary);
    CFD_CHECK(in.good(), "cannot open restart file '" << path << "'");
    char magic[8];
    in.read(magic, 8);
    CFD_CHECK(std::memcmp(magic, kRestartMagic, 8) == 0,
              "'" << path << "' is not a solver restart file");
    int nvar = 0, levels = 0;
    long long ncells = 0, step = 0;
    double time = 0.0;
    in.read(reinterpret_cast<char*>(&nvar), sizeof(int));
    in.read(reinterpret_cast<char*>(&levels), sizeof(int));
    in.read(reinterpret_cast<char*>(&ncells), sizeof(long long));
    in.read(reinterpret_cast<char*>(&time), sizeof(double));
    in.read(reinterpret_cast<char*>(&step), sizeof(long long));
    CFD_CHECK(nvar == kNVar, "restart file has " << nvar << " variables, expected " << kNVar);
    CFD_CHECK(levels == 1 || levels == 3,
              "restart file declares " << levels << " time levels (expected 1 or 3)");
    CFD_CHECK(ncells == m.global_num_cells,
              "restart file has " << ncells << " cells but the mesh has " << m.global_num_cells);
    global.resize(levels);
    for (int l = 0; l < levels; ++l) {
      global[l].resize(static_cast<std::size_t>(ncells) * kNVar);
      in.read(reinterpret_cast<char*>(global[l].data()),
              static_cast<std::streamsize>(global[l].size() * sizeof(double)));
      CFD_CHECK(in.good(), "restart file '" << path << "' is truncated");
    }
    info.time = time;
    info.step = step;
    info.levels = levels;
    header[0] = levels;
    header[1] = step;
  }
  MPI_Bcast(header, 2, MPI_LONG_LONG, 0, op.comm());
  MPI_Bcast(&info.time, 1, MPI_DOUBLE, 0, op.comm());
  info.levels = static_cast<int>(header[0]);
  info.step = header[1];

  // Scatter by global cell id: rank 0 sends each rank exactly the rows it owns.
  int size = 1;
  MPI_Comm_size(op.comm(), &size);
  std::vector<long long> gids(m.num_owned);
  for (Index c = 0; c < m.num_owned; ++c) gids[c] = m.cell_gid[c];
  std::vector<int> counts;
  std::vector<long long> all_g = gatherv(gids, MPI_LONG_LONG, op.comm(), rank, size, &counts);
  std::vector<int> displs(size, 0);
  if (rank == 0) {
    int t = 0;
    for (int r = 0; r < size; ++r) { displs[r] = t; t += counts[r]; }
  }
  for (int l = 0; l < info.levels; ++l) {
    std::vector<double> send;
    if (rank == 0) {
      send.resize(all_g.size() * kNVar);
      for (std::size_t i = 0; i < all_g.size(); ++i)
        for (int k = 0; k < kNVar; ++k) send[i * kNVar + k] = global[l][all_g[i] * kNVar + k];
    }
    std::vector<int> vc(size, 0), vd(size, 0);
    for (int r = 0; r < size; ++r) { vc[r] = counts.empty() ? 0 : counts[r] * kNVar; }
    if (rank == 0) { int t = 0; for (int r = 0; r < size; ++r) { vd[r] = t; t += vc[r]; } }
    std::vector<double> recv(static_cast<std::size_t>(m.num_owned) * kNVar);
    MPI_Scatterv(send.data(), vc.data(), vd.data(), MPI_DOUBLE, recv.data(),
                 static_cast<int>(recv.size()), MPI_DOUBLE, 0, op.comm());
    if (l == 0) {
      op.setConservative(recv);
    } else if (l == 1 && un) {
      std::copy(recv.begin(), recv.end(), un->begin());
    } else if (l == 2 && unm1) {
      std::copy(recv.begin(), recv.end(), unm1->begin());
    }
  }
  return info;
}

// ---------------------------------------------------------------------------
void writePartitionDiagnostics(const std::string& dir,
                               const std::vector<PartitionDiagnostics>& diags,
                               GlobalIndex edge_cut, int rank) {
  if (rank != 0) return;
  std::ofstream csv(dir + "/partition_diagnostics.csv");
  CFD_CHECK(csv.good(), "cannot open partition_diagnostics.csv");
  csv << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,"
         "neighbor_ranks,send_cells,recv_cells\n";
  long long total_owned = 0, min_owned = -1, max_owned = 0;
  for (const auto& d : diags) {
    std::string nb;
    for (std::size_t i = 0; i < d.neighbor_ranks.size(); ++i) {
      if (i) nb += " ";
      nb += std::to_string(d.neighbor_ranks[i]);
    }
    csv << d.rank << "," << d.num_cells_owned << "," << d.num_cells_ghost << ","
        << d.num_boundary_faces << "," << d.num_neighbor_ranks << ",\"" << nb << "\","
        << d.send_cells << "," << d.recv_cells << "\n";
    total_owned += d.num_cells_owned;
    max_owned = std::max<long long>(max_owned, d.num_cells_owned);
    min_owned = (min_owned < 0) ? d.num_cells_owned : std::min<long long>(min_owned, d.num_cells_owned);
  }
  const double mean = diags.empty() ? 0.0 : static_cast<double>(total_owned) / diags.size();
  json j;
  j["num_ranks"] = diags.size();
  j["edge_cut"] = edge_cut;
  j["total_owned_cells"] = total_owned;
  j["min_owned_cells"] = min_owned;
  j["max_owned_cells"] = max_owned;
  j["mean_owned_cells"] = mean;
  j["load_balance_ratio"] = mean > 0 ? static_cast<double>(max_owned) / mean : 1.0;
  json ranks = json::array();
  for (const auto& d : diags) {
    json r;
    r["rank"] = d.rank;
    r["num_cells_owned"] = d.num_cells_owned;
    r["num_cells_ghost"] = d.num_cells_ghost;
    r["num_boundary_faces"] = d.num_boundary_faces;
    r["num_neighbor_ranks"] = d.num_neighbor_ranks;
    r["neighbor_ranks"] = d.neighbor_ranks;
    r["send_cells"] = d.send_cells;
    r["recv_cells"] = d.recv_cells;
    ranks.push_back(r);
  }
  j["ranks"] = ranks;
  std::ofstream js(dir + "/partition_diagnostics.json");
  js << j.dump(2) << "\n";
}

}  // namespace cfd
