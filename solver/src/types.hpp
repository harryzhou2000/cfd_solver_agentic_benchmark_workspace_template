#pragma once

#include <cgnslib.h>  // cgsize_t (CGNS index type)

#include <cstddef>
#include <string>
#include <vector>

namespace cfd {

// 2D position / vector in physical space.
struct Vector2 {
    double x = 0.0;
    double y = 0.0;
};

// Conservative state per cell: rho, rho*u, rho*v, total energy.
struct Vector4 {
    double r = 0.0;   // density
    double u = 0.0;   // x-momentum (rho * ux)
    double v = 0.0;   // y-momentum (rho * uy)
    double e = 0.0;   // total energy per unit volume
};

// Primitive variables per cell: rho, ux, uy, pressure.
struct PrimitiveState {
    double rho = 0.0;
    double u = 0.0;
    double v = 0.0;
    double p = 0.0;
};

// Vector4 arithmetic operators (component-wise).
inline Vector4 operator+(const Vector4& a, const Vector4& b) {
    return {a.r + b.r, a.u + b.u, a.v + b.v, a.e + b.e};
}

inline Vector4 operator-(const Vector4& a, const Vector4& b) {
    return {a.r - b.r, a.u - b.u, a.v - b.v, a.e - b.e};
}

inline Vector4 operator*(const Vector4& a, double s) {
    return {a.r * s, a.u * s, a.v * s, a.e * s};
}

inline Vector4 operator*(double s, const Vector4& a) { return a * s; }

inline Vector4& operator+=(Vector4& a, const Vector4& b) {
    a.r += b.r; a.u += b.u; a.v += b.v; a.e += b.e;
    return a;
}

inline Vector4& operator-=(Vector4& a, const Vector4& b) {
    a.r -= b.r; a.u -= b.u; a.v -= b.v; a.e -= b.e;
    return a;
}

inline Vector4& operator*=(Vector4& a, double s) {
    a.r *= s; a.u *= s; a.v *= s; a.e *= s;
    return a;
}

// Calorically perfect gas parameters.
struct GasParams {
    double gamma = 1.4;  // ratio of specific heats
    double R = 1.0;      // specific gas constant
    double Pr = 0.72;    // Prandtl number
};

// Freestream / reference conditions for non-dimensionalization and BCs.
// (Reynolds number lives in the physics section of the case file, not here.)
struct FreestreamParams {
    double mach = 0.0;      // freestream Mach number
    double alpha = 0.0;     // angle of attack [deg]
    double density = 1.0;   // freestream density
    double velocity = 1.0;  // freestream velocity magnitude
    double pressure = 1.0;  // freestream pressure
    double temperature = 1.0;
};

// Run control parameters parsed from the JSON case file (run_control section).
struct RunControlParams {
    std::string type = "steady";          // "steady" | "transient"
    std::string time_integrator = "none"; // "bdf2_or_trapezoidal" for transient
    double time_step = 0.0;
    double final_time = 0.0;
    double rusanov_dissipation_scale = 1.0;
    std::string inner_residual_norm = "none";
    std::string bdf2_history_update = "none";

    int max_iterations = 1000;
    double cfl = 1.0;        // cfl_initial
    double cfl_max = 1.0e6;
    int cfl_ramp_steps = 0;  // pseudo_cfl_ramp_steps
    double residual_target = 1.0e-12;  // absolute L2 target
    double residual_reduction_target = 0.0;  // orders-of-magnitude reduction
    int min_inner_iterations = 1;
    int max_inner_iterations = 1;
    double inner_residual_reduction_target = 0.0;
    int output_interval = 100;
    std::string output_dir = "results";
    int n_stages = 1;  // RK stages / Newton sub-iterations
};

// Output control parameters (outputs section of the case file).
struct OutputControlParams {
    bool write_final_field = true;
    bool write_surface = true;
    int write_forces_every = 1;
    int write_residuals_every = 1;
};

// Reference quantities for force/moment coefficients.
struct ReferenceParams {
    double length = 1.0;
    double area = 1.0;
    double moment_x = 0.25;
    double moment_y = 0.0;
    double reynolds_length = 1.0;
};

// Aggregate mesh description shared by reader, partitioner, solver and
// output modules (avoids passing loose vectors around).
struct Mesh {
    // Coordinates
    std::vector<double> x, y;
    cgsize_t n_nodes = 0;

    // Cell info
    cgsize_t n_cells = 0;
    std::vector<double> cell_vol;       // cell volumes
    std::vector<double> cell_center_x, cell_center_y;

    // Face info (internal faces)
    cgsize_t n_faces = 0;
    std::vector<cgsize_t> face_left, face_right;  // 0-based cell indices
    std::vector<double> face_nx, face_ny;         // face normal components (magnitude = area)
    std::vector<double> face_center_x, face_center_y;
    std::vector<double> face_area;

    // Boundary face metadata
    std::vector<cgsize_t> bface_cell;    // adjacent cell for each boundary face
    std::vector<int> bface_bc_type;      // BC type enum per boundary face
    std::vector<std::string> bface_tag;  // boundary family name per face
    // Boundary face geometry: outward-pointing normals (magnitude = edge
    // length), areas, and face centers. Filled by build_geometry; used by
    // the residual boundary loop and the force/moment integrator.
    std::vector<double> bface_nx, bface_ny;
    std::vector<double> bface_area;
    std::vector<double> bface_center_x, bface_center_y;
    cgsize_t n_boundary_faces = 0;

    // Partition info (filled by partitioner)
    std::vector<cgsize_t> owned_cells_global_ids;  // global cell IDs this rank owns
    cgsize_t n_owned = 0;
    cgsize_t n_ghost = 0;
};

}  // namespace cfd
