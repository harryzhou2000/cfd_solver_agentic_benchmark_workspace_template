#include "vtk_writer.hpp"
#include <fstream>

void write_vtk_field(const std::string& path,
                     const RankMesh& rm,
                     const std::vector<StateVector>& state,
                     const GasModel& gas,
                     int rank_id) {
    (void)rm; (void)state; (void)gas; (void)rank_id;
    std::ofstream f(path);
    f << "<?xml version=\"1.0\"?>\n";
    f << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\">\n";
    f << "<UnstructuredGrid>\n";
    f << "<Piece NumberOfPoints=\"0\" NumberOfCells=\"0\">\n";
    f << "</Piece>\n</UnstructuredGrid>\n</VTKFile>\n";
}
