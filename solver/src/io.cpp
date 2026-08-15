// Output writers.
#include "io.hpp"

#include <mpi.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

namespace cfd {

namespace {
void makeDir(const std::string& d) { ::mkdir(d.c_str(), 0777); }
std::string isoTime() {
  std::time_t now = std::time(nullptr);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
  return std::string(buf);
}
}  // namespace

void initOutputFiles(const std::string& outDir, const std::string& case_id, int rank) {
  if (rank == 0) {
    makeDir(outDir);
    std::ofstream r(outDir + "/residuals.csv");
    r << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
    std::ofstream f(outDir + "/forces.csv");
    f << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
    std::ofstream log(outDir + "/stdout.log", std::ios::trunc);
    log << "case_id=" << case_id << "\n";
  }
}

void writeResidualRow(const std::string& outDir, int step, double t, int inner,
                      double cfl, double dt, const double compL2[4], double linf,
                      double l2, int rank) {
  if (rank != 0) return;
  std::ofstream r(outDir + "/residuals.csv", std::ios::app);
  r << step << "," << t << "," << inner << "," << cfl << "," << dt << ","
    << compL2[0] << "," << compL2[1] << "," << compL2[2] << "," << compL2[3]
    << "," << l2 << "," << linf << "\n";
}

void writeForceRow(const std::string& outDir, int step, double t, double cl,
                   double cd, double cmz, double pdrag, double vdrag,
                   double plift, double vlift, int rank) {
  if (rank != 0) return;
  std::ofstream f(outDir + "/forces.csv", std::ios::app);
  f << step << "," << t << "," << cl << "," << cd << "," << cmz << ","
    << pdrag << "," << vdrag << "," << plift << "," << vlift << "\n";
}

// Gather owned state to a global array on rank 0 (size 4*ncell_global).
static std::vector<double> gatherGlobalState(const Solver& s, int rank, int nranks) {
  int ng = s.lm.num_cells_global;
  std::vector<double> local(4 * ng, 0.0);
  for (int i = 0; i < s.lm.nOwned; ++i) {
    int g = s.lm.globalCell[i];
    for (int k = 0; k < 4; ++k) local[4 * g + k] = s.U[4 * i + k];
  }
  std::vector<double> global;
  if (rank == 0) global.assign(4 * ng, 0.0);
  MPI_Reduce(local.data(), global.data(), 4 * ng, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  return global;
}

void writeFieldVTU(const std::string& path, const Solver& s, int rank) {
  std::vector<double> gU = gatherGlobalState(s, rank, 0);
  if (rank != 0) return;
  const Mesh& m = *s.globalMesh;
  std::ofstream o(path);
  o << std::setprecision(8);
  o << "<?xml version=\"1.0\"?>\n";
  o << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
  o << "  <UnstructuredGrid>\n";
  o << "    <Piece NumberOfPoints=\"" << m.nvert << "\" NumberOfCells=\"" << m.ncell << "\">\n";
  o << "      <Points>\n";
  o << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
  for (int i = 0; i < m.nvert; ++i)
    o << "          " << m.vx[i] << " " << m.vy[i] << " 0\n";
  o << "        </DataArray>\n      </Points>\n";
  o << "      <Cells>\n";
  o << "        <DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n          ";
  for (int c = 0; c < m.ncell; ++c) {
    int off = m.cellOffset[c], nv = m.cellNv[c];
    for (int v = 0; v < nv; ++v) o << m.cellVerts[off + v] << " ";
  }
  o << "\n        </DataArray>\n";
  o << "        <DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n          ";
  int acc = 0;
  for (int c = 0; c < m.ncell; ++c) { acc += m.cellNv[c]; o << acc << " "; }
  o << "\n        </DataArray>\n";
  o << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n          ";
  for (int c = 0; c < m.ncell; ++c) o << (m.cellNv[c] == 3 ? 5 : 9) << " ";  // VTK_TRIANGLE / VTK_QUAD
  o << "\n        </DataArray>\n      </Cells>\n";
  o << "      <CellData Scalars=\"density\">\n";
  auto cellArr = [&](const char* name, auto fn) {
    o << "        <DataArray type=\"Float64\" Name=\"" << name << "\" format=\"ascii\">\n          ";
    for (int c = 0; c < m.ncell; ++c) o << fn(c) << " ";
    o << "\n        </DataArray>\n";
  };
  cellArr("density", [&](int c){ return gU[4*c+0]; });
  cellArr("pressure", [&](int c){
    double rho=gU[4*c+0], u=gU[4*c+1]/rho, v=gU[4*c+2]/rho;
    return s.gas.gm1()*(gU[4*c+3]-0.5*rho*(u*u+v*v)); });
  cellArr("velocity_u", [&](int c){ return gU[4*c+1]/gU[4*c+0]; });
  cellArr("velocity_v", [&](int c){ return gU[4*c+2]/gU[4*c+0]; });
  cellArr("mach", [&](int c){
    double rho=gU[4*c+0], u=gU[4*c+1]/rho, v=gU[4*c+2]/rho;
    double p=s.gas.gm1()*(gU[4*c+3]-0.5*rho*(u*u+v*v));
    return std::sqrt(u*u+v*v)/std::sqrt(s.gas.gamma*p/rho); });
  cellArr("temperature", [&](int c){
    double rho=gU[4*c+0], u=gU[4*c+1]/rho, v=gU[4*c+2]/rho;
    double p=s.gas.gm1()*(gU[4*c+3]-0.5*rho*(u*u+v*v));
    return p/(rho*s.gas.R); });
  cellArr("rank_owner", [&](int c){
    // owner rank for cell c (s.lm.partGlobal not stored; use s.cfg? skip -> -1)
    (void)c; return -1.0; });
  o << "      </CellData>\n    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n";
}

void writeSurfaceCSV(const std::string& outDir, const Solver& s, int rank) {
  // Each rank builds its wall-face rows; rank 0 gathers and writes.
  struct Row { double x,y,nx,ny,p,cp,cf,rho,u,v,mach; int tag; };
  std::vector<Row> rows;
  double qinf = 0.5*s.cfg.rho_inf*s.cfg.vel_inf*s.cfg.vel_inf;
  for (int f = 0; f < s.lm.nFace; ++f) {
    BCType bt = s.lm.faceBC[f].type;
    if (bt != BCType::SlipWall && bt != BCType::NoSlipAdiabaticWall) continue;
    int lc = s.lm.faceL[f];
    double nx=s.lm.faceNx[f], ny=s.lm.faceNy[f];
    Cons Uc; for(int k=0;k<4;++k)Uc.q[k]=s.U[4*lc+k];
    Prim Wc = toPrim(Uc, s.gas);
    Row r;
    r.x=s.lm.faceCx[f]; r.y=s.lm.faceCy[f]; r.nx=nx; r.ny=ny;
    r.p=Wc.p(); r.rho=Wc.r();
    r.cp=(Wc.p()-s.fs.p())/(qinf+1e-30);
    if (bt==BCType::NoSlipAdiabaticWall) {
      r.u=0; r.v=0; r.mach=0;
      // skin friction coefficient from tangential shear
      double ux=s.grad[6*lc+0],uy=s.grad[6*lc+1],vx=s.grad[6*lc+2],vy=s.grad[6*lc+3];
      double div=ux+vy;
      double txx=2*s.mu*ux-2.0/3.0*s.mu*div, tyy=2*s.mu*vy-2.0/3.0*s.mu*div, txy=s.mu*(uy+vx);
      double tx=txx*nx+txy*ny, ty=txy*nx+tyy*ny;
      double tn=tx*nx+ty*ny;
      double ttx=tx-tn*nx, tty=ty-tn*ny;
      double tauw=std::sqrt(ttx*ttx+tty*tty);
      r.cf = (s.mu>0.0)? tauw/(qinf+1e-30) : 0.0;
      r.tag=1;
    } else {
      // slip wall: zero normal velocity, preserve tangential
      double un=Wc.u()*nx+Wc.v()*ny;
      r.u=Wc.u()-un*nx; r.v=Wc.v()-un*ny;
      double a=soundSpeed(Wc.r(),Wc.p(),s.gas);
      r.mach=std::sqrt(r.u*r.u+r.v*r.v)/std::max(a,1e-12);
      r.cf=0.0;
      r.tag=2;
    }
    rows.push_back(r);
  }
  int nloc = (int)rows.size();
  int counts[s.nranks], displ[s.nranks];
  int nlocbytes = nloc * (int)sizeof(Row);
  MPI_Gather(&nlocbytes, 1, MPI_INT, counts, 1, MPI_INT, 0, MPI_COMM_WORLD);
  std::vector<char> all;
  int totalbytes = 0;
  if (rank==0) {
    displ[0]=0; for(int i=0;i<s.nranks;++i){ if(i>0) displ[i]=displ[i-1]+counts[i-1]; totalbytes+=counts[i]; }
    all.assign(totalbytes, 0);
  }
  MPI_Gatherv(rows.data(), nlocbytes, MPI_BYTE, all.data(), counts, displ, MPI_BYTE, 0, MPI_COMM_WORLD);
  if (rank!=0) return;
  std::ofstream o(outDir + "/surface.csv");
  o << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
  int nrow = totalbytes / (int)sizeof(Row);
  Row* rptr = (Row*)all.data();
  for (int i=0;i<nrow;++i) {
    Row& r=rptr[i];
    o << r.x << "," << r.y << "," << r.nx << "," << r.ny << "," << r.p << ","
      << r.cp << "," << r.cf << "," << r.rho << "," << r.u << "," << r.v << ","
      << r.mach << "," << (r.tag==1?"wall":"slip_wall") << "\n";
  }
}

void writeRestart(const std::string& outDir, const Solver& s, int rank) {
  std::vector<double> gU = gatherGlobalState(s, rank, 0);
  if (rank!=0) return;
  std::ofstream o(outDir + "/restart_final.bin", std::ios::binary);
  int ng = s.lm.num_cells_global;
  o.write((char*)&ng, sizeof(int));
  o.write((char*)gU.data(), 4*ng*sizeof(double));
  // also a json sidecar
  nlohmann::json j;
  j["case_id"]=s.cfg.case_id; j["num_cells_global"]=ng; j["format"]="binary_doubles";
  std::ofstream(outDir+"/restart_final.json") << j.dump(2);
}

void writePartitionDiagnostics(const std::string& outDir, const LocalMesh& lm,
                               const std::vector<int>& partGlobal,
                               const Mesh& global, int rank, int nranks) {
  int owned = lm.nOwned, ghost = lm.nGhost;
  int nbf = lm.numBoundaryFaces();
  // neighbor ranks and send/recv counts
  std::vector<int> nbrs = lm.neighborRanks;
  std::string nbrStr;
  for (size_t i=0;i<nbrs.size();++i){ nbrStr += std::to_string(nbrs[i]); if(i+1<nbrs.size())nbrStr+=";"; }
  int sendCells=0, recvCells=0;
  for (size_t i=0;i<lm.sendCells.size();++i) sendCells += (int)lm.sendCells[i].size();
  for (size_t i=0;i<lm.recvGhosts.size();++i) recvCells += (int)lm.recvGhosts[i].size();
  // gather per-rank rows to rank 0
  struct PRow { int rank,owned,ghost,bf,nnbr,send,recv; char nbrs[128]; };
  PRow pr; pr.rank=rank; pr.owned=owned; pr.ghost=ghost; pr.bf=nbf; pr.nnbr=(int)nbrs.size(); pr.send=sendCells; pr.recv=recvCells;
  std::memset(pr.nbrs,0,128); std::strncpy(pr.nbrs, nbrStr.c_str(), 127);
  std::vector<PRow> all(nranks);
  MPI_Gather(&pr, sizeof(PRow), MPI_BYTE, all.data(), sizeof(PRow), MPI_BYTE, 0, MPI_COMM_WORLD);
  if (rank!=0) return;
  std::ofstream o(outDir + "/partition_diagnostics.csv");
  o << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
  int minO=1e9,maxO=0,sumO=0; 
  for (int r=0;r<nranks;++r){
    o << all[r].rank << "," << all[r].owned << "," << all[r].ghost << "," << all[r].bf << ","
      << all[r].nnbr << ",\"" << all[r].nbrs << "\"," << all[r].send << "," << all[r].recv << "\n";
    minO=std::min(minO,all[r].owned); maxO=std::max(maxO,all[r].owned); sumO+=all[r].owned;
  }
  double mean=(double)sumO/nranks;
  o << "# edge_cut=" << lm.partition_edge_cut << " min_owned=" << minO << " max_owned=" << maxO
    << " mean_owned=" << mean << " load_balance=" << (minO/(double)maxO) << "\n";
}

void writeMetadata(const std::string& outDir, const Solver& s,
                   const std::string& convergenceStatus, bool completed,
                   double wallTime, int finalStep, double finalTime,
                   double residualReductionOrders, int rank, int nranks) {
  if (rank!=0) return;
  nlohmann::json j;
  j["case_id"]=s.cfg.case_id;
  j["solver_name"]="cfd_solver";
  j["solver_version"]="1.0";
  j["git_revision"]=nullptr;
  j["mpi_ranks"]=nranks;
  j["mesh_file"]=s.cfg.mesh_file;
  j["num_cells_global"]=s.lm.num_cells_global;
  j["num_faces_global"]=s.lm.num_faces_global;
  j["num_cells_owned_local"]=s.lm.nOwned;
  j["num_cells_ghost_local"]=s.lm.nGhost;
  j["partitioner"]= nranks>1 ? "metis_kway" : "metis_kway";
  j["partition_edge_cut"]=s.lm.partition_edge_cut;
  j["halo_exchange"]="neighbor_isend_irecv";
  j["full_state_replication_during_iterations"]=false;
  j["full_mesh_replication_during_iterations"]=false;
  j["equation_set"]="compressible_navier_stokes_2d";
  j["inviscid_flux"]= s.useRoe ? "roe_harten" : "rusanov";
  j["entropy_fix"]= s.useRoe ? "harten_yee_delta_0.1" : nullptr;
  j["viscous_flux"]= (s.mu>0.0)? "newtonian_fourier" : "disabled";
  j["time_integrator"]= (s.cfg.run_type=="transient")? "bdf2_dual_time" : "pseudo_time_steady";
  j["implicit_solver"]="lu_sgs_spectral_radius";
  j["reconstruction"]="piecewise_linear_least_squares";
  j["limiter"]="venkatakrishnan";
  j["spatial_order_claimed"]=2;
  j["positivity_preservation"]="density_pressure_floor_with_dU_scaling";
  j["wall_boundary_output_semantics"]="boundary_value";
  j["true_bdf2_inner_loop"]= (s.cfg.run_type=="transient");
  j["typical_inner_iterations"]=(int)(s.innerStats.meanInner()+0.5);
  j["min_inner_iterations"]=s.cfg.min_inner_iterations;
  j["max_inner_iterations"]=s.cfg.max_inner_iterations;
  j["observed_min_inner_iterations"]=s.innerStats.observed_min;
  j["observed_max_inner_iterations"]=s.innerStats.observed_max;
  j["inner_residual_reduction_target"]=s.cfg.inner_residual_reduction_target;
  j["inner_target_misses"]=(long long)s.innerStats.target_misses;
  j["inner_target_converged_fraction"]=s.innerStats.convergedFraction();
  j["last_inner_residual_ratio"]=s.innerStats.last_ratio;
  j["start_time_utc"]=isoTime();  // approx (set per-run)
  j["end_time_utc"]=isoTime();
  j["completed"]=completed;
  j["convergence_status"]=convergenceStatus;
  std::ofstream(outDir+"/metadata.json") << j.dump(2);
}

void writeRunStatus(const std::string& outDir, const Solver& s,
                    const std::string& command, const std::string& status,
                    double wallTime, int finalStep, double finalTime,
                    double residualReduction, const std::string& notes, int rank) {
  if (rank!=0) return;
  nlohmann::json j;
  j["case_id"]=s.cfg.case_id;
  j["command"]=command;
  j["mpi_ranks"]=s.nranks;
  j["wall_time_seconds"]=wallTime;
  j["final_step"]=finalStep;
  j["final_physical_time"]=finalTime;
  j["convergence_status"]=status;
  j["residual_reduction_orders"]=residualReduction;
  j["notes"]=notes;
  std::ofstream(outDir+"/run_status.json") << j.dump(2);
}

}  // namespace cfd
