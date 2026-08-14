#pragma once
#include "partition_types.hpp"
#include "mesh_types.hpp"
#include <vector>

namespace solver {

// kStateSize (rho, rhou, rhov, rhoE) is defined in partition_types.hpp.

// Largest first: state_interleaved[c*4 + var] for cell c, variable var (0=rho, 1=rhou, 2=rhov, 3=rhoE)
// num_total = num_owned + num_ghost

// Compute least-squares gradients on unstructured mesh
// state: interleaved flat array [num_total * 4]
// Returns gradients as 4*num_total Vec2: grad_rho[0..N-1], grad_rhou[0..N-1], grad_rhov[0..N-1], grad_rhoE[0..N-1]
// where N = mesh.owned_cells.size() + mesh.ghost_cells.size()
std::vector<Vec2> compute_gradients(const DistributedMesh& mesh,
                                     const std::vector<double>& state,
                                     double gamma);

// Apply piecewise-linear reconstruction to get UL/UR at each face
// face_UL, face_UR: output, sized to mesh.local_faces.size()
void reconstruct_face_states(const DistributedMesh& mesh,
                              const std::vector<double>& state,
                              const std::vector<Vec2>& gradients,
                              std::vector<Vec4>& face_UL,
                              std::vector<Vec4>& face_UR);

} // namespace solver
