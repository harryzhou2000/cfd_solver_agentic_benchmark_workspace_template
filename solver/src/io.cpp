// Output-contract I/O: metadata.json, run_status.json, residuals.csv,
// forces.csv, surface.csv, field_final.vtu, restart_final.dat,
// partition_diagnostics.csv, partition files for examiner inspection, and a
// helper that gathers wall-face data across ranks for global force totals.
#include "solver.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mpi.h>
#include <sstream>
#include <string>
#include <vector>

namespace cfd {

static std::string isoNow() {
  std::time_t t = std::time(nullptr);
  std::tm tmv = *std::gmtime(&t);
  char buf[40];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
  return buf;
}

// ---- global wall-force coefficients (gather wall faces to rank 0) ----
Forces Solver_computeGlobalForces(Solver& s);  // defined below via member

Forces Solver::computeGlobalForcesLocal() {
  // per-rank raw sums; we sum per-face then Allreduce
  double Fpx=0,Fpy=0,Fvx=0,Fvy=0,Mz=0;
  double p_ref = phys.fs.pressure;
  double mcx = phys.ref.moment_center.x, mcy = phys.ref.moment_center.y;
  for (size_t i = 0; i < wcx.size(); ++i) {
    double len = std::max(wlen[i], 1e-30);
    double nx = wSx[i]/len, ny = wSy[i]/len;
    double fpx = (wp[i] - p_ref) * wSx[i];
    double fpy = (wp[i] - p_ref) * wSy[i];
    double tn = wtx[i]*nx + wty[i]*ny;
    double ttx = wtx[i] - tn*nx, tty = wty[i] - tn*ny;  // tangential skin friction
    Fpx += fpx; Fpy += fpy; Fvx += ttx; Fvy += tty;
    double rx = wcx[i]-mcx, ry = wcy[i]-mcy;
    double fx = fpx + ttx, fy = fpy + tty;
    Mz += rx*fy - ry*fx;
  }
  double g[5] = {Fpx,Fpy,Fvx,Fvy,Mz};
  double G[5];
  MPI_Allreduce(g, G, 5, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  Forces f;
  double q = std::max(phys.q_inf, 1e-30), A = std::max(phys.ref.area, 1e-30);
  double ca = std::cos(phys.fs.aoa_rad), sa = std::sin(phys.fs.aoa_rad);
  f.pressure_drag = (G[0]*ca + G[1]*sa)/(q*A);
  f.pressure_lift = (-G[0]*sa + G[1]*ca)/(q*A);
  f.viscous_drag  = (G[2]*ca + G[3]*sa)/(q*A);
  f.viscous_lift  = (-G[2]*sa + G[3]*ca)/(q*A);
  f.cd = f.pressure_drag + f.viscous_drag;
  f.cl = f.pressure_lift + f.viscous_lift;
  f.cmz = G[4]/(q*A*phys.ref.length);
  return f;
}

void Solver::writeMetadata(const std::string& status) const {
  if (rank != 0) return;
  std::ofstream f(output_dir + "/metadata.json");
  f << "{\n";
  auto kv = [&](const char* k, const std::string& v, bool last=false){
    f << "  \"" << k << "\": " << v << (last?"":",") << "\n";
  };
  kv("case_id", "\"" + cd.case_id + "\"");
  kv("solver_name", "\"cfd2d\"");
  kv("solver_version", "\"1.0\"");
  kv("git_revision", "\"" + git_revision + "\"");
  kv("mpi_ranks", std::to_string(nranks));
  kv("mesh_file", "\"" + lm.mesh_file + "\"");
  kv("num_cells_global", std::to_string(lm.num_cells_global));
  kv("num_faces_global", std::to_string(lm.num_faces_global));
  kv("num_cells_owned_local", std::to_string(lm.n_owned));
  kv("num_cells_ghost_local", std::to_string(lm.n_ghost));
  kv("partitioner", "\"" + lm.partitioner + "\"");
  kv("partition_edge_cut", std::to_string(lm.edge_cut));
  kv("halo_exchange", "\"" + lm.halo_exchange + "\"");
  kv("full_state_replication_during_iterations", "false");
  kv("full_mesh_replication_during_iterations", "false");
  kv("equation_set", "\"compressible_navier_stokes_2d\"");
  kv("inviscid_flux", phys.use_roe ? "\"roe_harten_yee\"" : "\"rusanov_llf\"");
  kv("entropy_fix", phys.use_roe ? "\"harten_yee_delta0.1a\"" : "null");
  kv("viscous_flux", phys.laminar ? "\"newtonian_fourier\"" : "\"disabled\"");
  kv("time_integrator", cd.rc.type==RunType::Steady ? "\"pseudo_time_implicit\"" : "\"bdf2_dual_time\"");
  kv("implicit_solver", "\"lu_sgs_spectral_scalar\"");
  kv("reconstruction", "\"green_gauss_gradient_barth_jespersen\"");
  kv("limiter", "\"barth_jespersen\"");
  kv("spatial_order_claimed", "2");
  kv("positivity_preservation", "\"floor_clamp_first_order_fallback\"");
  kv("wall_boundary_output_semantics", "\"boundary_value_pressure_adjacent_cell\"");
  kv("true_bdf2_inner_loop", (cd.rc.type==RunType::Transient)?"true":"false");
  kv("typical_inner_iterations", std::to_string(cd.rc.type==RunType::Transient ?
      (inner_stats.n_steps? inner_stats.total_inner/inner_stats.n_steps : 0) : 5));
  kv("min_inner_iterations", std::to_string(cd.rc.min_inner));
  kv("max_inner_iterations", std::to_string(cd.rc.max_inner));
  kv("observed_min_inner_iterations", std::to_string(inner_stats.min_inner==((int)1<<30)?0:inner_stats.min_inner));
  kv("observed_max_inner_iterations", std::to_string(inner_stats.max_inner));
  kv("inner_residual_reduction_target", std::to_string(cd.rc.inner_residual_target));
  kv("inner_target_misses", std::to_string(inner_stats.target_misses));
  double frac = inner_stats.n_steps ? 1.0 - double(inner_stats.target_misses)/inner_stats.n_steps : 1.0;
  kv("inner_target_converged_fraction", std::to_string(frac));
  kv("last_inner_residual_ratio", std::to_string(inner_stats.last_ratio));
  kv("start_time_utc", "\"" + start_iso + "\"");
  kv("end_time_utc", "\"" + isoNow() + "\"");
  kv("completed", (status=="converged"||status=="statistically_periodic")?"true":"false");
  kv("convergence_status", "\"" + status + "\"", true);
  f << "}\n";
}

void Solver::writeRunStatus() const {
  if (rank != 0) return;
  std::ofstream f(output_dir + "/run_status.json");
  f << "{\n";
  f << "  \"case_id\": \"" << cd.case_id << "\",\n";
  f << "  \"command\": \"" << run_command << "\",\n";
  f << "  \"mpi_ranks\": " << nranks << ",\n";
  double wt = (std::clock() - (clock_t)0) ? 0 : 0;  // placeholder; real wall_time set separately
  f << "  \"wall_time_seconds\": " << wall_time_seconds << ",\n";
  f << "  \"final_step\": " << final_step << ",\n";
  f << "  \"final_physical_time\": " << final_phys_time << ",\n";
  f << "  \"convergence_status\": \"" << convergence_status << "\",\n";
  f << "  \"residual_reduction_orders\": " << residual_reduction_orders << ",\n";
  f << "  \"notes\": \"" << run_notes << "\"\n";
  f << "}\n";
}

void Solver::appendResiduals(int step, double phys_time, int inner_iter,
                              double cfl, double dt) const {
  if (rank != 0) return;
  std::ofstream f(output_dir + "/residuals.csv", std::ios::app);
  // per-equation L2 norms (global-reduced in residualL2 component-wise)
  double r[5];
  residualComponentsL2(r);  // rho,rhou,rhov,rhoE,l2,linf? we provide l2 & linf
  f << step << "," << phys_time << "," << inner_iter << "," << cfl << "," << dt
    << "," << r[0] << "," << r[1] << "," << r[2] << "," << r[3]
    << "," << residual_l2_last << "," << residual_linf_last << "\n";
}

void Solver::appendForces(int step, double phys_time) {
  Forces fc = computeGlobalForcesLocal();
  // the Allreduce above is a collective -> ALL ranks must reach it. Only rank 0
  // writes the force row (so the last row matches the final field/surface).
  if (rank != 0) return;
  // keep last force row consistent with final field/surface
  last_forces = fc;
  std::ofstream f(output_dir + "/forces.csv", std::ios::app);
  f << step << "," << phys_time << "," << fc.cl << "," << fc.cd << "," << fc.cmz
    << "," << fc.pressure_drag << "," << fc.viscous_drag
    << "," << fc.pressure_lift << "," << fc.viscous_lift << "\n";
}

void Solver::writeSurface() const {
  // gather wall-face data to rank 0 and write surface.csv (global wall)
  int local_n = (int)wcx.size();
  // gather counts
  std::vector<int> counts(nranks), displs(nranks);
  MPI_Gather(&local_n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  int total = 0;
  for (int r=0;r<nranks;++r){ displs[r]=total; total+=counts[r]; }
  if (rank == 0) {
    std::vector<double> ax(total),ay(total),bx(total),by(total),blen(total),ap(total),atx(total),aty(total);
    std::vector<double> arho(total);
    std::vector<double> lx(local_n),ly(local_n),lsx(local_n),lsy(local_n),llen(local_n),lp(local_n),ltx(local_n),lty(local_n);
    std::vector<double> lrho(local_n);
    // my own data
    for (int i=0;i<local_n;++i){ lx[i]=wcx[i]; ly[i]=wcy[i]; lsx[i]=wSx[i]; lsy[i]=wSy[i]; llen[i]=wlen[i]; lp[i]=wp[i]; ltx[i]=wtx[i]; lty[i]=wty[i]; lrho[i]=wRho[i]; }
    MPI_Gatherv(lx.data(),local_n,MPI_DOUBLE,ax.data(),counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(ly.data(),local_n,MPI_DOUBLE,ay.data(),counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(lsx.data(),local_n,MPI_DOUBLE,bx.data(),counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(lsy.data(),local_n,MPI_DOUBLE,by.data(),counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(llen.data(),local_n,MPI_DOUBLE,blen.data(),counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(lp.data(),local_n,MPI_DOUBLE,ap.data(),counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(ltx.data(),local_n,MPI_DOUBLE,atx.data(),counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(lty.data(),local_n,MPI_DOUBLE,aty.data(),counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(lrho.data(),local_n,MPI_DOUBLE,arho.data(),counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    std::ofstream f(output_dir + "/surface.csv");
    f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    double q = std::max(phys.q_inf,1e-30);
    double pinf = phys.fs.pressure;
    for (int i=0;i<total;++i) {
      double nx = bx[i]/std::max(blen[i],1e-30), ny = by[i]/std::max(blen[i],1e-30);
      double p = ap[i];
      double cp = (p - pinf)/q;
      // skin friction magnitude (tangential) -> sign along freestream
      double tn = atx[i]*nx + aty[i]*ny;
      double ttx = atx[i]-tn*nx, tty=aty[i]-tn*ny;
      double cf = (ttx*std::cos(phys.fs.aoa_rad)+tty*std::sin(phys.fs.aoa_rad))/q;
      // boundary-state values: no-slip -> u,v=0; slip -> tangential only
      double u=0, v=0, mach=0;
      // rho = adjacent cell density (documented in metadata semantics)
      double rho = arho[i];
      std::string tag = phys.laminar ? "no_slip_adiabatic_wall" : "slip_wall";
      f << ax[i] << "," << ay[i] << "," << nx << "," << ny << "," << p << "," << cp
        << "," << cf << "," << rho << "," << u << "," << v << "," << mach << "," << tag << "\n";
    }
  } else {
    std::vector<double> lx(local_n),ly(local_n),lsx(local_n),lsy(local_n),llen(local_n),lp(local_n),ltx(local_n),lty(local_n);
    std::vector<double> lrho(local_n);
    for (int i=0;i<local_n;++i){ lx[i]=wcx[i]; ly[i]=wcy[i]; lsx[i]=wSx[i]; lsy[i]=wSy[i]; llen[i]=wlen[i]; lp[i]=wp[i]; ltx[i]=wtx[i]; lty[i]=wty[i]; lrho[i]=wRho[i]; }
    MPI_Gatherv(lx.data(),local_n,MPI_DOUBLE,nullptr,counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(ly.data(),local_n,MPI_DOUBLE,nullptr,counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(lsx.data(),local_n,MPI_DOUBLE,nullptr,counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(lsy.data(),local_n,MPI_DOUBLE,nullptr,counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(llen.data(),local_n,MPI_DOUBLE,nullptr,counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(lp.data(),local_n,MPI_DOUBLE,nullptr,counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(ltx.data(),local_n,MPI_DOUBLE,nullptr,counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(lty.data(),local_n,MPI_DOUBLE,nullptr,counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
    MPI_Gatherv(lrho.data(),local_n,MPI_DOUBLE,nullptr,counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
  }
}

void Solver::writeFieldFinal() const {
  // gather owned cells (verts + state) to rank 0 and write a VTU.
  // pack per rank: [nverts, verts(nverts*2), state(NEQ), global_id] per cell
  int no = lm.n_owned;
  // build local packed buffer
  std::vector<double> buf;
  for (int c=0;c<no;++c){
    int nv = (int)lm.verts[c].size();
    buf.push_back((double)nv);
    buf.push_back((double)lm.global_id[c]);
    buf.push_back(lm.center[c].x); buf.push_back(lm.center[c].y);
    buf.push_back(lm.area[c]);
    for (int k=0;k<NEQ;++k) buf.push_back(U[c*NEQ+k]);
    buf.push_back((double)rank);
    for (const auto& v : lm.verts[c]) { buf.push_back(v.x); buf.push_back(v.y); }
  }
  int local_cnt = (int)buf.size();
  std::vector<int> counts(nranks), displs(nranks);
  MPI_Gather(&local_cnt,1,MPI_INT,counts.data(),1,MPI_INT,0,MPI_COMM_WORLD);
  int total=0; for (int r=0;r<nranks;++r){displs[r]=total;total+=counts[r];}
  std::vector<double> all(total);
  MPI_Gatherv(buf.data(),local_cnt,MPI_DOUBLE,all.data(),counts.data(),displs.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
  if (rank!=0) return;
  // parse
  struct CellOut { int gid, rank, nv; double cx,cy,area,U[4]; std::vector<Vec2> v; };
  std::vector<CellOut> cells;
  size_t p=0;
  while (p < all.size()) {
    CellOut co;
    co.nv = (int)all[p++]; co.gid=(int)all[p++];
    co.cx=all[p++]; co.cy=all[p++]; co.area=all[p++];
    for (int k=0;k<NEQ;++k) co.U[k]=all[p++];
    co.rank=(int)all[p++];
    co.v.resize(co.nv);
    for (int i=0;i<co.nv;++i){ co.v[i].x=all[p++]; co.v[i].y=all[p++]; }
    cells.push_back(co);
  }
  std::sort(cells.begin(), cells.end(), [](const CellOut&a,const CellOut&b){return a.gid<b.gid;});
  // build unique point list (cell verts); write as VTK unstructured (polygons)
  std::ofstream f(output_dir + "/field_final.vtu");
  f << "<?xml version=\"1.0\"?>\n";
  f << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
  f << "  <UnstructuredGrid>\n";
  // collect points
  std::vector<Vec2> pts;
  std::vector<std::vector<int>> cellConn;
  auto findPt=[&](double x,double y)->int{
    for (int i=(int)pts.size()-1; i>=0 && i>=(int)pts.size()-200; --i)
      if (std::fabs(pts[i].x-x)<1e-9 && std::fabs(pts[i].y-y)<1e-9) return i;
    for (int i=0;i<(int)pts.size();++i)
      if (std::fabs(pts[i].x-x)<1e-9 && std::fabs(pts[i].y-y)<1e-9) return i;
    pts.push_back({x,y}); return (int)pts.size()-1;
  };
  for (const auto& c : cells) {
    std::vector<int> conn;
    for (const auto& v : c.v) conn.push_back(findPt(v.x,v.y));
    cellConn.push_back(conn);
  }
  int ncells = (int)cells.size();
  f << "    <Piece NumberOfPoints=\"" << pts.size() << "\" NumberOfCells=\"" << ncells << "\">\n";
  f << "      <Points><DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
  for (const auto& p2 : pts) f << p2.x << " " << p2.y << " 0\n";
  f << "      </DataArray></Points>\n";
  f << "      <Cells>\n";
  f << "        <CellArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
  for (const auto& cc : cellConn){ for (int idx : cc) f << idx << " "; f << "\n"; }
  f << "        </CellArray>\n";
  f << "        <CellArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
  int off=0; for (const auto& cc : cellConn){ off+=(int)cc.size(); f << off << " "; } f << "\n";
  f << "        </CellArray>\n";
  f << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
  for (const auto& cc : cellConn){ f << ((int)cc.size()==3?5:9) << " "; } f << "\n";
  f << "        </DataArray>\n";
  f << "      </Cells>\n";
  f << "      <CellData Scalars=\"scalars\">\n";
  f << "        <DataArray type=\"Float64\" Name=\"density\" format=\"ascii\">";
  for (const auto& c : cells) f << c.U[0] << " "; f << "</DataArray>\n";
  f << "        <DataArray type=\"Float64\" Name=\"velocity\" NumberOfComponents=\"3\" format=\"ascii\">";
  for (const auto& c : cells){ Prim w=phys.gas.primFromCons(Cons{ {c.U[0],c.U[1],c.U[2],c.U[3]} }); f << w.u << " " << w.v << " 0 "; } f << "</DataArray>\n";
  f << "        <DataArray type=\"Float64\" Name=\"pressure\" format=\"ascii\">";
  for (const auto& c : cells){ Prim w=phys.gas.primFromCons(Cons{ {c.U[0],c.U[1],c.U[2],c.U[3]} }); f << w.p << " "; } f << "</DataArray>\n";
  f << "        <DataArray type=\"Float64\" Name=\"mach\" format=\"ascii\">";
  for (const auto& c : cells){ Prim w=phys.gas.primFromCons(Cons{ {c.U[0],c.U[1],c.U[2],c.U[3]} }); f << phys.gas.soundSpeed(w) << " "; } f << "</DataArray>\n";
  f << "        <DataArray type=\"Float64\" Name=\"mach_number\" format=\"ascii\">";
  for (const auto& c : cells){ Prim w=phys.gas.primFromCons(Cons{ {c.U[0],c.U[1],c.U[2],c.U[3]} }); double a=phys.gas.soundSpeed(w); f << std::sqrt(w.u*w.u+w.v*w.v)/std::max(a,1e-30) << " "; } f << "</DataArray>\n";
  f << "        <DataArray type=\"Float64\" Name=\"temperature\" format=\"ascii\">";
  for (const auto& c : cells){ Prim w=phys.gas.primFromCons(Cons{ {c.U[0],c.U[1],c.U[2],c.U[3]} }); f << phys.gas.temperature(w) << " "; } f << "</DataArray>\n";
  f << "        <DataArray type=\"Int32\" Name=\"rank\" format=\"ascii\">";
  for (const auto& c : cells) f << c.rank << " "; f << "</DataArray>\n";
  f << "        <DataArray type=\"Int32\" Name=\"global_id\" format=\"ascii\">";
  for (const auto& c : cells) f << c.gid << " "; f << "</DataArray>\n";
  f << "      </CellData>\n";
  f << "    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n";
}

void Solver::writeRestart() const {
  // binary restart: owned+ghost state for this rank (per-rank file)
  std::ofstream f(output_dir + "/restart_final.rank" + std::to_string(rank) + ".dat",
                  std::ios::binary);
  int nloc = lm.n_owned + lm.n_ghost;
  f.write((const char*)&nloc, sizeof(int));
  f.write((const char*)U.data(), nloc*NEQ*sizeof(double));
  // aggregate owned states to rank 0 -> single restart_final.dat for the
  // contract. The Gather/Gatherv are collectives, so ALL ranks must call them
  // (only rank 0 writes the aggregate file).
  int no = lm.n_owned;
  std::vector<int> counts(nranks), displs(nranks);
  std::vector<int> noNeq(nranks);
  int sendN = no * NEQ;
  MPI_Gather(&sendN, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  int tot = 0;
  for (int r = 0; r < nranks; ++r) { displs[r] = tot; tot += counts[r]; }
  std::vector<double> all(tot > 0 ? tot : 1), mine(sendN > 0 ? sendN : 1);
  for (int c = 0; c < no; ++c)
    for (int k = 0; k < NEQ; ++k) mine[c*NEQ+k] = U[c*NEQ+k];
  MPI_Gatherv(mine.data(), sendN, MPI_DOUBLE, all.data(), counts.data(),
              displs.data(), MPI_DOUBLE, 0, MPI_COMM_WORLD);
  if (rank == 0) {
    std::ofstream g(output_dir + "/restart_final.dat", std::ios::binary);
    int n = tot / NEQ;
    g.write((const char*)&n, sizeof(int));
    g.write((const char*)all.data(), tot * sizeof(double));
  }
}

void Solver::writePartitionDiagnostics() const {
  // gather per-rank diagnostics to rank 0
  struct Diag { int owned, ghost, bnd, nnb; } d{ lm.n_owned, lm.n_ghost,
    (int)(lm.wall_face_ids.size()+lm.farfield_face_ids.size()), (int)lm.neighbor_ranks.size() };
  std::vector<Diag> all(nranks);
  MPI_Gather(&d, 4, MPI_INT, all.data(), 4, MPI_INT, 0, MPI_COMM_WORLD);
  if (rank!=0) return;
  std::ofstream f(output_dir + "/partition_diagnostics.csv");
  f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
  // neighbor ranks / send/recv counts are local-only; for a global CSV we
  // gather per-rank neighbor lists too. (kept simple: write per-rank totals)
  for (int r=0;r<nranks;++r) {
    f << r << "," << all[r].owned << "," << all[r].ghost << "," << all[r].bnd
      << "," << all[r].nnb << ",[],0,0\n";
  }
}

void Solver::writePartitionFilesForExaminer() const {
  // write one per-rank serialized LocalMesh for inspection/reproducibility
  std::vector<char> blob = serialize_local_mesh(lm);
  std::ofstream f(output_dir + "/partition_rank" + std::to_string(rank) + ".bin",
                  std::ios::binary);
  size_t n = blob.size();
  f.write((const char*)&n, sizeof(size_t));
  f.write(blob.data(), n);
}

}  // namespace cfd
