#pragma once
#include "MeshData.hpp"
#include "Config.hpp"
#include <string>

// Read a CGNS file and return a GlobalMesh (pre-partitioned)
// Handles single-zone (NACA) and multi-zone (Cylinder) meshes
// Applies BC mapping from the case config
GlobalMesh readCgnsMesh(const std::string& filename,
                         const std::map<std::string, BcType>& bc_map);
