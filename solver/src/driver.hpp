#pragma once
// MPI driver and command-line orchestration for one case run.

#include <string>

namespace cfd2d {

// Execute: cfd2d solve --case <case.json> --output <dir>
// Returns process exit code (0 on normal completion). MPI is initialized by
// main and finalized after this returns.
int runSolve(int argc, char** argv, int rank, int nRanks);

}  // namespace cfd2d
