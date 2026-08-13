#pragma once

#include "solver.hpp"

namespace cfd {

void compute_primitives(Solver& s);
void compute_gradients(Solver& s);
void compute_limiters(Solver& s);

// Piecewise-linear Barth-limited reconstruction of `cell` at face `f`.
// Applies a first-order positivity fallback and returns false when it did.
bool reconstruct_face(const Solver& s, int cell, const LocalMesh::Face& f,
                      Prim& q);

}  // namespace cfd
