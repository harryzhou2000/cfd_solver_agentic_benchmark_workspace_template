#pragma once
#include "common/types.hpp"
#include "common/gas_model.hpp"
#include "mesh/mesh.hpp"
#include <string>

// Write VTK unstructured grid (.vtu) field file
void write_vtk_field(const std::string& path,
                     const RankMesh& rm,
                     const std::vector<StateVector>& state,
                     const GasModel& gas,
                     int rank_id);
