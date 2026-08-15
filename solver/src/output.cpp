#include "output.hpp"

#include <nlohmann/json.hpp>

#include <mpi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#ifndef GIT_REVISION
#define GIT_REVISION "unknown"
#endif

namespace cfd {

using nlohmann::json;

namespace {

std::string utcNow() {
  // ISO-8601 UTC timestamp (best effort; no external deps needed)
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[40];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

void writeCsv(const std::string& path, const std::string& header,
              const std::vector<std::vector<double>>& rows) {
  std::ofstream f(path);
  f << header << "\n";
  for (const auto& r : rows) {
    for (size_t k = 0; k < r.size(); ++k) {
      if (k) f << ",";
      f << std::setprecision(16) << r[k];
    }
    f << "\n";
  }
}

std::string bcTypeName(int bc) {
  switch (static_cast<BcType>(bc)) {
    case BcType::Farfield: return "farfield";
    case BcType::SlipWall: return "slip_wall";
    case BcType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
  }
  return "unknown";
}

}  // namespace

void gatherFieldData(const LocalMesh& mesh, const std::vector<State>& U, int rank,
                     int nRanks, OutputData& out) {
  // Pack owned-cell data: ints (globalId, nverts, vertIds) and doubles
  // (coords, U).
  std::vector<int> ints;
  std::vector<double> dbls;
  for (int i = 0; i < mesh.nOwned; ++i) {
    ints.push_back(mesh.globalId[i]);
    ints.push_back(rank);
    ints.push_back(static_cast<int>(mesh.cellPoints[i].size()));
    for (int v : mesh.cellPointIds[i]) ints.push_back(v);
    for (const auto& p : mesh.cellPoints[i]) {
      dbls.push_back(p[0]);
      dbls.push_back(p[1]);
    }
    for (int k = 0; k < 4; ++k) dbls.push_back(U[i][k]);
  }

  std::vector<int> intCounts(nRanks), intOffs(nRanks);
  std::vector<int> dblCounts(nRanks), dblOffs(nRanks);
  int ni = static_cast<int>(ints.size());
  int nd = static_cast<int>(dbls.size());
  MPI_Gather(&ni, 1, MPI_INT, intCounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Gather(&nd, 1, MPI_INT, dblCounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

  std::vector<int> allInts;
  std::vector<double> allDbls;
  if (rank == 0) {
    int totalI = 0, totalD = 0;
    for (int r = 0; r < nRanks; ++r) {
      intOffs[r] = totalI;
      totalI += intCounts[r];
      dblOffs[r] = totalD;
      totalD += dblCounts[r];
    }
    allInts.resize(totalI);
    allDbls.resize(totalD);
  }
  MPI_Gatherv(ints.data(), ni, MPI_INT, allInts.data(), intCounts.data(), intOffs.data(),
              MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Gatherv(dbls.data(), nd, MPI_DOUBLE, allDbls.data(), dblCounts.data(),
              dblOffs.data(), MPI_DOUBLE, 0, MPI_COMM_WORLD);

  if (rank == 0) {
    out.cellGlobalId.clear();
    out.cellPoints.clear();
    out.cellPointIds.clear();
    out.U.clear();
    size_t ip = 0, dp = 0;
    for (int r = 0; r < nRanks; ++r) {
      size_t endI = static_cast<size_t>(intOffs[r] + intCounts[r]);
      while (ip < endI) {
        const int gid = allInts[ip++];
        const int rk = allInts[ip++];
        const int nv = allInts[ip++];
        std::vector<int> ids(nv);
        for (int k = 0; k < nv; ++k) ids[k] = allInts[ip++];
        std::vector<Vec2> pts(nv);
        for (int k = 0; k < nv; ++k) {
          pts[k] = {allDbls[dp], allDbls[dp + 1]};
          dp += 2;
        }
        State u;
        for (int k = 0; k < 4; ++k) u[k] = allDbls[dp++];
        out.cellGlobalId.push_back(gid);
        out.cellRank.push_back(rk);
        out.cellPointIds.push_back(std::move(ids));
        out.cellPoints.push_back(std::move(pts));
        out.U.push_back(u);
      }
    }
  }
}

void gatherPartitionStats(const LocalMesh& mesh, int rank, int nRanks, OutputData& out) {
  long vals[2] = {mesh.nOwned, mesh.nGhost};
  std::vector<long> allOwned(nRanks * 2);
  MPI_Gather(vals, 2, MPI_LONG, allOwned.data(), 2, MPI_LONG, 0, MPI_COMM_WORLD);
  long bf = static_cast<long>(mesh.wallFaceIdx.size());
  std::vector<long> allBf(nRanks);
  MPI_Gather(&bf, 1, MPI_LONG, allBf.data(), 1, MPI_LONG, 0, MPI_COMM_WORLD);
  long sr[2] = {0, 0};
  for (const auto& v : mesh.sendCells) sr[0] += static_cast<long>(v.size());
  for (const auto& v : mesh.recvCells) sr[1] += static_cast<long>(v.size());
  std::vector<long> allSr(nRanks * 2);
  MPI_Gather(sr, 2, MPI_LONG, allSr.data(), 2, MPI_LONG, 0, MPI_COMM_WORLD);

  // neighbor rank lists per rank (variable length)
  std::string localNbrs;
  for (size_t k = 0; k < mesh.neighborRanks.size(); ++k) {
    if (k) localNbrs += ";";
    localNbrs += std::to_string(mesh.neighborRanks[k]);
  }
  std::vector<int> lens(nRanks);
  int llen = static_cast<int>(localNbrs.size());
  MPI_Gather(&llen, 1, MPI_INT, lens.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  std::vector<int> offs(nRanks);
  std::vector<char> allNbrs;
  if (rank == 0) {
    int total = 0;
    for (int r = 0; r < nRanks; ++r) {
      offs[r] = total;
      total += lens[r];
    }
    allNbrs.resize(total);
  }
  MPI_Gatherv(localNbrs.data(), llen, MPI_CHAR, allNbrs.data(), lens.data(), offs.data(),
              MPI_CHAR, 0, MPI_COMM_WORLD);

  if (rank == 0) {
    out.ownedGhost.resize(nRanks);
    out.boundarySend.resize(nRanks);
    out.boundaryFacesPerRank.resize(nRanks);
    out.neighborRanksAll.resize(nRanks);
    for (int r = 0; r < nRanks; ++r) {
      out.ownedGhost[r] = {allOwned[r * 2], allOwned[r * 2 + 1]};
      out.boundarySend[r] = {allSr[r * 2], allSr[r * 2 + 1]};
      out.boundaryFacesPerRank[r] = allBf[r];
      std::string s(allNbrs.data() + offs[r], lens[r]);
      size_t p = 0;
      while (p < s.size()) {
        size_t q = s.find(';', p);
        if (q == std::string::npos) q = s.size();
        if (q > p) out.neighborRanksAll[r].push_back(std::stoi(s.substr(p, q - p)));
        p = q + 1;
      }
    }
  }
}

void writeOutputs(const Case& c, const OutputData& out, int rank, int nRanks,
                  const std::string& outDir, const SolveStats& stats,
                  const std::string& commandLine, const std::string& fieldFileName,
                  bool writeIntermediateField) {
  if (rank != 0) return;
  std::string dir = outDir;
  if (dir.back() != '/') dir += '/';

  // Intermediate field dumps only need the field file.
  if (writeIntermediateField) {
    writeVtu(dir + fieldFileName, out, c.gas);
    return;
  }

  // ---- residuals.csv ----
  {
    std::ofstream f(dir + "residuals.csv");
    f << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
    for (const auto& r : out.residuals)
      f << std::setprecision(16) << r.step << "," << r.time << "," << r.innerIter << ","
        << r.cfl << "," << r.dt << "," << r.rho << "," << r.rhou << "," << r.rhov << ","
        << r.rhoE << "," << r.l2 << "," << r.linf << "\n";
  }
  // ---- forces.csv ----
  {
    std::ofstream f(dir + "forces.csv");
    f << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
    for (const auto& r : out.forces)
      f << std::setprecision(16) << r.step << "," << r.time << "," << r.cl << "," << r.cd
        << "," << r.cmz << "," << r.pressureDrag << "," << r.viscousDrag << ","
        << r.pressureLift << "," << r.viscousLift << "\n";
  }
  // ---- surface.csv ----
  {
    std::ofstream f(dir + "surface.csv");
    f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    for (const auto& r : out.wall)
      f << std::setprecision(16) << r.x << "," << r.y << "," << r.nx << "," << r.ny << ","
        << r.pressure << "," << r.cp << "," << r.cf << "," << r.rho << "," << r.u << ","
        << r.v << "," << r.mach << "," << r.tag << "\n";
  }
  // ---- partition_diagnostics.csv ----
  {
    std::ofstream f(dir + "partition_diagnostics.csv");
    f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,"
         "neighbor_ranks,send_cells,recv_cells\n";
    for (int r = 0; r < nRanks; ++r) {
      f << r << "," << out.ownedGhost[r][0] << "," << out.ownedGhost[r][1] << ","
        << out.boundaryFacesPerRank[r] << "," << out.neighborRanksAll[r].size() << ",\"";
      for (size_t k = 0; k < out.neighborRanksAll[r].size(); ++k) {
        if (k) f << ";";
        f << out.neighborRanksAll[r][k];
      }
      f << "\"," << out.boundarySend[r][0] << "," << out.boundarySend[r][1] << "\n";
    }
  }

  // ---- metadata.json / run_status.json ----
  {
    json md;
    md["case_id"] = c.case_id;
    md["solver_name"] = "cfd_solver";
    md["solver_version"] = "1.0.0";
    md["git_revision"] = GIT_REVISION;
    md["mpi_ranks"] = nRanks;
    md["mesh_file"] = c.mesh_file;
    md["num_cells_global"] = out.numCellsGlobal;
    md["num_faces_global"] = out.numFacesGlobal;
    md["num_cells_owned_local"] = out.ownedGhost[0][0];
    md["num_cells_ghost_local"] = out.ownedGhost[0][1];
    md["partitioner"] = nRanks == 1 ? "metis_kway (single-rank trivial)" : "metis_kway";
    md["partition_edge_cut"] = out.edgeCut;
    md["halo_exchange"] = "neighbor_isend_irecv";
    md["full_state_replication_during_iterations"] = false;
    md["full_mesh_replication_during_iterations"] = false;
    md["equation_set"] = "compressible_navier_stokes_2d";
    md["inviscid_flux"] = "rusanov_llf";
    md["entropy_fix"] = nullptr;
    md["viscous_flux"] = c.isLaminar() ? "central_gradient_with_directional_correction"
                                       : "disabled";
    md["time_integrator"] =
        c.run.type == "transient" ? "bdf2_dual_time" : "implicit_pseudo_time";
    md["implicit_solver"] = "lusgs_matrix_free";
    md["reconstruction"] = "weighted_least_squares_piecewise_linear";
    md["limiter"] = "barth_jespersen";
    md["spatial_order_claimed"] = 2;
    md["positivity_preservation"] = "first_order_fallback_on_negative_rho_or_p";
    md["wall_boundary_output_semantics"] = "boundary_value";
    md["true_bdf2_inner_loop"] = (c.run.type == "transient");
    md["typical_inner_iterations"] =
        stats.innerCount > 0 ? static_cast<double>(stats.innerSum) / stats.innerCount
                             : c.run.min_inner_iterations;
    md["min_inner_iterations"] = c.run.min_inner_iterations;
    md["max_inner_iterations"] = c.run.max_inner_iterations;
    md["observed_min_inner_iterations"] = stats.innerMin;
    md["observed_max_inner_iterations"] = stats.innerMax;
    md["inner_residual_reduction_target"] = c.run.inner_residual_reduction_target;
    md["inner_target_misses"] = stats.innerCount - static_cast<long>(
        stats.convergedFraction * stats.innerCount);
    md["inner_target_converged_fraction"] = stats.convergedFraction;
    md["last_inner_residual_ratio"] = stats.innerLastRatio;
    md["start_time_utc"] = utcNow();
    md["end_time_utc"] = utcNow();
    md["completed"] = true;
    md["convergence_status"] = stats.convergenceStatus;
    std::ofstream f(dir + "metadata.json");
    f << md.dump(2) << "\n";
  }
  {
    json st;
    st["case_id"] = c.case_id;
    st["command"] = commandLine;
    st["mpi_ranks"] = nRanks;
    st["wall_time_seconds"] = stats.wallSeconds;
    st["final_step"] = stats.finalStep;
    st["final_physical_time"] = stats.finalTime;
    st["convergence_status"] = stats.convergenceStatus;
    st["residual_reduction_orders"] = stats.residualReductionOrders;
    st["notes"] = "";
    std::ofstream f(dir + "run_status.json");
    f << st.dump(2) << "\n";
  }

  // ---- field file ----
  if (c.outputs.write_final_field || writeIntermediateField) {
    writeVtu(dir + fieldFileName, out, c.gas);
  }
}

void writeRestartFile(const Case& c, const OutputData& out, int rank, int nRanks,
                      const std::string& outDir, const std::string& name, long step,
                      double time) {
  (void)nRanks;
  if (rank != 0) return;
  std::string dir = outDir;
  if (dir.back() != '/') dir += '/';
  std::ofstream f(dir + name, std::ios::binary);
  const char magic[9] = "CFDREST1";
  f.write(magic, 8);
  int version = 1;
  f.write(reinterpret_cast<const char*>(&version), sizeof(int));
  f.write(reinterpret_cast<const char*>(&step), sizeof(long));
  f.write(reinterpret_cast<const char*>(&time), sizeof(double));
  long n = static_cast<long>(out.U.size());
  f.write(reinterpret_cast<const char*>(&n), sizeof(long));
  f.write(reinterpret_cast<const char*>(out.U.data()), n * sizeof(State));
}

void writeVtu(const std::string& path, const OutputData& out, const GasModel& gas) {
  // Build a unique point list from global vertex ids.
  std::vector<Vec2> pts;
  std::vector<int> gidToPt;
  std::unordered_map<int, int> gidMap;
  std::vector<int> cellOffsets(out.cellPoints.size() + 1, 0);
  std::vector<int> cellTypes(out.cellPoints.size(), 5);  // VTK_TRIANGLE
  std::vector<int> connectivity;
  for (size_t c = 0; c < out.cellPoints.size(); ++c) {
    const int nv = static_cast<int>(out.cellPoints[c].size());
    cellTypes[c] = (nv == 4) ? 9 : 5;  // VTK_QUAD / VTK_TRIANGLE
    for (int k = 0; k < nv; ++k) {
      const int gid = out.cellPointIds[c][k];
      auto it = gidMap.find(gid);
      int pi;
      if (it == gidMap.end()) {
        pi = static_cast<int>(pts.size());
        gidMap[gid] = pi;
        pts.push_back(out.cellPoints[c][k]);
      } else {
        pi = it->second;
      }
      connectivity.push_back(pi);
    }
    cellOffsets[c + 1] = cellOffsets[c] + nv;
  }

  std::ofstream f(path);
  f << "<?xml version=\"1.0\"?>\n";
  f << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
  f << "  <UnstructuredGrid>\n";
  f << "    <Piece NumberOfPoints=\"" << pts.size()
    << "\" NumberOfCells=\"" << out.cellPoints.size() << "\">\n";
  f << "      <Points>\n";
  f << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
  for (const auto& p : pts)
    f << std::setprecision(12) << p[0] << " " << p[1] << " 0\n";
  f << "        </DataArray>\n      </Points>\n";
  f << "      <Cells>\n";
  f << "        <DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n";
  for (int v : connectivity) f << v << " ";
  f << "\n        </DataArray>\n";
  f << "        <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n";
  for (size_t i = 1; i < cellOffsets.size(); ++i) f << cellOffsets[i] << " ";
  f << "\n        </DataArray>\n";
  f << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
  for (int t : cellTypes) f << t << " ";
  f << "\n        </DataArray>\n      </Cells>\n";
  f << "      <CellData>\n";

  // density
  f << "        <DataArray type=\"Float64\" Name=\"Density\" format=\"ascii\">\n";
  for (const auto& u : out.U) f << std::setprecision(12) << u[0] << "\n";
  f << "        </DataArray>\n";
  // velocity
  f << "        <DataArray type=\"Float64\" Name=\"Velocity\" NumberOfComponents=\"3\" "
       "format=\"ascii\">\n";
  for (const auto& u : out.U)
    f << std::setprecision(12) << u[1] / u[0] << " " << u[2] / u[0] << " 0\n";
  f << "        </DataArray>\n";
  // pressure, mach, temperature, total energy, rank, global id
  f << "        <DataArray type=\"Float64\" Name=\"Pressure\" format=\"ascii\">\n";
  for (const auto& u : out.U) {
    Prim w = toPrimitive(u, gas);
    f << std::setprecision(12) << w[3] << "\n";
  }
  f << "        </DataArray>\n";
  f << "        <DataArray type=\"Float64\" Name=\"Mach\" format=\"ascii\">\n";
  for (const auto& u : out.U) {
    Prim w = toPrimitive(u, gas);
    f << std::setprecision(12) << mach(w, gas) << "\n";
  }
  f << "        </DataArray>\n";
  f << "        <DataArray type=\"Float64\" Name=\"Temperature\" format=\"ascii\">\n";
  for (const auto& u : out.U) {
    Prim w = toPrimitive(u, gas);
    f << std::setprecision(12) << temperature(w, gas) << "\n";
  }
  f << "        </DataArray>\n";
  f << "        <DataArray type=\"Float64\" Name=\"TotalEnergy\" format=\"ascii\">\n";
  for (const auto& u : out.U) f << std::setprecision(12) << u[3] / u[0] << "\n";
  f << "        </DataArray>\n";
  f << "        <DataArray type=\"Int32\" Name=\"RankId\" format=\"ascii\">\n";
  for (int rk : out.cellRank) f << rk << "\n";
  f << "        </DataArray>\n";
  f << "        <DataArray type=\"Int32\" Name=\"GlobalCellId\" format=\"ascii\">\n";
  for (int gid : out.cellGlobalId) f << gid << "\n";
  f << "        </DataArray>\n";
  f << "      </CellData>\n";
  f << "    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n";
}

}  // namespace cfd
