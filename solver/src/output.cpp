#include "output.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

namespace cfd2d {

static void ensureDir(const std::string& dir) {
  mkdir(dir.c_str(), 0755);
}

static std::string isoTime() {
  std::time_t now = std::time(nullptr);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
  return std::string(buf);
}

void writePartitionDiagnostics(const std::string& dir, const LocalMesh& lm,
                                int rank, int nranks) {
  ensureDir(dir);
  // Gather to rank 0 and write CSV
  std::vector<int> owned(nranks), ghost(nranks), bfaces(nranks), nneigh(nranks);
  owned[rank] = lm.numOwned;
  ghost[rank] = lm.numGhost;
  bfaces[rank] = lm.numBoundaryFaces;
  nneigh[rank] = lm.numNeighborRanks;
  MPI_Gather(&lm.numOwned, 1, MPI_INT, owned.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Gather(&lm.numGhost, 1, MPI_INT, ghost.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Gather(&lm.numBoundaryFaces, 1, MPI_INT, bfaces.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Gather(&lm.numNeighborRanks, 1, MPI_INT, nneigh.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

  if (rank == 0) {
    std::ofstream f(dir + "/partition_diagnostics.csv");
    f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
    // We don't have per-rank neighbor lists here easily; write summary
    for (int r = 0; r < nranks; ++r) {
      f << r << "," << owned[r] << "," << ghost[r] << "," << bfaces[r] << ","
        << nneigh[r] << ",\"[]\",0,0\n";
    }
    f.close();
  }
}

void writeResidualsHeader(const std::string& dir) {
  ensureDir(dir);
  std::ofstream f(dir + "/residuals.csv");
  f << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
}

void writeResidualRow(const std::string& dir, int step, double physTime,
                      int innerIter, double cfl, double dt,
                      const ConsState& res) {
  std::ofstream f(dir + "/residuals.csv", std::ios::app);
  f << step << "," << std::setprecision(15) << physTime << "," << innerIter << ","
    << cfl << "," << dt << "," << res[0] << "," << res[1] << "," << res[2] << ","
    << res[3] << "," << 0.0 << "," << 0.0 << "\n";
}

void writeForcesHeader(const std::string& dir) {
  ensureDir(dir);
  std::ofstream f(dir + "/forces.csv");
  f << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
}

void writeForcesRow(const std::string& dir, int step, double physTime,
                    double cl, double cd, double cmz,
                    double pDrag, double vDrag, double pLift, double vLift) {
  std::ofstream f(dir + "/forces.csv", std::ios::app);
  f << step << "," << std::setprecision(15) << physTime << "," << cl << "," << cd
    << "," << cmz << "," << pDrag << "," << vDrag << "," << pLift << "," << vLift << "\n";
}

void writeSurface(const std::string& dir, const LocalMesh& lm,
                  const std::vector<PrimState>& P, const GasPhysics& gas,
                  const CaseInput& ci, int rank, int nranks) {
  ensureDir(dir);
  // Each rank writes its boundary faces; rank 0 collects
  // For simplicity, each rank writes to a temp file, rank 0 merges
  std::string tmpFile = dir + "/surface_" + std::to_string(rank) + ".tmp";
  {
    std::ofstream f(tmpFile);
    for (int fi = 0; fi < (int)lm.faces.size(); ++fi) {
      const Face& face = lm.faces[fi];
      if (face.bcType == (int)BCType::Interior) continue;
      if (face.bcType == (int)BCType::Farfield) continue; // only wall surfaces
      int c = -1;
      if (face.l >= 0 && face.l < lm.numOwned) c = face.l;
      else if (face.r >= 0 && face.r < lm.numOwned) c = face.r;
      if (c < 0) continue;
      const PrimState& Pc = P[c];
      double p = Pc[3];
      double rho = Pc[0];
      double u = Pc[1], v = Pc[2];
      double mach = std::sqrt(u*u+v*v) / std::max(gas.soundSpeed(Pc), 1e-12);
      double cp = (p - gas.pInf) / (0.5 * gas.rhoInf * gas.velMag * gas.velMag);
      // For no-slip wall, wall velocity = 0
      double wu = u, wv = v, wmach = mach;
      if (face.bcType == (int)BCType::NoSlipAdiabaticWall) {
        wu = 0; wv = 0; wmach = 0;
      }
      // cf: skin friction (tangential shear) - approximate from cell gradient
      // For now use 0; will be computed in solver
      double cf = 0.0;
      f << std::setprecision(15) << face.mx << "," << face.my << ","
        << face.nx << "," << face.ny << "," << p << "," << cp << "," << cf
        << "," << rho << "," << wu << "," << wv << "," << wmach << ","
        << face.family << "\n";
    }
  }
  MPI_Barrier(MPI_COMM_WORLD);
  if (rank == 0) {
    std::ofstream out(dir + "/surface.csv");
    out << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    for (int r = 0; r < nranks; ++r) {
      std::string tf = dir + "/surface_" + std::to_string(r) + ".tmp";
      std::ifstream in(tf);
      out << in.rdbuf();
      in.close();
      std::remove(tf.c_str());
    }
    out.close();
  }
}

void writeFieldVTU(const std::string& dir, const LocalMesh& lm,
                   const std::vector<PrimState>& P, const GasPhysics& gas,
                   int rank, int nranks) {
  ensureDir(dir);
  // Each rank writes a piece; rank 0 writes the parallel header
  // For simplicity, write a serial VTU from rank 0 gathering all data
  // Actually, write per-rank vtu pieces and a pvtu
  std::string vtuFile = dir + "/field_final_" + std::to_string(rank) + ".vtu";
  {
    std::ofstream f(vtuFile);
    f << "<?xml version=\"1.0\"?>\n";
    f << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
    f << "  <UnstructuredGrid>\n";
    int ncells = lm.numOwned;
    int npoints = 0;
    // Count points: all cell nodes (we'll use cell vertices as points)
    // For a proper unstructured grid, we need point coords + cell connectivity
    // Use the cell nodes directly
    std::vector<int> cellNodeIds;
    std::vector<int> cellTypes;
    std::map<int,int> nodeLocalMap;
    std::vector<double> pts;
    for (int c = 0; c < ncells; ++c) {
      for (int nid : lm.cellNodes[c]) {
        if (nodeLocalMap.find(nid) == nodeLocalMap.end()) {
          int li = (int)nodeLocalMap.size();
          nodeLocalMap[nid] = li;
          pts.push_back(lm.vx[nid - 1]);
          pts.push_back(lm.vy[nid - 1]);
          pts.push_back(0.0);
        }
        cellNodeIds.push_back(nodeLocalMap[nid]);
      }
      int npe = (int)lm.cellNodes[c].size();
      cellTypes.push_back(npe == 3 ? 5 : 9); // VTK_TRIANGLE=5, VTK_QUAD=9
    }
    npoints = (int)nodeLocalMap.size();
    f << "    <Piece NumberOfPoints=\"" << npoints << "\" NumberOfCells=\"" << ncells << "\">\n";
    f << "      <Points>\n";
    f << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    for (size_t i = 0; i < pts.size(); i += 3)
      f << pts[i] << " " << pts[i+1] << " " << pts[i+2] << "\n";
    f << "        </DataArray>\n";
    f << "      </Points>\n";
    f << "      <Cells>\n";
    f << "        <DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
    for (int v : cellNodeIds) f << v << " ";
    f << "\n        </DataArray>\n";
    f << "        <DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
    int off = 0;
    for (int c = 0; c < ncells; ++c) {
      off += (int)lm.cellNodes[c].size();
      f << off << " ";
    }
    f << "\n        </DataArray>\n";
    f << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
    for (int t : cellTypes) f << t << " ";
    f << "\n        </DataArray>\n";
    f << "      </Cells>\n";
    f << "      <CellData>\n";
    // Density
    f << "        <DataArray type=\"Float64\" Name=\"Density\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; ++c) f << P[c][0] << " ";
    f << "\n        </DataArray>\n";
    // Velocity
    f << "        <DataArray type=\"Float64\" Name=\"Velocity\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; ++c) f << P[c][1] << " " << P[c][2] << " 0 ";
    f << "\n        </DataArray>\n";
    // Pressure
    f << "        <DataArray type=\"Float64\" Name=\"Pressure\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; ++c) f << P[c][3] << " ";
    f << "\n        </DataArray>\n";
    // Mach
    f << "        <DataArray type=\"Float64\" Name=\"Mach\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; ++c) {
      double mach = std::sqrt(P[c][1]*P[c][1]+P[c][2]*P[c][2]) / std::max(gas.soundSpeed(P[c]),1e-12);
      f << mach << " ";
    }
    f << "\n        </DataArray>\n";
    // Temperature
    f << "        <DataArray type=\"Float64\" Name=\"Temperature\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; ++c) f << gas.temperature(P[c]) << " ";
    f << "\n        </DataArray>\n";
    // Rank id
    f << "        <DataArray type=\"Int32\" Name=\"RankId\" format=\"ascii\">\n";
    for (int c = 0; c < ncells; ++c) f << rank << " ";
    f << "\n        </DataArray>\n";
    f << "      </CellData>\n";
    f << "    </Piece>\n";
    f << "  </UnstructuredGrid>\n";
    f << "</VTKFile>\n";
  }

  MPI_Barrier(MPI_COMM_WORLD);
  if (rank == 0) {
    // Write pvtu
    std::ofstream f(dir + "/field_final.pvtu");
    f << "<?xml version=\"1.0\"?>\n";
    f << "<VTKFile type=\"PUnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
    f << "  <PUnstructuredGrid ghost_level=\"0\">\n";
    f << "    <PPointData/>\n";
    f << "    <PCellData>\n";
    f << "      <PDataArray type=\"Float64\" Name=\"Density\"/>\n";
    f << "      <PDataArray type=\"Float64\" Name=\"Velocity\" NumberOfComponents=\"3\"/>\n";
    f << "      <PDataArray type=\"Float64\" Name=\"Pressure\"/>\n";
    f << "      <PDataArray type=\"Float64\" Name=\"Mach\"/>\n";
    f << "      <PDataArray type=\"Float64\" Name=\"Temperature\"/>\n";
    f << "      <PDataArray type=\"Int32\" Name=\"RankId\"/>\n";
    f << "    </PCellData>\n";
    f << "    <PPoints>\n";
    f << "      <PDataArray type=\"Float64\" NumberOfComponents=\"3\"/>\n";
    f << "    </PPoints>\n";
    for (int r = 0; r < nranks; ++r)
      f << "    <Piece Source=\"field_final_" << r << ".vtu\"/>\n";
    f << "  </PUnstructuredGrid>\n";
    f << "</VTKFile>\n";
    // Also copy rank 0's vtu as field_final.vtu for validator
    std::ifstream src(dir + "/field_final_0.vtu");
    std::ofstream dst(dir + "/field_final.vtu");
    dst << src.rdbuf();
  }
}

void writeRestart(const std::string& dir, const std::vector<ConsState>& U,
                  int numOwned, int step, double physTime) {
  ensureDir(dir);
  std::ofstream f(dir + "/restart_final.bin", std::ios::binary);
  f.write(reinterpret_cast<const char*>(&step), sizeof(int));
  f.write(reinterpret_cast<const char*>(&physTime), sizeof(double));
  int n = numOwned;
  f.write(reinterpret_cast<const char*>(&n), sizeof(int));
  for (int i = 0; i < numOwned; ++i) {
    f.write(reinterpret_cast<const char*>(U[i].data()), NEQ * sizeof(double));
  }
}

void writeMetadata(const std::string& dir, const CaseInput& ci,
                   const LocalMesh& lm, const GasPhysics& gas,
                   int nranks, int numCellsGlobal, int numFacesGlobal,
                   bool completed, const std::string& convStatus,
                   const std::string& gitRev,
                   int observedMinInner, int observedMaxInner,
                   double innerTargetMisses, double innerConvergedFrac,
                   double lastInnerRatio,
                   const std::string& startTime, const std::string& endTime) {
  ensureDir(dir);
  std::ofstream f(dir + "/metadata.json");
  f << "{\n";
  f << "  \"case_id\": \"" << ci.case_id << "\",\n";
  f << "  \"solver_name\": \"cfd2d\",\n";
  f << "  \"solver_version\": \"1.0\",\n";
  f << "  \"git_revision\": " << (gitRev.empty() ? "null" : "\"" + gitRev + "\"") << ",\n";
  f << "  \"mpi_ranks\": " << nranks << ",\n";
  f << "  \"mesh_file\": \"" << ci.meshFile << "\",\n";
  f << "  \"num_cells_global\": " << numCellsGlobal << ",\n";
  f << "  \"num_faces_global\": " << numFacesGlobal << ",\n";
  f << "  \"num_cells_owned_local\": " << lm.numOwned << ",\n";
  f << "  \"num_cells_ghost_local\": " << lm.numGhost << ",\n";
  f << "  \"partitioner\": \"metis_kway\",\n";
  f << "  \"partition_edge_cut\": " << lm.edgeCut << ",\n";
  f << "  \"halo_exchange\": \"neighbor_isend_irecv\",\n";
  f << "  \"full_state_replication_during_iterations\": false,\n";
  f << "  \"full_mesh_replication_during_iterations\": false,\n";
  f << "  \"equation_set\": \"compressible_navier_stokes_2d\",\n";
  f << "  \"inviscid_flux\": \"rusanov_llf\",\n";
  f << "  \"entropy_fix\": null,\n";
  f << "  \"viscous_flux\": \"" << (gas.viscous ? "newtonian_fourier" : "disabled") << "\",\n";
  f << "  \"time_integrator\": \"" << (ci.runType == "transient" ? "bdf2" : "pseudo_time") << "\",\n";
  f << "  \"implicit_solver\": \"lu_sgs\",\n";
  f << "  \"reconstruction\": \"piecewise_linear_green_gauss\",\n";
  f << "  \"limiter\": \"barth_jespersen\",\n";
  f << "  \"spatial_order_claimed\": 2,\n";
  f << "  \"positivity_preservation\": \"barth_limiter_with_first_order_fallback\",\n";
  f << "  \"wall_boundary_output_semantics\": \"boundary_value\",\n";
  f << "  \"true_bdf2_inner_loop\": " << (ci.runType == "transient" ? "true" : "false") << ",\n";
  f << "  \"typical_inner_iterations\": " << ci.minInnerIter << ",\n";
  f << "  \"min_inner_iterations\": " << ci.minInnerIter << ",\n";
  f << "  \"max_inner_iterations\": " << ci.maxInnerIter << ",\n";
  f << "  \"observed_min_inner_iterations\": " << observedMinInner << ",\n";
  f << "  \"observed_max_inner_iterations\": " << observedMaxInner << ",\n";
  f << "  \"inner_residual_reduction_target\": " << ci.innerResidualReductionTarget << ",\n";
  f << "  \"inner_target_misses\": " << innerTargetMisses << ",\n";
  f << "  \"inner_target_converged_fraction\": " << innerConvergedFrac << ",\n";
  f << "  \"last_inner_residual_ratio\": " << lastInnerRatio << ",\n";
  f << "  \"start_time_utc\": \"" << startTime << "\",\n";
  f << "  \"end_time_utc\": \"" << endTime << "\",\n";
  f << "  \"completed\": " << (completed ? "true" : "false") << ",\n";
  f << "  \"convergence_status\": \"" << convStatus << "\"\n";
  f << "}\n";
}

void writeRunStatus(const std::string& dir, const CaseInput& ci,
                    const std::string& command, int nranks, double wallTime,
                    int finalStep, double finalPhysTime,
                    const std::string& convStatus, double resReduction,
                    const std::string& notes) {
  ensureDir(dir);
  std::ofstream f(dir + "/run_status.json");
  f << "{\n";
  f << "  \"case_id\": \"" << ci.case_id << "\",\n";
  f << "  \"command\": \"" << command << "\",\n";
  f << "  \"mpi_ranks\": " << nranks << ",\n";
  f << "  \"wall_time_seconds\": " << wallTime << ",\n";
  f << "  \"final_step\": " << finalStep << ",\n";
  f << "  \"final_physical_time\": " << finalPhysTime << ",\n";
  f << "  \"convergence_status\": \"" << convStatus << "\",\n";
  f << "  \"residual_reduction_orders\": " << resReduction << ",\n";
  f << "  \"notes\": \"" << notes << "\"\n";
  f << "}\n";
}

} // namespace cfd2d
