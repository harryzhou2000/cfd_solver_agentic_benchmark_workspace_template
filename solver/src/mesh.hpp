#pragma once

#include "common.hpp"

namespace cfd {

// Read a CGNS mesh (single-zone or multi-zone with 1-to-1 connections) and
// return a fully constructed GlobalMesh with cell/face geometry and boundary
// family tags.
GlobalMesh read_mesh(const std::string& filename);

} // namespace cfd
