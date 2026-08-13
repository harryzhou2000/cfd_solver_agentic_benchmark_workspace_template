#include "output.hpp"

#include "physics.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace aerofv {
namespace {

constexpr const char *kResidualHeader =
    "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
constexpr const char *kForceHeader =
    "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
constexpr const char *kSurfaceHeader = "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
constexpr const char *kPartitionHeader =
    "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";

[[noreturn]] void fail(const std::string &message) { throw OutputError(message); }

void mpi_check(int status, const char *operation) {
  if (status == MPI_SUCCESS) {
    return;
  }
  char message[MPI_MAX_ERROR_STRING]{};
  int length = 0;
  MPI_Error_string(status, message, &length);
  fail(std::string(operation) + ": " + std::string(message, static_cast<std::size_t>(length)));
}

bool finite_vec(const Vec2 &value) { return std::isfinite(value.x) && std::isfinite(value.y); }

void require_finite(double value, const std::string &where) {
  if (!std::isfinite(value)) {
    fail(where + " must be finite");
  }
}

void require_nonnegative(double value, const std::string &where) {
  require_finite(value, where);
  if (value < 0.0) {
    fail(where + " must be nonnegative");
  }
}

std::string number(double value) {
  require_finite(value, "output value");
  std::ostringstream stream;
  stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
  return stream.str();
}

std::string json_escape(const std::string &value) {
  std::ostringstream escaped;
  for (const unsigned char character : value) {
    switch (character) {
    case '\\': escaped << "\\\\"; break;
    case '"': escaped << "\\\""; break;
    case '\b': escaped << "\\b"; break;
    case '\f': escaped << "\\f"; break;
    case '\n': escaped << "\\n"; break;
    case '\r': escaped << "\\r"; break;
    case '\t': escaped << "\\t"; break;
    default:
      if (character < 0x20U) {
        escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                << static_cast<unsigned int>(character) << std::dec << std::setfill(' ');
      } else {
        escaped << static_cast<char>(character);
      }
    }
  }
  return escaped.str();
}

std::string csv_escape(const std::string &value) {
  if (value.find_first_of(",\"\r\n") == std::string::npos) {
    return value;
  }
  std::string escaped{"\""};
  for (const char character : value) {
    if (character == '"') {
      escaped += "\"\"";
    } else {
      escaped += character;
    }
  }
  return escaped + '"';
}

std::string utc_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream stream;
  stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

void require_nonempty(const std::string &value, const std::string &where) {
  if (value.empty()) {
    fail(where + " must not be empty");
  }
}

void validate_residual(const ResidualRecord &record) {
  if (record.step < 0 || record.inner_iter < 0) {
    fail("residual step and inner_iter must be nonnegative");
  }
  for (const auto value : {record.physical_time, record.cfl, record.dt, record.rho, record.rhou,
                           record.rhov, record.rhoE, record.residual_l2, record.residual_linf}) {
    require_finite(value, "residual record value");
  }
  if (record.residual_l2 < 0.0 || record.residual_linf < 0.0) {
    fail("residual norms must be nonnegative");
  }
}

void validate_force(const ForceRecord &record) {
  if (record.step < 0) {
    fail("force step must be nonnegative");
  }
  for (const auto value : {record.physical_time, record.cl, record.cd, record.cmz, record.pressure_drag,
                           record.viscous_drag, record.pressure_lift, record.viscous_lift}) {
    require_finite(value, "force record value");
  }
}

void validate_surface(const SurfaceRecord &record) {
  for (const auto value : {record.x, record.y, record.nx, record.ny, record.pressure, record.cp,
                           record.cf, record.rho, record.u, record.v, record.mach}) {
    require_finite(value, "surface record value");
  }
  if (record.rho <= 0.0 || record.pressure <= 0.0 || record.mach < 0.0) {
    fail("surface record must have positive density/pressure and nonnegative Mach");
  }
  require_nonempty(record.tag, "surface record tag");
}

template <typename T> void append_bytes(std::vector<char> &destination, const T &value) {
  static_assert(std::is_trivially_copyable_v<T>);
  const auto *source = reinterpret_cast<const char *>(&value);
  destination.insert(destination.end(), source, source + sizeof(T));
}

template <typename T> T consume_bytes(const std::vector<char> &source, std::size_t &offset,
                                      const char *what) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (offset > source.size() || source.size() - offset < sizeof(T)) {
    fail(std::string("truncated final-output payload while reading ") + what);
  }
  T value{};
  std::memcpy(&value, source.data() + offset, sizeof(T));
  offset += sizeof(T);
  return value;
}

void append_string(std::vector<char> &destination, const std::string &value) {
  const std::uint64_t size = value.size();
  append_bytes(destination, size);
  destination.insert(destination.end(), value.begin(), value.end());
}

