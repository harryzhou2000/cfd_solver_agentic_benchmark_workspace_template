#pragma once

#include <string>
#include <map>
#include <vector>
#include "types.h"

namespace cfd2d {

struct CaseInput {
  int schema_version = 1;
  std::string case_id;
  std::string description;
  std::string meshFile;
  std::string meshFormat;
  int dimension = 2;
  std::string physicsMode; // "inviscid" or "laminar"
  double reynolds = 0.0;
  std::string viscosityModel; // "constant"
  double gamma = 1.4;
  double R = 1.0;
  double prandtl = 0.72;
  double mach = 0.1;
  double aoaDeg = 0.0;
  double rhoInf = 1.0;
  double velMag = 1.0;
  double pressureInf = 1.0;
  double refLength = 1.0;
  double refArea = 1.0;
  double momentCx = 0.25, momentCy = 0.0;
  double reynoldsLength = 1.0;
  std::map<std::string, std::string> boundaryConditions;
  // run control
  std::string runType = "steady"; // "steady" or "transient"
  int maxSteps = 20000;
  double residualReductionTarget = 4.0;
  double cflInitial = 1.0;
  double cflMax = 100.0;
  int pseudoCflRampSteps = 2000;
  int minInnerIter = 3;
  int maxInnerIter = 50;
  double innerResidualReductionTarget = 0.01;
  // transient
  std::string timeIntegrator;
  double timeStep = 0.01;
  double finalTime = 300.0;
  std::string innerResidualNorm;
  std::string bdf2HistoryUpdate;
  double rusanovDissipationScale = 1.0;
  // outputs
  bool writeFinalField = true;
  bool writeSurface = true;
  int writeForcesEvery = 1;
  int writeResidualsEvery = 1;
  double writeFieldEveryTime = 0.0;
  std::string wakeVisualization;
  std::vector<double> recommendedVorticityClipRange;
};

// Load and parse a case JSON file. Returns false on error.
bool loadCase(const std::string& path, CaseInput& ci, std::string& err);

// Resolve mesh path relative to case file directory.
std::string resolveMeshPath(const std::string& casePath, const std::string& meshFile);

} // namespace cfd2d
