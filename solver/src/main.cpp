#include "solver/case_config.hpp"
#include "solver/mesh_reader.hpp"
#include "solver/geometry.hpp"
#include "solver/adjacency.hpp"
#include <iostream>
#include <fmt/core.h>

int main(int argc, char* argv[]) {
    // For now, hardcode a test — later will parse CLI
    if (argc < 2) {
        fmt::print(stderr, "Usage: {} <case.json>\n", argv[0]);
        return 1;
    }
    
    auto config = solver::parse_case_config(argv[1]);
    fmt::print("Case: {}\n", config.case_id);
    fmt::print("Mesh: {}\n", config.mesh.file);
    
    auto mesh = solver::read_cgns_mesh(config.mesh.file, config);
    fmt::print("Cells: {}, Faces: {}, Bnd faces: {}\n", 
               mesh.num_cells, mesh.num_faces, mesh.num_bnd_faces);
    
    solver::compute_geometry(mesh);
    fmt::print("Geometry computed. Cell 0 centroid: ({}, {})\n",
               mesh.cells[0].centroid.x(), mesh.cells[0].centroid.y());
    
    auto graph = solver::build_cell_graph(mesh);
    fmt::print("Cell graph: {} edges\n", graph.adjncy.size());
    
    return 0;
}
