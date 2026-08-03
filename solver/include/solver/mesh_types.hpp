#pragma once
#include <vector>
#include <array>
#include <string>
#include <Eigen/Dense>

namespace solver {

using Scalar = double;
using Vec2 = Eigen::Matrix<Scalar, 2, 1>;
using Vec4 = Eigen::Matrix<Scalar, 4, 1>;  // conservative state [rho, rhou, rhov, rhoE]
using Mat2 = Eigen::Matrix<Scalar, 2, 2>;
using Mat4x2 = Eigen::Matrix<Scalar, 4, 2>;

// 2D cell definition
struct Cell {
    std::vector<int> face_ids;  // indices into face array
    Vec2 centroid;
    Scalar volume;
};

// 2D face definition (interior or boundary)
struct Face {
    std::array<int, 2> cell_ids;  // left=-1 or right=-1 for boundary faces
    std::vector<Vec2> nodes;      // face vertices (2 for 2D edge)
    Vec2 centroid;
    Vec2 normal;    // unit normal pointing from left to right
    Scalar area;    // face length
    int bc_type;    // -1 for interior, >= 0 index into boundary_conditions[]
};

// Boundary condition family
struct BCFamily {
    std::string name;       // CGNS family name
    std::string bc_type;    // "farfield", "slip_wall", "no_slip_adiabatic_wall"
    std::vector<int> face_ids;  // boundary face indices
};

// Complete mesh (serial, full mesh for preprocessing)
struct Mesh {
    std::vector<Cell> cells;
    std::vector<Face> faces;
    std::vector<BCFamily> bc_families;
    std::vector<Vec2> nodes;     // all mesh nodes

    // Total counts
    int num_cells = 0;
    int num_faces = 0;
    int num_bnd_faces = 0;

    // Boundary face indices for quick access
    std::vector<int> boundary_face_ids;
    std::vector<int> interior_face_ids;
};

// Cell adjacency representation for METIS
struct CellGraph {
    std::vector<int> xadj;    // CSR offset array, size num_cells+1
    std::vector<int> adjncy;  // CSR adjacency array
};

} // namespace solver