std::string consume_string(const std::vector<char> &source, std::size_t &offset, const char *what) {
  const std::uint64_t size = consume_bytes<std::uint64_t>(source, offset, what);
  if (size > source.size() - offset) {
    fail(std::string("truncated final-output payload while reading ") + what);
  }
  std::string value(source.data() + offset, source.data() + offset + static_cast<std::size_t>(size));
  offset += static_cast<std::size_t>(size);
  return value;
}

std::vector<char> gather_bytes(const std::vector<char> &local, MPI_Comm communicator, int rank, int ranks) {
  if (local.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    fail("final-output MPI payload exceeds MPI_Gatherv integer count limit");
  }
  const std::uint64_t local_size = local.size();
  std::vector<std::uint64_t> sizes(rank == 0 ? static_cast<std::size_t>(ranks) : 0U);
  mpi_check(MPI_Gather(&local_size, 1, MPI_UINT64_T, rank == 0 ? sizes.data() : nullptr, 1,
                       MPI_UINT64_T, 0, communicator), "MPI_Gather(final payload sizes)");
  std::vector<int> counts;
  std::vector<int> displacements;
  std::vector<char> gathered;
  if (rank == 0) {
    counts.resize(static_cast<std::size_t>(ranks));
    displacements.resize(static_cast<std::size_t>(ranks));
    std::uint64_t total = 0;
    for (int peer = 0; peer < ranks; ++peer) {
      if (sizes[static_cast<std::size_t>(peer)] > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
          total > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) - sizes[static_cast<std::size_t>(peer)]) {
        fail("combined final-output MPI payload exceeds MPI_Gatherv integer count limit");
      }
      counts[static_cast<std::size_t>(peer)] = static_cast<int>(sizes[static_cast<std::size_t>(peer)]);
      displacements[static_cast<std::size_t>(peer)] = static_cast<int>(total);
      total += sizes[static_cast<std::size_t>(peer)];
    }
    gathered.resize(static_cast<std::size_t>(total));
  }
  mpi_check(MPI_Gatherv(local.empty() ? nullptr : local.data(), static_cast<int>(local.size()), MPI_BYTE,
                        rank == 0 ? gathered.data() : nullptr, rank == 0 ? counts.data() : nullptr,
                        rank == 0 ? displacements.data() : nullptr, MPI_BYTE, 0, communicator),
            "MPI_Gatherv(final payload)");
  return gathered;
}

struct GlobalVertex {
  std::int64_t id{-1};
  Vec2 point{};
};

struct GlobalCell {
  std::int64_t id{-1};
  int owner_rank{-1};
  Conservative state{};
  PrimitiveGradient gradient{};
  std::vector<GlobalVertex> vertices;
};

std::vector<char> pack_final_cells(const LocalMesh &mesh, const std::vector<Conservative> &states,
                                   const std::vector<PrimitiveGradient> &gradients, const GasModel &gas,
                                   int rank) {
  if (mesh.owned_cell_count < 0 || static_cast<std::size_t>(mesh.owned_cell_count) > mesh.cells.size() ||
      states.size() < static_cast<std::size_t>(mesh.owned_cell_count) ||
      gradients.size() < static_cast<std::size_t>(mesh.owned_cell_count)) {
    fail("final state/gradient arrays must cover exactly all owned local cells");
  }
  std::vector<char> payload;
  append_bytes<std::uint64_t>(payload, static_cast<std::uint64_t>(mesh.owned_cell_count));
  for (int local_cell = 0; local_cell < mesh.owned_cell_count; ++local_cell) {
    const Cell &cell = mesh.cells[static_cast<std::size_t>(local_cell)];
    const Conservative &state = states[static_cast<std::size_t>(local_cell)];
    const PrimitiveGradient &gradient = gradients[static_cast<std::size_t>(local_cell)];
    if (!physically_valid(state, gas)) {
      fail("owned final conservative state is non-finite or nonphysical");
    }
    for (const Vec2 &component : gradient) {
      if (!finite_vec(component)) {
        fail("owned final primitive gradient is non-finite");
      }
    }
    if ((cell.vertices.size() != 3U && cell.vertices.size() != 4U) ||
        static_cast<std::size_t>(local_cell) >= mesh.global_cell_ids.size()) {
      fail("owned final cell has unsupported topology or missing global id");
    }
    append_bytes<std::int64_t>(payload, mesh.global_cell_ids[static_cast<std::size_t>(local_cell)]);
    append_bytes<int>(payload, rank);
    for (const double value : state) {
      append_bytes<double>(payload, value);
    }
    for (const Vec2 &component : gradient) {
      append_bytes<double>(payload, component.x);
      append_bytes<double>(payload, component.y);
    }
    append_bytes<std::uint32_t>(payload, static_cast<std::uint32_t>(cell.vertices.size()));
    for (const int vertex : cell.vertices) {
      if (vertex < 0 || static_cast<std::size_t>(vertex) >= mesh.vertices.size() ||
          static_cast<std::size_t>(vertex) >= mesh.global_vertex_ids.size()) {
        fail("owned final cell references invalid local vertex");
      }
      const Vec2 &point = mesh.vertices[static_cast<std::size_t>(vertex)];
      if (!finite_vec(point)) {
        fail("owned final cell has non-finite vertex coordinate");
      }
      append_bytes<std::int64_t>(payload, mesh.global_vertex_ids[static_cast<std::size_t>(vertex)]);
      append_bytes<double>(payload, point.x);
      append_bytes<double>(payload, point.y);
    }
  }
  return payload;
}

