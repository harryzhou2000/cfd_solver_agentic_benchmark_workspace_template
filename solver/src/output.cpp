#include "output.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <stdexcept>

#include "solver.hpp"

namespace cfd {

#ifndef CFD_GIT_REVISION
#define CFD_GIT_REVISION "unknown"
#endif

const char* git_revision() { return CFD_GIT_REVISION; }

namespace {

template <typename T>
void gatherv_to_root(const std::vector<T>& send, std::vector<T>& recv,
                     std::vector<int>& counts, MPI_Comm comm, int root,
                     int rank, int np, MPI_Datatype type) {
  int n = (int)send.size();
  recv.clear();
  counts.clear();
  if (rank == root) counts.resize(np);
  MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, root, comm);
  std::vector<int> disp;
  if (rank == root) {
    disp.resize(np);
    int off = 0;
    for (int r = 0; r < np; ++r) {
      disp[r] = off;
      off += counts[r];
    }
    recv.resize(off);
  }
  MPI_Gatherv(send.data(), n, type, recv.data(), counts.data(), disp.data(),
              type, root, comm);
}

}  // namespace

void Solver::write_field(const std::string& name) {
  const int nfields = 9;
  std::vector<double> buf;
  std::vector<int32_t> iconn;
  for (int i = 0; i < m_.n_owned; ++i) {
    int nv = m_.cell_nverts[i];
    for (int v = 0; v < nv; ++v) {
      int32_t node = m_.cell_nodes[i][v];
      buf.push_back(m_.node_x[node]);
      buf.push_back(m_.node_y[node]);
    }
    Vec5 w = W_[i];
    double a2 = gamma_ * w[3] / w[0];
    double mach = std::sqrt((w[1] * w[1] + w[2] * w[2]) / std::max(a2, 1e-30));
    double ke = 0.5 * (w[1] * w[1] + w[2] * w[2]);
    buf.push_back(w[0]);
    buf.push_back(w[1]);
    buf.push_back(w[2]);
    buf.push_back(w[3]);
    buf.push_back(mach);
    buf.push_back(w[4]);
    buf.push_back(w[3] / (gamma_ - 1.0) + w[0] * ke);
    buf.push_back((double)rank_);
    double rn = 0.0;
    if (i < (int)R_.size())
      for (int k = 0; k < 4; ++k) rn += R_[i][k] * R_[i][k];
    buf.push_back(std::sqrt(rn) / std::max(m_.cell_vol[i], 1e-30));
    iconn.push_back(nv);
  }

  std::vector<double> all;
  std::vector<int> counts;
  gatherv_to_root(buf, all, counts, comm_, 0, rank_, np_, MPI_DOUBLE);
  std::vector<int32_t> iconn_all;
  std::vector<int> icounts;
  gatherv_to_root(iconn, iconn_all, icounts, comm_, 0, rank_, np_, MPI_INT);

  if (rank_ != 0) return;

  std::string path = out_dir_ + "/" + name;
  FILE* f = fopen(path.c_str(), "w");
  if (!f) throw std::runtime_error("cannot write field file " + path);

  int64_t ncells = (int64_t)iconn_all.size();
  int64_t npoints = 0;
  for (int32_t nv : iconn_all) npoints += nv;

  fprintf(f, "<?xml version=\"1.0\"?>\n");
  fprintf(f,
          "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" "
          "byte_order=\"LittleEndian\">\n<UnstructuredGrid>\n");
  fprintf(f,
          "<Piece NumberOfPoints=\"%lld\" NumberOfCells=\"%lld\">\n",
          (long long)npoints, (long long)ncells);
  fprintf(f, "<Points>\n<DataArray type=\"Float64\" NumberOfComponents=\"3\" "
             "format=\"ascii\">\n");
  {
    int64_t p = 0;
    for (size_t c = 0; c < iconn_all.size(); ++c) {
      int nv = iconn_all[c];
      int64_t base = p * 2 + c * nfields;
      for (int v = 0; v < nv; ++v)
        fprintf(f, "%.10e %.10e 0\n", all[base + 2 * v], all[base + 2 * v + 1]);
      p += nv;
    }
  }
  fprintf(f, "</DataArray>\n</Points>\n<Cells>\n");
  fprintf(f, "<DataArray type=\"Int32\" Name=\"connectivity\" "
             "format=\"ascii\">\n");
  {
    int64_t p = 0;
    for (size_t c = 0; c < iconn_all.size(); ++c) {
      int nv = iconn_all[c];
      for (int v = 0; v < nv; ++v) fprintf(f, "%lld ", (long long)(p + v));
      fprintf(f, "\n");
      p += nv;
    }
  }
  fprintf(f, "</DataArray>\n<DataArray type=\"Int32\" Name=\"offsets\" "
             "format=\"ascii\">\n");
  {
    int64_t p = 0;
    for (size_t c = 0; c < iconn_all.size(); ++c) {
      p += iconn_all[c];
      fprintf(f, "%lld\n", (long long)p);
    }
  }
  fprintf(f, "</DataArray>\n<DataArray type=\"UInt8\" Name=\"types\" "
             "format=\"ascii\">\n");
  for (size_t c = 0; c < iconn_all.size(); ++c)
    fprintf(f, "%d\n", iconn_all[c] == 3 ? 5 : 9);
  fprintf(f, "</DataArray>\n</Cells>\n<CellData>\n");

  const char* fnames[nfields] = {"Density",  "VelocityX", "VelocityY",
                                 "Pressure", "Mach",      "Temperature",
                                 "Energy",   "Rank",      "ResidualNorm"};
  for (int fld = 0; fld < nfields; ++fld) {
    fprintf(f, "<DataArray type=\"Float64\" Name=\"%s\" format=\"ascii\">\n",
            fnames[fld]);
    int64_t p = 0;
    for (size_t c = 0; c < iconn_all.size(); ++c) {
      fprintf(f, "%.10e\n", all[p * 2 + c * nfields + 2 * iconn_all[c] + fld]);
      p += iconn_all[c];
    }
    fprintf(f, "</DataArray>\n");
  }
  fprintf(f, "</CellData>\n</Piece>\n</UnstructuredGrid>\n</VTKFile>\n");
  fclose(f);
}

