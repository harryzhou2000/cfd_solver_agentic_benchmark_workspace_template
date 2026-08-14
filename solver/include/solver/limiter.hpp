#pragma once
#include "partition_types.hpp"
#include "mesh_types.hpp"
#include <vector>

namespace solver {

// Barth-Jespersen slope limiter for shock/oscillation control
// Modifies gradients in-place
void apply_barth_jespersen_limiter(const DistributedMesh& mesh,
                                    const std::vector<double>& state,
                                    std::vector<Vec2>& gradients,
                                    double gamma);

} // namespace solver