std::vector<GlobalCell> unpack_final_cells(const std::vector<char> &payload) {
  std::size_t offset = 0;
  std::vector<GlobalCell> cells;
  while (offset < payload.size()) {
    const std::uint64_t cell_count = consume_bytes<std::uint64_t>(payload, offset, "cell count");
    if (cell_count > payload.size()) {
      fail("invalid final-output cell count");
    }
    for (std::uint64_t index = 0; index < cell_count; ++index) {
      GlobalCell cell;
      cell.id = consume_bytes<std::int64_t>(payload, offset, "global cell id");
      cell.owner_rank = consume_bytes<int>(payload, offset, "owner rank");
      for (double &value : cell.state) {
        value = consume_bytes<double>(payload, offset, "conservative state");
      }
      for (Vec2 &component : cell.gradient) {
        component.x = consume_bytes<double>(payload, offset, "primitive gradient");
        component.y = consume_bytes<double>(payload, offset, "primitive gradient");
      }
      const std::uint32_t vertex_count = consume_bytes<std::uint32_t>(payload, offset, "vertex count");
      if (vertex_count != 3U && vertex_count != 4U) {
        fail("final-output cell is not a triangle or quadrilateral");
      }
      cell.vertices.resize(vertex_count);
      for (GlobalVertex &vertex : cell.vertices) {
        vertex.id = consume_bytes<std::int64_t>(payload, offset, "global vertex id");
        vertex.point.x = consume_bytes<double>(payload, offset, "vertex x");
        vertex.point.y = consume_bytes<double>(payload, offset, "vertex y");
      }
      cells.push_back(std::move(cell));
    }
  }
  return cells;
}

std::vector<char> pack_surface_rows(const std::vector<SurfaceRecord> &rows) {
  std::vector<char> payload;
  append_bytes<std::uint64_t>(payload, static_cast<std::uint64_t>(rows.size()));
  for (const SurfaceRecord &row : rows) {
    validate_surface(row);
    for (const double value : {row.x, row.y, row.nx, row.ny, row.pressure, row.cp, row.cf,
                               row.rho, row.u, row.v, row.mach}) {
      append_bytes<double>(payload, value);
    }
    append_string(payload, row.tag);
  }
  return payload;
}

std::vector<SurfaceRecord> unpack_surface_rows(const std::vector<char> &payload) {
  std::size_t offset = 0;
  std::vector<SurfaceRecord> rows;
  while (offset < payload.size()) {
    const std::uint64_t count = consume_bytes<std::uint64_t>(payload, offset, "surface row count");
    if (count > payload.size()) {
      fail("invalid final-output surface row count");
    }
    for (std::uint64_t index = 0; index < count; ++index) {
      SurfaceRecord row;
      for (double *value : {&row.x, &row.y, &row.nx, &row.ny, &row.pressure, &row.cp, &row.cf,
                            &row.rho, &row.u, &row.v, &row.mach}) {
        *value = consume_bytes<double>(payload, offset, "surface value");
      }
      row.tag = consume_string(payload, offset, "surface tag");
      validate_surface(row);
      rows.push_back(std::move(row));
    }
  }
  return rows;
}

