#pragma once
#include "types.hpp"
#include <map>

Mesh read_cgns_mesh(const std::string& filename,
                    const std::map<std::string, BCType>& bc_map);

void compute_geometry(Mesh& mesh);

void build_cell_face_adjacency(Mesh& mesh);
