#pragma once

#include "mesh.hpp"

// Compute cell centroids, volumes, and face centroids/normals for the serial mesh
void compute_geometry(Mesh& mesh);

// Compute cell volumes using divergence theorem over faces
Real compute_cell_volume(const Cell& cell, const std::vector<Face>& faces);

// Classify wall faces based on BC mapping
void classify_boundary_faces(Mesh& mesh, const CaseConfig& cfg);