void write_vtu(const std::filesystem::path &path, std::vector<GlobalCell> cells, const GasModel &gas) {
  if (cells.empty()) {
    fail("cannot write field_final.vtu with no owned global cells");
  }
  std::sort(cells.begin(), cells.end(), [](const GlobalCell &left, const GlobalCell &right) {
    return left.id < right.id;
  });
  std::map<std::int64_t, Vec2> points;
  std::int64_t previous_cell = -1;
  for (const GlobalCell &cell : cells) {
    if (cell.id < 0 || cell.id == previous_cell || !physically_valid(cell.state, gas)) {
      fail("final global field has duplicate/invalid cell ids or nonphysical states");
    }
    previous_cell = cell.id;
    for (const GlobalVertex &vertex : cell.vertices) {
      if (vertex.id < 0 || !finite_vec(vertex.point)) {
        fail("final global field has invalid vertex data");
      }
      const auto [it, inserted] = points.emplace(vertex.id, vertex.point);
      if (!inserted && (std::abs(it->second.x - vertex.point.x) > 1.0e-12 ||
                        std::abs(it->second.y - vertex.point.y) > 1.0e-12)) {
        fail("final global field has inconsistent duplicate vertex coordinates");
      }
    }
  }
  std::map<std::int64_t, std::size_t> point_index;
  std::size_t index = 0;
  for (const auto &[id, point] : points) {
    (void)point;
    point_index.emplace(id, index++);
  }

  std::ofstream output(path);
  if (!output) {
    fail("could not open final VTU file: " + path.string());
  }
  output << "<?xml version=\"1.0\"?>\n<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n"
         << "  <UnstructuredGrid>\n    <Piece NumberOfPoints=\"" << points.size()
         << "\" NumberOfCells=\"" << cells.size() << "\">\n"
         << "      <Points>\n        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n          ";
  for (const auto &[id, point] : points) {
    (void)id;
    output << number(point.x) << ' ' << number(point.y) << " 0 ";
  }
  output << "\n        </DataArray>\n      </Points>\n      <Cells>\n"
         << "        <DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n          ";
  for (const GlobalCell &cell : cells) {
    for (const GlobalVertex &vertex : cell.vertices) {
      output << point_index.at(vertex.id) << ' ';
    }
  }
  output << "\n        </DataArray>\n        <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n          ";
  std::size_t offset = 0;
  for (const GlobalCell &cell : cells) {
    offset += cell.vertices.size();
    output << offset << ' ';
  }
  output << "\n        </DataArray>\n        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n          ";
  for (const GlobalCell &cell : cells) {
    output << (cell.vertices.size() == 3U ? 5 : 9) << ' ';
  }
  output << "\n        </DataArray>\n      </Cells>\n      <CellData>\n";

  const auto scalar = [&output, &cells](const char *name, const auto &value) {
    output << "        <DataArray type=\"Float64\" Name=\"" << name << "\" format=\"ascii\">\n          ";
    for (const GlobalCell &cell : cells) {
      output << number(value(cell)) << ' ';
    }
    output << "\n        </DataArray>\n";
  };
  scalar("density", [&gas](const GlobalCell &cell) { return conservative_to_primitive(cell.state, gas).rho; });
  output << "        <DataArray type=\"Float64\" Name=\"velocity\" NumberOfComponents=\"3\" format=\"ascii\">\n          ";
  for (const GlobalCell &cell : cells) {
    const Primitive q = conservative_to_primitive(cell.state, gas);
    output << number(q.u) << ' ' << number(q.v) << " 0 ";
  }
  output << "\n        </DataArray>\n";
  scalar("velocity_magnitude", [&gas](const GlobalCell &cell) {
    const Primitive q = conservative_to_primitive(cell.state, gas);
    return std::sqrt(q.u * q.u + q.v * q.v);
  });
  scalar("pressure", [&gas](const GlobalCell &cell) { return conservative_to_primitive(cell.state, gas).p; });
  scalar("mach", [&gas](const GlobalCell &cell) {
    const Primitive q = conservative_to_primitive(cell.state, gas);
    return std::sqrt(q.u * q.u + q.v * q.v) / sound_speed(q, gas);
  });
  scalar("temperature", [&gas](const GlobalCell &cell) { return temperature(conservative_to_primitive(cell.state, gas), gas); });
  scalar("total_energy", [](const GlobalCell &cell) { return cell.state[3]; });
  scalar("vorticity", [](const GlobalCell &cell) { return cell.gradient[2].x - cell.gradient[1].y; });
  output << "        <DataArray type=\"Int32\" Name=\"owner_rank\" format=\"ascii\">\n          ";
  for (const GlobalCell &cell : cells) output << cell.owner_rank << ' ';
  output << "\n        </DataArray>\n        <DataArray type=\"Int64\" Name=\"global_cell_id\" format=\"ascii\">\n          ";
  for (const GlobalCell &cell : cells) output << cell.id << ' ';
  output << "\n        </DataArray>\n      </CellData>\n    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n";
  output.flush();
  if (!output) {
    fail("failed while writing final VTU file: " + path.string());
  }
}

void write_restart(const std::filesystem::path &path, std::vector<GlobalCell> cells) {
  std::sort(cells.begin(), cells.end(), [](const GlobalCell &left, const GlobalCell &right) { return left.id < right.id; });
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    fail("could not open final restart file: " + path.string());
  }
  constexpr std::array<char, 16> magic{{'A', 'E', 'R', 'O', 'F', 'V', '_', 'R', 'E', 'S', 'T', 'A', 'R', 'T', '\0', '\0'}};
  constexpr std::uint32_t version = 1;
  const std::uint64_t count = cells.size();
  output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
  output.write(reinterpret_cast<const char *>(&version), sizeof(version));
  output.write(reinterpret_cast<const char *>(&count), sizeof(count));
  for (const GlobalCell &cell : cells) {
    output.write(reinterpret_cast<const char *>(&cell.id), sizeof(cell.id));
    output.write(reinterpret_cast<const char *>(cell.state.data()), static_cast<std::streamsize>(sizeof(double) * cell.state.size()));
  }
  output.flush();
  if (!output) {
    fail("failed while writing final restart file: " + path.string());
  }
}

