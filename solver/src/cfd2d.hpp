#pragma once
// cfd2d.hpp - Core data structures and declarations for the 2D compressible
// Navier-Stokes finite-volume solver.
//
// Design goals:
//   - Cell-centered unstructured FVM on mixed tri/quad meshes.
//   - MPI domain decomposition via METIS; rank-local owned + ghost cells.
//   - Second-order reconstruction with Barth-Jespersen limiter.
//   - Roe approximate Riemann flux with entropy fix (fallback Rusanov).
//   - Laminar viscous terms (Newtonian stress, Fourier heat flux).
//   - LU-SGS implicit pseudo-time stepping for steady cases.
//   - BDF2 dual-time stepping with inner iterations for transient cases.
//   - Extensible: gas model, BC, and equation-set abstractions are not
//     hard-coded to specific cases.

#include <mpi.h>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <nlohmann/json.hpp>
#include <vector>
#include <array>
#include <string>
#include <map>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include <functional>
#include <fstream>

namespace cfd2d {

using json = nlohmann::json;

// Conservative state: [rho, rho*u, rho*v, rho*E]
constexpr int NEQ = 4;
using State = Eigen::Matrix<double, NEQ, 1>;
using StateVec = std::vector<State>;

// Primitive state: [rho, u, v, p] (and T derived)
constexpr int NPRIM = 5; // rho, u, v, p, T
using Prim = Eigen::Matrix<double, NPRIM, 1>;
using PrimVec = std::vector<Prim>;

// Index types
using idx_t = int64_t;

// ---- Gas model (calorically perfect) ----
struct GasModel {
    double gamma = 1.4;
    double R = 1.0;         // specific gas constant (nondimensional)
    double prandtl = 0.72;
    double cv() const { return R / (gamma - 1.0); }
    double cp() const { return gamma * R / (gamma - 1.0); }
};

// ---- Freestream / reference ----
struct Freestream {
    double mach = 0.0;
    double aoa = 0.0;          // radians
    double rho = 1.0;
    double velocity = 1.0;     // velocity magnitude
    double pressure = 1.0;
    double T() const;          // temperature from p = rho*R*T
};

struct Reference {
    double length = 1.0;
    double area = 1.0;
    std::array<double,2> moment_center = {0.0, 0.0};
    double reynolds_length = 1.0;
};

// ---- Boundary condition types ----
enum class BCType {
    Interior,        // not a boundary
    Farfield,        // farfield / Riemann-based
    SlipWall,        // inviscid wall
    NoSlipAdiabatic, // viscous wall
    Unknown
};

// BC specification from case file
struct BCSpec {
    std::string familyName;
    BCType type = BCType::Unknown;
};

// ---- Physics configuration ----
struct PhysicsConfig {
    std::string mode = "inviscid";  // "inviscid" or "laminar"
    bool viscous = false;
    double reynolds = 0.0;
    std::string viscosityModel = "constant";
    double mu() const;              // dynamic viscosity from Re
};

// ---- Run control ----
struct RunControl {
    std::string type = "steady";    // "steady" or "transient"
    int maxSteps = 10000;
    double residualReductionTarget = 4.0;
    double cflInitial = 1.0;
    double cflMax = 100.0;
    int pseudoCflRampSteps = 2000;
    int minInner = 3;
    int maxInner = 50;
    double innerResidualTarget = 0.01;
    // transient
    std::string timeIntegrator = "";
    double timeStep = 0.0;
    double finalTime = 0.0;
    std::string bdf2HistoryUpdate = "";
    double rusanovDissipationScale = 1.0;
    std::string innerResidualNorm = "";
    int writeFieldEvery = 0;
    int forcesEvery = 1;
    int residualsEvery = 1;
};

// ---- Mesh data structures ----
struct Cell {
    std::vector<idx_t> faces;       // face indices
    std::vector<idx_t> neighbors;   // neighbor cell indices (global)
    double xc, yc;                  // cell center
    double volume;                  // cell area in 2D
    int type;                       // element type (3=tri, 4=quad)
    std::vector<idx_t> nodes;       // vertex indices (global)
    idx_t globalId;                 // global cell id
    bool isGhost = false;
    int ghostOwnerRank = -1;
    int ghostRemoteId = -1;         // remote local id of the source cell
    int partition = -1;
};

struct Face {
    std::array<idx_t,2> cells;     // [left, right] cell global ids; right=-1 for boundary
    std::array<idx_t,2> nodes;     // face vertices
    double nx, ny;                  // unit normal (points from left to right)
    double area;                    // face length
    double fcx, fcy;                // face center
    bool isBoundary = false;
    int bcType = (int)BCType::Interior;
    std::string bcFamily;
    bool isInterface = false;       // inter-partition face (ghost)
    int neighborRank = -1;          // for ghost faces
    int ghostLocalId = -1;          // local ghost cell id for this face's right cell
};

struct GlobalMesh {
    std::vector<double> coordsX, coordsY;   // node coordinates
    std::vector<Cell> cells;
    std::vector<Face> faces;
    int numNodes = 0;
    idx_t numCellsGlobal = 0;
    idx_t numFacesGlobal = 0;
    std::map<std::string, BCType> bcMap;   // family name -> BC type
    std::vector<std::string> wallFamilies; // families that are walls
};

// ---- Rank-local mesh ----
struct LocalMesh {
    // Owned + ghost cells in a contiguous array: [0, nOwned) = owned, [nOwned, nOwned+nGhost) = ghost
    std::vector<Cell> cells;
    std::vector<Face> faces;
    int nOwned = 0;
    int nGhost = 0;
    int nNodes = 0;
    std::vector<double> coordsX, coordsY;
    
