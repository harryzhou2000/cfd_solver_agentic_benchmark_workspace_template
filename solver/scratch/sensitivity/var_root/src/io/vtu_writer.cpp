#include "io/vtu_writer.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <vector>
#include <algorithm>

#include "core/exceptions.h"
#include "core/logging.h"

namespace cns2d {
namespace {

// VTK cell type ids.
constexpr int kVtkTriangle = 5;
constexpr int kVtkQuad = 9;

// One cell's worth of geometry + field values, as gathered on rank 0.
struct CellRecord {
  GlobalIndex global_id{0};
  std::int32_t num_nodes{0};
  std::int32_t rank{0};
  Real x[4]{};
  Real y[4]{};
  Real density{0.0};
  Real u{0.0};
  Real v{0.0};
  Real pressure{0.0};
  Real mach{0.0};
  Real temperature{0.0};
  Real total_energy{0.0};
  Real vorticity{0.0};
};

// Node deduplication key: quantised coordinates.  The mesh nodes are exact
// doubles shared between neighbouring cells, so an exact bit comparison is
// sufficient and avoids introducing a tolerance.
struct NodeKey {
  Real x;
  Real y;
  bool operator<(const NodeKey &o) const {
    if (x != o.x) return x < o.x;
    return y < o.y;
  }
};

}  // namespace

void writeFieldVtu(const std::string &path, const DistributedMesh &mesh, const FlowContext &flow,
                   const ResidualAssembler &assembler, const StateField &U, MPI_Comm comm) {
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  const Index num_owned = mesh.numOwned();
  const StateField &W = assembler.primitives();
  const GradientField &grad = assembler.gradients();

  // --- pack owned cells --------------------------------------------------
  std::vector<CellRecord> local(static_cast<std::size_t>(num_owned));
  for (Index c = 0; c < num_owned; ++c) {
    CellRecord &r = local[static_cast<std::size_t>(c)];
    r.global_id = mesh.globalCellId()[static_cast<std::size_t>(c)];
    r.num_nodes = mesh.cellNumNodes()[static_cast<std::size_t>(c)];
    r.rank = rank;
    for (int k = 0; k < r.num_nodes; ++k) {
      r.x[k] = mesh.cellNodes()[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)].x;
      r.y[k] = mesh.cellNodes()[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)].y;
    }
    const Real *w = W.cell(c);
    r.density = w[kPrimRho];
    r.u = w[kPrimU];
    r.v = w[kPrimV];
    r.pressure = w[kPrimP];
    const Real speed = std::sqrt(w[kPrimU] * w[kPrimU] + w[kPrimV] * w[kPrimV]);
    if (w[kPrimRho] > 0.0 && w[kPrimP] > 0.0) {
      r.mach = speed / flow.gas.soundSpeed(w[kPrimRho], w[kPrimP]);
      r.temperature = flow.gas.temperatureFromRhoP(w[kPrimRho], w[kPrimP]);
    }
    r.total_energy = U.get(c)[kRhoE] / std::max(w[kPrimRho], kTiny);
    // Vorticity omega_z = dv/dx - du/dy, from the least-squares gradients.
    const Vec2 gu = grad.get(c, kPrimU);
    const Vec2 gv = grad.get(c, kPrimV);
    r.vorticity = gv.x - gu.y;
  }

  // --- gather to rank 0 --------------------------------------------------
  std::vector<int> counts(static_cast<std::size_t>(size), 0);
  const int my_bytes = static_cast<int>(local.size() * sizeof(CellRecord));
  MPI_Gather(&my_bytes, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
  std::vector<int> displs(static_cast<std::size_t>(size), 0);
  int total_bytes = 0;
  if (rank == 0) {
    for (int r = 0; r < size; ++r) {
      displs[static_cast<std::size_t>(r)] = total_bytes;
      total_bytes += counts[static_cast<std::size_t>(r)];
    }
  }
  std::vector<CellRecord> all;
  if (rank == 0) all.resize(static_cast<std::size_t>(total_bytes) / sizeof(CellRecord));
  MPI_Gatherv(local.data(), my_bytes, MPI_BYTE, rank == 0 ? all.data() : nullptr, counts.data(),
              displs.data(), MPI_BYTE, 0, comm);

  if (rank != 0) {
    MPI_Barrier(comm);
    return;
  }

  // Order by global cell id so the file is identical regardless of rank count
  // (except for the 'rank' array, which is the partition map by design).
  std::sort(all.begin(), all.end(),
            [](const CellRecord &a, const CellRecord &b) { return a.global_id < b.global_id; });

  // --- build the deduplicated node list ---------------------------------
  std::map<NodeKey, std::int64_t> node_id;
  std::vector<Real> node_x;
  std::vector<Real> node_y;
  std::vector<std::vector<std::int64_t>> cell_nodes(all.size());
  for (std::size_t c = 0; c < all.size(); ++c) {
    const CellRecord &r = all[c];
    cell_nodes[c].resize(static_cast<std::size_t>(r.num_nodes));
    for (int k = 0; k < r.num_nodes; ++k) {
      const NodeKey key{r.x[k], r.y[k]};
      auto it = node_id.find(key);
      if (it == node_id.end()) {
        const std::int64_t id = static_cast<std::int64_t>(node_x.size());
        node_id.emplace(key, id);
        node_x.push_back(key.x);
        node_y.push_back(key.y);
        cell_nodes[c][static_cast<std::size_t>(k)] = id;
      } else {
        cell_nodes[c][static_cast<std::size_t>(k)] = it->second;
      }
    }
  }

  std::ofstream out(path);
  if (!out) throw CnsError("cannot open field file for writing: " + path);
  out.setf(std::ios::scientific);
  out.precision(9);

  const std::size_t num_points = node_x.size();
  const std::size_t num_cells = all.size();

  out << "<?xml version=\"1.0\"?>\n";
  out << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
  out << "  <UnstructuredGrid>\n";
  out << "    <Piece NumberOfPoints=\"" << num_points << "\" NumberOfCells=\"" << num_cells
      << "\">\n";

  out << "      <Points>\n";
  out << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
  for (std::size_t i = 0; i < num_points; ++i) {
    out << "          " << node_x[i] << ' ' << node_y[i] << ' ' << 0.0 << '\n';
  }
  out << "        </DataArray>\n";
  out << "      </Points>\n";

  out << "      <Cells>\n";
  out << "        <DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n";
  for (std::size_t c = 0; c < num_cells; ++c) {
    out << "         ";
    for (const std::int64_t n : cell_nodes[c]) out << ' ' << n;
    out << '\n';
  }
  out << "        </DataArray>\n";
  out << "        <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n";
  {
    std::int64_t offset = 0;
    for (std::size_t c = 0; c < num_cells; ++c) {
      offset += static_cast<std::int64_t>(cell_nodes[c].size());
      out << "          " << offset << '\n';
    }
  }
  out << "        </DataArray>\n";
  out << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
  for (std::size_t c = 0; c < num_cells; ++c) {
    out << "          " << (all[c].num_nodes == 3 ? kVtkTriangle : kVtkQuad) << '\n';
  }
  out << "        </DataArray>\n";
  out << "      </Cells>\n";

  out << "      <CellData Scalars=\"density\" Vectors=\"velocity\">\n";

  auto writeScalar = [&](const char *name, Real (*getter)(const CellRecord &)) {
    out << "        <DataArray type=\"Float64\" Name=\"" << name
        << "\" NumberOfComponents=\"1\" format=\"ascii\">\n";
    for (std::size_t c = 0; c < num_cells; ++c) {
      out << "          " << getter(all[c]) << '\n';
    }
    out << "        </DataArray>\n";
  };

  writeScalar("density", [](const CellRecord &r) { return r.density; });
  out << "        <DataArray type=\"Float64\" Name=\"velocity\" NumberOfComponents=\"3\" "
         "format=\"ascii\">\n";
  for (std::size_t c = 0; c < num_cells; ++c) {
    out << "          " << all[c].u << ' ' << all[c].v << ' ' << 0.0 << '\n';
  }
  out << "        </DataArray>\n";
  writeScalar("pressure", [](const CellRecord &r) { return r.pressure; });
  writeScalar("mach", [](const CellRecord &r) { return r.mach; });
  writeScalar("temperature", [](const CellRecord &r) { return r.temperature; });
  writeScalar("total_energy", [](const CellRecord &r) { return r.total_energy; });
  writeScalar("vorticity", [](const CellRecord &r) { return r.vorticity; });
  out << "        <DataArray type=\"Int32\" Name=\"rank\" NumberOfComponents=\"1\" "
         "format=\"ascii\">\n";
  for (std::size_t c = 0; c < num_cells; ++c) {
    out << "          " << all[c].rank << '\n';
  }
  out << "        </DataArray>\n";
  out << "      </CellData>\n";

  out << "    </Piece>\n";
  out << "  </UnstructuredGrid>\n";
  out << "</VTKFile>\n";
  out.flush();
  if (!out.good()) throw CnsError("failed while writing field file: " + path);

  MPI_Barrier(comm);
}

}  // namespace cns2d
