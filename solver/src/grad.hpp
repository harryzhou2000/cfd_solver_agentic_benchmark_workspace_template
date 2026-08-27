#pragma once
// Weighted least-squares gradients and Barth-Jespersen-type slope limiting
// on unstructured meshes.
#include "common.hpp"
#include "local_mesh.hpp"
#include "flux.hpp"
#include <functional>

namespace fv {

struct Grads {
  Vec2 rho, u, v, p, T;
};

struct Limiters {
  double rho = 1, u = 1, v = 1, p = 1;
};

// Boundary ghost primitive state given the BC type, cell state, and outward normal.
using GhostFn = std::function<Prim(int bc, const Prim& wc, double nx, double ny)>;

// Compute LSQ gradients of [rho,u,v,p,T] for all owned cells.
void computeGrads(const LocalMesh& m, const vector<Prim>& W, const Gas& gas,
                  const GhostFn& ghost, vector<Grads>& grads);

// Barth-Jespersen or Venkatakrishnan limiter for [rho,u,v,p] with positivity
// floors on rho and p. venkatK > 0 selects the Venkatakrishnan smooth limiter
// with that K parameter; venkatK <= 0 selects Barth-Jespersen.
void computeLimiters(const LocalMesh& m, const vector<Prim>& W, const vector<Grads>& grads,
                     const GhostFn& ghost, double rhoFloor, double pFloor, double venkatK,
                     vector<Limiters>& lim);

}  // namespace fv
