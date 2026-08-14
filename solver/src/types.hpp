#ifndef CFD2D_TYPES_HPP
#define CFD2D_TYPES_HPP

#include <vector>
#include <array>
#include <string>
#include <cmath>
#include <cstdint>

namespace cfd2d {

constexpr int NEQ = 4;
using Cons = std::array<double, NEQ>;
using Prim = std::array<double, NEQ>;

enum class BCType : int {
  Interior   = 0,
  Farfield   = 1,
  SlipWall   = 2,
  NoSlipWall = 3,
};

struct GasModel {
  double gamma = 1.4, R = 1.0, Pr = 0.72;
  double gamma1, cp;
  GasModel() { init(1.4, 1.0, 0.72); }
  GasModel(double g, double r, double pr) { init(g, r, pr); }
  void init(double g, double r, double pr) {
    gamma = g; R = r; Pr = pr; gamma1 = g - 1.0; cp = g * r / gamma1;
  }
  inline double pressure(const Cons& U) const {
    return gamma1 * (U[3] - 0.5 * (U[1]*U[1] + U[2]*U[2]) / U[0]);
  }
  inline double soundSpeed(double rho, double p) const { return std::sqrt(gamma * p / rho); }
  inline double temperature(double rho, double p) const { return p / (rho * R); }
  inline Prim toPrim(const Cons& U) const {
    Prim W; W[0] = U[0]; W[1] = U[1]/U[0]; W[2] = U[2]/U[0];
    double e = U[3]/U[0] - 0.5*(W[1]*W[1]+W[2]*W[2]); W[3] = gamma1*U[0]*e; return W;
  }
  inline Cons toCons(const Prim& W) const {
    Cons U; U[0] = W[0]; U[1] = W[0]*W[1]; U[2] = W[0]*W[2];
    double e = W[3]/(gamma1*W[0]); U[3] = W[0]*(e + 0.5*(W[1]*W[1]+W[2]*W[2])); return U;
  }
};

struct Freestream {
  double mach = 0, aoa = 0, rho = 1, vel = 1, pressure = 1;
  Prim prim; Cons cons;
  void compute(const GasModel& gas) {
    prim[0] = rho; prim[1] = vel*std::cos(aoa); prim[2] = vel*std::sin(aoa); prim[3] = pressure;
    cons = gas.toCons(prim);
  }
};

struct CaseConfig {
  std::string caseId, meshFile, meshFormat, mode, viscosityModel;
  int meshDim = 2;
  double reynolds = 0;
  GasModel gas;
  Freestream fs;
  double refLength = 1.0, refArea = 1.0, momentCx = 0.25, momentCy = 0.0, reynoldsLength = 1.0;
  std::vector<std::pair<std::string, BCType>> bcMap;
  std::string runType = "steady";
  int maxSteps = 20000;
  double residualTarget = 4.0, cflInitial = 1.0, cflMax = 100.0;
  int cflRampSteps = 2000, minInner = 3, maxInner = 50;
  double innerTarget = 0.01, timeStep = 0.01, finalTime = 300.0, rusanovScale = 1.0;
  std::string timeIntegrator = "bdf2";
  double viscosity() const {
    if (mode == "inviscid") return 0.0;
    return fs.rho * fs.vel * reynoldsLength / reynolds;
  }
};

}
#endif