void write_surface(const std::filesystem::path &path, const std::vector<SurfaceRecord> &rows) {
  if (rows.empty()) {
    fail("cannot write surface.csv with no wall rows");
  }
  std::ofstream output(path);
  if (!output) {
    fail("could not open surface.csv: " + path.string());
  }
  output << kSurfaceHeader;
  for (const SurfaceRecord &row : rows) {
    validate_surface(row);
    output << number(row.x) << ',' << number(row.y) << ',' << number(row.nx) << ',' << number(row.ny) << ','
           << number(row.pressure) << ',' << number(row.cp) << ',' << number(row.cf) << ',' << number(row.rho) << ','
           << number(row.u) << ',' << number(row.v) << ',' << number(row.mach) << ',' << csv_escape(row.tag) << '\n';
  }
  output.flush();
  if (!output) {
    fail("failed while writing surface.csv");
  }
}

bool is_final_status(const std::string &status) {
  return status == "converged" || status == "statistically_periodic";
}

void write_json_pair(std::ofstream &output, const char *key, const std::string &value, bool comma = true) {
  output << "  \"" << key << "\": \"" << json_escape(value) << "\"" << (comma ? "," : "") << '\n';
}

void write_json_number(std::ofstream &output, const char *key, double value, bool comma = true) {
  output << "  \"" << key << "\": " << number(value) << (comma ? "," : "") << '\n';
}

void write_json_integer(std::ofstream &output, const char *key, std::int64_t value, bool comma = true) {
  output << "  \"" << key << "\": " << value << (comma ? "," : "") << '\n';
}

void write_metadata(const std::filesystem::path &path, const CaseConfig &config, const LocalMesh &mesh,
                    int ranks, const MethodMetadata &method, const RunSummary &summary,
                    const std::string &start_time, const std::string &end_time) {
  if (!summary.completed || !is_final_status(summary.convergence_status)) {
    fail("final metadata may only be written for completed converged/statistically_periodic runs");
  }
  if (config.run_control.type == RunType::transient && !method.true_bdf2_inner_loop) {
    fail("transient metadata requires true_bdf2_inner_loop=true");
  }
  for (const std::string *field : {&method.solver_name, &method.solver_version, &method.partitioner,
                                   &method.halo_exchange, &method.inviscid_flux, &method.viscous_flux,
                                   &method.time_integrator, &method.implicit_solver, &method.reconstruction,
                                   &method.limiter, &method.positivity_preservation,
                                   &method.wall_boundary_output_semantics}) {
    require_nonempty(*field, "method metadata field");
  }
  if (method.observed_min_inner_iterations < 0 || method.observed_max_inner_iterations < method.observed_min_inner_iterations ||
      method.typical_inner_iterations < 0 || method.inner_target_misses < 0) {
    fail("method metadata has invalid inner-iteration statistics");
  }
  for (const double value : {method.observed_mean_inner_iterations, method.inner_target_converged_fraction,
                             method.last_inner_residual_ratio,
                             method.steady_recovery_cfl_cap,
                             method.steady_recovery_relaxation_cap}) {
    require_nonnegative(value, "method metadata statistic");
  }
  if (method.steady_recovery_activation_step < 0) {
    fail("method metadata has invalid steady recovery activation step");
  }
  if (method.inner_target_converged_fraction > 1.0) {
    fail("inner_target_converged_fraction cannot exceed one");
  }

  std::ofstream output(path);
  if (!output) {
    fail("could not open metadata.json");
  }
  output << "{\n";
  write_json_pair(output, "case_id", config.case_id);
  write_json_pair(output, "solver_name", method.solver_name);
  write_json_pair(output, "solver_version", method.solver_version);
  if (method.git_revision) {
    write_json_pair(output, "git_revision", *method.git_revision);
  } else {
    output << "  \"git_revision\": null,\n";
  }
  write_json_integer(output, "mpi_ranks", ranks);
  write_json_pair(output, "mesh_file", config.mesh.file.string());
  write_json_integer(output, "num_cells_global", mesh.global_cell_count);
  write_json_integer(output, "num_faces_global", mesh.global_face_count);
  write_json_integer(output, "num_cells_owned_local", mesh.owned_cell_count);
  write_json_integer(output, "num_cells_ghost_local", static_cast<std::int64_t>(mesh.cells.size()) - mesh.owned_cell_count);
  write_json_pair(output, "partitioner", method.partitioner);
  write_json_integer(output, "partition_edge_cut", mesh.edge_cut);
  write_json_pair(output, "halo_exchange", method.halo_exchange);
  output << "  \"full_state_replication_during_iterations\": false,\n"
         << "  \"full_mesh_replication_during_iterations\": false,\n";
  write_json_pair(output, "equation_set", "compressible_navier_stokes_2d");
  write_json_pair(output, "inviscid_flux", method.inviscid_flux);
  if (method.entropy_fix) {
    write_json_pair(output, "entropy_fix", *method.entropy_fix);
  } else {
    output << "  \"entropy_fix\": null,\n";
  }
  write_json_pair(output, "viscous_flux", method.viscous_flux);
  write_json_pair(output, "time_integrator", method.time_integrator);
  write_json_pair(output, "implicit_solver", method.implicit_solver);
  write_json_pair(output, "reconstruction", method.reconstruction);
  write_json_pair(output, "limiter", method.limiter);
  write_json_integer(output, "spatial_order_claimed", config.numerics.spatial_order);
  write_json_pair(output, "positivity_preservation", method.positivity_preservation);
  write_json_pair(output, "wall_boundary_output_semantics", method.wall_boundary_output_semantics);
  output << "  \"true_bdf2_inner_loop\": " << (method.true_bdf2_inner_loop ? "true" : "false") << ",\n";
  write_json_integer(output, "typical_inner_iterations", method.typical_inner_iterations);
  write_json_integer(output, "min_inner_iterations", config.run_control.min_inner_iterations);
  write_json_integer(output, "max_inner_iterations", config.run_control.max_inner_iterations);
  write_json_integer(output, "observed_min_inner_iterations", method.observed_min_inner_iterations);
  write_json_integer(output, "observed_max_inner_iterations", method.observed_max_inner_iterations);
  write_json_number(output, "observed_mean_inner_iterations", method.observed_mean_inner_iterations);
  write_json_number(output, "inner_residual_reduction_target", config.run_control.inner_residual_reduction_target);
  write_json_integer(output, "inner_target_misses", method.inner_target_misses);
  write_json_number(output, "inner_target_converged_fraction", method.inner_target_converged_fraction);
  write_json_number(output, "last_inner_residual_ratio", method.last_inner_residual_ratio);
  output << "  \"steady_recovery_activated\": "
         << (method.steady_recovery_activated ? "true" : "false") << ",\n";
  write_json_integer(output, "steady_recovery_activation_step",
                     method.steady_recovery_activation_step);
  write_json_number(output, "steady_recovery_cfl_cap",
                    method.steady_recovery_cfl_cap);
  write_json_number(output, "steady_recovery_relaxation_cap",
                    method.steady_recovery_relaxation_cap);
  write_json_pair(output, "start_time_utc", start_time);
  write_json_pair(output, "end_time_utc", end_time);
  output << "  \"completed\": true,\n";
  write_json_pair(output, "convergence_status", summary.convergence_status, false);
  output << "}\n";
  output.flush();
  if (!output) {
    fail("failed while writing metadata.json");
  }
}

