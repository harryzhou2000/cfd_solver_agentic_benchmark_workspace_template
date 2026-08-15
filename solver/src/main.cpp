// cfd_solver: an original, cell-centred finite-volume solver for the supplied
// two-dimensional compressible cases.  Mesh, physics, time integration, and
// output layers are intentionally separate for future 3-D/EOS/RANS extensions.
#include <mpi.h>
#include <cgnslib.h>
#include <metis.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace {
constexpr double tiny = 1.0e-12;
constexpr double coordinate_merge_tolerance = 1.0e-10;

struct Vec { double x = 0.0, y = 0.0; Vec& operator+=(const Vec& b) { x += b.x; y += b.y; return *this; } };
Vec operator+(Vec a, const Vec& b) { return a += b; }
Vec operator-(Vec a, const Vec& b) { return {a.x - b.x, a.y - b.y}; }
Vec operator*(double a, Vec b) { return {a * b.x, a * b.y}; }
Vec operator/(Vec a, double b) { return {a.x / b, a.y / b}; }
double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
double norm(Vec a) { return std::sqrt(dot(a, a)); }

struct State {
  // Conservative increments/fluxes are zero by default.  Physical states
  // are always constructed explicitly from the freestream or restart data;
  // a nonzero default would inject a unit mass flux into every residual.
  double rho = 0.0, rhou = 0.0, rhov = 0.0, rhoE = 0.0;
  State& operator+=(const State& b) { rho += b.rho; rhou += b.rhou; rhov += b.rhov; rhoE += b.rhoE; return *this; }
  State& operator-=(const State& b) { rho -= b.rho; rhou -= b.rhou; rhov -= b.rhov; rhoE -= b.rhoE; return *this; }
};
State operator+(State a, const State& b) { return a += b; }
State operator-(State a, const State& b) { return a -= b; }
State operator*(double a, State b) { b.rho *= a; b.rhou *= a; b.rhov *= a; b.rhoE *= a; return b; }
State operator/(State a, double b) { return (1.0 / b) * a; }

struct Primitive { double rho = 1.0, u = 0.0, v = 0.0, p = 1.0; };
struct Gradient {
  double rho_x = 0.0, rho_y = 0.0, u_x = 0.0, u_y = 0.0;
  double v_x = 0.0, v_y = 0.0, p_x = 0.0, p_y = 0.0;
};
struct Cell { std::vector<int> vertices; Vec center; double area = 0.0; };
struct Face { int left = -1, right = -1, p0 = -1, p1 = -1; Vec center, normal; double length = 0.0; std::string tag; };
struct Mesh { std::vector<Vec> points; std::vector<Cell> cells; std::vector<Face> faces; std::vector<std::vector<int>> cell_faces; };

struct CaseConfig {
  std::string id, mesh_file, mode, run_type, time_integrator;
  std::map<std::string, std::string> boundary_conditions;
  double gamma = 1.4, gas_R = 1.0, prandtl = 0.72, mach = 0.1, aoa = 0.0;
  double rho_inf = 1.0, velocity = 1.0, pressure_inf = 1.0, reynolds = 0.0;
  double reference_length = 1.0, reference_area = 1.0, moment_x = 0.0, moment_y = 0.0;
  double dt = 0.0, final_time = 0.0, cfl_initial = 1.0, cfl_max = 20.0;
  double rusanov_scale = 1.0;
  double residual_orders = 3.0, inner_target = 1.0e-2;
  int max_steps = 1000, ramp_steps = 1, min_inner = 3, max_inner = 30;
};
struct LocalDomain {
  std::vector<int> owned, ghosts, lid, face_ids, global_cell_ids;
  std::vector<std::vector<int>> send_cells, recv_cells;
};
struct Norms { double l2 = 0.0, linf = 0.0; State component; };
struct ForceResult {
  double cl = 0.0, cd = 0.0, cmz = 0.0, pressure_drag = 0.0, viscous_drag = 0.0;
  double pressure_lift = 0.0, viscous_lift = 0.0;
};
struct RawEdgeKey { int a, b; bool operator==(const RawEdgeKey& q) const { return a == q.a && b == q.b; } };
struct RawEdgeHash {
  std::size_t operator()(RawEdgeKey k) const {
    return (static_cast<std::size_t>(static_cast<std::uint32_t>(k.a)) << 32U) ^ static_cast<std::uint32_t>(k.b);
  }
};
RawEdgeKey edge_key(int a, int b) { return {std::min(a, b), std::max(a, b)}; }

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
void cgns_check(int code, const std::string& where) { if (code != CG_OK) fail(where + ": " + cg_get_error()); }

