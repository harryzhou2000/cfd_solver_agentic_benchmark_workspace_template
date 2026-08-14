#include "output.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <mpi.h>

#include <nlohmann/json.hpp>

namespace cfd2d {

std::vector<double> gatherCellField(const LocalMesh& lm,
                                    const std::vector<double>& localVals,
                                    int width, int nCellsGlobal, int rank,
                                    int nRanks) {
  const int nOwned = lm.nOwned;
  std::vector<double> packed(nOwned * (1 + width));
  for (int i = 0; i < nOwned; ++i) {
    packed[i * (1 + width)] = static_cast<double>(lm.cellGlobal[i]);
    for (int q = 0; q < width; ++q)
      packed[i * (1 + width) + 1 + q] = localVals[i * width + q];
  }
  int sendCount = static_cast<int>(packed.size());
  std::vector<int> counts, displs;
  if (rank == 0) counts.resize(nRanks);
  MPI_Gather(&sendCount, 1, MPI_INT, rank == 0 ? counts.data() : nullptr, 1,
             MPI_INT, 0, MPI_COMM_WORLD);
  int total = 0;
  if (rank == 0) {
    displs.resize(nRanks);
    for (int r = 0; r < nRanks; ++r) {
      displs[r] = total;
      total += counts[r];
    }
  }
  std::vector<double> recv;
  if (rank == 0) recv.resize(total);
  MPI_Gatherv(packed.data(), sendCount, MPI_DOUBLE,
              rank == 0 ? recv.data() : nullptr,
              rank == 0 ? counts.data() : nullptr,
              rank == 0 ? displs.data() : nullptr, MPI_DOUBLE, 0,
              MPI_COMM_WORLD);
  if (rank != 0) return {};
  std::vector<double> global(nCellsGlobal * width, 0.0);
  for (int r = 0; r < nRanks; ++r) {
    const int n = counts[r] / (1 + width);
    for (int i = 0; i < n; ++i) {
      const int base = displs[r] + i * (1 + width);
      const int g = static_cast<int>(recv[base]);
      for (int q = 0; q < width; ++q) global[g * width + q] = recv[base + 1 + q];
    }
  }
  return global;
}

std::vector<double> scatterCellField(const LocalMesh& lm,
                                     const std::vector<double>& globalVals,
                                     int width, int nCellsGlobal, int rank,
                                     int nRanks) {
  (void)nCellsGlobal;
  std::vector<int> counts, displs;
  if (rank == 0) {
    counts.resize(nRanks);
    displs.resize(nRanks);
  }
  int myCount = lm.nOwned * width;
  MPI_Gather(&myCount, 1, MPI_INT, rank == 0 ? counts.data() : nullptr, 1,
             MPI_INT, 0, MPI_COMM_WORLD);
  // Gather owned global ids so rank 0 can pack values in worker order.
  std::vector<int> gids(lm.nOwned);
  for (int i = 0; i < lm.nOwned; ++i) gids[i] = lm.cellGlobal[i];
  std::vector<int> gc, gd;
  if (rank == 0) {
    gc.resize(nRanks);
  }
  int ng = lm.nOwned;
  MPI_Gather(&ng, 1, MPI_INT, rank == 0 ? gc.data() : nullptr, 1, MPI_INT, 0,
             MPI_COMM_WORLD);
  std::vector<int> allG;
  if (rank == 0) {
    int totalG = 0;
    gd.resize(nRanks);
    for (int r = 0; r < nRanks; ++r) {
      gd[r] = totalG;
      totalG += gc[r];
    }
    allG.resize(totalG);
  }
  MPI_Gatherv(gids.data(), ng, MPI_INT, rank == 0 ? allG.data() : nullptr,
              rank == 0 ? gc.data() : nullptr, rank == 0 ? gd.data() : nullptr,
              MPI_INT, 0, MPI_COMM_WORLD);
  std::vector<double> packed;
  if (rank == 0) {
    int total = 0;
    for (int r = 0; r < nRanks; ++r) {
      displs[r] = total;
      total += counts[r];
    }
    packed.assign(total, 0.0);
    for (int r = 0; r < nRanks; ++r) {
      for (int i = 0; i < gc[r]; ++i) {
        const int g = allG[gd[r] + i];
        for (int q = 0; q < width; ++q)
          packed[displs[r] + i * width + q] = globalVals[g * width + q];
      }
    }
  }
  std::vector<double> local(lm.nOwned * width);
  MPI_Scatterv(rank == 0 ? packed.data() : nullptr,
               rank == 0 ? counts.data() : nullptr,
               rank == 0 ? displs.data() : nullptr, MPI_DOUBLE, local.data(),
               myCount, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  return local;
}

void CsvWriters::open(const std::string& outDir) {
  res_.open(outDir + "/residuals.csv", std::ios::out | std::ios::trunc);
  force_.open(outDir + "/forces.csv", std::ios::out | std::ios::trunc);
  if (!res_ || !force_) throw FatalError("cannot open CSV output files in " + outDir);
  res_ << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
  force_ << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
  res_ << std::setprecision(16);
  force_ << std::setprecision(16);
}

void CsvWriters::logResidual(int step, double time, int innerIter, double cfl,
                             double dt, const ResidualNorms& n) {
  if (!res_) return;
  res_ << step << ',' << time << ',' << innerIter << ',' << cfl << ',' << dt << ','
       << n.l2[0] << ',' << n.l2[1] << ',' << n.l2[2] << ',' << n.l2[3] << ','
       << n.l2Total << ',' << n.linf << '\n';
}

void CsvWriters::logForces(int step, double time, const ForceCoefficients& c) {
  if (!force_) return;
  force_ << step << ',' << time << ',' << c.cl << ',' << c.cd << ',' << c.cmz << ','
         << c.pressureDrag << ',' << c.viscousDrag << ',' << c.pressureLift << ','
         << c.viscousLift << '\n';
}

void CsvWriters::flush() {
  res_.flush();
  force_.flush();
}

void writeSurfaceCsv(const std::string& path,
                     const std::vector<SurfaceRow>& localRows, int rank,
                     int nRanks) {
  // Each row is serialized into fixed-width doubles plus a length-prefixed tag.
  // Use text gather to preserve tag strings and deterministic CSV formatting.
  std::ostringstream local;
  local << std::setprecision(16);
  for (const auto& r : localRows) {
    local << r.x << ',' << r.y << ',' << r.nx << ',' << r.ny << ',' << r.pressure
          << ',' << r.cp << ',' << r.cf << ',' << r.rho << ',' << r.u << ',' << r.v
          << ',' << r.mach << ',' << r.tag << '\n';
  }
  std::string s = local.str();
  int n = static_cast<int>(s.size());
  std::vector<int> counts, displs;
  if (rank == 0) counts.resize(nRanks);
  MPI_Gather(&n, 1, MPI_INT, rank == 0 ? counts.data() : nullptr, 1, MPI_INT, 0,
             MPI_COMM_WORLD);
  int total = 0;
  if (rank == 0) {
    displs.resize(nRanks);
    for (int r = 0; r < nRanks; ++r) {
      displs[r] = total;
      total += counts[r];
    }
  }
  std::vector<char> all;
  if (rank == 0) all.resize(total);
  MPI_Gatherv(s.data(), n, MPI_CHAR, rank == 0 ? all.data() : nullptr,
              rank == 0 ? counts.data() : nullptr,
              rank == 0 ? displs.data() : nullptr, MPI_CHAR, 0,
              MPI_COMM_WORLD);
  if (rank == 0) {
    std::ofstream out(path);
    if (!out) throw FatalError("cannot write " + path);
    out << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    out.write(all.data(), static_cast<std::streamsize>(all.size()));
  }
}

void writeVtu(const std::string& path, const GlobalMesh& gm,
              const FieldSet& fields) {
  std::ofstream out(path);
  if (!out) throw FatalError("cannot write VTK file " + path);
  out << std::setprecision(15);
  out << "<?xml version=\"1.0\"?>\n<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
  out << "<UnstructuredGrid><Piece NumberOfPoints=\"" << gm.nNodes
      << "\" NumberOfCells=\"" << gm.nCells << "\">\n";
  out << "<Points><DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
  for (int i = 0; i < gm.nNodes; ++i) out << gm.nodeX[i] << ' ' << gm.nodeY[i] << " 0\n";
  out << "</DataArray></Points>\n";
  out << "<Cells>\n<DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
  for (int c = 0; c < gm.nCells; ++c) {
    for (int k = 0; k < gm.cellNNodes[c]; ++k) out << gm.cellNodes[c][k] << ' ';
    out << '\n';
  }
  out << "</DataArray>\n<DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
  int off = 0;
  for (int c = 0; c < gm.nCells; ++c) {
    off += gm.cellNNodes[c];
    out << off << ' ';
  }
  out << "\n</DataArray>\n<DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
  for (int c = 0; c < gm.nCells; ++c) out << (gm.cellNNodes[c] == 3 ? 5 : 9) << ' ';
  out << "\n</DataArray></Cells>\n<CellData>\n";
  for (const auto& kv : fields.cellScalars) {
    out << "<DataArray type=\"Float64\" Name=\"" << kv.first
        << "\" format=\"ascii\">\n";
    for (double v : kv.second) out << v << ' ';
    out << "\n</DataArray>\n";
  }
  out << "</CellData></Piece></UnstructuredGrid></VTKFile>\n";
}

void writeRestart(const std::string& path, const std::string& caseId,
                  int64_t step, double time, int nCellsGlobal,
                  const std::vector<double>& U,
                  const std::vector<double>* Uprev) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) throw FatalError("cannot write restart " + path);
  const char magic[8] = {'C', 'F', 'D', '2', 'D', 'R', 'S', 'T'};
  out.write(magic, sizeof(magic));
  uint32_t version = 1;
  uint32_t idLen = static_cast<uint32_t>(caseId.size());
  uint32_t flags = Uprev ? 1u : 0u;
  out.write(reinterpret_cast<const char*>(&version), sizeof(version));
  out.write(reinterpret_cast<const char*>(&idLen), sizeof(idLen));
  out.write(caseId.data(), idLen);
  out.write(reinterpret_cast<const char*>(&step), sizeof(step));
  out.write(reinterpret_cast<const char*>(&time), sizeof(time));
  uint64_t n = static_cast<uint64_t>(nCellsGlobal);
  out.write(reinterpret_cast<const char*>(&n), sizeof(n));
  out.write(reinterpret_cast<const char*>(&flags), sizeof(flags));
  out.write(reinterpret_cast<const char*>(U.data()),
            static_cast<std::streamsize>(U.size() * sizeof(double)));
  if (Uprev)
    out.write(reinterpret_cast<const char*>(Uprev->data()),
              static_cast<std::streamsize>(Uprev->size() * sizeof(double)));
}