void write_run_status(const std::filesystem::path &path, const CaseConfig &config, int ranks,
                      const RunSummary &summary) {
  require_nonempty(summary.command, "run summary command");
  require_nonempty(summary.notes, "run summary notes");
  if (!summary.completed || !is_final_status(summary.convergence_status) || summary.final_step <= 0) {
    fail("run summary is not a completed final submission");
  }
  for (const double value : {summary.wall_time_seconds, summary.final_physical_time, summary.residual_reduction_orders}) {
    require_nonnegative(value, "run summary value");
  }
  std::ofstream output(path);
  if (!output) {
    fail("could not open run_status.json");
  }
  output << "{\n";
  write_json_pair(output, "case_id", config.case_id);
  write_json_pair(output, "command", summary.command);
  write_json_integer(output, "mpi_ranks", ranks);
  write_json_number(output, "wall_time_seconds", summary.wall_time_seconds);
  write_json_integer(output, "final_step", summary.final_step);
  write_json_number(output, "final_physical_time", summary.final_physical_time);
  write_json_pair(output, "convergence_status", summary.convergence_status);
  write_json_number(output, "residual_reduction_orders", summary.residual_reduction_orders);
  write_json_pair(output, "notes", summary.notes, false);
  output << "}\n";
  output.flush();
  if (!output) {
    fail("failed while writing run_status.json");
  }
}

} // namespace

OutputWriter::OutputWriter(const CaseConfig &config, const LocalMesh &local_mesh,
                           std::filesystem::path output_dir, MPI_Comm communicator,
                           MethodMetadata method)
    : config_(config), local_mesh_(local_mesh), output_dir_(std::move(output_dir)),
      communicator_(communicator), method_(std::move(method)) {
  if (output_dir_.empty() || communicator_ == MPI_COMM_NULL) {
    throw OutputError("output directory and MPI communicator must be valid");
  }
  mpi_check(MPI_Comm_rank(communicator_, &rank_), "MPI_Comm_rank(output)");
  mpi_check(MPI_Comm_size(communicator_, &ranks_), "MPI_Comm_size(output)");
}

OutputWriter::~OutputWriter() = default;

