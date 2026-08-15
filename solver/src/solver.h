#pragma once

#include "common.h"
#include "case.h"
#include "mesh.h"
#include "partition.h"
#include "physics.h"
#include "reconstruction.h"
#include "output.h"

namespace cfd {

// Runs the complete solve for one case. `mesh` is the global mesh (used by
// rank 0 for field/restart output). Writes all contract outputs into
// cfg.output dir. Returns 0 on success, nonzero on failure.
int run_solver(const CaseConfig& cfg, const LocalMesh& lm, const Mesh& mesh,
               const std::string& outdir, const std::string& restart_file,
               bool brief, RunStats& stats, std::string& err);

}  // namespace cfd
