// solver.hpp — Core FV solver: residual, reconstruction, limiter, implicit solve, MPI
#pragma once
#include "mesh.hpp"
#include "partition.hpp"
#include "case_input.hpp"
#include "physics.hpp"
#include <vector>
#include <cstdlib>
#include <mpi.h>

namespace cfd2d {

struct SolverConfig {
    std::string inviscidFluxName = "roe";        // "roe" or "rusanov"
    std::string limiterName = "barth_jespersen";  // "barth_jespersen" or "venkatakrishnan"
    std::string reconstructionName = "green_gauss_linear";
    std::string implicitSolverName = "lu_sgs";
    std::string timeIntegratorName = "steady";  // "steady", "bdf2", "trapezoidal"
    double rusanovScale = 1.0;
    bool usePositivityFallback = true;
    bool useRoeForSupersonic = true; // Use Roe for supersonic, Rusanov for subsonic
};

struct Solver {
    const CaseInput* caseInput = nullptr;
    const Mesh* globalMesh = nullptr;
    LocalMesh localMesh;
    GasModel gas;
    SolverConfig config;

    int mpiRank = 0;
    int mpiSize = 1;

    // Solution arrays: [nLocalCells][NEQ]
    std::vector<double> U;        // current solution
    std::vector<double> Un;       // previous physical-time solution (for transient)
    std::vector<double> Unm1;     // U^{n-1} (for BDF2)
    std::vector<double> dU;       // update
    std::vector<double> residual; // residual [nLocalCells][NEQ]

    // Gradients (Green-Gauss): [nLocalCells][4 primitive vars][2 dims]
    // primitive: rho, u, v, T
    std::vector<double> gradients; // size nLocalCells * 4 * 2
    std::vector<double> limiters;  // size nLocalCells * 4 (per-variable limiter)

    // Cell->face adjacency (local)
    std::vector<std::vector<int>> cellFaces;

    // Force accumulation
    double forceP_drag = 0, forceP_lift = 0;   // pressure drag/lift (force, not coefficient)
    double forceV_drag = 0, forceV_lift = 0;   // viscous drag/lift
    double moment = 0;

    // Inner iteration statistics
    int totalInnerIters = 0;
    int innerTargetMisses = 0;
    int nPhysicalSteps = 0;
    int obsMinInner = 1e9, obsMaxInner = 0;
    double lastInnerResidualRatio = 0.0;
    double sumInnerIters = 0;

    // History storage (filled during run)
    struct HistRow {
        int step;
        double physicalTime;
        int innerIter;
        double cfl;
        double dt;
        double resL2[NEQ];
        double residualL2, residualLinf;
        double cl, cd, cmz;
        double pressureDrag, viscousDrag, pressureLift, viscousLift;
    };
    std::vector<HistRow> history;
    
    // Step override (for testing/debugging, 0 = use case file value)
    int maxStepsOverride = 0;
    bool firstOrder = false;
    double cflCap = 1e9;

    // Residual norms (global)
    double resL2[NEQ] = {0};
    double resLinf[NEQ] = {0};

    // CFL
    double currentCFL = 1.0;
    double currentDt = 0.0; // physical dt for transient

    // Init
    void init(const CaseInput& ci, const Mesh& gm, int rank, int size);
    void buildCellFaceAdjacency();

    // Halo exchange
    void exchangeHalos();

    // Reconstruction
    void computeGradients();
    void computeLimiters();

    // Residual assembly
    void computeResidual(bool includeViscous);
    void applyBoundaryConditions();

    // Implicit solve
    void luSGSUpdate(double dt, bool transient, double alphaTime);

    // Time integration
    int runSteady();
    int runTransient();

    // Forces
    void computeForces();

    // Accessors
    double* cellU(int i) { return &U[i * NEQ]; }
    const double* cellU(int i) const { return &U.data()[i * NEQ]; }

    // Get local gradient for cell i, variable v, dimension d
   double& grad(int i, int v, int d) { return gradients[(i * 4 + v) * 2 + d]; }
   double& limiter(int i, int v) { return limiters[i * 4 + v]; }
    double grad(int i, int v, int d) const { return gradients[(i * 4 + v) * 2 + d]; }
    double limiter(int i, int v) const { return limiters[i * 4 + v]; }

    // Global reduction for residuals
    void globalResidualNorm();
};

} // namespace cfd2d