void OutputWriter::prepare() {
  if (prepared_) {
    fail("OutputWriter::prepare may only be called once");
  }
  if (rank_ == 0) {
    std::error_code error;
    std::filesystem::create_directories(output_dir_, error);
    if (error) {
      fail("could not create output directory '" + output_dir_.string() + "': " + error.message());
    }
    residuals_.open(output_dir_ / "residuals.csv", std::ios::trunc);
    forces_.open(output_dir_ / "forces.csv", std::ios::trunc);
    stdout_log_.open(output_dir_ / "stdout.log", std::ios::trunc);
    if (!residuals_ || !forces_ || !stdout_log_) {
      fail("could not create required rank-zero history/log files");
    }
    residuals_ << kResidualHeader << std::flush;
    forces_ << kForceHeader << std::flush;
    stdout_log_ << "AeroFV solver output log\n" << std::flush;
    start_time_utc_ = utc_now();
  }
  mpi_check(MPI_Barrier(communicator_), "MPI_Barrier(output preparation)");
  prepared_ = true;
}

void OutputWriter::append_residual(const ResidualRecord &record) {
  if (!prepared_) {
    fail("prepare must be called before append_residual");
  }
  validate_residual(record);
  if (rank_ != 0) {
    return;
  }
  if (last_residual_ && record.step < last_residual_->step) {
    fail("residual history steps must not decrease");
  }
  residuals_ << record.step << ',' << number(record.physical_time) << ',' << record.inner_iter << ','
             << number(record.cfl) << ',' << number(record.dt) << ',' << number(record.rho) << ','
             << number(record.rhou) << ',' << number(record.rhov) << ',' << number(record.rhoE) << ','
             << number(record.residual_l2) << ',' << number(record.residual_linf) << '\n' << std::flush;
  if (!residuals_) {
    fail("failed while appending residuals.csv");
  }
  last_residual_ = record;
}

void OutputWriter::append_force(const ForceRecord &record) {
  if (!prepared_) {
    fail("prepare must be called before append_force");
  }
  validate_force(record);
  if (rank_ != 0) {
    return;
  }
  if (last_force_ && record.step < last_force_->step) {
    fail("force history steps must not decrease");
  }
  forces_ << record.step << ',' << number(record.physical_time) << ',' << number(record.cl) << ','
          << number(record.cd) << ',' << number(record.cmz) << ',' << number(record.pressure_drag) << ','
          << number(record.viscous_drag) << ',' << number(record.pressure_lift) << ','
          << number(record.viscous_lift) << '\n' << std::flush;
  if (!forces_) {
    fail("failed while appending forces.csv");
  }
  last_force_ = record;
}

void OutputWriter::log(const std::string &message) {
  if (!prepared_) {
    fail("prepare must be called before log");
  }
  if (rank_ == 0) {
    stdout_log_ << message << '\n' << std::flush;
    if (!stdout_log_) {
      fail("failed while appending stdout.log");
    }
  }
}

void OutputWriter::update_method_metadata(MethodMetadata method) {
  if (!prepared_) {
    fail("prepare must be called before update_method_metadata");
  }
  method_ = std::move(method);
}

void OutputWriter::write_partition_diagnostics() {
  if (!prepared_) {
    fail("prepare must be called before write_partition_diagnostics");
  }
  if (partition_diagnostics_written_) {
    fail("partition diagnostics may only be written once");
  }
  std::int64_t boundary_faces = 0;
  for (const Face &face : local_mesh_.faces) {
    if (face.right_cell < 0) {
      ++boundary_faces;
    }
  }
  std::int64_t send_cells = 0;
  std::int64_t receive_cells = 0;
  for (const NeighborExchange &exchange : local_mesh_.exchanges) {
    send_cells += static_cast<std::int64_t>(exchange.send_owned_local.size());
    receive_cells += static_cast<std::int64_t>(exchange.receive_ghost_local.size());
  }
  const std::array<std::int64_t, 7> local{{rank_, local_mesh_.owned_cell_count,
                                            static_cast<std::int64_t>(local_mesh_.cells.size()) - local_mesh_.owned_cell_count,
                                            boundary_faces, static_cast<std::int64_t>(local_mesh_.exchanges.size()),
                                            send_cells, receive_cells}};
  std::vector<std::int64_t> gathered(rank_ == 0 ? static_cast<std::size_t>(ranks_) * local.size() : 0U);
  mpi_check(MPI_Gather(local.data(), static_cast<int>(local.size()), MPI_INT64_T,
                       rank_ == 0 ? gathered.data() : nullptr, static_cast<int>(local.size()), MPI_INT64_T,
                       0, communicator_), "MPI_Gather(partition diagnostics)");
  std::vector<std::int64_t> local_neighbors(static_cast<std::size_t>(ranks_), -1);
  for (const NeighborExchange &exchange : local_mesh_.exchanges) {
    if (exchange.rank < 0 || exchange.rank >= ranks_) {
      fail("partition exchange references an invalid rank");
    }
    local_neighbors[static_cast<std::size_t>(exchange.rank)] = exchange.rank;
  }
  std::vector<std::int64_t> gathered_neighbors(
      rank_ == 0 ? static_cast<std::size_t>(ranks_) * static_cast<std::size_t>(ranks_) : 0U);
  mpi_check(MPI_Gather(local_neighbors.data(), ranks_, MPI_INT64_T,
                       rank_ == 0 ? gathered_neighbors.data() : nullptr, ranks_, MPI_INT64_T,
                       0, communicator_), "MPI_Gather(partition neighbor ranks)");
  if (rank_ == 0) {
    std::ofstream output(output_dir_ / "partition_diagnostics.csv", std::ios::trunc);
    if (!output) {
      fail("could not create partition_diagnostics.csv");
    }
    output << kPartitionHeader;
    for (int peer = 0; peer < ranks_; ++peer) {
      const std::size_t base = static_cast<std::size_t>(peer) * local.size();
      const auto neighbor_ranks = [&]() {
        std::ostringstream names;
        bool first = true;
        const std::size_t neighbor_base = static_cast<std::size_t>(peer) * static_cast<std::size_t>(ranks_);
        for (int candidate = 0; candidate < ranks_; ++candidate) {
          if (gathered_neighbors[neighbor_base + static_cast<std::size_t>(candidate)] < 0) {
            continue;
          }
          if (!first) names << ';';
          names << candidate;
          first = false;
        }
        return names.str();
      }();
      output << gathered[base] << ',' << gathered[base + 1] << ',' << gathered[base + 2] << ','
             << gathered[base + 3] << ',' << gathered[base + 4] << ',' << csv_escape(neighbor_ranks) << ','
             << gathered[base + 5] << ',' << gathered[base + 6] << '\n';
    }
    output.flush();
    if (!output) {
      fail("failed while writing partition_diagnostics.csv");
    }
  }
  partition_diagnostics_written_ = true;
}

