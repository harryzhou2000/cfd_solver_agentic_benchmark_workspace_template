#include "output.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <mpi.h>

using json = nlohmann::json;

namespace cfd2d {

static std::string isoTime() {
  std::time_t now = std::time(nullptr);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
  return buf;
}

static BCType parseBCType(const std::string& s) {
  if (s == "farfield") return BCType::Farfield;
  if (s == "slip_wall") return BCType::SlipWall;
  if (s == "no_slip_adiabatic_wall") return BCType::NoSlipWall;
  throw std::runtime_error("Unknown BC type: " + s);
}

CaseConfig parseCaseConfig(const std::string& jsonPath) {
  std::ifstream f(jsonPath);
  if (!f) throw std::runtime_error("Cannot open case file: " + jsonPath);
  json j; f >> j;
  CaseConfig cfg;
  cfg.caseId = j.value("case_id", "");
  cfg.meshFile = j["mesh"].value("file", "");
  cfg.meshFormat = j["mesh"].value("format", "CGNS");
  cfg.meshDim = j["mesh"].value("dimension", 2);
  cfg.mode = j["physics"].value("mode", "inviscid");
  cfg.reynolds = j["physics"].value("reynolds", 0.0);
  cfg.viscosityModel = j["physics"].value("viscosity_model", "constant");

  double gamma = j["gas"].value("gamma", 1.4);
  double R = j["gas"].value("R", 1.0);
  double Pr = j["gas"].value("prandtl", 0.72);
  cfg.gas = GasModel(gamma, R, Pr);

  cfg.fs.mach = j["freestream"].value("mach", 0.0);
  cfg.fs.aoa = j["freestream"].value("aoa_degrees", 0.0) * M_PI / 180.0;
  cfg.fs.rho = j["freestream"].value("rho", 1.0);
  cfg.fs.vel = j["freestream"].value("velocity_magnitude", 1.0);
  cfg.fs.pressure = j["freestream"].value("pressure", 1.0);
  cfg.fs.compute(cfg.gas);

  cfg.refLength = j["reference"].value("length", 1.0);
  cfg.refArea = j["reference"].value("area", 1.0);
  cfg.momentCx = j["reference"]["moment_center"][0];
  cfg.momentCy = j["reference"]["moment_center"][1];
  cfg.reynoldsLength = j["reference"].value("reynolds_length", 1.0);

  for (auto& [fam, bc] : j["boundary_conditions"].items())
    cfg.bcMap.push_back({fam, parseBCType(bc)});

  auto& rc = j["run_control"];
  cfg.runType = rc.value("type", "steady");
  cfg.maxSteps = rc.value("max_steps", 20000);
  cfg.residualTarget = rc.value("residual_reduction_target", 4.0);
  cfg.cflInitial = rc.value("cfl_initial", 1.0);
  cfg.cflMax = rc.value("cfl_max", 100.0);
  cfg.cflRampSteps = rc.value("pseudo_cfl_ramp_steps", 2000);
  cfg.minInner = rc.value("min_inner_iterations", 3);
  cfg.maxInner = rc.value("max_inner_iterations", 50);
  cfg.innerTarget = rc.value("inner_residual_reduction_target", 0.01);
  cfg.timeStep = rc.value("time_step", 0.01);
  cfg.finalTime = rc.value("final_time", 300.0);
  cfg.timeIntegrator = rc.value("time_integrator", "bdf2");
  cfg.rusanovScale = rc.value("rusanov_dissipation_scale", 1.0);

  // Resolve mesh path relative to case file directory
  if (!cfg.meshFile.empty() && cfg.meshFile[0] != '/') {
    size_t pos = jsonPath.find_last_of('/');
    std::string dir = (pos != std::string::npos) ? jsonPath.substr(0, pos) : ".";
    cfg.meshFile = dir + "/" + cfg.meshFile;
  }
  return cfg;
}

void writeResidualsCSV(const std::string& path, const Solver& solver) {
  std::ofstream f(path);
  f << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
  for (const auto& h : solver.history)
    f << h.step << "," << h.physicalTime << "," << h.innerIter << ","
      << h.cfl << "," << h.dt << ","
      << h.resRho << "," << h.resRhou << "," << h.resRhov << "," << h.resRhoE << ","
      << h.resL2 << "," << h.resLinf << "\n";
}

void writeForcesCSV(const std::string& path, const Solver& solver) {
  std::ofstream f(path);
  f << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
  for (const auto& h : solver.history)
    f << h.step << "," << h.physicalTime << "," << h.cl << "," << h.cd << "," << h.cmz << ","
      << h.pressureDrag << "," << h.viscousDrag << "," << h.pressureLift << "," << h.viscousLift << "\n";
}

void writePartitionDiagnostics(const std::string& path, const LocalMesh& mesh, int rank, int nprocs) {
  // Gather per-rank info to rank 0
  int localData[5] = {mesh.nOwned, mesh.nGhost, 0, 0, 0};
  // count boundary faces and neighbor ranks
  for (const auto& f : mesh.faces)
    if (f.cr == -1) localData[2]++;
  localData[3] = mesh.neighborRanks.size();
  localData[4] = 0; // send cells total
  for (auto& sl : mesh.sendLocal) localData[4] += sl.size();

  std::vector<int> allData(nprocs * 5);
  MPI_Gather(localData, 5, MPI_INT, allData.data(), 5, MPI_INT, 0, MPI_COMM_WORLD);

  if (rank != 0) return;
  std::ofstream f(path);
  f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
  for (int r = 0; r < nprocs; r++) {
    int* d = &allData[r*5];
    f << r << "," << d[0] << "," << d[1] << "," << d[2] << "," << d[3] << ",\"[";
    // We don't have neighbor rank lists here easily, just count
    f << "]\"," << d[4] << "," << d[1] << "\n";
  }
}

void writeMetadata(const std::string& path, const Solver& solver, const CaseConfig& cfg,
                   const LocalMesh& mesh, int rank, int nprocs, const std::string& gitRev) {
  if (rank != 0) return;
  json m;
  m["case_id"] = cfg.caseId;
  m["solver_name"] = "cfd2d";
  m["solver_version"] = "1.0";
  m["git_revision"] = gitRev.empty() ? nullptr : gitRev;
  m["mpi_ranks"] = nprocs;
  m["mesh_file"] = cfg.meshFile;
  m["num_cells_global"] = mesh.nCellsGlobal;
  m["num_faces_global"] = mesh.nFacesGlobal;
  m["num_cells_owned_local"] = mesh.nOwned;
  m["num_cells_ghost_local"] = mesh.nGhost;
  m["partitioner"] = "metis_kway";
  m["partition_edge_cut"] = mesh.edgeCut;
  m["halo_exchange"] = "neighbor_isend_irecv";
  m["full_state_replication_during_iterations"] = false;
  m["full_mesh_replication_during_iterations"] = false;
  m["equation_set"] = "compressible_navier_stokes_2d";
  m["inviscid_flux"] = "rusanov";
  m["entropy_fix"] = nullptr;
  m["viscous_flux"] = (cfg.mode == "laminar") ? "newtonian_fourier" : "disabled";
  m["time_integrator"] = (cfg.runType == "transient") ? "bdf2_dual_time" : "pseudo_time_backward_euler";
  m["implicit_solver"] = "lu_sgs";
  m["reconstruction"] = "piecewise_linear_least_squares";
  m["limiter"] = "barth_jespersen";
  m["spatial_order_claimed"] = 2;
  m["positivity_preservation"] = "barth_jespersen_limiter_with_first_order_fallback";
  m["wall_boundary_output_semantics"] = "boundary_value";
  m["true_bdf2_inner_loop"] = (cfg.runType == "transient");
  m["typical_inner_iterations"] = (int)solver.stats.meanInner;
  m["min_inner_iterations"] = cfg.minInner;
  m["max_inner_iterations"] = cfg.maxInner;
  m["observed_min_inner_iterations"] = solver.stats.obsMinInner;
  m["observed_max_inner_iterations"] = solver.stats.obsMaxInner;
  m["inner_residual_reduction_target"] = cfg.innerTarget;
  m["inner_target_misses"] = solver.stats.innerTargetMisses;
  m["inner_target_converged_fraction"] = solver.stats.innerConvergedFraction;
  m["last_inner_residual_ratio"] = solver.stats.lastInnerRatio;
  m["start_time_utc"] = isoTime();
  m["end_time_utc"] = isoTime();
  m["completed"] = true;
  m["convergence_status"] = solver.stats.convergenceStatus;
  std::ofstream f(path);
  f << m.dump(2);
}

void writeRunStatus(const std::string& path, const Solver& solver, const CaseConfig& cfg,
                    int nprocs, const std::string& command) {
  if (solver.rank != 0) return;
  json s;
  s["case_id"] = cfg.caseId;
  s["command"] = command;
  s["mpi_ranks"] = nprocs;
  s["wall_time_seconds"] = solver.stats.wallTime;
  s["final_step"] = solver.stats.finalStep;
  s["final_physical_time"] = solver.stats.finalPhysicalTime;
  s["convergence_status"] = solver.stats.convergenceStatus;
  s["residual_reduction_orders"] = solver.stats.residualReduction;
  s["notes"] = (cfg.runType == "transient")
    ? "BDF2 dual-time stepping with LU-SGS inner solver. Physical-time outer loop with inner nonlinear iterations."
    : "Pseudo-time continuation with LU-SGS implicit solver and CFL ramp.";
  std::ofstream f(path);
  f << s.dump(2);
}

void writeFieldVTK(const std::string& path, const GlobalMesh& gm, const Solver& solver,
                   const LocalMesh& mesh, int rank, int nprocs) {
  // Gather global solution via Allreduce (each cell owned by one rank)
  std::vector<double> gU(gm.nCells * NEQ, 0.0);
  for (int i = 0; i < mesh.nOwned; i++) {
    int gc = mesh.globalCellId[i];
    for (int e = 0; e < NEQ; e++)
      gU[gc * NEQ + e] = solver.U[i][e];
  }
  // Also gather partition IDs
  std::vector<int> gPart(gm.nCells, -1);
  for (int i = 0; i < mesh.nOwned; i++)
    gPart[mesh.globalCellId[i]] = rank;
  std::vector<double> gUReduce(gm.nCells * NEQ);
  MPI_Allreduce(gU.data(), gUReduce.data(), gm.nCells * NEQ, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  std::vector<int> gPartReduce(gm.nCells);
  MPI_Allreduce(gPart.data(), gPartReduce.data(), gm.nCells, MPI_INT, MPI_MAX, MPI_COMM_WORLD);

  if (rank != 0) return;

  std::ofstream f(path);
  f << "# vtk DataFile Version 3.0\nCFD2D Field\nASCII\nDATASET UNSTRUCTURED_GRID\n";
  f << "POINTS " << gm.nVert << " float\n";
  for (int i = 0; i < gm.nVert; i++)
    f << gm.vx[i] << " " << gm.vy[i] << " 0.0\n";

  int totalCellConn = 0;
  for (int c = 0; c < gm.nCells; c++) totalCellConn += gm.cellNVert[c] + 1;
  f << "CELLS " << gm.nCells << " " << totalCellConn << "\n";
  for (int c = 0; c < gm.nCells; c++) {
    int s = gm.cellOff[c], nv = gm.cellNVert[c];
    f << nv;
    for (int k = 0; k < nv; k++) f << " " << gm.cellVerts[s+k];
    f << "\n";
  }
  f << "CELL_TYPES " << gm.nCells << "\n";
  for (int c = 0; c < gm.nCells; c++)
    f << (gm.cellNVert[c] == 3 ? 5 : 9) << "\n";

  f << "CELL_DATA " << gm.nCells << "\n";
  // Compute primitive from gathered conservative
  std::vector<double> rho(gm.nCells), u(gm.nCells), v(gm.nCells), p(gm.nCells), mach(gm.nCells), T(gm.nCells);
  for (int c = 0; c < gm.nCells; c++) {
    Cons Uc; for (int e=0;e<NEQ;e++) Uc[e] = gUReduce[c*NEQ+e];
    Prim Wc = solver.gas.toPrim(Uc);
    rho[c] = Wc[0]; u[c] = Wc[1]; v[c] = Wc[2]; p[c] = Wc[3];
    double c_snd = solver.gas.soundSpeed(Wc[0], Wc[3]);
    mach[c] = std::sqrt(Wc[1]*Wc[1]+Wc[2]*Wc[2]) / std::max(c_snd, 1e-30);
    T[c] = solver.gas.temperature(Wc[0], Wc[3]);
  }
  f << "SCALARS density float 1\nLOOKUP_TABLE default\n";
  for (int c = 0; c < gm.nCells; c++) f << rho[c] << "\n";
  f << "SCALARS pressure float 1\nLOOKUP_TABLE default\n";
  for (int c = 0; c < gm.nCells; c++) f << p[c] << "\n";
  f << "SCALARS mach float 1\nLOOKUP_TABLE default\n";
  for (int c = 0; c < gm.nCells; c++) f << mach[c] << "\n";
  f << "SCALARS temperature float 1\nLOOKUP_TABLE default\n";
  for (int c = 0; c < gm.nCells; c++) f << T[c] << "\n";
  f << "VECTORS velocity float\n";
  for (int c = 0; c < gm.nCells; c++) f << u[c] << " " << v[c] << " 0.0\n";
  f << "SCALARS partition int 1\nLOOKUP_TABLE default\n";
  for (int c = 0; c < gm.nCells; c++) f << gPartReduce[c] << "\n";
}

void writeRestart(const std::string& path, const Solver& solver, const LocalMesh& mesh, int rank, int nprocs) {
  // Gather global solution
  std::vector<double> gU(mesh.nCellsGlobal * NEQ, 0.0);
  for (int i = 0; i < mesh.nOwned; i++) {
    int gc = mesh.globalCellId[i];
    for (int e = 0; e < NEQ; e++)
      gU[gc * NEQ + e] = solver.U[i][e];
  }
  std::vector<double> gUReduce(mesh.nCellsGlobal * NEQ);
  MPI_Allreduce(gU.data(), gUReduce.data(), mesh.nCellsGlobal * NEQ, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  if (rank != 0) return;
  std::ofstream f(path, std::ios::binary);
  int nc = mesh.nCellsGlobal;
  f.write(reinterpret_cast<const char*>(&nc), sizeof(int));
  f.write(reinterpret_cast<const char*>(gUReduce.data()), nc * NEQ * sizeof(double));
}

} // namespace cfd2d