RestartData readRestart(const std::string& path, const std::string& caseId,
                        int nCellsGlobal) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw FatalError("cannot open restart " + path);
  char magic[8];
  in.read(magic, sizeof(magic));
  const char expected[8] = {'C', 'F', 'D', '2', 'D', 'R', 'S', 'T'};
  if (std::memcmp(magic, expected, sizeof(magic)) != 0)
    throw FatalError("invalid restart magic");
  uint32_t version = 0, idLen = 0;
  in.read(reinterpret_cast<char*>(&version), sizeof(version));
  in.read(reinterpret_cast<char*>(&idLen), sizeof(idLen));
  if (version != 1 || idLen > 10000) throw FatalError("unsupported restart header");
  std::string id(idLen, '\0');
  in.read(id.data(), idLen);
  if (id != caseId) throw FatalError("restart case_id mismatch");
  RestartData d;
  in.read(reinterpret_cast<char*>(&d.step), sizeof(d.step));
  in.read(reinterpret_cast<char*>(&d.time), sizeof(d.time));
  uint64_t n = 0;
  uint32_t flags = 0;
  in.read(reinterpret_cast<char*>(&n), sizeof(n));
  in.read(reinterpret_cast<char*>(&flags), sizeof(flags));
  if (n != static_cast<uint64_t>(nCellsGlobal)) throw FatalError("restart mesh size mismatch");
  d.U.resize(nCellsGlobal * 4);
  in.read(reinterpret_cast<char*>(d.U.data()), d.U.size() * sizeof(double));
  if (flags & 1u) {
    d.Uprev.resize(nCellsGlobal * 4);
    in.read(reinterpret_cast<char*>(d.Uprev.data()), d.Uprev.size() * sizeof(double));
  }
  if (!in) throw FatalError("truncated restart file");
  return d;
}

void writePartitionDiagnostics(const std::string& path,
                               const std::vector<PartitionRow>& rows) {
  std::ofstream out(path);
  if (!out) throw FatalError("cannot write " + path);
  out << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
  for (const auto& r : rows) {
    std::string safeList = r.neighborList;
    for (auto& ch : safeList) if (ch == ',') ch = ';';
    out << r.rank << ',' << r.nOwned << ',' << r.nGhost << ',' << r.nBoundaryFaces
        << ',' << r.nNeighbors << ',"' << safeList << '"' << ',' << r.sendCells
        << ',' << r.recvCells << '\n';
  }
}

}  // namespace cfd2d
