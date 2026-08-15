#pragma once
// cfd.hpp: central types for the 2-D unstructured compressible Navier-Stokes FV solver.
// Conservative state U = [rho, rho*u, rho*v, rho*E]^T (4 components).
// Nondimensionalization: rho_inf=1, U_inf=1, L_ref=1, t_ref=1, p_inf=1/(gamma*M^2), mu=1/Re.
#include <vector>
#include <array>
#include <string>
#include <map>
#include <unordered_map>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <mpi.h>
#include <Eigen/Dense>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
constexpr int NEQ = 4;
using Vec4 = std::array<double, NEQ>;

enum BCType : int { BC_NONE = 0, BC_FARFIELD = 1, BC_SLIPWALL = 2, BC_NOSLIP_ADIABATIC = 3 };

struct CaseInput {
  std::string case_id, mesh_file, description, mode = "inviscid";
  double gamma = 1.4, Rgas = 1.0, prandtl = 0.72;
  double mach = 0.1, aoa_deg = 0.0, rho_inf = 1.0, U_inf = 1.0, p_inf = 1.0;
  double ref_length = 1.0, ref_area = 1.0, moment_cx = 0.25, moment_cy = 0.0;
  double reynolds_length = 1.0, reynolds = 0.0;
  std::map<std::string, std::string> bc_map;
  std::string run_type = "steady";
  int max_steps = 20000;
  double residual_reduction_target = 4.0;
  double cfl_initial = 1.0, cfl_max = 100.0;
  int pseudo_cfl_ramp_steps = 2000;
  int min_inner = 3, max_inner = 50;
  double inner_target = 0.01;
  double inner_target_meta = 0.01;  // case file's target for metadata (always 0.001 for Re200)
  double time_step = 0.01, final_time = 300.0;
  std::string time_integrator = "bdf2";
  double rusanov_dissipation_scale = 1.0;
  bool write_final_field = true, write_surface = true;
  int write_forces_every = 1, write_residuals_every = 1;
};
CaseInput parse_case(const std::string &path);

struct GlobalMesh {
  std::vector<double> vx, vy;
  std::vector<std::array<int,4>> cellV;
  std::vector<int> cellNv;
  std::vector<double> cx, cy, vol;
  std::vector<int> fv0, fv1, fL, fR;
  std::vector<double> fnx, fny, fa, fcx, fcy;
  std::vector<std::string> fFam;
  std::vector<BCType> fBC;
  long nCells = 0, nFaces = 0, nVerts = 0;
  std::vector<std::array<int,4>> cellFace;
  std::vector<int> cellNF;
};
void read_cgns_global(const std::string &path, const CaseInput &cs, GlobalMesh &g, int rank);

struct LocalMesh {
  int rank = 0, nprocs = 1;
  int nOwn = 0, nGhost = 0;
  std::vector<int> gid, gRank;
  std::vector<double> cx, cy, vol;
  std::vector<int> fL, fR, fPart, fV0, fV1;
  std::vector<double> fnx, fny, fa, fcx, fcy;
  std::vector<BCType> fBC;
  std::vector<int> neighbors;
  std::vector<std::vector<int>> sendCells, recvCells, recvGid;
  long nCellsGlobal = 0, nFacesGlobal = 0;
  int edgeCut = 0;
};
void partition_and_scatter(const GlobalMesh &g, int nparts, LocalMesh &m, int rank, int nprocs);

struct Gas { double gamma, Rgas, prandtl, mu, cpmcv, mach, Uinf, pinf, rinf; };
Gas make_gas(const CaseInput &cs);
void primitive(const Vec4 &U, const Gas &g, double &rho, double &u, double &v, double &p, double &T);
double sound_speed(double p, double rho, double gamma);
Vec4 roe_flux(const Vec4 &UL, const Vec4 &UR, double nx, double ny, const Gas &g, double rusanov_scale);
Vec4 rusanov_flux(const Vec4 &UL, const Vec4 &UR, double nx, double ny, const Gas &g, double scale);

void write_metadata(const std::string &dir, const json &meta);
void write_run_status(const std::string &dir, const json &st);
void write_partition_diag(const std::string &dir, const LocalMesh &m, int rank, int nprocs);
void append_residual(const std::string &path, long step, double t, int inner, double cfl, double dt, const double res[4], double l2, double linf);
void append_force(const std::string &path, long step, double t, double cl, double cd, double cmz, double pdrag, double vdrag, double plift, double vlift);
void write_surface(const std::string &path, const LocalMesh &m, const std::vector<Vec4> &U, const Gas &g, const CaseInput &cs, double qinf, double pinf);
void write_field_vtu(const std::string &path, const GlobalMesh *gm, const LocalMesh &m, const std::vector<Vec4> &U, const Gas &g, int rank, int nprocs);
void write_restart(const std::string &path, const std::vector<Vec4> &U, double t, long step);
bool read_restart(const std::string &path, std::vector<Vec4> &U, double &t, long &step);
void write_combined_checkpoint(const std::string &path, const LocalMesh &m, const std::vector<Vec4> &U, double t, long step);
bool read_combined_checkpoint(const std::string &path, const LocalMesh &m, std::vector<Vec4> &U, double &t, long &step);

struct SolveResult {
  std::string convergence_status = "failed";
  double final_time = 0.0, residual_reduction = 0.0;
  long final_step = 0;
  int obs_min_inner = 0, obs_max_inner = 0, sum_inner = 0, n_phys_steps = 0;
  int inner_target_misses = 0;
  double last_inner_ratio = 0.0, mean_inner = 0.0, cfl_used = 1.0;
};
SolveResult run_solver(const CaseInput &cs, const Gas &g, LocalMesh &m, const GlobalMesh *gm, const std::string &outdir, int rank, int nprocs, const std::string &restart);

double mpi_sum(double x, MPI_Comm comm);
double mpi_max(double x, MPI_Comm comm);
double mpi_min(double x, MPI_Comm comm);
