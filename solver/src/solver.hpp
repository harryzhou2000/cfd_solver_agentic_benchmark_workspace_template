#pragma once
#include "types.hpp"
#include "mesh.hpp"
#include "physics.hpp"
#include <mpi.h>
#include <string>

namespace cfd {

struct CaseInput {
    std::string caseId;
    std::string meshFile;
    bool viscous = false;
    Real reynolds = 0.0;
    Real gamma = 1.4, Rgas = 1.0, prandtl = 0.72;
    Real machInf = 0.1, aoaDeg = 0.0, rhoInf = 1.0, velInf = 1.0, pInf = 1.0;
    Real refLength = 1.0, refArea = 1.0;
    Real momentCx = 0.25, momentCy = 0.0;
    std::map<std::string, BCType> bcMap;
    // run control
    bool transient = false;
    Real timeStep = 0.01, finalTime = 300.0;
    int maxSteps = 20000;
    Real cflInitial = 1.0, cflMax = 100.0;
    int cflRampSteps = 2000;
    int minInner = 3, maxInner = 50;
    Real innerTarget = 0.01;
    Real residualTarget = 4.0; // orders
    Real rusanovScale = 1.0;
    std::string timeIntegrator;
};

class Solver {
public:
    CasePhysics phys;
    CaseInput input;
    LocalMesh lm;
    MPI_Comm comm;
    int rank = 0, nranks = 1;

    std::vector<Cons> U;    // owned + ghost
    std::vector<Cons> Un, Unm1; // owned only (BDF2 history)

    int nSweep = 2;          // LU-SGS sweeps per implicit solve
    bool useOffDiag = true;   // off-diagonal coupling in LU-SGS
    bool diverged = false;    // set if run diverged
    // diagnostics accumulation
    Real qInf = 0.0;

    void setup(const CaseInput& in, MPI_Comm comm);
    // steady pseudo-time march; returns final residual L2
    void runSteady(const std::string& outDir);
    // transient BDF2 march
    void runTransient(const std::string& outDir);

    // residual + diagnostics
    void computeResidual(const std::vector<Cons>& Ustate, std::vector<Cons>& R,
                         std::vector<Real>& faceLam, bool secondOrder);
    void luSgs(const std::vector<Cons>& R, const std::vector<Real>& faceLam,
               std::vector<Cons>& dU, Real cfl, Real physDtAlpha, Real dtPhys);
    void computeForces(const std::vector<Cons>& Ustate, Real& cl, Real& cd, Real& cmz,
                       Real& pdrag, Real& vdrag, Real& plift, Real& vlift);
    Real globalResidualL2(const std::vector<Cons>& R);
    Real globalResidualTotal(const std::vector<Cons>& R);

private:
    void initFreestream();
    // Write all output-contract files for one run.
    void writeOutputs(const std::string& outDir, const std::string& mode,
                      double wall, int finalStep, Real physTime, Real res0,
                      Real finalRes,
                      const std::vector<std::string>& resRows,
                      const std::vector<std::string>& forceRows,
                      int minObs, int maxObs, int nInnerTotal, int nStepsTotal,
                      int targetMisses, Real lastInnerRatio);
    void computeGradients(const std::vector<Cons>& Ustate,
                          std::array<std::vector<Vec2>,4>& grad); // grad of rho,u,v,p
    void computeLimiters(const std::vector<Cons>& Ustate,
                         const std::array<std::vector<Vec2>,4>& grad,
                         std::array<std::vector<Real>,4>& phi);
};

CaseInput parseCase(const std::string& path, const std::string& meshBaseDir);

} // namespace cfd
