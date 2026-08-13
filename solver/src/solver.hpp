#pragma once
// solver.hpp - Main solver class orchestrating mesh, partition, FVM residual,
// implicit solve, time integration, halo exchange, and output.
#include "cfd2d.hpp"

namespace cfd2d {

struct SolverConfig {
    std::string fluxType = "roe";      // "roe" or "rusanov"
    double rusanovScale = 1.0;
    int maxSteps = 10000;
    bool secondOrder = true;
    bool useLimiter = true;
    std::string outputDir;
    std::string restartFile;
    int reportLevel = 1;
    std::string caseJsonPath;
};

struct ResidualHistory {
    std::vector<int> step;
    std::vector<double> physicalTime;
    std::vector<int> innerIter;
    std::vector<double> cfl;
    std::vector<double> dt;
    std::vector<double> rho, rhou, rhov, rhoE;
    std::vector<double> residualL2, residualLinf;
};

struct ForceHistory {
    std::vector<int> step;
    std::vector<double> physicalTime;
    std::vector<double> cl, cd, cmz;
    std::vector<double> pressureDrag, viscousDrag;
    std::vector<double> pressureLift, viscousLift;
};

struct SurfaceRow {
    double x, y, nx, ny;
    double pressure, cp, cf;
    double rho, u, v, mach;
    std::string tag;
};
struct SurfaceData {
    std::vector<SurfaceRow> rows;
};

class Solver {
public:
    Solver(MPI_Comm comm);
    ~Solver();

    int run(const std::string& caseJsonPath, const std::string& outputDir,
            const std::string& restartFile, int reportLevel);

private:
    MPI_Comm comm_;
    int rank_, nProcs_;
    
    CaseDef case_;
    SolverConfig config_;
    GasModel gas_;
    
    GlobalMesh globalMesh_;
    LocalMesh localMesh_;
    
    // Solution state
    StateVec U_;        // current solution (owned + ghost)
    StateVec Un_;       // previous time level (for BDF2)
    StateVec Unm1_;     // two time levels ago (for BDF2)
    
    // Gradients and limiters
    std::vector<Eigen::Matrix<double,2,NEQ>> grads_;
    std::vector<double> limiters_;
    
    // Residual
    StateVec rhs_;      // residual for owned cells
    StateVec du_;       // update
    
    // Timing/output
    ResidualHistory resHistory_;
    ForceHistory forceHistory_;
    
    // Inner iteration statistics (for Re200)
    int obsMinInner_ = 999999;
    int obsMaxInner_ = 0;
    long totalInnerIters_ = 0;
    long totalPhysicalSteps_ = 0;
    int innerTargetMisses_ = 0;
    double lastInnerResidualRatio_ = 0.0;
    
    // Freestream state
    State Uinf_;
    Prim Winf_;
    
    // Methods
    void initialize();
    void readAndPartitionMesh();
    void setInitialCondition();
    void setFreestream();
    
    // Halo exchange
    void exchangeHalo(StateVec& U);
    void buildHaloCommunication();
    std::vector<int> neighborRanks_;
    // Send/receive buffers
    std::vector<std::vector<double>> sendBuf_;  // [neighborIdx] -> flat state data
    std::vector<std::vector<double>> recvBuf_;
    std::vector<std::vector<int>> sendLocalIds_; // local owned cell IDs to send to each neighbor
    std::vector<std::vector<int>> recvLocalIds_; // local ghost cell IDs to fill from each neighbor
    std::vector<MPI_Request> sendReqs_, recvReqs_;
    bool haloCommBuilt_ = false;
    
    // Residual computation
    void computeResidual(const StateVec& U, StateVec& rhs, bool includeViscous);
    void computeGradientsAndLimiters();
    void applyBoundaryConditions(StateVec& U);
    State boundaryState(const Face& face, const State& Ucell, const Prim& Wcell);
    
    // Implicit solve (LU-SGS)
    void solveLU_SGS(StateVec& U, const StateVec& rhs, double dt, const StateVec& source);
    void solveLU_SGS_BDF2(StateVec& U, const StateVec& totalRhs, double dtau, double bdf2_a0);
    void implicitStep(double dt, int maxInner, double innerTarget, 
                      bool isTransient, const StateVec& Un, const StateVec& Unm1,
                      double physicalDt, int& innerIters, double& finalRatio);
    
    // Time stepping
    void runSteady();
    void runTransient();
    
    // CFL
    double computeCFL(int step, int rampSteps, double cflInit, double cflMax) const;
    double computeLocalDt(const StateVec& U, double cfl) const;
    
    // Forces
    void computeForces(const StateVec& U, double& cl, double& cd, double& cmz,
                       double& pDrag, double& vDrag, double& pLift, double& vLift);
    
    // Surface output
    SurfaceData computeSurface(const StateVec& U);
    
    // Output
    void writeMetadata(const std::string& outputDir, double wallTime, int finalStep,
                       double finalTime, const std::string& convStatus, bool completed);
    void writeRunStatus(const std::string& outputDir, const std::string& command,
                        double wallTime, int finalStep, double finalTime,
                        const std::string& convStatus, double resReduction,
                        const std::string& notes);
    void writePartitionDiagnostics(const std::string& outputDir);
    void writeResidualsCSV(const std::string& outputDir);
    void writeForcesCSV(const std::string& outputDir);
    void writeSurfaceCSV(const std::string& outputDir, const StateVec& U);
    void writeFieldVTU(const std::string& outputDir, const StateVec& U);
    void writeRestart(const std::string& outputDir, const StateVec& U);
    void writeStdoutLog(const std::string& outputDir, const std::string& msg);
    
    // Utility
    double globalReduceSum(double local) const;
    double globalReduceMax(double local) const;
    double globalReduceMin(double local) const;
    void globalReduceSumArray(double* local, double* global, int n) const;
    
    // Residual norm
    void computeResidualNorm(const StateVec& rhs, double& l2, double& linf,
                             double& compRho, double& compRhou, double& compRhov, double& compRhoE);
};

} // namespace cfd2d
