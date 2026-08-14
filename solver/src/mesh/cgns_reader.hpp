#pragma once

#include "mesh.hpp"
#include <cgnslib.h>
#include <string>

// Read a 2-D unstructured CGNS mesh.
// Handles unstructured zones with TRI_3/QUAD_4/MIXED elements.
// Extracts nodes, cells, faces, and boundary-family tags.
Mesh read_cgns_mesh(const std::string& filepath);
