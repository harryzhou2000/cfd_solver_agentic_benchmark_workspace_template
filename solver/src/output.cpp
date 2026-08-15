#include "cfd.hpp"
#include <fstream>

void append_residual(const std::string &path, long step, double t, int inner, double cfl, double dt,
                     const double res[4], double l2, double linf) {
  std::ofstream f(path, std::ios::app);
  f << step << "," << t << "," << inner << "," << cfl << "," << dt;
  for (int c = 0; c < 4; c++) f << "," << res[c];
  f << "," << l2 << "," << linf << "\n";
}
void append_force(const std::string &path, long step, double t, double cl, double cd, double cmz,
                  double pdrag, double vdrag, double plift, double vlift) {
  std::ofstream f(path, std::ios::app);
  f << step << "," << t << "," << cl << "," << cd << "," << cmz << ","
    << pdrag << "," << vdrag << "," << plift << "," << vlift << "\n";
}
void write_metadata(const std::string &dir, const json &meta) { std::ofstream(dir + "/metadata.json") << meta.dump(2) << "\n"; }
void write_run_status(const std::string &dir, const json &st) { std::ofstream(dir + "/run_status.json") << st.dump(2) << "\n"; }

static const char *bc_tag(BCType bc) {
  if (bc == BC_FARFIELD) return "farfield";
  if (bc == BC_SLIPWALL) return "slip_wall";
  if (bc == BC_NOSLIP_ADIABATIC) return "no_slip_adiabatic_wall";
  return "none";
}

