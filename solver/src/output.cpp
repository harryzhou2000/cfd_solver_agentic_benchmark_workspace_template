// Output writers: CSV histories, surface CSV, legacy VTK field files,
// JSON metadata/status, partition diagnostics, and binary restart files.
// Final outputs are assembled on rank 0 from per-rank owned data via
// MPI gathers (no full-state replication during iterations).
#include "solver.hpp"
#include "output.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <map>
#include <set>
#include <chrono>
#include <ctime>

namespace fv {

static std::string g_cmd = "";
static std::string g_rev = "unknown";
void setCommandLine(const std::string& cmd) { g_cmd = cmd; }
const std::string& commandLine() { return g_cmd; }
void setGitRevision(const std::string& rev) { g_rev = rev; }
const std::string& gitRevision() { return g_rev; }

static std::string isoNow() {
  std::time_t t = std::time(nullptr);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}

// ---- generic byte gather to rank 0 ----
static std::vector<char> gatherBytes(MPI_Comm comm, const std::vector<char>& local, int rank,
                                     std::vector<int>* countsOut = nullptr) {
  int nranks;
  MPI_Comm_size(comm, &nranks);
  int n = (int)local.size();
  std::vector<int> counts(nranks), displs(nranks);
  MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
  std::vector<char> all;
  if (rank == 0) {
    int tot = 0;
    for (int r = 0; r < nranks; ++r) { displs[r] = tot; tot += counts[r]; }
    all.resize(tot);
  }
  MPI_Gatherv(local.data(), n, MPI_BYTE, all.data(), counts.data(), displs.data(), MPI_BYTE, 0,
              comm);
  if (countsOut) *countsOut = counts;
  return all;
}

// ================= surface.csv =================

void Solver::writeSurfaceCsv() {
  // per-rank wall face records
  double qinf = cfg_.dynamic_pressure();
  std::vector<char> local;
  auto putD = [&](double v) { const char* p = (const char*)&v; local.insert(local.end(), p, p + 8); };
  auto putL = [&](long v) { const char* p = (const char*)&v; local.insert(local.end(), p, p + 8); };
  for (int fi : mesh_.wallFaces) {
    const LocalFace& f = mesh_.faces[fi];
    const Prim& w = W_[f.c0];
    double Tc = w.T(cfg_.gas);
    double pw = w.p;
    double rhoB, uB, vB;
    if (f.bc == (int)BCType::NoSlipAdiabaticWall) {
      uB = 0.0; vB = 0.0;
      rhoB = pw / (cfg_.gas.R * Tc);  // adiabatic wall: T_wall = T_cell
    } else {
      double un = w.u * f.nx + w.v * f.ny;
      uB = w.u - un * f.nx;  // tangential part only
      vB = w.v - un * f.ny;
      rhoB = w.rho;
    }
    double aB = std::sqrt(cfg_.gas.gamma * pw / std::max(rhoB, rhoFloor_));
    double machB = std::hypot(uB, vB) / aB;
    double cp = (pw - cfg_.fs_pressure) / qinf;
    double cf = 0.0;
    if (cfg_.viscous() && f.bc == (int)BCType::NoSlipAdiabaticWall) {
      const Grads& gr = grads_[f.c0];
      double dxr = 2.0 * (f.fx - mesh_.xc[f.c0]);
      double dyr = 2.0 * (f.fy - mesh_.yc[f.c0]);
      double d2 = std::max(dxr * dxr + dyr * dyr, 1e-30);
      auto corr = [&](Vec2 gl, double phiL, double phiR) -> Vec2 {
        double resid = (phiR - phiL) - (gl.x * dxr + gl.y * dyr);
        return Vec2(gl.x + resid * dxr / d2, gl.y + resid * dyr / d2);
      };
      Vec2 gu = corr(gr.u, w.u, -w.u);
      Vec2 gv = corr(gr.v, w.v, -w.v);
      double div = gu.x + gv.y;
      double txx = 2.0 * mu_ * gu.x - (2.0 / 3.0) * mu_ * div;
      double tyy = 2.0 * mu_ * gv.y - (2.0 / 3.0) * mu_ * div;
      double txy = mu_ * (gu.y + gv.x);
      double sx = txx * f.nx + txy * f.ny;
      double sy = txy * f.nx + tyy * f.ny;
      double sn = sx * f.nx + sy * f.ny;
      double stx = sx - sn * f.nx;
      double sty = sy - sn * f.ny;
      // signed tangential shear on the body, projected on tangent t = (-ny, nx)
      cf = -(stx * (-f.ny) + sty * f.nx) / qinf;
    }
    putL(f.globalId);
    putD(f.fx); putD(f.fy); putD(f.nx); putD(f.ny);
    putD(pw); putD(cp); putD(cf); putD(rhoB); putD(uB); putD(vB); putD(machB);
    putL((long)f.family);
  }
  std::vector<int> counts;
  std::vector<char> all = gatherBytes(comm_, local, rank_, &counts);
  if (rank_ != 0) return;
  FILE* f = std::fopen((outdir_ + "/surface.csv").c_str(), "w");
  std::fprintf(f, "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n");
  const int recSize = 8 * 13;
  // collect and sort by (family, x, y)
  struct Rec { double x, y, nx, ny, p, cp, cf, rho, u, v, mach; long fam; };
  std::vector<Rec> recs;
  {
    size_t off = 0;
    for (int r = 0; r < (int)counts.size(); ++r) {
      size_t end = off + counts[r];
      while (off + recSize <= end) {
        const char* p = all.data() + off;
        auto gL = [&]() { long v; std::memcpy(&v, p, 8); p += 8; return v; };
        auto gD = [&]() { double v; std::memcpy(&v, p, 8); p += 8; return v; };
        gL();  // globalId (not needed for output)
        Rec rc;
        rc.x = gD(); rc.y = gD(); rc.nx = gD(); rc.ny = gD();
        rc.p = gD(); rc.cp = gD(); rc.cf = gD(); rc.rho = gD();
        rc.u = gD(); rc.v = gD(); rc.mach = gD();
        rc.fam = gL();
        recs.push_back(rc);
        off += recSize;
      }
    }
  }
  std::sort(recs.begin(), recs.end(), [](const Rec& a, const Rec& b) {
    if (a.fam != b.fam) return a.fam < b.fam;
    if (a.x != b.x) return a.x < b.x;
    return a.y < b.y;
  });
  for (const Rec& rc : recs) {
    std::fprintf(f, "%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%s\n",
                 rc.x, rc.y, rc.nx, rc.ny, rc.p, rc.cp, rc.cf, rc.rho, rc.u, rc.v, rc.mach,
                 mesh_.familyNames[rc.fam].c_str());
  }
  std::fclose(f);
}

// ================= VTK field =================

static void swap4(void* p) {
  char* c = (char*)p;
  std::swap(c[0], c[3]);
  std::swap(c[1], c[2]);
}

void Solver::writeFieldVtk(const string& path) {
  // pack local owned cells
  std::vector<char> local;
  auto putI = [&](int v) { const char* p = (const char*)&v; local.insert(local.end(), p, p + 4); };
  auto putL = [&](long v) { const char* p = (const char*)&v; local.insert(local.end(), p, p + 8); };
  auto putD = [&](double v) { const char* p = (const char*)&v; local.insert(local.end(), p, p + 8); };
  // unique nodes used by owned cells
  std::map<long, int> nodeSet;
  for (int c = 0; c < mesh_.nOwn; ++c)
    for (int k = 0; k < mesh_.cellNNodes[c]; ++k)
      nodeSet[mesh_.globalNodeId[mesh_.cellNodes[c][k]]] = 0;
  int nn = (int)nodeSet.size();
  {
    int i = 0;
    for (auto& kv : nodeSet) kv.second = i++;
  }
  putI(mesh_.nOwn);
  putI(nn);
  for (auto& kv : nodeSet) {
    long gid = kv.first;
    putL(gid);
  }
  // node coords (need global node id -> local): invert
  {
    std::vector<double> nx(nn), ny(nn);
    for (int i = 0; i < mesh_.nNodes; ++i) {
      auto it = nodeSet.find(mesh_.globalNodeId[i]);
      if (it != nodeSet.end()) { nx[it->second] = mesh_.nodeX[i]; ny[it->second] = mesh_.nodeY[i]; }
    }
    for (int i = 0; i < nn; ++i) { putD(nx[i]); putD(ny[i]); }
  }
  for (int c = 0; c < mesh_.nOwn; ++c) {
    int nvc = mesh_.cellNNodes[c];
    putI(nvc);
    for (int k = 0; k < nvc; ++k)
      putL(mesh_.globalNodeId[mesh_.cellNodes[c][k]]);
  }
  // optional residual field dump (debug)
  bool dumpRes = std::getenv("FV2D_DEBUG_RESVTK") != nullptr;
  for (int c = 0; c < mesh_.nOwn; ++c) {
    const Prim& w = W_[c];
    double T = w.T(cfg_.gas);
    double a = w.a(cfg_.gas);
    double mach = std::hypot(w.u, w.v) / a;
    double rhoE = U_[c][3];
    double vort = grads_[c].v.x - grads_[c].u.y;
    putD(w.rho); putD(w.u); putD(w.v); putD(w.p); putD(mach); putD(T); putD(rhoE); putD(vort);
    if (dumpRes) {
      double rl = 0.0;
      for (int v = 0; v < 4; ++v) rl += R_[c][v] * R_[c][v];
      putD(std::sqrt(rl) / mesh_.vol[c]);
    }
    putI(rank_);
  }
  std::vector<char> all = gatherBytes(comm_, local, rank_);
  if (rank_ != 0) return;
  // assemble on rank 0
  std::map<long, int> gnode;
  std::vector<double> gx, gy;
  struct Cell { int nn; long ids[4]; double vals[9]; int part; };
  std::vector<Cell> cells;
  size_t off = 0;
  while (off < all.size()) {
    auto gI = [&]() { int v; std::memcpy(&v, all.data() + off, 4); off += 4; return v; };
    auto gL = [&]() { long v; std::memcpy(&v, all.data() + off, 8); off += 8; return v; };
    auto gD = [&]() { double v; std::memcpy(&v, all.data() + off, 8); off += 8; return v; };
    int nc = gI();
    int nno = gI();
    std::vector<long> gids(nno);
    for (int i = 0; i < nno; ++i) gids[i] = gL();
    std::vector<int> locIdx(nno);
    for (int i = 0; i < nno; ++i) {
      auto it = gnode.find(gids[i]);
      if (it == gnode.end()) {
        int id = (int)gnode.size();
        gnode[gids[i]] = id;
        gx.push_back(0); gy.push_back(0);
        locIdx[i] = id;
      } else locIdx[i] = it->second;
    }
    for (int i = 0; i < nno; ++i) {
      double x = gD(), y = gD();
      gx[locIdx[i]] = x; gy[locIdx[i]] = y;
    }
    for (int c = 0; c < nc; ++c) {
      Cell cell;
      cell.nn = gI();
      for (int k = 0; k < cell.nn; ++k) {
        long gid = gL();
        cell.ids[k] = gnode[gid];
      }
      cells.push_back(cell);
    }
    for (int c = 0; c < nc; ++c) {
      Cell& cell = cells[cells.size() - nc + c];
      int nv = dumpRes ? 9 : 8;
      for (int k = 0; k < nv; ++k) cell.vals[k] = gD();
      cell.part = gI();
    }
  }
  // ---- write binary legacy VTK
  std::ofstream out(path, std::ios::binary);
  out << "# vtk DataFile Version 3.0\n";
  out << "fv2d field " << cfg_.case_id << "\n";
  out << "BINARY\n";
  out << "DATASET UNSTRUCTURED_GRID\n";
  int np = (int)gx.size(), ncell = (int)cells.size();
  out << "POINTS " << np << " float\n";
  {
    std::vector<float> buf(3 * np);
    for (int i = 0; i < np; ++i) {
      buf[3 * i] = (float)gx[i];
      buf[3 * i + 1] = (float)gy[i];
      buf[3 * i + 2] = 0.0f;
    }
    for (auto& v : buf) swap4(&v);
    out.write((const char*)buf.data(), 4 * buf.size());
  }
  out << "\n";
  long clist = 0;
  for (auto& c : cells) clist += c.nn + 1;
  out << "CELLS " << ncell << " " << clist << "\n";
  {
    std::vector<int> buf;
    buf.reserve(clist);
    for (auto& c : cells) {
      buf.push_back(c.nn);
      for (int k = 0; k < c.nn; ++k) buf.push_back((int)c.ids[k]);
    }
    for (auto& v : buf) swap4(&v);
    out.write((const char*)buf.data(), 4 * buf.size());
  }
  out << "\n";
  out << "CELL_TYPES " << ncell << "\n";
  {
    std::vector<int> buf(ncell);
    for (int i = 0; i < ncell; ++i) buf[i] = (cells[i].nn == 3) ? 5 : 9;  // TRIANGLE / QUAD
    for (auto& v : buf) swap4(&v);
    out.write((const char*)buf.data(), 4 * buf.size());
  }
  out << "\n";
  out << "CELL_DATA " << ncell << "\n";
  auto writeScalar = [&](const char* name, int idx) {
    out << "SCALARS " << name << " float 1\nLOOKUP_TABLE default\n";
    std::vector<float> buf(ncell);
    for (int i = 0; i < ncell; ++i) buf[i] = (float)cells[i].vals[idx];
    for (auto& v : buf) swap4(&v);
    out.write((const char*)buf.data(), 4 * buf.size());
    out << "\n";
  };
  writeScalar("density", 0);
  writeScalar("pressure", 3);
  writeScalar("mach", 4);
  writeScalar("temperature", 5);
  writeScalar("total_energy", 6);
  writeScalar("vorticity", 7);
  if (dumpRes) writeScalar("residual_norm", 8);
  {
    out << "VECTORS velocity float\n";
    std::vector<float> buf(3 * ncell);
    for (int i = 0; i < ncell; ++i) {
      buf[3 * i] = (float)cells[i].vals[1];
      buf[3 * i + 1] = (float)cells[i].vals[2];
      buf[3 * i + 2] = 0.0f;
    }
    for (auto& v : buf) swap4(&v);
    out.write((const char*)buf.data(), 4 * buf.size());
    out << "\n";
  }
  {
    out << "SCALARS partition int 1\nLOOKUP_TABLE default\n";
    std::vector<int> buf(ncell);
    for (int i = 0; i < ncell; ++i) buf[i] = cells[i].part;
    for (auto& v : buf) swap4(&v);
    out.write((const char*)buf.data(), 4 * buf.size());
    out << "\n";
  }
}

// ================= partition diagnostics =================

void Solver::writePartitionDiag() {
  int myOwn = mesh_.nOwn, myGhost = mesh_.nGhost;
  int myBnd = (int)mesh_.boundaryFaces.size();
  int myNbr = (int)mesh_.neighbors.size();
  std::string nbrStr;
  long mySend = 0, myRecv = 0;
  for (auto& nb : mesh_.neighbors) {
    if (!nbrStr.empty()) nbrStr += ";";
    nbrStr += std::to_string(nb.rank);
    mySend += (long)nb.sendIdx.size();
    myRecv += (long)nb.recvIdx.size();
  }
  int ints[4] = {myOwn, myGhost, myBnd, myNbr};
  std::vector<int> allInts;
  if (rank_ == 0) allInts.resize(4 * nranks_);
  MPI_Gather(ints, 4, MPI_INT, allInts.data(), 4, MPI_INT, 0, comm_);
  long longs[2] = {mySend, myRecv};
  std::vector<long> allLongs;
  if (rank_ == 0) allLongs.resize(2 * nranks_);
  MPI_Gather(longs, 2, MPI_LONG, allLongs.data(), 2, MPI_LONG, 0, comm_);
  std::vector<char> nbrBytes(nbrStr.begin(), nbrStr.end());
  std::vector<int> counts;
  std::vector<char> allNbr = gatherBytes(comm_, nbrBytes, rank_, &counts);
  if (rank_ != 0) return;
  FILE* f = std::fopen((outdir_ + "/partition_diagnostics.csv").c_str(), "w");
  std::fprintf(f, "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n");
  size_t off = 0;
  for (int r = 0; r < nranks_; ++r) {
    std::string ns(allNbr.data() + off, counts[r]);
    off += counts[r];
    std::fprintf(f, "%d,%d,%d,%d,%d,%s,%ld,%ld\n", r, allInts[4 * r], allInts[4 * r + 1],
                 allInts[4 * r + 2], allInts[4 * r + 3], ns.empty() ? "-" : ns.c_str(),
                 allLongs[2 * r], allLongs[2 * r + 1]);
  }
  std::fclose(f);
}

// ================= metadata / status =================

void Solver::writeMetadataJson() {
  if (rank_ != 0) return;
  nlohmann::json md;
  md["case_id"] = cfg_.case_id;
  md["solver_name"] = "fv2d";
  md["solver_version"] = "1.0.0";
  md["git_revision"] = gitRevision();
  md["mpi_ranks"] = nranks_;
  md["mesh_file"] = cfg_.mesh_file;
  md["num_cells_global"] = mesh_.nCellsGlobal;
  md["num_faces_global"] = mesh_.nFacesGlobal;
  md["num_cells_owned_local"] = mesh_.nOwn;
  md["num_cells_ghost_local"] = mesh_.nGhost;
  md["partitioner"] = "metis_kway";
  md["partition_edge_cut"] = mesh_.partitionEdgeCut;
  md["halo_exchange"] = "neighbor_isend_irecv";
  md["full_state_replication_during_iterations"] = false;
  md["full_mesh_replication_during_iterations"] = false;
  md["equation_set"] = "compressible_navier_stokes_2d";
  md["inviscid_flux"] = useRoe_ ? "roe" : "rusanov_llf";
  md["entropy_fix"] = useRoe_ ? nlohmann::json("harten") : nlohmann::json(nullptr);
  md["rusanov_dissipation_scale"] = cfg_.rc.rusanov_dissipation_scale;
  md["viscous_flux"] = cfg_.viscous() ? "corrected_average_face_gradient_newtonian_fourier"
                                      : "none_inviscid";
  md["time_integrator"] = cfg_.transient() ? "bdf2_dual_time" : "implicit_euler_pseudo_time";
  md["implicit_solver"] = "scalar_lusgs_symmetric_gauss_seidel_block_jacobi_across_ranks";
  md["reconstruction"] = "weighted_least_squares_linear";
  md["limiter"] = (venkatK_ > 0.0) ? "venkatakrishnan_with_positivity_floors"
                                   : "barth_jespersen_with_positivity_floors";
  md["spatial_order_claimed"] = 2;
  md["positivity_preservation"] = "limiter_floors_and_update_backtracking";
  md["wall_boundary_output_semantics"] = "boundary_value";
  md["true_bdf2_inner_loop"] = cfg_.transient();
  md["typical_inner_iterations"] = inner_.mean();
  md["min_inner_iterations"] = cfg_.transient() ? cfg_.rc.min_inner_iterations
                                                : cfg_.rc.min_inner_iterations;
  md["max_inner_iterations"] = cfg_.rc.max_inner_iterations;
  md["observed_min_inner_iterations"] = inner_.minIt < 0 ? 0 : inner_.minIt;
  md["observed_max_inner_iterations"] = inner_.maxIt;
  md["inner_residual_reduction_target"] = cfg_.transient()
                                              ? cfg_.rc.inner_residual_reduction_target
                                              : cfg_.rc.inner_residual_reduction_target;
  md["inner_target_misses"] = inner_.misses;
  md["inner_target_converged_fraction"] = cfg_.transient() ? inner_.convergedFraction() : 1.0;
  md["last_inner_residual_ratio"] = inner_.lastRatio;
  md["start_time_utc"] = startTimeUtc_;
  md["end_time_utc"] = isoNow();
  md["completed"] = converged_;
  md["convergence_status"] = convergenceStatus_;
  std::ofstream out(outdir_ + "/metadata.json");
  out << md.dump(2) << "\n";
}

void Solver::writeRunStatusJson() {
  double wall = MPI_Wtime() - wallStart_;
  double wallg = 0.0;
  MPI_Allreduce(&wall, &wallg, 1, MPI_DOUBLE, MPI_MAX, comm_);
  if (rank_ != 0) return;
  nlohmann::json st;
  st["case_id"] = cfg_.case_id;
  st["command"] = commandLine();
  st["mpi_ranks"] = nranks_;
  st["wall_time_seconds"] = wallg;
  st["final_step"] = finalStep_;
  st["final_physical_time"] = time_;
  st["convergence_status"] = convergenceStatus_;
  st["residual_reduction_orders"] = residualReductionOrders_;
  st["notes"] = notes_;
  std::ofstream out(outdir_ + "/run_status.json");
  out << st.dump(2) << "\n";
}

// ================= restart =================

void Solver::writeRestart(const string& name) {
  int nstate = cfg_.transient() ? 3 : 1;
  std::vector<char> local;
  auto putL = [&](long v) { const char* p = (const char*)&v; local.insert(local.end(), p, p + 8); };
  auto putD = [&](double v) { const char* p = (const char*)&v; local.insert(local.end(), p, p + 8); };
  for (int c = 0; c < mesh_.nOwn; ++c) {
    putL(mesh_.globalCellId[c]);
    for (int v = 0; v < 4; ++v) putD(U_[c][v]);
    if (nstate == 3) {
      for (int v = 0; v < 4; ++v) putD(Un_[c][v]);
      for (int v = 0; v < 4; ++v) putD(Unm1_[c][v]);
    }
  }
  std::vector<char> all = gatherBytes(comm_, local, rank_);
  if (rank_ != 0) return;
  // assemble in global cell order
  long nc = mesh_.nCellsGlobal;
  std::vector<State> U(nc), Un(nc), Unm1(nc);
  size_t off = 0;
  int strideD = nstate == 3 ? 12 : 4;
  while (off < all.size()) {
    long gid;
    std::memcpy(&gid, all.data() + off, 8);
    off += 8;
    for (int v = 0; v < 4; ++v) { std::memcpy(&U[gid][v], all.data() + off, 8); off += 8; }
    if (nstate == 3) {
      for (int v = 0; v < 4; ++v) { std::memcpy(&Un[gid][v], all.data() + off, 8); off += 8; }
      for (int v = 0; v < 4; ++v) { std::memcpy(&Unm1[gid][v], all.data() + off, 8); off += 8; }
    }
    (void)strideD;
  }
  std::ofstream out(outdir_ + "/" + name, std::ios::binary);
  int magic = 0x46563252, version = 1;
  long step = step_, ncout = nc;
  double time = time_;
  out.write((const char*)&magic, 4);
  out.write((const char*)&version, 4);
  out.write((const char*)&nstate, 4);
  out.write((const char*)&step, 8);
  out.write((const char*)&time, 8);
  out.write((const char*)&ncout, 8);
  out.write((const char*)U.data(), 32 * nc);
  if (nstate == 3) {
    out.write((const char*)Un.data(), 32 * nc);
    out.write((const char*)Unm1.data(), 32 * nc);
  }
}

void Solver::readRestart(const string& path) {
  long step = 0, nc = 0;
  double time = 0.0;
  int nstate = 1;
  std::vector<double> payload;
  if (rank_ == 0) {
    std::ifstream in(path, std::ios::binary);
    check(in.good(), "cannot open restart file: " + path);
    int magic = 0, version = 0;
    in.read((char*)&magic, 4);
    in.read((char*)&version, 4);
    check(magic == 0x46563252, "bad restart magic in " + path);
    in.read((char*)&nstate, 4);
    in.read((char*)&step, 8);
    in.read((char*)&time, 8);
    in.read((char*)&nc, 8);
    check(nc == mesh_.nCellsGlobal, "restart cell count mismatch");
    payload.resize((size_t)nc * 4 * nstate);
    in.read((char*)payload.data(), payload.size() * 8);
  }
  MPI_Bcast(&step, 1, MPI_LONG, 0, comm_);
  MPI_Bcast(&time, 1, MPI_DOUBLE, 0, comm_);
  MPI_Bcast(&nstate, 1, MPI_INT, 0, comm_);
  MPI_Bcast(&nc, 1, MPI_LONG, 0, comm_);
  if (rank_ != 0) payload.resize((size_t)nc * 4 * nstate);
  MPI_Bcast(payload.data(), (int)(payload.size()), MPI_DOUBLE, 0, comm_);
  step_ = step;
  time_ = time;
  for (int c = 0; c < mesh_.nAll; ++c) {
    long gid = mesh_.globalCellId[c];
    const double* p = payload.data() + (size_t)gid * 4 * nstate;
    for (int v = 0; v < 4; ++v) U_[c][v] = p[v];
    if (nstate == 3) {
      for (int v = 0; v < 4; ++v) Un_[c][v] = p[4 + v];
      for (int v = 0; v < 4; ++v) Unm1_[c][v] = p[8 + v];
    }
  }
  if (nstate != 3) {
    Un_ = U_;
    Unm1_ = U_;
  }
}

void Solver::finalizeOutputs() {
  writeSurfaceCsv();
  writeFieldVtk(outdir_ + "/field_final.vtk");
  writeRestart("restart_final.bin");
  writePartitionDiag();
  writeMetadataJson();
  writeRunStatusJson();
}

}  // namespace fv