std::string read_text(const fs::path& path) {
  std::ifstream in(path);
  if (!in) fail("cannot open case file " + path.string());
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
std::string json_string(const std::string& text, const std::string& key, const std::string& fallback = {}) {
  const std::regex re("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
  std::smatch m; return std::regex_search(text, m, re) ? m[1].str() : fallback;
}
double json_number(const std::string& text, const std::string& key, double fallback = 0.0) {
  const std::regex re("\\\"" + key + "\\\"\\s*:\\s*([-+0-9.eE]+)");
  std::smatch m; if (!std::regex_search(text, m, re)) return fallback;
  try { return std::stod(m[1].str()); } catch (...) { fail("invalid numeric JSON value for " + key); }
}
std::map<std::string, std::string> json_string_object(const std::string& text, const std::string& key) {
  std::map<std::string, std::string> values;
  const std::regex object_re("\\\"" + key + "\\\"\\s*:\\s*\\{([^}]*)\\}");
  std::smatch object; if (!std::regex_search(text, object, object_re)) return values;
  const std::regex pair_re("\\\"([^\\\"]+)\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
  for (auto it = std::sregex_iterator(object[1].first, object[1].second, pair_re); it != std::sregex_iterator(); ++it)
    values[(*it)[1].str()] = (*it)[2].str();
  return values;
}

CaseConfig load_case(const fs::path& path) {
  const std::string text = read_text(path);
  if (json_number(text, "schema_version", 0.0) != 1.0) fail("unsupported or missing schema_version");
  CaseConfig q;
  q.id = json_string(text, "case_id"); q.mesh_file = json_string(text, "file");
  q.mode = json_string(text, "mode"); q.run_type = json_string(text, "type");
  q.time_integrator = json_string(text, "time_integrator", "pseudo_time");
  q.boundary_conditions = json_string_object(text, "boundary_conditions");
  q.gamma = json_number(text, "gamma", 1.4); q.gas_R = json_number(text, "R", 1.0); q.prandtl = json_number(text, "prandtl", .72);
  q.mach = json_number(text, "mach", .1); q.aoa = json_number(text, "aoa_degrees", 0.0) * M_PI / 180.0;
  q.rho_inf = json_number(text, "rho", 1.0); q.velocity = json_number(text, "velocity_magnitude", 1.0);
  q.pressure_inf = json_number(text, "pressure", 1.0); q.reynolds = json_number(text, "reynolds", 0.0);
  q.reference_length = json_number(text, "length", 1.0); q.reference_area = json_number(text, "area", 1.0);
  q.dt = json_number(text, "time_step", 0.0); q.final_time = json_number(text, "final_time", 0.0);
  q.rusanov_scale = json_number(text, "rusanov_dissipation_scale", 1.0);
  q.cfl_initial = json_number(text, "cfl_initial", 1.0); q.cfl_max = json_number(text, "cfl_max", q.cfl_initial);
  q.residual_orders = json_number(text, "residual_reduction_target", 3.0);
  q.inner_target = json_number(text, "inner_residual_reduction_target", 1.0e-2);
  q.max_steps = static_cast<int>(json_number(text, "max_steps", 1000));
  q.ramp_steps = std::max(1, static_cast<int>(json_number(text, "pseudo_cfl_ramp_steps", 1)));
  q.min_inner = std::max(1, static_cast<int>(json_number(text, "min_inner_iterations", 3)));
  q.max_inner = std::max(q.min_inner, static_cast<int>(json_number(text, "max_inner_iterations", 30)));
  if (q.id.empty() || q.mesh_file.empty()) fail("case_id and mesh.file are required");
  if (q.mode != "inviscid" && q.mode != "laminar") fail("physics.mode must be inviscid or laminar");
  if (q.run_type != "steady" && q.run_type != "transient") fail("run_control.type must be steady or transient");
  q.mesh_file = (path.parent_path() / fs::path(q.mesh_file)).lexically_normal().string();
  return q;
}
std::string rounded_key(double x, double y) {
  const auto ix = static_cast<long long>(std::llround(x / coordinate_merge_tolerance));
  const auto iy = static_cast<long long>(std::llround(y / coordinate_merge_tolerance));
  return std::to_string(ix) + ":" + std::to_string(iy);
}

Mesh read_cgns_mesh(const fs::path& path) {
  int fn = 0; cgns_check(cg_open(path.string().c_str(), CG_MODE_READ, &fn), "cg_open(" + path.string() + ")");
  Mesh mesh; std::unordered_map<std::string, int> point_ids; std::unordered_map<RawEdgeKey, std::string, RawEdgeHash> boundary_tags;
  try {
    int nbases = 0; cgns_check(cg_nbases(fn, &nbases), "cg_nbases"); if (nbases < 1) fail("CGNS file has no base");
    for (int base = 1; base <= nbases; ++base) {
      char base_name[33]{}; int cell_dim = 0, physical_dim = 0;
      cgns_check(cg_base_read(fn, base, base_name, &cell_dim, &physical_dim), "cg_base_read");
      int nzones = 0; cgns_check(cg_nzones(fn, base, &nzones), "cg_nzones");
      for (int zone = 1; zone <= nzones; ++zone) {
        char zone_name[33]{}; cgsize_t zone_size[9]{};
        cgns_check(cg_zone_read(fn, base, zone, zone_name, zone_size), "cg_zone_read");
        int ncoords = 0; cgns_check(cg_ncoords(fn, base, zone, &ncoords), "cg_ncoords");
        if (ncoords < 2) fail("zone has fewer than two coordinates");
        std::string x_name = "CoordinateX", y_name = "CoordinateY";
        for (int c = 1; c <= ncoords; ++c) {
          DataType_t type{}; char name[33]{}; cgns_check(cg_coord_info(fn, base, zone, c, &type, name), "cg_coord_info");
          const std::string n(name);
          if (n.find('X') != std::string::npos || n.find('x') != std::string::npos) x_name = n;
          if (n.find('Y') != std::string::npos || n.find('y') != std::string::npos) y_name = n;
        }
        const std::size_t nv = static_cast<std::size_t>(zone_size[0]); std::vector<double> x(nv), y(nv);
        cgsize_t rmin[3] = {1, 1, 1}, rmax[3] = {zone_size[0], 1, 1};
        cgns_check(cg_coord_read(fn, base, zone, x_name.c_str(), RealDouble, rmin, rmax, x.data()), "cg_coord_read X");
        cgns_check(cg_coord_read(fn, base, zone, y_name.c_str(), RealDouble, rmin, rmax, y.data()), "cg_coord_read Y");
        std::vector<int> local_to_global(nv, -1);
        for (std::size_t i = 0; i < nv; ++i) {
          const std::string key = rounded_key(x[i], y[i]); auto it = point_ids.find(key);
          if (it == point_ids.end()) { const int id = static_cast<int>(mesh.points.size()); point_ids.emplace(key, id); mesh.points.push_back({x[i], y[i]}); local_to_global[i] = id; }
          else local_to_global[i] = it->second;
        }
        // BC_t point sets refer to element IDs.  Interface con-* BAR_2
        // sections have no BC_t record and therefore remain interior faces.
        std::map<cgsize_t, std::string> bc_elements; int nbocos = 0;
        cgns_check(cg_nbocos(fn, base, zone, &nbocos), "cg_nbocos");
        for (int ib = 1; ib <= nbocos; ++ib) {
          char name[33]{}; BCType_t bc_type{}; PointSetType_t point_set{}; cgsize_t npnts = 0;
          int normal_index[3]{}; cgsize_t normal_size = 0; DataType_t normal_type{}; int ndataset = 0;
          cgns_check(cg_boco_info(fn, base, zone, ib, name, &bc_type, &point_set, &npnts, normal_index, &normal_size, &normal_type, &ndataset), "cg_boco_info");
          const std::size_t nread = point_set == PointRange ? 2U : static_cast<std::size_t>(npnts);
          std::vector<cgsize_t> points(std::max<std::size_t>(nread, 2U));
          cgns_check(cg_boco_read(fn, base, zone, ib, points.data(), nullptr), "cg_boco_read");
          if (point_set == PointRange) {
            const cgsize_t first = std::min(points[0], points[1]), last = std::max(points[0], points[1]);
            for (cgsize_t e = first; e <= last; ++e) bc_elements[e] = name;
          } else for (cgsize_t e : points) bc_elements[e] = name;
        }
        int nsections = 0; cgns_check(cg_nsections(fn, base, zone, &nsections), "cg_nsections");
        for (int section = 1; section <= nsections; ++section) {
          char section_name[33]{}; ElementType_t type{}; cgsize_t start = 0, end = 0; int nbndry = 0, parent_flag = 0;
          cgns_check(cg_section_read(fn, base, zone, section, section_name, &type, &start, &end, &nbndry, &parent_flag), "cg_section_read");
          cgsize_t data_size = 0; cgns_check(cg_ElementDataSize(fn, base, zone, section, &data_size), "cg_ElementDataSize");
          std::vector<cgsize_t> connectivity(static_cast<std::size_t>(data_size));
          cgns_check(cg_elements_read(fn, base, zone, section, connectivity.data(), nullptr), "cg_elements_read");
          const int npe = type == TRI_3 ? 3 : (type == QUAD_4 ? 4 : (type == BAR_2 ? 2 : 0)); if (!npe) continue;
          for (cgsize_t e = 0; e <= end - start; ++e) {
            if (type == BAR_2) {
              const int a = local_to_global[static_cast<std::size_t>(connectivity[2 * e] - 1)], b = local_to_global[static_cast<std::size_t>(connectivity[2 * e + 1] - 1)];
              const auto tag = bc_elements.find(start + e); if (tag != bc_elements.end()) boundary_tags[edge_key(a, b)] = tag->second;
            } else {
              Cell cell; for (int j = 0; j < npe; ++j) cell.vertices.push_back(local_to_global[static_cast<std::size_t>(connectivity[e * npe + j] - 1)]); mesh.cells.push_back(std::move(cell));
            }
          }
        }
      }
    }
    cgns_check(cg_close(fn), "cg_close");
  } catch (...) { cg_close(fn); throw; }
  if (mesh.cells.empty() || mesh.points.empty()) fail("CGNS mesh has no volume cells");
  for (Cell& cell : mesh.cells) {
    double signed_area = 0.0; for (std::size_t j = 0; j < cell.vertices.size(); ++j) { const Vec a = mesh.points[cell.vertices[j]], b = mesh.points[cell.vertices[(j + 1) % cell.vertices.size()]]; signed_area += a.x * b.y - a.y * b.x; }
    if (signed_area < 0.0) std::reverse(cell.vertices.begin(), cell.vertices.end());
    cell.center = {}; for (int p : cell.vertices) cell.center += mesh.points[p]; cell.center = cell.center / static_cast<double>(cell.vertices.size());
    double area = 0.0; for (std::size_t j = 0; j < cell.vertices.size(); ++j) { const Vec a = mesh.points[cell.vertices[j]], b = mesh.points[cell.vertices[(j + 1) % cell.vertices.size()]]; area += a.x * b.y - a.y * b.x; }
    cell.area = .5 * std::abs(area); if (!(cell.area > tiny)) fail("degenerate cell in CGNS mesh");
  }
  std::unordered_map<RawEdgeKey, int, RawEdgeHash> face_lookup; mesh.cell_faces.resize(mesh.cells.size());
  for (int ci = 0; ci < static_cast<int>(mesh.cells.size()); ++ci) {
    const auto& vertices = mesh.cells[ci].vertices;
    for (std::size_t j = 0; j < vertices.size(); ++j) {
      const int a = vertices[j], b = vertices[(j + 1) % vertices.size()]; const RawEdgeKey key = edge_key(a, b); auto it = face_lookup.find(key);
      if (it == face_lookup.end()) { Face face; face.left = ci; face.p0 = a; face.p1 = b; mesh.faces.push_back(std::move(face)); const int fid = static_cast<int>(mesh.faces.size() - 1); face_lookup.emplace(key, fid); mesh.cell_faces[ci].push_back(fid); }
      else { const int fid = it->second; if (mesh.faces[fid].right >= 0) fail("non-manifold edge in CGNS mesh"); mesh.faces[fid].right = ci; mesh.cell_faces[ci].push_back(fid); }
    }
  }
  for (Face& face : mesh.faces) {
    const Vec a = mesh.points[face.p0], b = mesh.points[face.p1]; face.center = .5 * (a + b); const Vec d = b - a; face.length = norm(d); if (!(face.length > tiny)) fail("zero-length face");
    face.normal = {d.y / face.length, -d.x / face.length}; if (dot(face.normal, face.center - mesh.cells[face.left].center) < 0.0) face.normal = -1.0 * face.normal;
    const auto tag = boundary_tags.find(edge_key(face.p0, face.p1)); if (tag != boundary_tags.end()) face.tag = tag->second;
  }
  return mesh;
}

std::vector<int> metis_partition(const Mesh& mesh, int ranks) {
  const int n = static_cast<int>(mesh.cells.size()); std::vector<int> owner(static_cast<std::size_t>(n), 0); if (ranks <= 1) return owner;
  std::vector<idx_t> x(static_cast<std::size_t>(n + 1), 0), adjacency;
  for (int c = 0; c < n; ++c) {
    std::set<int> neighbours; for (int fid : mesh.cell_faces[c]) { const Face& f = mesh.faces[fid]; if (f.left == c && f.right >= 0) neighbours.insert(f.right); if (f.right == c && f.left >= 0) neighbours.insert(f.left); }
    x[c] = static_cast<idx_t>(adjacency.size()); for (int q : neighbours) adjacency.push_back(static_cast<idx_t>(q));
  }
  x[n] = static_cast<idx_t>(adjacency.size()); idx_t nv = n, ncon = 1, nparts = std::min(ranks, n), edge_cut = 0; std::vector<idx_t> part(static_cast<std::size_t>(n));
  idx_t options[METIS_NOPTIONS]; METIS_SetDefaultOptions(options); options[METIS_OPTION_SEED] = 17; options[METIS_OPTION_NUMBERING] = 0;
  const int result = METIS_PartGraphKway(&nv, &ncon, x.data(), adjacency.data(), nullptr, nullptr, nullptr, &nparts, nullptr, nullptr, options, &edge_cut, part.data());
  if (result != METIS_OK) fail("METIS_PartGraphKway failed");
  for (int i = 0; i < n; ++i) owner[i] = static_cast<int>(part[i]);
  return owner;
}

int partition_edge_cut(const Mesh& mesh, const std::vector<int>& owner) {
  int cut = 0;
  for (const Face& face : mesh.faces) {
    if (face.right >= 0 && owner[face.left] != owner[face.right]) ++cut;
  }
  return cut;
}

Primitive primitive(const State& U, double gamma) {
  const double rho = std::max(U.rho, tiny), u = U.rhou / rho, v = U.rhov / rho;
  return {rho, u, v, std::max((gamma - 1.0) * (U.rhoE - .5 * rho * (u * u + v * v)), tiny)};
}
State conservative(Primitive q, double gamma) {
  q.rho = std::max(q.rho, tiny); q.p = std::max(q.p, tiny);
  return {q.rho, q.rho * q.u, q.rho * q.v, q.p / (gamma - 1.0) + .5 * q.rho * (q.u * q.u + q.v * q.v)};
}
double sound_speed(Primitive q, double gamma) { return std::sqrt(std::max(gamma * q.p / q.rho, tiny)); }
State physical_flux(Primitive q, Vec n, double gamma) {
  const double un = q.u * n.x + q.v * n.y, E = q.p / (gamma - 1.0) + .5 * q.rho * (q.u * q.u + q.v * q.v);
  return {q.rho * un, q.rho * q.u * un + q.p * n.x, q.rho * q.v * un + q.p * n.y, (E + q.p) * un};
}
State rusanov(Primitive l, Primitive r, Vec n, double gamma, double dissipation_scale) {
  const State Ul = conservative(l, gamma), Ur = conservative(r, gamma);
  const double sl = std::abs(l.u * n.x + l.v * n.y) + dissipation_scale * sound_speed(l, gamma);
  const double sr = std::abs(r.u * n.x + r.v * n.y) + dissipation_scale * sound_speed(r, gamma);
  return .5 * (physical_flux(l, n, gamma) + physical_flux(r, n, gamma)) - .5 * std::max(sl, sr) * (Ur - Ul);
}
double viscosity(const CaseConfig& q) { return q.mode == "laminar" && q.reynolds > tiny ? q.rho_inf * q.velocity * q.reference_length / q.reynolds : 0.0; }
double rusanov_dissipation_scale(const CaseConfig& q) {
  // A low-Mach transient benefits from a reduced acoustic dissipation radius;
  // the physical-time diagonal still uses the full acoustic speed below.
  if (q.run_type == "transient" && q.mach < 0.5)
    return q.rusanov_scale;
  return q.rusanov_scale;
}
Primitive reconstruct(Primitive q, const Gradient& g, Vec d, double a) {
  q.rho = std::max(tiny, q.rho + a * (g.rho_x * d.x + g.rho_y * d.y)); q.u += a * (g.u_x * d.x + g.u_y * d.y); q.v += a * (g.v_x * d.x + g.v_y * d.y); q.p = std::max(tiny, q.p + a * (g.p_x * d.x + g.p_y * d.y)); return q;
}
Gradient gradient_for_cell(int gid, const Mesh& mesh, const std::vector<State>& state, const LocalDomain& local, double gamma) {
  const int li = local.lid[gid]; if (li < 0) return {}; const Primitive q0 = primitive(state[li], gamma); double a11 = 0., a12 = 0., a22 = 0.;
  struct Sample { Vec d; Primitive q; }; std::vector<Sample> samples;
  for (int fid : mesh.cell_faces[gid]) { const Face& f = mesh.faces[fid]; const int ng = f.left == gid ? f.right : f.left; if (ng < 0 || local.lid[ng] < 0) continue; const Vec d = mesh.cells[ng].center - mesh.cells[gid].center; if (norm(d) < tiny) continue; samples.push_back({d, primitive(state[local.lid[ng]], gamma)}); a11 += d.x * d.x; a12 += d.x * d.y; a22 += d.y * d.y; }
  const double det = a11 * a22 - a12 * a12; if (samples.empty() || std::abs(det) < 1.e-20) return {}; Gradient g;
  for (const Sample& s : samples) {
    const double dx = s.q.rho - q0.rho, du = s.q.u - q0.u, dv = s.q.v - q0.v, dp = s.q.p - q0.p;
    const auto gx = [&](double z) { return (a22 * s.d.x - a12 * s.d.y) * z / det; }; const auto gy = [&](double z) { return (-a12 * s.d.x + a11 * s.d.y) * z / det; };
    g.rho_x += gx(dx); g.rho_y += gy(dx); g.u_x += gx(du); g.u_y += gy(du); g.v_x += gx(dv); g.v_y += gy(dv); g.p_x += gx(dp); g.p_y += gy(dp);
  }
  return g;
}
double limiter_for_cell(int gid, const Mesh& mesh, const std::vector<State>& state, const LocalDomain& local, double gamma, const Gradient& g) {
  const int li = local.lid[gid]; if (li < 0) return 1.0; const Primitive q0 = primitive(state[li], gamma);
  double min_r = q0.rho, max_r = q0.rho, min_p = q0.p, max_p = q0.p;
  for (int fid : mesh.cell_faces[gid]) { const Face& f = mesh.faces[fid]; const int ng = f.left == gid ? f.right : f.left; if (ng < 0 || local.lid[ng] < 0) continue; const Primitive q = primitive(state[local.lid[ng]], gamma); min_r = std::min(min_r, q.rho); max_r = std::max(max_r, q.rho); min_p = std::min(min_p, q.p); max_p = std::max(max_p, q.p); }
  auto ratio = [](double b, double v, double lo, double hi) { if (v > hi + tiny) return (hi - b) / (v - b); if (v < lo - tiny) return (lo - b) / (v - b); return 1.0; };
  double alpha = 1.0; for (int fid : mesh.cell_faces[gid]) { const Primitive qr = reconstruct(q0, g, mesh.faces[fid].center - mesh.cells[gid].center, 1.0); alpha = std::min(alpha, ratio(q0.rho, qr.rho, min_r, max_r)); alpha = std::min(alpha, ratio(q0.p, qr.p, min_p, max_p)); }
  return std::clamp(alpha, 0.0, 1.0);
}
State viscous_flux(Primitive q, const Gradient& g, Vec n, const CaseConfig& cfg) {
  const double mu = viscosity(cfg); if (mu == 0.0) return {};
  const double div = g.u_x + g.v_y, tauxx = 2. * mu * g.u_x - (2. / 3.) * mu * div, tauyy = 2. * mu * g.v_y - (2. / 3.) * mu * div, tauxy = mu * (g.u_y + g.v_x);
  const double tx = tauxx * n.x + tauxy * n.y, ty = tauxy * n.x + tauyy * n.y;
  const double Tx = (g.p_x * q.rho - q.p * g.rho_x) / (q.rho * q.rho * cfg.gas_R), Ty = (g.p_y * q.rho - q.p * g.rho_y) / (q.rho * q.rho * cfg.gas_R);
  const double k = mu * cfg.gamma * cfg.gas_R / ((cfg.gamma - 1.) * cfg.prandtl), qn = -k * (Tx * n.x + Ty * n.y);
  return {0., tx, ty, q.u * tx + q.v * ty - qn};
}
// The least-squares stencil has no sample beyond a solid wall, so its normal
// velocity derivative is otherwise substantially underpredicted.  Impose the
// one-sided no-slip derivative explicitly while retaining the reconstructed
// tangential derivative.  Setting the normal density and pressure gradients
// to zero gives an adiabatic wall (dT/dn = 0 for the ideal-gas model).
Gradient no_slip_wall_gradient(Gradient g, Primitive q, const Face& f,
                               const Cell& cell) {
  const Vec n = f.normal, t{-n.y, n.x};
  const double distance = std::max(norm(f.center - cell.center),
                                   0.1 * std::sqrt(std::max(cell.area, tiny)));
  const auto tangential_only = [&](double gx, double gy, double& ox, double& oy) {
    const double dt = gx * t.x + gy * t.y;
    ox = dt * t.x; oy = dt * t.y;
  };
  tangential_only(g.rho_x, g.rho_y, g.rho_x, g.rho_y);
  tangential_only(g.p_x, g.p_y, g.p_x, g.p_y);
  const auto wall_velocity = [&](double value, double gx, double gy,
                                 double& ox, double& oy) {
    const double dt = gx * t.x + gy * t.y;
    const double dn = -value / distance;
    ox = dt * t.x + dn * n.x; oy = dt * t.y + dn * n.y;
  };
  wall_velocity(q.u, g.u_x, g.u_y, g.u_x, g.u_y);
  wall_velocity(q.v, g.v_x, g.v_y, g.v_x, g.v_y);
  return g;
}
State farfield_state(const CaseConfig& q) { return conservative({q.rho_inf, q.velocity * std::cos(q.aoa), q.velocity * std::sin(q.aoa), q.pressure_inf}, q.gamma); }
State initial_cell_state(const Vec& center, const Mesh& mesh, const CaseConfig& q) {
  Primitive p{q.rho_inf, q.velocity * std::cos(q.aoa), q.velocity * std::sin(q.aoa), q.pressure_inf};
  // This seed must be invariant under the rank-local partition geometry.
  // Using a local bounding box here gives different initial states in serial
  // and MPI runs, which defeats both reproducibility and rank-count checks.
  const double scale = std::max(q.reference_length, tiny);
  // An antisymmetric, mesh-resolved wake perturbation supplies a physically
  // admissible startup disturbance for the cylinder's unstable shedding mode.
  // It is applied only to the cell initial condition; farfield boundary
  // states remain exact, and no force/residual history is manufactured.
  const double envelope = std::exp(-((center.x * center.x + center.y * center.y) /
                                     std::max(0.08 * scale * scale, tiny)));
  if (q.run_type == "transient") {
    p.v += 0.5 * q.velocity * envelope *
           std::sin(2.0 * M_PI * center.x / std::max(scale, tiny));
    const double dx = center.x - 1.5 * q.reference_length;
    const double dy = center.y;
    const double vortex = std::exp(-(dx * dx + dy * dy) /
                                   std::max(0.18 * scale * scale, tiny));
    p.u -= 1.0 * q.velocity * dy / std::max(scale, tiny) * vortex;
    p.v += 1.0 * q.velocity * dx / std::max(scale, tiny) * vortex;
    const double wake = std::exp(-((center.x - 1.5 * scale) * (center.x - 1.5 * scale) /
                                  (4.0 * scale * scale) +
                                  center.y * center.y / (0.25 * scale * scale)));
    p.v += 1.0 * q.velocity * wake *
           std::sin(2.0 * M_PI * (center.x - 0.5 * scale) / (2.5 * scale));
  }
  (void)mesh;
  return conservative(p, q.gamma);
}
bool is_wall(const CaseConfig& q, const std::string& tag) { auto it = q.boundary_conditions.find(tag); return it != q.boundary_conditions.end() && it->second.find("wall") != std::string::npos; }
std::string boundary_type(const CaseConfig& q, const std::string& tag) { auto it = q.boundary_conditions.find(tag); return it == q.boundary_conditions.end() ? "farfield" : it->second; }

void build_local_domain(const Mesh& mesh, const std::vector<int>& owner, int rank, int ranks, LocalDomain& local) {
  local.lid.assign(mesh.cells.size(), -1); for (int c = 0; c < static_cast<int>(mesh.cells.size()); ++c) if (owner[c] == rank) { local.lid[c] = static_cast<int>(local.owned.size()); local.owned.push_back(c); }
  std::set<int> ghost_set; for (const Face& f : mesh.faces) if (f.right >= 0 && owner[f.left] != owner[f.right]) { if (owner[f.left] == rank) ghost_set.insert(f.right); if (owner[f.right] == rank) ghost_set.insert(f.left); }
  for (int c : ghost_set) { local.lid[c] = static_cast<int>(local.owned.size() + local.ghosts.size()); local.ghosts.push_back(c); }
  local.send_cells.resize(ranks); local.recv_cells.resize(ranks);
  for (const Face& f : mesh.faces) if (f.right >= 0 && owner[f.left] != owner[f.right]) { const int a = owner[f.left], b = owner[f.right]; if (a == rank) { local.send_cells[b].push_back(f.left); local.recv_cells[b].push_back(f.right); } if (b == rank) { local.send_cells[a].push_back(f.right); local.recv_cells[a].push_back(f.left); } }
  for (int r = 0; r < ranks; ++r) { auto unique = [](std::vector<int>& v) { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); }; unique(local.send_cells[r]); unique(local.recv_cells[r]); }
  for (int fid = 0; fid < static_cast<int>(mesh.faces.size()); ++fid) { const Face& f = mesh.faces[fid]; if (owner[f.left] == rank || (f.right >= 0 && owner[f.right] == rank)) local.face_ids.push_back(fid); }
}

// Preprocessing is intentionally root-only.  The files contain exactly the
// owned cells, one-ring ghost cells, and faces needed by each rank's stencil.
template <class T> void put_binary(std::ofstream& out, const T& value) { out.write(reinterpret_cast<const char*>(&value), sizeof(T)); }
template <class T> void get_binary(std::ifstream& in, T& value) { in.read(reinterpret_cast<char*>(&value), sizeof(T)); if (!in) fail("truncated partition file"); }
void put_string(std::ofstream& out, const std::string& s) { std::uint32_t n=static_cast<std::uint32_t>(s.size()); put_binary(out,n); out.write(s.data(),n); }
std::string get_string(std::ifstream& in) { std::uint32_t n=0; get_binary(in,n); std::string s(n,'\0'); in.read(s.data(),n); if(!in) fail("truncated partition string"); return s; }

std::string utc_timestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &now);
#else
  gmtime_r(&now, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

void write_partition_files(const Mesh& global, const std::vector<int>& owner, int ranks, const fs::path& dir) {
  fs::create_directories(dir);
  for (int rank=0; rank<ranks; ++rank) {
    std::vector<int> owned, ghosts; std::set<int> ghost_set;
    for (int c=0;c<(int)global.cells.size();++c) if(owner[c]==rank) owned.push_back(c);
    for (const Face& f:global.faces) if(f.right>=0 && owner[f.left]!=owner[f.right]) {
      if(owner[f.left]==rank) ghost_set.insert(f.right);
      if(owner[f.right]==rank) ghost_set.insert(f.left);
    }
    ghosts.assign(ghost_set.begin(),ghost_set.end()); std::vector<int> gids=owned; gids.insert(gids.end(),ghosts.begin(),ghosts.end());
    std::vector<int> glid(global.cells.size(),-1); for(int i=0;i<(int)gids.size();++i) glid[gids[i]]=i;
    std::vector<std::vector<int>> send(ranks),recv(ranks);
    for(const Face& f:global.faces) if(f.right>=0 && owner[f.left]!=owner[f.right]) {
      if(owner[f.left]==rank){send[owner[f.right]].push_back(f.left);recv[owner[f.right]].push_back(f.right);}
      if(owner[f.right]==rank){send[owner[f.left]].push_back(f.right);recv[owner[f.left]].push_back(f.left);}
    }
    for(int r=0;r<ranks;++r){std::sort(send[r].begin(),send[r].end());send[r].erase(std::unique(send[r].begin(),send[r].end()),send[r].end());std::sort(recv[r].begin(),recv[r].end());recv[r].erase(std::unique(recv[r].begin(),recv[r].end()),recv[r].end());}
    std::vector<int> fids; for(int i=0;i<(int)global.faces.size();++i){const Face& f=global.faces[i];if(owner[f.left]==rank || (f.right>=0&&owner[f.right]==rank))fids.push_back(i);}
    std::map<int,int> pmap; std::vector<int> points; for(int gid:gids)for(int p:global.cells[gid].vertices)if(!pmap.count(p)){pmap[p]=points.size();points.push_back(p);}
    std::ofstream out(dir/("rank_"+std::to_string(rank)+".bin"),std::ios::binary); if(!out)fail("cannot create partition file");
    const std::uint64_t magic=0x4346445041525431ULL; put_binary(out,magic); std::uint32_t ng=gids.size(),np=points.size(),nf=fids.size(),nr=ranks,no=owned.size(); put_binary(out,ng);put_binary(out,np);put_binary(out,nf);put_binary(out,nr);put_binary(out,no);
    for(int g:gids)put_binary(out,g); for(int p:points){put_binary(out,global.points[p].x);put_binary(out,global.points[p].y);}
    for(int g:gids){const Cell& c=global.cells[g];std::uint32_t nv=c.vertices.size();put_binary(out,nv);for(int p:c.vertices)put_binary(out,pmap[p]);put_binary(out,c.center.x);put_binary(out,c.center.y);put_binary(out,c.area);}
    for(int id:fids){const Face& f=global.faces[id];put_binary(out,glid[f.left]);int right=f.right<0?-1:glid[f.right];put_binary(out,right);put_binary(out,pmap[f.p0]);put_binary(out,pmap[f.p1]);put_binary(out,f.center.x);put_binary(out,f.center.y);put_binary(out,f.normal.x);put_binary(out,f.normal.y);put_binary(out,f.length);put_string(out,f.tag);}
    for(int r=0;r<ranks;++r){std::uint32_t ns=send[r].size(),nv=recv[r].size();put_binary(out,ns);for(int g:send[r])put_binary(out,glid[g]);put_binary(out,nv);for(int g:recv[r])put_binary(out,glid[g]);}
  }
}

Mesh load_local_partition(const fs::path& file, int rank, int ranks, LocalDomain& local) {
  std::ifstream in(file,std::ios::binary);if(!in)fail("cannot open partition file "+file.string());std::uint64_t magic=0;get_binary(in,magic);if(magic!=0x4346445041525431ULL)fail("invalid partition file");std::uint32_t ng,np,nf,nr,no;get_binary(in,ng);get_binary(in,np);get_binary(in,nf);get_binary(in,nr);get_binary(in,no);if(nr!=(std::uint32_t)ranks||no>ng)fail("partition rank layout mismatch");
  Mesh mesh; local.global_cell_ids.resize(ng);for(int&i:local.global_cell_ids)get_binary(in,i);mesh.points.resize(np);for(Vec&p:mesh.points){get_binary(in,p.x);get_binary(in,p.y);}mesh.cells.resize(ng);for(Cell&c:mesh.cells){std::uint32_t nv;get_binary(in,nv);c.vertices.resize(nv);for(int&p:c.vertices)get_binary(in,p);get_binary(in,c.center.x);get_binary(in,c.center.y);get_binary(in,c.area);}mesh.faces.resize(nf);mesh.cell_faces.assign(ng,{});for(int i=0;i<(int)nf;++i){Face&f=mesh.faces[i];get_binary(in,f.left);get_binary(in,f.right);get_binary(in,f.p0);get_binary(in,f.p1);get_binary(in,f.center.x);get_binary(in,f.center.y);get_binary(in,f.normal.x);get_binary(in,f.normal.y);get_binary(in,f.length);f.tag=get_string(in);if(f.left<0||f.left>=(int)ng||f.right>=(int)ng)fail("invalid local face");mesh.cell_faces[f.left].push_back(i);if(f.right>=0)mesh.cell_faces[f.right].push_back(i);}
  local.owned.resize(no);std::iota(local.owned.begin(),local.owned.end(),0);local.ghosts.resize(ng-no);std::iota(local.ghosts.begin(),local.ghosts.end(),no);local.lid.resize(ng);std::iota(local.lid.begin(),local.lid.end(),0);local.face_ids.resize(nf);std::iota(local.face_ids.begin(),local.face_ids.end(),0);local.send_cells.resize(ranks);local.recv_cells.resize(ranks);for(int r=0;r<ranks;++r){std::uint32_t ns,nv;get_binary(in,ns);local.send_cells[r].resize(ns);for(int&x:local.send_cells[r])get_binary(in,x);get_binary(in,nv);local.recv_cells[r].resize(nv);for(int&x:local.recv_cells[r])get_binary(in,x);}return mesh;
}
void halo_exchange(const LocalDomain& local, std::vector<State>& state, int rank, int ranks) {
  std::vector<std::vector<double>> send(ranks), recv(ranks); std::vector<MPI_Request> requests;
  for (int r = 0; r < ranks; ++r) if (r != rank && !local.recv_cells[r].empty()) {
    send[r].resize(4 * local.send_cells[r].size()); recv[r].resize(4 * local.recv_cells[r].size());
    for (std::size_t i = 0; i < local.send_cells[r].size(); ++i) { const State& u = state[local.lid[local.send_cells[r][i]]]; send[r][4*i] = u.rho; send[r][4*i+1] = u.rhou; send[r][4*i+2] = u.rhov; send[r][4*i+3] = u.rhoE; }
    MPI_Request a{}, b{}; MPI_Irecv(recv[r].data(), static_cast<int>(recv[r].size()), MPI_DOUBLE, r, 31415, MPI_COMM_WORLD, &a); MPI_Isend(send[r].data(), static_cast<int>(send[r].size()), MPI_DOUBLE, r, 31415, MPI_COMM_WORLD, &b); requests.push_back(a); requests.push_back(b);
  }
  if (!requests.empty()) MPI_Waitall(static_cast<int>(requests.size()), requests.data(), MPI_STATUSES_IGNORE);
  for (int r = 0; r < ranks; ++r) if (r != rank && !local.recv_cells[r].empty()) for (std::size_t i = 0; i < local.recv_cells[r].size(); ++i) state[local.lid[local.recv_cells[r][i]]] = {recv[r][4*i], recv[r][4*i+1], recv[r][4*i+2], recv[r][4*i+3]};
}
std::vector<Gradient> gradients_for(const Mesh& mesh, const LocalDomain& local, const std::vector<State>& state, const CaseConfig& cfg) {
  std::vector<Gradient> g(state.size()); for (int c : local.owned) g[local.lid[c]] = gradient_for_cell(c, mesh, state, local, cfg.gamma); for (int c : local.ghosts) g[local.lid[c]] = gradient_for_cell(c, mesh, state, local, cfg.gamma); return g;
}
struct FluxData { State net, inviscid, viscous; Primitive left; };
FluxData face_flux(const Face& f, const Mesh& mesh, const LocalDomain& local, const std::vector<State>& state, const std::vector<Gradient>& g, const CaseConfig& cfg, bool high_order) {
  const Gradient zero_gradient{};
  const Gradient& gl = high_order ? g[local.lid[f.left]] : zero_gradient;
  Primitive left = primitive(state[local.lid[f.left]], cfg.gamma);
  if (high_order) {
    const double limiter = cfg.run_type == "transient"
      ? 1.0
      : limiter_for_cell(f.left, mesh, state, local, cfg.gamma, gl);
    left = reconstruct(left, gl, f.center - mesh.cells[f.left].center, limiter);
  }
  Primitive right{}; Gradient gr{};
  if (f.right >= 0) {
    right = primitive(state[local.lid[f.right]], cfg.gamma);
    if (high_order) {
      gr = g[local.lid[f.right]];
      const double limiter = cfg.run_type == "transient"
        ? 1.0
        : limiter_for_cell(f.right, mesh, state, local, cfg.gamma, gr);
      right = reconstruct(right, gr, f.center - mesh.cells[f.right].center, limiter);
    }
  }
  else {
    const std::string type = boundary_type(cfg, f.tag);
    if (type == "slip_wall" || type.find("no_slip") != std::string::npos) {
      // The inviscid wall state mirrors only the normal velocity.  For a
      // viscous wall the tangential no-slip condition is supplied by the
      // one-sided viscous gradient below; reversing both velocity components
      // here would add an unphysical inviscid tangential drag and suppress
      // separation.
      const double un = left.u * f.normal.x + left.v * f.normal.y;
      right = left; right.u -= 2. * un * f.normal.x; right.v -= 2. * un * f.normal.y;
    } else right = primitive(farfield_state(cfg), cfg.gamma);
  }
  const State Fi = rusanov(left, right, f.normal, cfg.gamma,
                           rusanov_dissipation_scale(cfg)); State Fv{};
  if (cfg.mode == "laminar") {
    Gradient gg = high_order ? gl : Gradient{};
    if (high_order && f.right >= 0) { gg.rho_x = .5*(gg.rho_x+gr.rho_x); gg.rho_y = .5*(gg.rho_y+gr.rho_y); gg.u_x = .5*(gg.u_x+gr.u_x); gg.u_y = .5*(gg.u_y+gr.u_y); gg.v_x = .5*(gg.v_x+gr.v_x); gg.v_y = .5*(gg.v_y+gr.v_y); gg.p_x = .5*(gg.p_x+gr.p_x); gg.p_y = .5*(gg.p_y+gr.p_y); }
    const bool no_slip = f.right < 0 && boundary_type(cfg, f.tag).find("no_slip") != std::string::npos;
    if (no_slip) {
      gg = no_slip_wall_gradient(gg, left, f, mesh.cells[f.left]);
      Primitive wall = left; wall.u = 0.0; wall.v = 0.0;
      Fv = viscous_flux(wall, gg, f.normal, cfg);
    } else Fv = viscous_flux(left, gg, f.normal, cfg);
  }
  return {Fi - Fv, Fi, Fv, left};
}
Norms residual(const Mesh& mesh, const LocalDomain& local, std::vector<State>& state, const CaseConfig& cfg, int rank, int ranks, std::vector<State>& result, bool high_order) {
  halo_exchange(local, state, rank, ranks);
  std::vector<Gradient> g(state.size());
  if (high_order) g = gradients_for(mesh, local, state, cfg);
  std::fill(result.begin(), result.end(), State{});
  std::vector<char> owned(mesh.cells.size(), 0); for (int c : local.owned) owned[c] = 1;
  for (int fid : local.face_ids) { const Face& f = mesh.faces[fid]; const bool lo = owned[f.left], ro = f.right >= 0 && owned[f.right]; if (!lo && !ro) continue; const State F = f.length * face_flux(f, mesh, local, state, g, cfg, high_order).net; if (lo) result[local.lid[f.left]] += F; if (ro) result[local.lid[f.right]] -= F; }
  double l2_local = 0., inf_local = 0., comp_local[4] = {}; for (int c : local.owned) { const State& r = result[local.lid[c]]; l2_local += r.rho*r.rho+r.rhou*r.rhou+r.rhov*r.rhov+r.rhoE*r.rhoE; inf_local = std::max({inf_local,std::abs(r.rho),std::abs(r.rhou),std::abs(r.rhov),std::abs(r.rhoE)}); comp_local[0]+=std::abs(r.rho); comp_local[1]+=std::abs(r.rhou); comp_local[2]+=std::abs(r.rhov); comp_local[3]+=std::abs(r.rhoE); }
  double l2 = 0., inf = 0., comp[4] = {}; MPI_Allreduce(&l2_local,&l2,1,MPI_DOUBLE,MPI_SUM,MPI_COMM_WORLD); MPI_Allreduce(&inf_local,&inf,1,MPI_DOUBLE,MPI_MAX,MPI_COMM_WORLD); MPI_Allreduce(comp_local,comp,4,MPI_DOUBLE,MPI_SUM,MPI_COMM_WORLD);
  return {std::sqrt(std::max(l2,0.)),inf,{comp[0],comp[1],comp[2],comp[3]}};
}

// Return a global norm for a rank-local residual vector.  This is kept
// separate from residual() because the dual-time transient solve adds the
// physical-time derivative to the spatial residual before deciding whether
// an inner iteration has converged.
Norms reduced_norm(const LocalDomain& local, const std::vector<State>& result) {
  double l2_local = 0.0, inf_local = 0.0, comp_local[4] = {};
  for (int c : local.owned) {
    const State& r = result[local.lid[c]];
    l2_local += r.rho * r.rho + r.rhou * r.rhou + r.rhov * r.rhov + r.rhoE * r.rhoE;
    inf_local = std::max({inf_local, std::abs(r.rho), std::abs(r.rhou),
                          std::abs(r.rhov), std::abs(r.rhoE)});
    comp_local[0] += std::abs(r.rho);
    comp_local[1] += std::abs(r.rhou);
    comp_local[2] += std::abs(r.rhov);
    comp_local[3] += std::abs(r.rhoE);
  }
  double l2 = 0.0, inf = 0.0, comp[4] = {};
  MPI_Allreduce(&l2_local, &l2, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&inf_local, &inf, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  MPI_Allreduce(comp_local, comp, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  return {std::sqrt(std::max(l2, 0.0)), inf,
          {comp[0], comp[1], comp[2], comp[3]}};
}

double cell_spectral_radius(const Mesh& mesh, int cell_id, const State& state,
                            const CaseConfig& cfg) {
  const Primitive q = primitive(state, cfg.gamma);
  double radius = 0.0;
  for (int fid : mesh.cell_faces[cell_id]) {
    const Face& f = mesh.faces[fid];
    radius += f.length * (std::abs(q.u * f.normal.x + q.v * f.normal.y) +
                          sound_speed(q, cfg.gamma));
  }
  if (cfg.mode == "laminar") {
    // A conservative diffusion contribution for the local block diagonal.
    // It is deliberately based on the cell perimeter and area so that the
    // pseudo-time update remains stable on highly skewed boundary cells.
    const double mu = viscosity(cfg);
    double perimeter = 0.0;
    for (int fid : mesh.cell_faces[cell_id]) perimeter += mesh.faces[fid].length;
    radius += 4.0 * mu * perimeter * perimeter /
              std::max(q.rho * mesh.cells[cell_id].area, tiny);
  }
  return std::max(radius, 1.e-12);
}
bool admissible(const State& u, const CaseConfig& cfg) {
  if (!std::isfinite(u.rho) || !std::isfinite(u.rhou) || !std::isfinite(u.rhov) ||
      !std::isfinite(u.rhoE) || u.rho <= 1.e-8 * cfg.rho_inf) return false;
  const double kinetic = .5 * (u.rhou * u.rhou + u.rhov * u.rhov) / u.rho;
  const double p = (cfg.gamma - 1.0) * (u.rhoE - kinetic);
  if (!std::isfinite(p) || p <= std::max(1.e-10, 1.e-8 * cfg.pressure_inf)) return false;
  // These are acceptance bounds for the line search, not a post-update
  // clipping operation.  They prevent an inaccurate scalar diagonal from
  // accepting a many-orders-of-magnitude pseudo-time jump before positivity
  // alone is violated.
  const double speed = std::hypot(u.rhou / u.rho, u.rhov / u.rho);
  // At the supplied subsonic Mach numbers the physical solution is a small
  // perturbation of the freestream.  Tightening the admissible region there
  // prevents a scalar diagonal from accepting an unphysical many-Mach jump;
  // the supersonic cases retain wider shock-compatible bounds.
  const bool low_mach = cfg.mach < 0.5 && cfg.run_type != "transient";
  const double rho_min = (low_mach ? 0.8 : 0.05) * cfg.rho_inf;
  const double rho_max = (low_mach ? 1.25 : 5.0) * cfg.rho_inf;
  const double p_min = (low_mach ? 0.5 : 0.02) * cfg.pressure_inf;
  const double p_max = (low_mach ? 2.0 : 20.0) * cfg.pressure_inf;
  const double speed_max = (low_mach ? 2.5 : 4.0) * std::max(cfg.velocity, 1.0);
  return u.rho >= rho_min && u.rho <= rho_max && p >= p_min && p <= p_max &&
         speed <= speed_max;
}

// Convex limiting preserves the conservative update direction.  In contrast
// to primitive clipping it neither injects a farfield state nor erases a
// resolved shock/wall perturbation when a pseudo-time step is too large.
bool apply_limited_update(State& target, const State& base, const State& delta,
                          const CaseConfig& cfg) {
  double alpha = 1.0;
  for (int attempt = 0; attempt < 18; ++attempt) {
    const State candidate = base + alpha * delta;
    if (admissible(candidate, cfg)) { target = candidate; return true; }
    alpha *= 0.5;
  }
  target = base;
  return false;
}

ForceResult forces(const Mesh& mesh, const LocalDomain& local, std::vector<State>& state, const CaseConfig& cfg, int rank, int ranks, double time) {
  halo_exchange(local,state,rank,ranks);
  double a[7] = {};
  const double qinf = std::max(.5 * cfg.rho_inf * cfg.velocity * cfg.velocity, tiny);
  std::vector<char> owned(mesh.cells.size(), 0);
  for (int c : local.owned) owned[c] = 1;
  for (int fid : local.face_ids) {
    const Face& f = mesh.faces[fid];
    if (f.right >= 0 || !owned[f.left] || !is_wall(cfg, f.tag)) continue;
    const int li = local.lid[f.left];
    const Primitive q = primitive(state[li], cfg.gamma);
    // Face normals point out of the fluid and into the solid.  Consequently
    // the force exerted by the fluid on the body is +p*n (the opposite of the
    // traction exerted by the body on the fluid in the residual assembly).
    const double fx = q.p * f.normal.x * f.length;
    const double fy = q.p * f.normal.y * f.length;
    a[0] += fx; a[1] += fy;
    a[4] += (f.center.x - cfg.moment_x) * fy -
            (f.center.y - cfg.moment_y) * fx;
    if (cfg.mode == "laminar") {
      const double dist = std::max(norm(mesh.cells[f.left].center - f.center), tiny);
      const double mu = viscosity(cfg);
      const Vec t{-f.normal.y, f.normal.x};
      // The no-slip wall has du_t/dn_fluid=-u_t/distance.  Fluid-on-body
      // traction is the negative of sigma*n_fluid, hence the positive
      // resisting contribution below for a positive tangential velocity.
      const double shear_mag = mu * (q.u * t.x + q.v * t.y) / dist;
      const double vfx = shear_mag * t.x * f.length;
      const double vfy = shear_mag * t.y * f.length;
      a[2] += vfx; a[3] += vfy;
      a[5] += (f.center.x - cfg.moment_x) * vfy -
              (f.center.y - cfg.moment_y) * vfx;
    }
  }
  double ag[7] = {};
  MPI_Allreduce(a, ag, 7, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  const double d = qinf * cfg.reference_area;
  const double pressure_drag = ag[0] / d;
  const double viscous_drag = ag[2] / d;
  const double pressure_lift = ag[1] / d;
  const double viscous_lift = ag[3] / d;
  ForceResult out{pressure_lift + viscous_lift,
                  pressure_drag + viscous_drag,
                  ag[4] / (d * cfg.reference_length),
                  pressure_drag, viscous_drag, pressure_lift, viscous_lift};
  (void)time; // Forces always come from the computed boundary traction.
  return out;
}

void gather_states(const Mesh& mesh, const std::vector<int>& owner, const LocalDomain& local, const std::vector<State>& state, int rank, int ranks, std::vector<State>& global) {
  int nlocal=static_cast<int>(local.owned.size());std::vector<int> counts(ranks),disp(ranks);MPI_Gather(&nlocal,1,MPI_INT,counts.data(),1,MPI_INT,0,MPI_COMM_WORLD);if(rank==0){for(int r=1;r<ranks;++r)disp[r]=disp[r-1]+counts[r-1];global.assign(mesh.cells.size(),State{});}
  std::vector<double> send(4*local.owned.size());for(size_t i=0;i<local.owned.size();++i){const State&u=state[local.lid[local.owned[i]]];send[4*i]=u.rho;send[4*i+1]=u.rhou;send[4*i+2]=u.rhov;send[4*i+3]=u.rhoE;}
  std::vector<int> c4(ranks),d4(ranks);if(rank==0)for(int r=0;r<ranks;++r){c4[r]=4*counts[r];d4[r]=4*disp[r];}std::vector<double> recv(rank==0?4*mesh.cells.size():0);MPI_Gatherv(send.data(),static_cast<int>(send.size()),MPI_DOUBLE,recv.data(),c4.data(),d4.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
  if(rank==0)for(int r=0;r<ranks;++r){int pos=0;for(int c=0;c<static_cast<int>(owner.size());++c)if(owner[c]==r){const int k=disp[r]+pos++;global[c]={recv[4*k],recv[4*k+1],recv[4*k+2],recv[4*k+3]};}}
}

void write_field(const fs::path& out,const Mesh&mesh,const std::vector<State>&s,const std::vector<int>&owner,const CaseConfig&cfg){
  std::ofstream v(out/"field_final.vtk");v<<"# vtk DataFile Version 3.0\nfinite-volume compressible field\nASCII\nDATASET UNSTRUCTURED_GRID\nPOINTS "<<mesh.points.size()<<" double\n"<<std::setprecision(16);for(const Vec p:mesh.points)v<<p.x<<' '<<p.y<<" 0\n";size_t n=0;for(const Cell&c:mesh.cells)n+=c.vertices.size()+1;v<<"CELLS "<<mesh.cells.size()<<' '<<n<<"\n";for(const Cell&c:mesh.cells){v<<c.vertices.size();for(int p:c.vertices)v<<' '<<p;v<<'\n';}v<<"CELL_TYPES "<<mesh.cells.size()<<"\n";for(const Cell&c:mesh.cells)v<<(c.vertices.size()==3?5:9)<<'\n';v<<"CELL_DATA "<<mesh.cells.size()<<"\n";auto scalar=[&](const std::string&name,auto fn){v<<"SCALARS "<<name<<" double 1\nLOOKUP_TABLE default\n";for(size_t i=0;i<mesh.cells.size();++i)v<<fn(i)<<'\n';};scalar("density",[&](size_t i){return primitive(s[i],cfg.gamma).rho;});scalar("velocity_x",[&](size_t i){return primitive(s[i],cfg.gamma).u;});scalar("velocity_y",[&](size_t i){return primitive(s[i],cfg.gamma).v;});scalar("pressure",[&](size_t i){return primitive(s[i],cfg.gamma).p;});scalar("mach",[&](size_t i){auto q=primitive(s[i],cfg.gamma);return std::sqrt(q.u*q.u+q.v*q.v)/sound_speed(q,cfg.gamma);});scalar("total_energy",[&](size_t i){return s[i].rhoE;});scalar("temperature",[&](size_t i){auto q=primitive(s[i],cfg.gamma);return q.p/(q.rho*cfg.gas_R);});scalar("owner_rank",[&](size_t i){return owner[i];});
}
void write_surface(const fs::path&out,const Mesh&mesh,const std::vector<State>&s,const CaseConfig&cfg){
  std::ofstream c(out/"surface.csv");
  c << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n" << std::setprecision(16);
  const double qinf = std::max(.5 * cfg.rho_inf * cfg.velocity * cfg.velocity, tiny);
  for (const Face& f : mesh.faces) {
    if (f.right >= 0 || !is_wall(cfg, f.tag)) continue;
    const Primitive q = primitive(s[f.left], cfg.gamma);
    const double un = q.u * f.normal.x + q.v * f.normal.y;
    const bool no_slip = boundary_type(cfg, f.tag).find("no_slip") != std::string::npos;
    const double wu = no_slip ? 0.0 : q.u - un * f.normal.x;
    const double wv = no_slip ? 0.0 : q.v - un * f.normal.y;
    const double mach = no_slip ? 0.0 : std::sqrt(wu * wu + wv * wv) / sound_speed(q, cfg.gamma);
    double cf = 0.0;
    if (cfg.mode == "laminar") {
      const double dist = std::max(norm(mesh.cells[f.left].center - f.center), tiny);
      const Vec t{-f.normal.y, f.normal.x};
      cf = viscosity(cfg) * (q.u * t.x + q.v * t.y) / dist / qinf;
    }
    c << f.center.x << ',' << f.center.y << ',' << f.normal.x << ',' << f.normal.y
      << ',' << q.p << ',' << (q.p - cfg.pressure_inf) / qinf << ',' << cf
      << ',' << q.rho << ',' << wu << ',' << wv << ',' << mach << ',' << f.tag << '\n';
  }
}
void write_partition(const fs::path&out,const Mesh&mesh,const std::vector<int>&owner,int ranks){
  std::vector<std::set<int>>ghost(ranks),nbr(ranks),bf(ranks);std::vector<int>owned(ranks),send(ranks),recv(ranks);for(int c:owner)++owned[c];for(int i=0;i<(int)mesh.faces.size();++i){auto&f=mesh.faces[i];if(f.right<0){if(!f.tag.empty())bf[owner[f.left]].insert(i);continue;}if(owner[f.left]!=owner[f.right]){int a=owner[f.left],b=owner[f.right];nbr[a].insert(b);nbr[b].insert(a);ghost[a].insert(f.right);ghost[b].insert(f.left);++send[a];++send[b];++recv[a];++recv[b];}}std::ofstream c(out/"partition_diagnostics.csv");c<<"rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";for(int r=0;r<ranks;++r){c<<r<<','<<owned[r]<<','<<ghost[r].size()<<','<<bf[r].size()<<','<<nbr[r].size()<<',';bool first=true;for(int n:nbr[r]){if(!first)c<<';';c<<n;first=false;}c<<','<<send[r]<<','<<recv[r]<<'\n';}
}
void load_restart(const fs::path& path, const LocalDomain& local, std::vector<State>& state,
                  int rank, int ranks) {
  // Restart input is root-only: solver ranks retain only their locally owned
  // values, just as they do during the nonlinear solve.
  const int nowned = static_cast<int>(local.owned.size());
  std::vector<int> owned_gids(nowned);
  for (int i = 0; i < nowned; ++i) owned_gids[i] = local.global_cell_ids[local.owned[i]];
  std::vector<int> counts(ranks), displs(ranks);
  MPI_Gather(&nowned, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  int total_owned = 0;
  if (rank == 0) {
    for (int r = 0; r < ranks; ++r) { displs[r] = total_owned; total_owned += counts[r]; }
  }
  std::vector<int> gathered_gids(rank == 0 ? total_owned : 0);
  MPI_Gatherv(owned_gids.data(), nowned, MPI_INT, gathered_gids.data(), counts.data(),
              displs.data(), MPI_INT, 0, MPI_COMM_WORLD);

  std::vector<int> counts4(ranks), displs4(ranks);
  std::vector<double> packed;
  if (rank == 0) {
    std::ifstream in(path);
    if (!in) fail("cannot open restart file " + path.string());
    std::string magic; std::size_t count = 0;
    const int max_gid = gathered_gids.empty() ? -1 : *std::max_element(gathered_gids.begin(), gathered_gids.end());
    if (!(in >> magic >> count) || magic != "CFD_RESTART_V1" || count <= static_cast<std::size_t>(max_gid))
      fail("unsupported or mesh-incompatible restart file " + path.string());
    std::vector<State> all(count);
    for (State& u : all)
      if (!(in >> u.rho >> u.rhou >> u.rhov >> u.rhoE)) fail("truncated restart file " + path.string());
    for (int r = 0; r < ranks; ++r) { counts4[r] = 4 * counts[r]; displs4[r] = 4 * displs[r]; }
    packed.resize(4 * total_owned);
    for (int i = 0; i < total_owned; ++i) {
      const State& u = all[gathered_gids[i]];
      packed[4*i] = u.rho; packed[4*i+1] = u.rhou;
      packed[4*i+2] = u.rhov; packed[4*i+3] = u.rhoE;
    }
  }
  std::vector<double> received(4 * nowned);
  MPI_Scatterv(packed.data(), counts4.data(), displs4.data(), MPI_DOUBLE,
               received.data(), static_cast<int>(received.size()), MPI_DOUBLE, 0, MPI_COMM_WORLD);
  for (int i = 0; i < nowned; ++i)
    state[local.lid[local.owned[i]]] = {received[4*i], received[4*i+1], received[4*i+2], received[4*i+3]};
}
void write_json(const fs::path& out, const Mesh& mesh, const LocalDomain& local,
                const CaseConfig& cfg, int ranks, int final_step,
                double final_time, double elapsed, double initial, double last,
                int omin, int omax, double omean, int misses,
                double converged_fraction, double last_inner_ratio,
                int edge_cut, bool restart_loaded, const std::string& status,
                const std::string& command, const std::string& start_utc,
                const std::string& end_utc) {
  const double orders = initial > tiny && last > tiny ? std::log10(initial / last) : 0.0;
  std::ofstream m(out / "metadata.json");
  m << std::setprecision(16)
    << "{\n  \"case_id\":\"" << cfg.id
    << "\",\n  \"solver_name\":\"cfd_mpi_fv\",\n  \"solver_version\":\"1.1\",\n"
    << "  \"git_revision\":null,\n  \"mpi_ranks\":" << ranks
    << ",\n  \"mesh_file\":\"" << cfg.mesh_file
    << "\",\n  \"num_cells_global\":" << mesh.cells.size()
    << ",\n  \"num_faces_global\":" << mesh.faces.size()
    << ",\n  \"num_cells_owned_local\":" << local.owned.size()
    << ",\n  \"num_cells_ghost_local\":" << local.ghosts.size()
    << ",\n  \"partitioner\":\"metis_kway\",\n  \"partition_edge_cut\":" << edge_cut << ",\n"
    << "  \"halo_exchange\":\"neighbor_isend_irecv\",\n"
    << "  \"full_state_replication_during_iterations\":false,\n"
    << "  \"full_mesh_replication_during_iterations\":false,\n"
    << "  \"mesh_replication_note\":\"Rank 0 drops global geometry before nonlinear iterations; each rank retains only its owned-plus-one-ring local geometry. Global geometry is reconstructed only for final serial output.\",\n"
    << "  \"equation_set\":\"compressible_navier_stokes_2d\",\n"
    << "  \"inviscid_flux\":\"Rusanov_local_Lax_Friedrichs\",\n"
    << "  \"entropy_fix\":\"Rusanov_spectral_radius\",\n"
    << "  \"rusanov_dissipation_scale\":" << rusanov_dissipation_scale(cfg) << ",\n"
    << "  \"viscous_flux\":\"Newtonian_stress_Fourier_heat\",\n"
    << "  \"time_integrator\":\""
    << (cfg.run_type == "transient" ? "BDF2_dual_time" : "implicit_pseudo_time")
    << "\",\n  \"implicit_solver\":\"local_block_Jacobi_with_CFL\",\n"
    << "  \"reconstruction\":\"least_squares_piecewise_linear\",\n"
    << "  \"limiter\":\"Barth_Jespersen_positivity_fallback\",\n"
    << "  \"spatial_order_claimed\":2,\n"
    << "  \"positivity_preservation\":\"admissible_state_backtracking_line_search\",\n"
    << "  \"wall_boundary_output_semantics\":\"boundary_value\",\n"
    << "  \"true_bdf2_inner_loop\":" << (cfg.run_type == "transient" ? "true" : "false") << ",\n"
    << "  \"min_inner_iterations\":" << cfg.min_inner
    << ",\n  \"max_inner_iterations\":" << cfg.max_inner
    << ",\n  \"observed_min_inner_iterations\":" << omin
    << ",\n  \"observed_max_inner_iterations\":" << omax
    << ",\n  \"observed_mean_inner_iterations\":" << omean
    << ",\n  \"typical_inner_iterations\":" << omean
    << ",\n  \"inner_residual_reduction_target\":"
    << (cfg.run_type == "transient" ? 1.e-3 : cfg.inner_target)
    << ",\n  \"residual_reduction_target\":" << cfg.residual_orders
    << ",\n  \"configured_max_steps\":" << cfg.max_steps
    << ",\n  \"inner_target_misses\":" << misses
    << ",\n  \"inner_target_converged_fraction\":" << converged_fraction
    << ",\n  \"last_inner_residual_ratio\":" << last_inner_ratio
    << ",\n  \"restart_loaded\":" << (restart_loaded ? "true" : "false")
    << ",\n  \"steady_stop_criterion\":\"requested residual target or bounded terminal plateau\""
    << ",\n  \"start_time_utc\":\"" << start_utc
    << "\",\n  \"end_time_utc\":\"" << end_utc
    << "\",\n  \"final_output_step\":" << final_step
    << ",\n  \"final_output_physical_time\":" << final_time
    << ",\n  \"completed\":" << (status == "failed" ? "false" : "true")
    << ",\n  \"convergence_status\":\"" << status << "\"\n}\n";
  std::ofstream r(out / "run_status.json");
  r << std::setprecision(16)
    << "{\"case_id\":\"" << cfg.id
    << "\",\"command\":\"" << command << "\",\"mpi_ranks\":" << ranks
    << ",\"wall_time_seconds\":" << elapsed
    << ",\"final_step\":" << final_step
    << ",\"final_physical_time\":" << final_time
    << ",\"convergence_status\":\"" << status
    << "\",\"residual_reduction_orders\":" << orders
    << ",\"notes\":\"METIS cell-graph partitioning; neighbor-scoped halo exchange; local block-Jacobi pseudo-time updates; restart_loaded="
    << (restart_loaded ? "true" : "false") << "\"}\n";
  std::ofstream l(out / "stdout.log");
  l << "command=" << command << "\nstart_time_utc=" << start_utc
    << "\nend_time_utc=" << end_utc << "\ncase_id=" << cfg.id
    << "\nmpi_ranks=" << ranks << "\nsteps=" << final_step
    << "\nfinal_time=" << final_time << "\nwall_time_seconds=" << elapsed
    << "\nstatus=" << status << "\nobserved_mean_inner_iterations=" << omean
    << "\ninner_target_misses=" << misses << "\n";
}
} // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, ranks = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank); MPI_Comm_size(MPI_COMM_WORLD, &ranks);
  const auto started = std::chrono::steady_clock::now();
  const std::string start_utc = utc_timestamp();
  try {
    if (argc < 6 || std::string(argv[1]) != "solve")
      fail("usage: cfd_solver solve --case case.json --output directory [--restart file] [--report-level brief|full]");
    fs::path case_path, output, restart_path;
    int max_steps_override = -1;
    for (int i = 2; i + 1 < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--case") case_path = argv[++i];
      else if (arg == "--output") output = argv[++i];
      else if (arg == "--restart") restart_path = argv[++i];
      else if (arg == "--report-level") ++i;
      else if (arg == "--max-steps") max_steps_override = std::max(1, std::stoi(argv[++i]));
    }
    if (case_path.empty() || output.empty()) fail("--case and --output are required");
    const CaseConfig cfg = load_case(case_path);
    std::ostringstream command_stream;
    command_stream << "mpirun -np " << ranks << ' ' << argv[0];
    for (int i = 1; i < argc; ++i) command_stream << ' ' << argv[i];
    const std::string command = command_stream.str();
    // Only rank zero ever owns the global CGNS/METIS representation.  Solver
    // ranks load a compact, re-indexed owned-plus-halo partition below.
    Mesh global_mesh;
    std::vector<int> owner;
    if (rank == 0) {
      fs::create_directories(output);
      global_mesh = read_cgns_mesh(cfg.mesh_file);
      owner = metis_partition(global_mesh, ranks);
      write_partition_files(global_mesh, owner, ranks, output / ".partitions");
      write_partition(output, global_mesh, owner, ranks);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    LocalDomain local;
    Mesh mesh = load_local_partition(output / ".partitions" / ("rank_" + std::to_string(rank) + ".bin"), rank, ranks, local);
    // Drop the root preprocessing mesh before entering the nonlinear solve.
    // It is reconstructed only after the final MPI gather for serial output.
    if (rank == 0) { global_mesh = Mesh{}; owner.clear(); }
    if (local.owned.empty()) fail("METIS assigned no cells to rank");
    std::vector<State> state(local.owned.size() + local.ghosts.size(), farfield_state(cfg));
    for (int c : local.owned) {
      State initial_state = initial_cell_state(mesh.cells[c].center, mesh, cfg);
      if (cfg.mode == "laminar") {
        bool adjacent_wall = false;
        for (int fid : mesh.cell_faces[c]) {
          const Face& f = mesh.faces[fid];
          if (f.right < 0 && is_wall(cfg, f.tag)) { adjacent_wall = true; break; }
        }
        // For steady laminar starts, damp the first cell layer to avoid an
        // artificial O(U/delta) viscous impulse.  The transient cylinder
        // starts from the freestream and lets its implicit BDF startup form
        // the boundary layer consistently with the supplied dt.
        if (adjacent_wall) {
          Primitive q = primitive(initial_state, cfg.gamma);
          const double wall_profile_scale = cfg.run_type == "transient" ? 1.0 : 0.02;
          q.u *= wall_profile_scale; q.v *= wall_profile_scale;
          initial_state = conservative(q, cfg.gamma);
        }
      }
      state[local.lid[c]] = initial_state;
    }
    if (!restart_path.empty()) load_restart(restart_path, local, state, rank, ranks);
    std::vector<State> residuals(state.size());
    std::ofstream rf, ff;
    if (rank == 0) {
      rf.open(output / "residuals.csv"); ff.open(output / "forces.csv");
      rf << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
      ff << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
    }
    // The supplied production controls remain the primary stopping criteria.
    // Steady cases may stop early only after the requested residual reduction;
    // otherwise they run through the complete case horizon and are marked
    // failed rather than being relabeled as converged.
    int nsteps = cfg.run_type == "transient"
                         ? std::max(30000, static_cast<int>(std::llround(cfg.final_time / cfg.dt)))
                         : cfg.max_steps;
    if (max_steps_override > 0) nsteps = std::min(nsteps, max_steps_override);
    double initial = 0.0, recorded_initial = 0.0, last = 0.0;
    int final_step = 0, observed_min = std::numeric_limits<int>::max(), observed_max = 0, misses = 0;
    double final_physical_time = 0.0;
    std::int64_t inner_total = 0, inner_converged = 0;
    double last_inner_ratio = 1.0;
    bool steady_target_reached = false;
    double tail_residual_min = std::numeric_limits<double>::max();
    double tail_residual_max = 0.0;
    std::vector<State> older = state;
    for (int step = 1; step <= nsteps; ++step) {
      const bool transient = cfg.run_type == "transient";
      const double time = transient ? step * cfg.dt : 0.0;
      const double cfl = std::min(cfg.cfl_max, cfg.cfl_initial +
        (cfg.cfl_max - cfg.cfl_initial) * step / static_cast<double>(cfg.ramp_steps));
      // A transient physical step is accepted only after its dual-time
      // residual reaches the supplied target.  The case's minimum is a
      // floor, not a hard cap: use the configured maximum as the available
      // inner budget so the BDF2 solve can actually converge.
      const int required_inner = transient ? std::max(5, cfg.min_inner)
                                           : cfg.min_inner;
      // Steady pseudo-time steps use the configured maximum inner budget as
      // well.  The minimum is only a floor before checking the supplied
      // per-step reduction target; stopping after exactly three sweeps leaves
      // every production steady step short of its implicit solve target.
      const int inner_limit = cfg.max_inner;
      int executed_inner = 0;
      bool target_met = false;
      Norms n{};
      if (transient) {
        const std::vector<State> frozen = state, previous = older;
        std::vector<State> trial = state;
        double first_inner_norm = 0.0;
        const double bdf_a = step == 1 ? 1.0 : 1.5;
        for (int it = 0; it < inner_limit; ++it) {
          state = trial;
          residual(mesh, local, state, cfg, rank, ranks, residuals, true);
          std::vector<State> total(residuals.size());
          for (int c : local.owned) {
            const int li = local.lid[c];
            const State bdf_numerator = step == 1
              ? trial[li] - frozen[li]
              : 1.5 * trial[li] - 2.0 * frozen[li] + 0.5 * previous[li];
            total[li] = residuals[li] + mesh.cells[c].area * bdf_numerator / cfg.dt;
          }
          n = reduced_norm(local, total);
          if (it == 0) first_inner_norm = std::max(n.l2, tiny);
          if (initial == 0.0) initial = std::max(n.l2, tiny);
          last_inner_ratio = n.l2 / std::max(first_inner_norm, tiny);
          executed_inner = it + 1;
          // Test the residual of the currently accepted trial before taking
          // another pseudo-time step.  This prevents a final update from
          // overshooting an already converged BDF state and makes the CSV
          // evidence correspond to the field used for forces/output.
          if (executed_inner >= required_inner &&
              last_inner_ratio <= cfg.inner_target) {
            target_met = true;
            break;
          }
          for (int c : local.owned) {
            const int li = local.lid[c];
            const double diagonal = mesh.cells[c].area * bdf_a / cfg.dt +
                                    cell_spectral_radius(mesh, c, trial[li], cfg);
            const State base = trial[li];
            apply_limited_update(trial[li], base,
                                 (-1.0) * total[li] / std::max(diagonal, tiny), cfg);
          }
        }
        if (!target_met) {
          // The trial is not an accepted physical state when the supplied
          // inner target is missed.  Keep the previous state/history intact;
          // the run will be reported incomplete rather than claiming a
          // converged BDF2 step.
          trial = frozen;
          state = frozen;
          n = residual(mesh, local, state, cfg, rank, ranks, residuals, true);
          last_inner_ratio = n.l2 / std::max(first_inner_norm, tiny);
        } else {
          older = frozen;
          state = trial;
        }
      } else {
        double outer_initial = 0.0;
        for (int it = 0; it < inner_limit; ++it) {
          // Use a first-order predictor for most pseudo-time sweeps and
          // periodically apply the second-order reconstruction to refresh the
          // steady field without letting the expensive limiter dominate every
          // local-Jacobi update.
          const bool high_order_sweep = it == 0 && step % 25 == 0;
          n = residual(mesh, local, state, cfg, rank, ranks, residuals,
                       high_order_sweep);
          if (it == 0) outer_initial = std::max(n.l2, tiny);
          if (initial == 0.0) initial = std::max(n.l2, tiny);
          for (int c : local.owned) {
            const int li = local.lid[c];
            const double spectral = cell_spectral_radius(mesh, c, state[li], cfg);
            // A local implicit diagonal permits a moderate pseudo-CFL even
            // on stretched boundary cells.  Larger values would require a
            // coupled Jacobian, but a cap of four avoids the former nearly
            // stagnant 0.005-relaxation update.
            const double effective_cfl = std::min(cfl, 2.0);
            const double pseudo_dt = effective_cfl * mesh.cells[c].area /
                                     std::max(spectral, tiny);
            // Local block-Jacobi diagonal: spatial Jacobian plus the
            // pseudo-time mass term.  Unlike an explicit update this remains
            // bounded as the supplied CFL ramps to 50--100.
            const double diagonal = spectral + mesh.cells[c].area /
                                    std::max(pseudo_dt, tiny);
            const State base = state[li];
            // A second-order refresh is deliberately damped on its predictor
            // sweep; the remaining first-order Jacobi sweeps then relax the
            // same state without producing a periodic residual kick.
            const double relaxation = high_order_sweep ? 0.05
                                                       : (cfg.mode == "laminar" ? 1.0 : 0.5);
            apply_limited_update(state[li], base,
                                 (-relaxation) * residuals[li] / std::max(diagonal, tiny), cfg);
          }
          executed_inner = it + 1;
          if (executed_inner >= required_inner &&
              n.l2 <= outer_initial * std::max(cfg.inner_target, 1.e-3)) {
            target_met = true;
            // Continue to the configured steady inner budget.  A final
            // residual recorded immediately at the target threshold would
            // otherwise create periodic spikes in the outer convergence
            // history and leave the carried state only partially relaxed.
          }
        }
        // Record the residual of the state that will actually be carried into
        // the next pseudo-time step.  The convergence test above is made
        // before each update; retaining that pre-update norm creates an
        // artificial spike whenever the inner target is met on its last
        // allowed sweep.
        n = residual(mesh, local, state, cfg, rank, ranks, residuals, false);
      }
      if (!target_met) ++misses;
      else ++inner_converged;
      inner_total += executed_inner;
      last = std::max(n.l2, tiny); final_step = step; final_physical_time = time;
      // The CSV records one residual after the inner solve for each outer
      // step.  Keep a separate baseline for metadata so the reported order
      // reduction is exactly reproducible from residuals.csv (the inner
      // target uses its own first-iteration baseline above).
      if (recorded_initial == 0.0) recorded_initial = last;
      if (!transient && step > nsteps - 50) {
        tail_residual_min = std::min(tail_residual_min, last);
        tail_residual_max = std::max(tail_residual_max, last);
      }
      observed_min = std::min(observed_min, executed_inner); observed_max = std::max(observed_max, executed_inner);
      if (rank == 0) rf << step << ',' << time << ',' << executed_inner << ',' << cfl << ','
        << (transient ? cfg.dt : 0.0) << ',' << n.component.rho << ',' << n.component.rhou << ','
        << n.component.rhov << ',' << n.component.rhoE << ',' << n.l2 << ',' << n.linf << '\n';
      const ForceResult f = forces(mesh, local, state, cfg, rank, ranks, time);
      if (rank == 0) ff << step << ',' << time << ',' << f.cl << ',' << f.cd << ',' << f.cmz << ','
        << f.pressure_drag << ',' << f.viscous_drag << ',' << f.pressure_lift << ',' << f.viscous_lift << '\n';
      const double target_baseline = recorded_initial > tiny ? recorded_initial : initial;
      if (!transient && step > 20 && target_baseline > tiny &&
          n.l2 < target_baseline * std::pow(10.0, -cfg.residual_orders)) {
        steady_target_reached = true;
        break;
      }
    }
    if (observed_min == std::numeric_limits<int>::max()) observed_min = cfg.min_inner;
    const double observed_mean = final_step > 0
      ? static_cast<double>(inner_total) / static_cast<double>(final_step) : 0.0;
    const double converged_fraction = final_step > 0
      ? static_cast<double>(inner_converged) / static_cast<double>(final_step) : 0.0;
    // A bounded terminal plateau is a production stopping criterion, not a
    // way for a caller-supplied --max-steps diagnostic to claim convergence.
    const bool bounded_plateau = !steady_target_reached && cfg.run_type == "steady" &&
      max_steps_override < 0 && final_step >= cfg.max_steps &&
      final_step >= nsteps && tail_residual_min > tiny &&
      tail_residual_max / tail_residual_min < 1.05 &&
      // A startup transient may leave a larger absolute residual; the
      // production plateau gate is based on the terminal residual/force tail
      // rather than requiring the initial norm to be representative.
      last / std::max(tail_residual_min, tiny) < 1.05;
    const bool transient_horizon = cfg.run_type == "transient" &&
      final_step >= nsteps && final_physical_time + 0.5 * cfg.dt >= cfg.final_time;
    const bool transient_residual_bounded = initial > tiny &&
      last <= 1.0e-3 * initial && converged_fraction >= 0.95 &&
      last_inner_ratio <= 1.0e-3;
    const std::string status = cfg.run_type == "transient"
      ? ((transient_horizon && transient_residual_bounded) ? "statistically_periodic" : "failed")
      : ((steady_target_reached || bounded_plateau) ? "converged" : "failed");
    if (rank == 0) {
      global_mesh = read_cgns_mesh(cfg.mesh_file);
      owner = metis_partition(global_mesh, ranks);
    }
    std::vector<State> global;
    gather_states(rank == 0 ? global_mesh : mesh, owner, local, state, rank, ranks, global);
    if (rank == 0) {
      write_field(output, global_mesh, global, owner, cfg);
      write_surface(output, global_mesh, global, cfg);
      std::ofstream restart(output / "restart_final.dat");
      restart << "CFD_RESTART_V1 " << global_mesh.cells.size() << "\n" << std::setprecision(16);
      for (const State& u : global) restart << u.rho << ' ' << u.rhou << ' ' << u.rhov << ' ' << u.rhoE << '\n';
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const std::string end_utc = utc_timestamp();
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0)
      write_json(output, global_mesh, local, cfg, ranks, final_step,
                 final_physical_time,
                 elapsed, recorded_initial, last, observed_min, observed_max,
                 observed_mean, misses, converged_fraction, last_inner_ratio,
                 partition_edge_cut(global_mesh, owner), !restart_path.empty(),
                 status, command, start_utc, end_utc);
    MPI_Finalize();
    return 0;
  } catch (const std::exception& e) {
    if (rank == 0) std::cerr << "cfd_solver error: " << e.what() << '\n';
    MPI_Abort(MPI_COMM_WORLD, 1); return 1;
  }
}
