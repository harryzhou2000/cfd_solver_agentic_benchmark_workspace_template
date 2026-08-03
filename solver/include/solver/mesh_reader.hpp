#pragma once
#include "mesh_types.hpp"
#include "case_config.hpp"
#include <string>

namespace solver {

Mesh read_cgns_mesh(const std::string& filepath, const CaseConfig& config);

} // namespace solver