void Solver::write_outputs_final() {
  std::vector<SurfaceRow> rows = compute_surface();
  int nloc = (int)rows.size();
  std::vector<double> buf(nloc * 11);
  std::vector<int32_t> fam(nloc);
  for (int i = 0; i < nloc; ++i) {
    const SurfaceRow& r = rows[i];
    double* p = buf.data() + i * 11;
    p[0] = r.x; p[1] = r.y; p[2] = r.nx; p[3] = r.ny; p[4] = r.p;
    p[5] = r.cp; p[6] = r.cf; p[7] = r.rho; p[8] = r.u; p[9] = r.v;
    p[10] = r.mach;
    fam[i] = r.family_id;
  }
  std::vector<double> all;
  std::vector<int> counts;
  gatherv_to_root(buf, all, counts, comm_, 0, rank_, np_, MPI_DOUBLE);
  std::vector<int32_t> fam_all;
  std::vector<int> fcounts;
  gatherv_to_root(fam, fam_all, fcounts, comm_, 0, rank_, np_, MPI_INT);

  if (rank_ == 0) {
    FILE* f = fopen((out_dir_ + "/surface.csv").c_str(), "w");
    if (!f) throw std::runtime_error("cannot write surface.csv");
    fprintf(f, "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n");
    for (size_t i = 0; i < fam_all.size(); ++i) {
      const double* p = all.data() + i * 11;
      fprintf(f,
              "%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,"
              "%.10e,%s\n",
              p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9],
              p[10], m_.family_names[fam_all[i]].c_str());
    }
    fclose(f);
  }

  {
    FILE* f = nullptr;
    if (rank_ == 0) {
      f = fopen((out_dir_ + "/partition_diagnostics.csv").c_str(), "w");
      if (!f) throw std::runtime_error("cannot write partition_diagnostics.csv");
      fprintf(f,
              "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_"
              "neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n");
    }
    for (int r = 0; r < np_; ++r) {
      if (rank_ == r) {
        int nsend = 0, nrecv = 0;
        std::string nbrs;
        for (size_t k = 0; k < m_.neighbor_ranks.size(); ++k) {
          nsend += (int)m_.send_cells[k].size();
          nrecv += (int)m_.recv_cells[k].size();
          if (k) nbrs += "|";
          nbrs += std::to_string(m_.neighbor_ranks[k]);
        }
        std::string line;
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "%d,%d,%d,%d,%d,%s,%d,%d\n", rank_,
                 m_.n_owned, m_.n_ghost, (int)m_.faces_bnd.size(),
                 (int)m_.neighbor_ranks.size(), nbrs.c_str(), nsend, nrecv);
        line = tmp;
        int len = (int)line.size();
        MPI_Send(&len, 1, MPI_INT, 0, 77, comm_);
        if (rank_ != 0) MPI_Send(line.data(), len, MPI_CHAR, 0, 78, comm_);
        if (rank_ == 0) fwrite(line.data(), 1, len, f);
      }
      if (rank_ == 0 && r != 0) {
        int len = 0;
        MPI_Recv(&len, 1, MPI_INT, r, 77, comm_, MPI_STATUS_IGNORE);
        std::string line(len, ' ');
        MPI_Recv(line.data(), len, MPI_CHAR, r, 78, comm_, MPI_STATUS_IGNORE);
        fwrite(line.data(), 1, len, f);
      }
      MPI_Barrier(comm_);
    }
    if (rank_ == 0) fclose(f);
  }
}

