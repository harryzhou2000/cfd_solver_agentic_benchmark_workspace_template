#pragma once

#include "solver.hpp"

namespace cfd {

// Reduces per-rank force partials to a global ForceData on all ranks.
ForceData reduce_forces(const ForceData& local, MPI_Comm comm);

// Computes force coefficients from integrated components.
void normalize_forces(ForceData& f, const FreeStream& fs, const Case& c);

}  // namespace cfd
