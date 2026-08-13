#pragma once
// Core types for the 2D unstructured finite-volume CFD solver.
//
// Phase 1 scope: mesh infrastructure. These types are the shared vocabulary
// used by the case reader, mesh builder, and (later) the solver itself.

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cfd {

// ---------------------------------------------------------------------------
// 2D vector
// ---------------------------------------------------------------------------
struct Vec2 {
  double x = 0.0;
  double y = 0.0;

  Vec2() = default;
  Vec2(double x_, double y_) : x(x_), y(y_) {}

  Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
  Vec2 operator*(double s) const { return {x * s, y * s}; }
  Vec2 operator/(double s) const { return {x / s, y / s}; }

  Vec2& operator+=(const Vec2& o) {
    x += o.x;
    y += o.y;
    return *this;
  }
  Vec2& operator-=(const Vec2& o) {
    x -= o.x;
    y -= o.y;
    return *this;
  }
  Vec2& operator*=(double s) {
    x *= s;
    y *= s;
    return *this;
  }
  Vec2& operator/=(double s) {
    x /= s;
    y /= s;
    return *this;
  }

  double dot(const Vec2& o) const { return x * o.x + y * o.y; }
  // Scalar z-component of the 2D cross product (pseudo-scalar).
  double cross(const Vec2& o) const { return x * o.y - y * o.x; }
  double norm() const { return std::sqrt(x * x + y * y); }
  Vec2 normalized() const {
    const double n = norm();
    return n > 0.0 ? Vec2(x / n, y / n) : Vec2();
  }
};

// ---------------------------------------------------------------------------
// Conservative state: U = {rho, rhou, rhov, rhoE}
// ---------------------------------------------------------------------------
struct ConsState {
  double rho = 0.0;
  double rhou = 0.0;
  double rhov = 0.0;
  double rhoE = 0.0;
};

// ---------------------------------------------------------------------------
// Gas model
// ---------------------------------------------------------------------------
struct GasConfig {
  std::string model = "calorically_perfect";  // gas.model from the case JSON
  double gamma = 1.4;
  double R = 1.0;
  double prandtl = 0.72;
};

// ---------------------------------------------------------------------------
// Freestream conditions
// ---------------------------------------------------------------------------
struct Freestream {
  double mach = 0.0;
  double aoa_rad = 0.0;  // angle of attack in radians
  double rho = 1.0;
  double u_mag = 1.0;  // freestream speed magnitude
  double pressure = 1.0;

  // Velocity components are derived: ux = cos(aoa) * u_mag,
  // uy = sin(aoa) * u_mag (computed, not stored).
  Vec2 velocity() const {
    return {std::cos(aoa_rad) * u_mag, std::sin(aoa_rad) * u_mag};
  }
};

// ---------------------------------------------------------------------------
// Boundary condition types
// ---------------------------------------------------------------------------
enum class BCType {
  Farfield,
  SlipWall,
  NoSlipAdiabaticWall,
  Interface,  // face between two zones of a multi-zone mesh
  Invalid,
};

// Map a BC type string from the case JSON ("farfield", "slip_wall",
// "no_slip_adiabatic_wall") to the BCType enum.
inline BCType bc_type_from_string(const std::string& s) {
  if (s == "farfield") return BCType::Farfield;
  if (s == "slip_wall") return BCType::SlipWall;
  if (s == "no_slip_adiabatic_wall") return BCType::NoSlipAdiabaticWall;
  if (s == "interface") return BCType::Interface;
  return BCType::Invalid;
}

inline const char* bc_type_to_string(BCType t) {
  switch (t) {
    case BCType::Farfield: return "Farfield";
    case BCType::SlipWall: return "SlipWall";
    case BCType::NoSlipAdiabaticWall: return "NoSlipAdiabaticWall";
    case BCType::Interface: return "Interface";
    default: return "Invalid";
  }
}

// ---------------------------------------------------------------------------
// Mesh entities
// ---------------------------------------------------------------------------

// A 2D cell (triangle or quad).
struct Cell {
  // Global index in the serial mesh (0..N_cells_global-1). After METIS
  // partitioning, rank-local position is the vector index, and id remains the
  // global index.
  int id = -1;
  std::array<int, 4> nodes{};  // 0-based node indices, ordered around the cell
  uint8_t n_nodes = 0;         // 3 for tri, 4 for quad
  int zone = 0;                // 0-based CGNS zone index this cell came from
  Vec2 centroid;               // average of the cell nodes
  double volume = 0.0;         // polygon area (shoelace); "volume" per unit depth
  // Sign of the shoelace sum (+1/-1); used as a mesh orientation consistency
  // check. All cells of a consistently oriented mesh share the same sign.
  double shoelace_sign = 1.0;

  bool is_tri() const { return n_nodes == 3; }
  bool is_quad() const { return n_nodes == 4; }
};

// A face shared by two cells (internal) or bounding the domain (boundary).
// normal is the area vector: unit normal scaled by the face length (area).
struct Face {
  int id = -1;
  int left_cell = -1;   // cell on the left of the oriented normal
  int right_cell = -1;  // -1 for boundary faces
  std::array<int, 2> nodes;  // 0-based node indices
  Vec2 centroid;
  Vec2 normal;  // area vector; for internal faces oriented left -> right,
                // for boundary faces oriented outward
  double area = 0.0;  // face length (2D "area")
};

// Boundary face with its boundary-condition tag. Geometry (nodes, centroid,
// normal, area) is NOT duplicated here: use Mesh::faces[face_id] instead.
struct BoundaryFace {
  int face_id = -1;  // index into Mesh::faces
  int cell = -1;     // adjacent interior cell
  BCType bc_type = BCType::Invalid;
  std::string family;  // family name from the mesh (BC section name)
};

struct Mesh {
  std::string file_path;
  std::string zone_name;  // primary (first) zone name
  std::vector<std::string> zone_names;  // all zone names, in zone order
  int n_nodes = 0;
  std::vector<Vec2> nodes;
  std::vector<Cell> cells;
  std::vector<Face> faces;
  std::vector<BoundaryFace> boundary_faces;
  // Faces whose left/right cells belong to different zones
  // (BCType::Interface).
  std::vector<int> interface_face_ids;
  int n_tri = 0;
  int n_quad = 0;
  std::vector<std::string> boundary_families;  // unique family names, sorted

  // CSR adjacency, built during mesh construction:
  //   cell_faces_offsets[i]..cell_faces_offsets[i+1]  -> cell_faces_data
  //     face indices touching cell i (needed for gradient reconstruction)
  //   cell_neighbors_offsets[i]..cell_neighbors_offsets[i+1] ->
  //     cell_neighbors_data: neighbor cell indices of cell i (needed for the
  //     METIS adjacency graph)
  std::vector<int> cell_faces_offsets;
  std::vector<int> cell_faces_data;
  std::vector<int> cell_neighbors_offsets;
  std::vector<int> cell_neighbors_data;
};

// ---------------------------------------------------------------------------
// Case configuration (all fields parsed from the case JSON)
// ---------------------------------------------------------------------------
struct RunConfig {
  int schema_version = 1;
  std::string case_id;
  std::string description;

  // mesh
  std::string mesh_file;  // absolute or case-relative path, resolved
  std::string mesh_format = "CGNS";
  int dimension = 2;

  // physics
  std::string equations;
  std::string mode;  // "inviscid" | "laminar" | ...
  double reynolds = 0.0;
  std::string viscosity_model;

  // gas
  GasConfig gas;

  // freestream
  Freestream freestream;

  // reference
  double ref_length = 1.0;
  double ref_area = 1.0;
  Vec2 moment_center;
  double reynolds_length = 1.0;

  // boundary conditions: mesh family name -> BC type string
  std::map<std::string, std::string> boundary_conditions;

  // numerics_required
  int spatial_order = 2;
  std::string inviscid_flux;
  std::string viscous_flux;
  std::string main_time_method;
  std::string implicit_solver;
  int transient_order = 0;  // order for transient time integration

  // run_control
  std::string run_type = "steady";
  int max_steps = 0;
  double residual_reduction_target = 0.0;
  double cfl_initial = 1.0;
  double cfl_max = 100.0;
  int pseudo_cfl_ramp_steps = 0;
  int min_inner_iterations = 0;
  int max_inner_iterations = 0;
  double inner_residual_reduction_target = 0.0;

  // run_control (transient)
  std::string time_integrator;     // e.g. "bdf2_or_trapezoidal"
  double time_step = 0.0;          // physical time step
  double final_time = 0.0;         // physical end time (0 = not transient)
  std::string inner_residual_norm; // e.g. "total_spatial_plus_physical_time"
  std::string bdf2_history_update; // e.g. "after_inner_convergence"
  double rusanov_dissipation_scale = 1.0;

  // outputs
  bool write_final_field = false;
  bool write_surface = false;
  int write_forces_every = 0;
  int write_residuals_every = 0;
  double write_field_every_time = 0.0;  // wall-clock interval, 0 = disabled
  std::string wake_visualization;       // e.g. "vorticity_or_velocity"
  std::array<double, 2> recommended_vorticity_clip_range = {-1.0, 1.0};
};

}  // namespace cfd