void write_surface(const std::string &path, const LocalMesh &m, const std::vector<Vec4> &U,
                   const Gas &g, const CaseInput &cs, double qinf, double pinf) {
  std::vector<double> rows;
  for (size_t f = 0; f < m.fL.size(); f++) {
    if (m.fPart[f] != 2) continue;
    BCType bc = m.fBC[f];
    if (bc != BC_NOSLIP_ADIABATIC && bc != BC_SLIPWALL) continue;
    int li = m.fL[f];
    double rho, u, v, p, T;
    primitive(U[li], g, rho, u, v, p, T);
    double nx = m.fnx[f], ny = m.fny[f];
    double cp = (p - pinf) / qinf;
    double uw, vw, mach, cf = 0;
    if (bc == BC_NOSLIP_ADIABATIC) {
      uw = 0; vw = 0; mach = 0;
      double dist = (m.fcx[f] - m.cx[li]) * nx + (m.fcy[f] - m.cy[li]) * ny;
      if (dist < 1e-12) dist = 1e-12;
      double ut = -u * ny + v * nx, tau = g.mu * ut / dist;
      cf = tau / qinf;
    } else {
      double un = u * nx + v * ny;
      uw = u - un * nx; vw = v - un * ny;
      double sp = std::sqrt(uw * uw + vw * vw);
      double a = sound_speed(p, rho, g.gamma);
      mach = sp / a;
    }
    rows.push_back(m.fcx[f]); rows.push_back(m.fcy[f]);
    rows.push_back(nx); rows.push_back(ny);
    rows.push_back(p); rows.push_back(cp); rows.push_back(cf); rows.push_back(rho);
    rows.push_back(uw); rows.push_back(vw); rows.push_back(mach); rows.push_back((double)bc);
  }
  int nrow = (int)rows.size() / 12;
  if (m.rank == 0) {
    std::vector<int> counts(m.nprocs), displs(m.nprocs);
    MPI_Gather(&nrow, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
    int tot = 0;
    for (int r = 0; r < m.nprocs; r++) { displs[r] = tot; tot += counts[r]; }
    std::vector<int> dcounts(m.nprocs), ddispls(m.nprocs);
    for (int r = 0; r < m.nprocs; r++) { dcounts[r] = counts[r] * 12; ddispls[r] = displs[r] * 12; }
    std::vector<double> all(tot * 12);
    MPI_Gatherv(rows.data(), nrow * 12, MPI_DOUBLE, all.data(), dcounts.data(), ddispls.data(), MPI_DOUBLE, 0, MPI_COMM_WORLD);
    std::ofstream f(path);
    f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    for (int i = 0; i < tot; i++) {
      double *r = &all[i * 12];
      int bc = (int)r[11];
      f << r[0] << "," << r[1] << "," << r[2] << "," << r[3] << "," << r[4] << "," << r[5] << ","
        << r[6] << "," << r[7] << "," << r[8] << "," << r[9] << "," << r[10] << "," << bc_tag((BCType)bc) << "\n";
    }
  } else {
    MPI_Gather(&nrow, 1, MPI_INT, nullptr, 0, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Gatherv(rows.data(), nrow * 12, MPI_DOUBLE, nullptr, nullptr, nullptr, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  }
}

void write_field_vtu(const std::string &path, const GlobalMesh *gm, const LocalMesh &m,
                     const std::vector<Vec4> &U, const Gas &g, int rank, int nprocs) {
  int nOwn = m.nOwn;
  std::vector<int> gids(nOwn);
  for (int i = 0; i < nOwn; i++) gids[i] = m.gid[i];
  std::vector<double> states(nOwn * 4);
  for (int i = 0; i < nOwn; i++) for (int c = 0; c < 4; c++) states[i * 4 + c] = U[i][c];
  std::vector<int> counts(nprocs), displs(nprocs);
  MPI_Gather(&nOwn, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (rank == 0) {
    int tot = 0;
    for (int r = 0; r < nprocs; r++) { displs[r] = tot; tot += counts[r]; }
    std::vector<int> allGid(tot);
    std::vector<double> allSt(tot * 4);
    MPI_Gatherv(gids.data(), nOwn, MPI_INT, allGid.data(), counts.data(), displs.data(), MPI_INT, 0, MPI_COMM_WORLD);
    std::vector<int> c4(nprocs), d4(nprocs);
    for (int r = 0; r < nprocs; r++) { c4[r] = counts[r] * 4; d4[r] = displs[r] * 4; }
    MPI_Gatherv(states.data(), nOwn * 4, MPI_DOUBLE, allSt.data(), c4.data(), d4.data(), MPI_DOUBLE, 0, MPI_COMM_WORLD);
    long ng = gm->nCells;
    std::vector<Vec4> gU(ng);
    std::vector<int> gRank(ng, -1);
    for (int r = 0; r < nprocs; r++)
      for (int i = 0; i < counts[r]; i++) {
        int gi = allGid[displs[r] + i];
        for (int c = 0; c < 4; c++) gU[gi][c] = allSt[(displs[r] + i) * 4 + c];
        gRank[gi] = r;
      }
    std::ofstream f(path);
    f << "<?xml version=\"1.0\"?>\n<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n<UnstructuredGrid>\n";
    f << "<Piece NumberOfPoints=\"" << gm->nVerts << "\" NumberOfCells=\"" << ng << "\">\n";
    f << "<Points><DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    for (long i = 0; i < gm->nVerts; i++) f << gm->vx[i] << " " << gm->vy[i] << " 0\n";
    f << "</DataArray></Points>\n<Cells>\n";
    f << "<DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
    for (long c = 0; c < ng; c++) { int nv = gm->cellNv[c]; for (int k = 0; k < nv; k++) f << gm->cellV[c][k] << " "; f << "\n"; }
    f << "</DataArray>\n<DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
    int off = 0;
    for (long c = 0; c < ng; c++) { off += gm->cellNv[c]; f << off << " "; }
    f << "\n</DataArray>\n<DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
    for (long c = 0; c < ng; c++) f << (gm->cellNv[c] == 3 ? 5 : 9) << " ";
    f << "\n</DataArray>\n</Cells>\n<CellData Scalars=\"rho\">\n";
    auto arr = [&](const char *name, const std::vector<double> &v) {
      f << "<DataArray type=\"Float64\" Name=\"" << name << "\" format=\"ascii\">\n";
      for (double x : v) f << x << " ";
      f << "\n</DataArray>\n";
    };
    std::vector<double> vrho, vu, vv, vp, vmach, vT, vrank;
    for (long c = 0; c < ng; c++) {
      double rho, u, v, p, T;
      primitive(gU[c], g, rho, u, v, p, T);
      double a = sound_speed(p, rho, g.gamma);
      vrho.push_back(rho); vu.push_back(u); vv.push_back(v); vp.push_back(p); vT.push_back(T);
      vmach.push_back(std::sqrt(u * u + v * v) / a); vrank.push_back(gRank[c]);
    }
    arr("rho", vrho); arr("u", vu); arr("v", vv); arr("p", vp); arr("mach", vmach); arr("T", vT); arr("rank", vrank);
    f << "</CellData>\n</Piece>\n</UnstructuredGrid>\n</VTKFile>\n";
  } else {
    MPI_Gatherv(gids.data(), nOwn, MPI_INT, nullptr, nullptr, nullptr, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Gatherv(states.data(), nOwn * 4, MPI_DOUBLE, nullptr, nullptr, nullptr, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  }
}

void write_restart(const std::string &path, const std::vector<Vec4> &U, double t, long step) {
  std::ofstream f(path, std::ios::binary);
  double magic = 2.0;
  f.write((char *)&magic, 8);
  f.write((char *)&t, 8);
  f.write((char *)&step, 8);
  int n = (int)U.size();
  f.write((char *)&n, 4);
  f.write((char *)U.data(), n * sizeof(Vec4));
}
bool read_restart(const std::string &path, std::vector<Vec4> &U, double &t, long &step) {
  std::ifstream f(path, std::ios::binary);
  if(!f) return false;
  double magic;
  f.read((char *)&magic, 8);
  if(magic != 2.0) return false;
  f.read((char *)&t, 8);
  f.read((char *)&step, 8);
  int n;
  f.read((char *)&n, 4);
  if(n != (int)U.size()) return false;
  f.read((char *)U.data(), n * sizeof(Vec4));
  return (bool)f;
}
void write_combined_checkpoint(const std::string &path, const LocalMesh &m, const std::vector<Vec4> &U, double t, long step) {
  int nOwn = m.nOwn;
  std::vector<int> rc(m.nprocs), disp(m.nprocs);
  MPI_Gather(&nOwn, 1, MPI_INT, rc.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  int total = 0;
  if (m.rank == 0) { for (int r = 0; r < m.nprocs; r++) { disp[r] = total; total += rc[r]; } }
  // Gather U (4 doubles per cell) — counts/displs in doubles
  std::vector<int> rc4(m.nprocs), disp4(m.nprocs);
  for (int r = 0; r < m.nprocs; r++) { rc4[r] = rc[r] * 4; disp4[r] = disp[r] * 4; }
  std::vector<Vec4> gathered;
  if (m.rank == 0) gathered.resize(total);
  MPI_Gatherv(U.data(), nOwn * 4, MPI_DOUBLE, gathered.data(), rc4.data(), disp4.data(), MPI_DOUBLE, 0, MPI_COMM_WORLD);
  // Gather gid (ints) — counts/displs in ints
  std::vector<int> all_gid;
  if (m.rank == 0) all_gid.resize(total);
  MPI_Gatherv(m.gid.data(), nOwn, MPI_INT, all_gid.data(), rc.data(), disp.data(), MPI_INT, 0, MPI_COMM_WORLD);
  if (m.rank == 0) {
    std::vector<Vec4> ordered(total);
    for (int i = 0; i < total; i++) ordered[all_gid[i]] = gathered[i];
    std::ofstream f(path, std::ios::binary);
    double magic = 2.0;
    f.write((char *)&magic, 8); f.write((char *)&t, 8); f.write((char *)&step, 8);
    int n = total; f.write((char *)&n, 4);
    f.write((char *)ordered.data(), n * sizeof(Vec4));
  }
}
bool read_combined_checkpoint(const std::string &path, const LocalMesh &m, std::vector<Vec4> &U, double &t, long &step) {
  std::ifstream f(path, std::ios::binary);
  if(!f) return false;
  double magic; f.read((char *)&magic, 8);
  if(magic != 2.0) return false;
  f.read((char *)&t, 8); f.read((char *)&step, 8);
  int n; f.read((char *)&n, 4);
  std::vector<Vec4> global_U; int global_n = n;
  if(m.rank == 0) { global_U.resize(global_n); f.read((char *)global_U.data(), global_n * sizeof(Vec4)); }
  MPI_Bcast(&global_n, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if(m.rank != 0) global_U.resize(global_n);
  MPI_Bcast(global_U.data(), global_n * (int)sizeof(Vec4), MPI_BYTE, 0, MPI_COMM_WORLD);
  for(int i = 0; i < m.nOwn; i++) { int g = m.gid[i]; if(g >= 0 && g < global_n) U[i] = global_U[g]; }
  return true;
}
