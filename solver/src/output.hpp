#pragma once

#include <string>

#include "case_file.hpp"
#include "mesh.hpp"
#include "solver.hpp"

namespace fv {

// Simple tee logger: rank 0 writes to stdout and to <outdir>/stdout.log.
void openLog(const std::string& path);
void slog(const std::string& msg);
void closeLog();

std::string nowUtcIso();

void writeMetadataJson(const std::string& path, const CaseConfig& cfg,
                       const LocalMesh& lm0, const GlobalMesh* gm, int mpiRanks,
                       long edgeCut, const std::string& fluxName,
                       const std::string& gitRev, const std::string& startUtc,
                       const std::string& endUtc, const RunStats& stats);

void writeRunStatusJson(const std::string& path, const CaseConfig& cfg,
                        const std::string& commandLine, int mpiRanks,
                        const RunStats& stats);

}  // namespace fv
