#pragma once
// Spatial discretization: LSQ gradients, linear reconstruction with
// Barth--Jespersen / Venkatakrishnan limiting, conservative residual
// assembly, boundary conditions, and force/surface extraction.

#include <mpi.h>
#include <string>
#include <vector>
#include "case_file.hpp"
#include "common.hpp"
#include "partition.hpp"

namespace cfd {

struct FlowState {
  std::vector<Vec4> U;                        // conserved state
  std::vector<Vec4> W;                        // primitive state
  std::vector<std::array<double, 8>> gradW;   // d(rho,u,v,p)/d(x,y)
  std::vector<Vec4> phi;                      // limiter per variable
  std::vector<Vec4> R;                        // residual on owned cells
  std::vector<double> face_lambda;            // spectral radius per face
  std::vector<double> lambda_sum;             // per-cell sum of face radii
  bool non_finite = false;

  void allocate(const LocalMesh& m);
};

// Neighbor-scoped nonblocking halo exchange of nvar doubles per cell.
struct Halo {
  MPI_Comm comm = MPI_COMM_WORLD;
  std::vector<double> send_buf, recv_buf;
  std::vector<MPI_Request> send_reqs, recv_reqs;
  // data layout: n_cells * nvar (row-major per cell)
  void exchange(const LocalMesh& m, double* data, int nvar);
};

struct AssembleOpts {
  bool viscous = false;
  int inviscid_flux = 0;   // 0 = Roe, 1 = Rusanov
  double rusanov_scale = 1.0;
  int limiter = 2;         // 0 = none (first order), 1 = Barth-Jespersen, 2 = Venkat
  double venkat_k = 5.0;
  bool compute_dt = true;  // refresh spectral radii
  double time = 0.0;       // physical time (startup perturbation support)
  double pert_aoa_deg = 0.0;
  double pert_duration = 0.0;
};

struct ForceSums {
  double fx_p = 0, fy_p = 0, fx_v = 0, fy_v = 0;
  double mz_p = 0, mz_v = 0;
  void reset() { *this = ForceSums(); }
};

// One row of surface.csv (wall boundary face).
struct SurfaceRow {
  double x, y, nx, ny, pressure, cp, cf, rho, u, v, mach;
  int family;
};

// Freestream primitive state, possibly with a startup perturbation of the
// flow direction (documented trigger used to seed vortex shedding).
Vec4 freestream_prim(const CaseFile& cs, double time, double pert_aoa_deg,
                     double pert_duration);

void compute_primitives(const LocalMesh& m, const GasModel& gas, FlowState& s);
void compute_gradients(const LocalMesh& m, const CaseFile& cs, FlowState& s,
                       const AssembleOpts& o);
void compute_limiter(const LocalMesh& m, const CaseFile& cs, FlowState& s,
                     const AssembleOpts& o);

// Assembles R = sum_faces F*A on owned cells; refreshes face_lambda and
// lambda_sum when o.compute_dt. Accumulates wall forces into fs.
void assemble_residual(const LocalMesh& m, const CaseFile& cs, FlowState& s,
                       const AssembleOpts& o, ForceSums& fs);

// Boundary (wall) surface rows at the current state; rank-local.
std::vector<SurfaceRow> compute_surface_rows(const LocalMesh& m, const CaseFile& cs,
                                             FlowState& s, const AssembleOpts& o);

// Boundary value used by the LSQ gradient stencil for boundary faces.
Vec4 boundary_lsq_value(BCType bc, const Vec4& Wi, double nx, double ny,
                        const Vec4& Wfs);

}  // namespace cfd
