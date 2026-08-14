#pragma once

#include "types.h"
#include "mesh.h"
#include "partition.h"
#include "fluxes.h"
#include <vector>
#include <array>

namespace cfd2d {

// Compute Green-Gauss cell gradients of primitive variables.
// Uses face midpoint values (average of left/right).
// Returns gradients: [cell][4] for [drho,du,dv,dT]
void computeGradients(const LocalMesh& lm, const std::vector<PrimState>& P,
                      std::vector<std::array<double,4>>& gradX,
                      std::vector<std::array<double,4>>& gradY,
                      const GasPhysics& gas);

// Barth-Jespersen limiter for reconstructed primitive variables.
// For each face, compute left/right states using limited linear reconstruction.
// Returns limited face states.
struct FaceRecon {
  PrimState left, right; // primitive states at face
};

// Compute reconstructed left/right primitive states at all faces.
// Uses Barth-Jespersen limiting. Falls back to first-order if needed for positivity.
void reconstructFaces(const LocalMesh& lm,
                       const std::vector<PrimState>& P,
                       const std::vector<std::array<double,4>>& gradX,
                       const std::vector<std::array<double,4>>& gradY,
                       std::vector<FaceRecon>& recon,
                       const GasPhysics& gas);

} // namespace cfd2d