void Solver::write_restart() {
  std::string path = out_dir_ + "/restart_final.rank" + std::to_string(rank_) +
                     ".bin";
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) throw std::runtime_error("cannot write restart file " + path);
  int64_t magic = 0x4346445253543101LL;
  fwrite(&magic, sizeof(magic), 1, f);
  fwrite(&np_, sizeof(np_), 1, f);
  fwrite(&rank_, sizeof(rank_), 1, f);
  fwrite(&m_.n_owned, sizeof(m_.n_owned), 1, f);
  fwrite(&phys_step_, sizeof(phys_step_), 1, f);
  fwrite(&phys_time_, sizeof(phys_time_), 1, f);
  int transient = cfg_.transient() ? 1 : 0;
  fwrite(&transient, sizeof(transient), 1, f);
  for (int i = 0; i < m_.n_owned; ++i) fwrite(U_[i].data(), sizeof(double), 4, f);
  if (transient) {
    for (int i = 0; i < m_.n_owned; ++i)
      fwrite(Un_[i].data(), sizeof(double), 4, f);
    for (int i = 0; i < m_.n_owned; ++i)
      fwrite(Unm1_[i].data(), sizeof(double), 4, f);
  }
  fclose(f);
}

void Solver::load_restart() {
  std::string path = restart_file_;
  if (path.find(".rank") == std::string::npos) {
    path = restart_file_ + "/restart_final.rank" + std::to_string(rank_) +
           ".bin";
  }
  FILE* f = fopen(path.c_str(), "rb");
  if (!f)
    throw std::runtime_error("cannot open restart file " + path +
                             " (expected per-rank restart files named "
                             "restart_final.rank<N>.bin; pass the directory "
                             "or the rank-0 file)");
  int64_t magic = 0;
  fread(&magic, sizeof(magic), 1, f);
  if (magic != 0x4346445253543101LL)
    throw std::runtime_error("bad magic in restart file " + path);
  int npf, rankf, nof, transient;
  fread(&npf, sizeof(npf), 1, f);
  fread(&rankf, sizeof(rankf), 1, f);
  fread(&nof, sizeof(nof), 1, f);
  if (npf != np_ || nof != m_.n_owned)
    throw std::runtime_error(
        "restart file rank count or mesh partition does not match current "
        "run (restarts require the same MPI rank count and partition)");
  fread(&phys_step_, sizeof(phys_step_), 1, f);
  fread(&phys_time_, sizeof(phys_time_), 1, f);
  fread(&transient, sizeof(transient), 1, f);
  for (int i = 0; i < m_.n_owned; ++i) fread(U_[i].data(), sizeof(double), 4, f);
  if (transient) {
    for (int i = 0; i < m_.n_owned; ++i)
      fread(Un_[i].data(), sizeof(double), 4, f);
    for (int i = 0; i < m_.n_owned; ++i)
      fread(Unm1_[i].data(), sizeof(double), 4, f);
  } else {
    Un_ = U_;
    Unm1_ = U_;
  }
  fclose(f);
  if (restart_as_initial_) {
    phys_step_ = 0;
    phys_time_ = 0.0;
    Un_ = U_;
    Unm1_ = U_;
  }
  cons_to_prim_all();
  halo_exchange(U_);
  cons_to_prim_all();
  if (rank_ == 0) {
    printf("restarted from %s at step %d time %.6f\n", path.c_str(),
           phys_step_, phys_time_);
    fflush(stdout);
  }
}

}  // namespace cfd