void OutputWriter::write_transient_field(const std::vector<Conservative> &local_states,
                                         const std::vector<PrimitiveGradient> &local_gradients,
                                         std::int64_t step, double physical_time) {
  if (!prepared_) {
    fail("prepare must be called before write_transient_field");
  }
  if (step < 0) {
    fail("transient field step must be nonnegative");
  }
  require_nonnegative(physical_time, "transient field physical_time");
  const std::vector<char> cells = gather_bytes(pack_final_cells(local_mesh_, local_states, local_gradients,
                                                                  config_.gas, rank_), communicator_, rank_, ranks_);
  if (rank_ != 0) {
    return;
  }
  std::vector<GlobalCell> global_cells = unpack_final_cells(cells);
  if (static_cast<std::int64_t>(global_cells.size()) != local_mesh_.global_cell_count) {
    fail("transient gathered owned-cell count does not match global mesh cell count");
  }
  std::ostringstream filename;
  filename << "field_t" << std::setw(8) << std::setfill('0') << step << ".vtu";
  write_vtu(output_dir_ / filename.str(), std::move(global_cells), config_.gas);
  log("Wrote transient field at t=" + number(physical_time) + ", step=" + std::to_string(step));
}

void OutputWriter::write_final(const std::vector<Conservative> &local_states,
                               const std::vector<PrimitiveGradient> &local_gradients,
                               const std::vector<SurfaceRecord> &local_surface_rows,
                               const RunSummary &summary) {
  if (!prepared_) {
    fail("prepare must be called before write_final");
  }
  if (!partition_diagnostics_written_) {
    fail("write_partition_diagnostics must precede final output");
  }
  const std::vector<char> cells = gather_bytes(pack_final_cells(local_mesh_, local_states, local_gradients,
                                                                  config_.gas, rank_), communicator_, rank_, ranks_);
  const std::vector<char> surface = gather_bytes(pack_surface_rows(local_surface_rows), communicator_, rank_, ranks_);
  if (rank_ != 0) {
    return;
  }
  if (!last_force_ || !last_residual_) {
    fail("final output requires at least one residual and force record");
  }
  if (last_force_->step != summary.final_step || last_residual_->step > summary.final_step ||
      std::abs(last_force_->physical_time - summary.final_physical_time) > 1.0e-10 *
          std::max(1.0, std::abs(summary.final_physical_time))) {
    fail("final force/residual histories do not correspond to the requested final state");
  }
  std::vector<GlobalCell> global_cells = unpack_final_cells(cells);
  if (static_cast<std::int64_t>(global_cells.size()) != local_mesh_.global_cell_count) {
    fail("final gathered owned-cell count does not match global mesh cell count");
  }
  const std::vector<SurfaceRecord> surface_rows = unpack_surface_rows(surface);
  write_vtu(output_dir_ / "field_final.vtu", global_cells, config_.gas);
  write_restart(output_dir_ / "restart_final.bin", global_cells);
  write_surface(output_dir_ / "surface.csv", surface_rows);
  const std::string end_time = summary.end_time_utc.empty() ? utc_now() : summary.end_time_utc;
  write_metadata(output_dir_ / "metadata.json", config_, local_mesh_, ranks_, method_, summary,
                 start_time_utc_, end_time);
  write_run_status(output_dir_ / "run_status.json", config_, ranks_, summary);
  log("Final output artifacts written successfully.");
}

} // namespace aerofv
