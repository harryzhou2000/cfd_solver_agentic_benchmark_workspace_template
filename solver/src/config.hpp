#pragma once
// Case-file (JSON) parsing. The case JSON is the source of truth for physics,
// boundary-condition mapping, and run-control parameters.

#include <map>
#include <string>

#include "common.hpp"

namespace cfd2d {

struct GasModel {
  double gamma = 1.4;
  double R = 1.0;
  double prandtl = 0.72;
  double cp() const { return gamma * R / (gamma - 1.0); }
  double cv() const { return R / (gamma - 1.0); }
};

struct FreeStream {
  double mach = 0.0;
  double aoaDeg = 0.0;
  double rho = 1.0;
  double velMag = 1.0;
  double pressure = 1.0;
  double u = 0.0;  // components derived from aoa
  double v = 0.0;
  double soundSpeed = 0.0;
};

struct ReferenceData {
  double length = 1.0;
  double area = 1.0;
  Vec2 momentCenter{0.0, 0.0};
  double reynoldsLength = 1.0;
};

struct RunControl {
  std::string type = "steady";  // "steady" or "transient"
  int maxSteps = 0;
  double residualReductionTarget = 4.0;  // orders of magnitude
  double cflInitial = 1.0;
  double cflMax = 100.0;
  int pseudoCflRampSteps = 0;
  int minInner = 3;
  int maxInner = 50;
  double innerResidualReductionTarget = 0.01;
  // transient
  std::string timeIntegrator;
  double timeStep = 0.0;
  double finalTime = 0.0;
  double rusanovDissipationScale = 1.0;
  double writeFieldEveryTime = 0.0;  // 0 = disabled
};

struct CaseConfig {
  int schemaVersion = 1;
  std::string caseId;
  std::string description;
  std::string meshFile;     // absolute path after resolution
  std::string physicsMode = "inviscid";  // "inviscid" | "laminar"
  double reynolds = 0.0;
  std::string viscosityModel = "constant";
  GasModel gas;
  FreeStream freestream;
  ReferenceData reference;
  // mesh boundary-family name -> solver BC type
  std::map<std::string, std::string> bcMap;
  RunControl run;
  // outputs
  int writeForcesEvery = 1;
  int writeResidualsEvery = 1;
  std::string recommendedVortClip;

  bool isViscous() const { return physicsMode == "laminar"; }
  // Constant viscosity matching the case Reynolds number:
  //   mu = rho_inf U_inf L_ref / Re
  double viscosity() const {
    return freestream.rho * freestream.velMag * reference.reynoldsLength / reynolds;
  }
  double thermalConductivity() const {
    return viscosity() * gas.cp() / gas.prandtl;
  }
};

// Load and validate a case JSON file. Throws FatalError with a clear message
// for malformed input, unsupported schema, or unsupported BC types.
CaseConfig loadCase(const std::string& path);

}  // namespace cfd2d