    // Ghost communication maps
    std::vector<std::vector<idx_t>> ghostRecvCells; // [neighborRank] -> list of remote global cell ids
    std::vector<std::vector<int>> ghostRecvLocalIds; // [neighborRank] -> list of local ghost cell ids
    std::vector<std::vector<int>> ghostSendLocalIds; // [neighborRank] -> list of local owned cell ids to send
    std::vector<int> neighborRanks;
    
    // Boundary faces
    std::vector<int> boundaryFaceIds;
    std::vector<int> wallFaceIds;
    
    // Partition diagnostics
    int edgeCut = 0;
    int rank = 0;
    int nProcs = 1;
    idx_t numCellsGlobal = 0;
};

// ---- Case definition (parsed from JSON) ----
struct CaseDef {
    std::string caseId;
    std::string meshFile;
    GasModel gas;
    Freestream fs;
    Reference ref;
    PhysicsConfig physics;
    RunControl run;
    std::vector<BCSpec> bcs;
    json rawJson;
    
    static CaseDef parse(const std::string& jsonPath);
};

// ---- Global functions ----

// mesh.cpp
GlobalMesh readCGNSMesh(const std::string& path, const std::map<std::string, BCType>& bcMap);
void buildCellFaceTopology(GlobalMesh& mesh);
void computeGeometry(GlobalMesh& mesh);

// partition.cpp
void partitionMesh(GlobalMesh& mesh, int nProcs);
LocalMesh buildLocalMesh(const GlobalMesh& global, int rank, int nProcs);

// flux.cpp
State roeFlux(const State& UL, const State& UR, const GasModel& gas, double scale);
State rusanovFlux(const State& UL, const State& UR, const GasModel& gas, double scale);
State eulerFlux(const State& U, const GasModel& gas);
Prim conservativeToPrimitive(const State& U, const GasModel& gas);
State primitiveToConservative(const Prim& W, const GasModel& gas);
double soundSpeed(const Prim& W, const GasModel& gas);
State inviscidNumericalFlux(const State& UL, const State& UR,
                            double nx, double ny, const GasModel& gas,
                            const std::string& fluxType, double rusanovScale);

// Viscous flux helpers
struct ViscousData {
    double dudx, dudy, dvdx, dvdy, dTdx, dTdy;
};
State viscousFaceFlux(const Prim& WL, const Prim& WR, const ViscousData& grad,
                      double nx, double ny, double mu, double k, const GasModel& gas);

// reconstruction.cpp
struct Gradient {
    double d[NEQ]; // gradient of each conservative or primitive var
};
std::vector<Eigen::Matrix<double,2,NEQ>> computeGradients(
    const LocalMesh& mesh, const StateVec& U, const GasModel& gas);
std::vector<Eigen::Matrix<double,2,NEQ>> computePrimitiveGradients(
    const LocalMesh& mesh, const PrimVec& W, const GasModel& gas);

State reconstructLeft(const State& Uc, const State& Unb,
                      const Eigen::Matrix<double,2,NEQ>& grad,
                      double dx, double dy, double limiter);
State reconstructRight(const State& Uc, const State& Unb,
                       const Eigen::Matrix<double,2,NEQ>& grad,
                       double dx, double dy, double limiter);

double barthJespersenLimiter(const LocalMesh& mesh, int cellId,
                             const StateVec& U,
                             const Eigen::Matrix<double,2,NEQ>& grad,
                             const GasModel& gas);
std::vector<double> computeLimiters(const LocalMesh& mesh, const StateVec& U,
                                     const std::vector<Eigen::Matrix<double,2,NEQ>>& grads,
                                     const GasModel& gas);
std::vector<Eigen::Matrix<double,2,NPRIM>> computePrimGradientsGG(
    const LocalMesh& mesh, const PrimVec& W, const GasModel& gas);
void reconstructFace(const State& UL_cell, const State& UR_cell,
                     const Eigen::Matrix<double,2,NEQ>& gradL,
                     const Eigen::Matrix<double,2,NEQ>& gradR,
                     double limL, double limR,
                     double fcx, double fcy,
                     double xcL, double ycL, double xcR, double ycR,
                     State& UL, State& UR);

// solver.cpp / implicit.cpp / output.cpp are orchestrated by Solver class below

} // namespace cfd2d
