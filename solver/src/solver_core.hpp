#pragma once

#include "common.hpp"
#include "case_file.hpp"
#include "mesh.hpp"
#include <mpi.h>

namespace fv {

// Primitive variable indices in the gradient/limiter arrays
enum { PRHO = 0, PU = 1, PV = 2, PP = 3, PT = 4 };
constexpr int NPRIM = 5;

struct Forces {
    double cl = 0, cd = 0, cmz = 0;
    double pressure_drag = 0, viscous_drag = 0;
    double pressure_lift = 0, viscous_lift = 0;
};

struct SurfaceRow {
    double x, y, nx, ny, pressure, cp, cf, rho, u, v, mach;
    int tag;
};

class Solver {
public:
    Solver(const CaseConfig& cfg, LocalMesh& m, MPI_Comm comm);

    // Full spatial residual R(U) into R (owned cells only). Includes halo exchange,
    // gradients, limiter, inviscid + viscous + boundary fluxes.
    void computeResidual(std::vector<Vec4>& U, std::vector<Vec4>& R);
    // Local pseudo time steps and LU-SGS operator data, frozen for the sweeps.
    void buildImplicitOperator(const std::vector<Vec4>& U, double cfl, double dtPhysAlpha);
    // One forward+backward LU-SGS sweep: solves approx (V/dtau + dR) dU = -R.
    void lusgsSweep(const std::vector<Vec4>& R, std::vector<Vec4>& dU);
    // Apply dU with positivity safeguard.
    void applyUpdate(std::vector<Vec4>& U, const std::vector<Vec4>& dU);

    // Global residual norms (per conserved var L2, combined L2 and Linf).
    void residualNorms(const std::vector<Vec4>& R, double l2var[NVAR], double& l2, double& linf);
    Forces computeForces(const std::vector<Vec4>& U);
    std::vector<SurfaceRow> surfaceRows(const std::vector<Vec4>& U);
    // Cell-centered vorticity (dv/dx - du/dy) for owned cells; runs the gradient pipeline.
    void computeCellVorticity(std::vector<Vec4>& U, std::vector<double>& omega);

    Vec4 freestreamU() const;
    void haloExchange(double* data, int stride);   // over all cells (owned -> ghost)
    void exchangeU(std::vector<Vec4>& U) { haloExchange(&U[0][0], NVAR); }

    const CaseConfig& cfg;
    LocalMesh& m;
    MPI_Comm comm;
    int rank, size;

    double gamma, Rgas, prandtl, mu, cp;
    double Winf[NPRIM];        // freestream primitive state
    double rhoFloor, pFloor;   // positivity floors
    double omega = 1.0;        // nonlinear under-relaxation factor
    bool lowMachFixEff = false; // effective low-Mach dissipation fix flag

    // transient source term data (BDF1/BDF2): Rt = alpha*V*(U - Utarget) + R(U)
    double timeAlpha = 0.0;    // 3/(2dt) or 1/dt; 0 for steady
    std::vector<Vec4> Utarget; // owned cells

    // frozen implicit-operator data
    std::vector<Vec4> Ufr;                 // frozen state (all cells)
    std::vector<double> faceLam;           // convective+viscous spectral radius incl. area
    std::vector<double> diagD;             // owned cells
    std::vector<double> dtau;              // owned cells pseudo time step

private:
    void computePrimitive(const std::vector<Vec4>& U, std::vector<std::array<double, NPRIM>>& W);
    void boundaryValue(int bcTag, const double Wi[NPRIM], double nx, double ny, double Wb[NPRIM]);
    void computeGradients(const std::vector<std::array<double, NPRIM>>& W,
                          std::vector<std::array<double, 2 * NPRIM>>& grad);
    void computeLimiter(const std::vector<std::array<double, NPRIM>>& W,
                        const std::vector<std::array<double, 2 * NPRIM>>& grad,
                        std::vector<std::array<double, NPRIM>>& phi);
    void inviscidFlux(const double WL[NPRIM], const double WR[NPRIM],
                      double nx, double ny, double S, double flux[NVAR]);
    void applyEulerJac(const Vec4& Uc, double nx, double ny, const Vec4& dU, Vec4& out);
    void viscousFaceFlux(const std::vector<std::array<double, NPRIM>>& W,
                         const std::vector<std::array<double, 2 * NPRIM>>& grad,
                         int f, double flux[NVAR], double* tauWall /*nullptr or [3]*/);
    void primitiveFromU(const Vec4& U, double W[NPRIM]) const;
    void conservativeFromW(const double W[NPRIM], Vec4& U) const;

    std::vector<std::array<double, NPRIM>> W_;        // all cells
    std::vector<std::array<double, 2 * NPRIM>> grad_; // all cells (ghosts via exchange)
    std::vector<std::array<double, NPRIM>> phi_;      // all cells (ghosts via exchange)

    // halo buffers
    std::vector<double> sendBuf_, recvBuf_;
};

} // namespace fv
