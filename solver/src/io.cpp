#include "cfd/io.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace cfd {
namespace {
constexpr const char* kResidualHeader = "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
constexpr const char* kForceHeader = "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
constexpr const char* kSurfaceHeader = "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
constexpr const char kRestartMagic[] = "CFDRST02";
constexpr int kRestartBinaryVersion = 2;
constexpr const char* kRestartManifestFormat = "cfd_rank_local_restart_v2";

using Json = nlohmann::json;

std::string esc(const std::string& value) {
  std::string out; out.reserve(value.size() + 8);
  for (const char c : value) { if (c == '"' || c == '\\') out += '\\'; if (c == '\n') out += "\\n"; else out += c; }
  return out;
}
std::string xml(const std::string& value) {
  std::string out; for (char c : value) { if (c == '&') out += "&amp;"; else if (c == '<') out += "&lt;"; else if (c == '"') out += "&quot;"; else out += c; } return out;
}
template <class T> void write_pod(std::ofstream& out, const T& value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); }
template <class T> void read_pod(std::ifstream& in, T& value) { in.read(reinterpret_cast<char*>(&value), sizeof(value)); if (!in) throw std::runtime_error("truncated restart file"); }
std::filesystem::path rank_path(const std::filesystem::path& dir, const char* stem, int rank, const char* extension) {
  std::ostringstream s; s << stem << ".rank" << std::setw(5) << std::setfill('0') << rank << extension; return dir / s.str();
}
void require_local_state(const LocalMesh& mesh, const std::vector<double>& state) {
  if (state.size() < static_cast<size_t>(mesh.owned_cell_count) * 4 || state.size() % 4 != 0) throw std::invalid_argument("output state must be local-cell-major with four conservative components");
}
Conserved state_at(const std::vector<double>& state, int cell) { const size_t base = static_cast<size_t>(cell) * 4; return {state[base], state[base + 1], state[base + 2], state[base + 3]}; }
void ensure_parent(const std::filesystem::path& dir) { std::filesystem::create_directories(dir); }
void append_header_if_empty(const std::filesystem::path& file, const char* header) {
  const bool fresh = !std::filesystem::exists(file) || std::filesystem::file_size(file) == 0;
  std::ofstream out(file, std::ios::app); if (!out) throw std::runtime_error("cannot write " + file.string()); if (fresh) out << header;
}

Json provenance_json(const RestartProvenance& provenance) {
  return Json{{"restarted", provenance.restarted},
              {"chain_depth", provenance.chain_depth},
              {"parent_manifest", provenance.parent_manifest.empty()
                                      ? Json(nullptr)
                                      : Json(provenance.parent_manifest)},
              {"cumulative_residual_trace", provenance.cumulative_residual_trace},
              {"compatibility_signature", provenance.compatibility_signature},
              {"segment_start_step", provenance.segment_start_step},
              {"segment_start_physical_time", provenance.segment_start_physical_time},
              {"residual_reference_l2", provenance.residual_reference_l2},
              {"residual_reference_linf", provenance.residual_reference_linf},
              {"segment_start_residual_l2", provenance.segment_start_residual_l2},
              {"segment_start_residual_linf", provenance.segment_start_residual_linf},
              {"checkpoint_step", provenance.checkpoint_step},
              {"checkpoint_physical_time", provenance.checkpoint_physical_time},
              {"checkpoint_residual_l2", provenance.checkpoint_residual_l2},
              {"checkpoint_residual_linf", provenance.checkpoint_residual_linf}};
}

bool approximately_equal(const double first, const double second) {
  return std::isfinite(first) && std::isfinite(second) &&
         std::abs(first - second) <= 1.0e-10 * std::max({1.0, std::abs(first), std::abs(second)});
}

void write_residual_row(std::ostream& out, const ResidualRecord& row) {
  out << std::setprecision(17) << row.step << ',' << row.physical_time << ',' << row.inner_iter << ',' << row.cfl
      << ',' << row.dt << ',' << row.components[0] << ',' << row.components[1] << ',' << row.components[2] << ','
      << row.components[3] << ',' << row.l2 << ',' << row.linf << '\n';
}

struct ResidualTraceTail {
  bool present{false};
  int step{0};
  double physical_time{0.0};
  double l2{0.0};
  double linf{0.0};
};

ResidualTraceTail validate_and_copy_parent_trace(const std::filesystem::path& source, std::ostream& destination) {
  std::ifstream input(source);
  if (!input) {
    throw std::runtime_error("cannot read parent cumulative residual trace " + source.string());
  }
  std::string header;
  if (!std::getline(input, header) || header != std::string(kResidualHeader).substr(0, std::string(kResidualHeader).size() - 1U)) {
    throw std::runtime_error("parent cumulative residual trace has an unexpected header");
  }
  destination << kResidualHeader;
  ResidualTraceTail tail;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) {
      throw std::runtime_error("parent cumulative residual trace contains an empty row");
    }
    std::array<std::string, 11> values{};
    std::istringstream row(line);
    for (std::size_t column = 0; column < values.size(); ++column) {
      if (!std::getline(row, values[column], ',')) {
        throw std::runtime_error("parent cumulative residual trace has a truncated row");
      }
    }
    std::string extra;
    if (std::getline(row, extra, ',')) {
      throw std::runtime_error("parent cumulative residual trace has too many columns");
    }
    try {
      const int step = std::stoi(values[0]);
      const double physical_time = std::stod(values[1]);
      const double l2 = std::stod(values[9]);
      const double linf = std::stod(values[10]);
      if (step < 0 || !std::isfinite(physical_time) || physical_time < 0.0 || !std::isfinite(l2) || l2 <= 0.0 ||
          !std::isfinite(linf) || linf < 0.0 || (tail.present && step < tail.step)) {
        throw std::runtime_error("parent cumulative residual trace has an invalid row");
      }
      tail = {true, step, physical_time, l2, linf};
    } catch (const std::exception&) {
      throw std::runtime_error("parent cumulative residual trace contains non-numeric data");
    }
    destination << line << '\n';
  }
  if (!tail.present) {
    throw std::runtime_error("parent cumulative residual trace has no data rows");
  }
  return tail;
}

const Json& required_member(const Json& object, const char* key) {
  if (!object.contains(key)) {
    throw std::runtime_error(std::string("restart manifest is missing required key: ") + key);
  }
  return object.at(key);
}

double finite_nonnegative(const Json& object, const char* key) {
  const double value = required_member(object, key).get<double>();
  if (!std::isfinite(value) || value < 0.0) {
    throw std::runtime_error(std::string("restart manifest key must be finite and nonnegative: ") + key);
  }
  return value;
}

int nonnegative_integer(const Json& object, const char* key) {
  const int value = required_member(object, key).get<int>();
  if (value < 0) {
    throw std::runtime_error(std::string("restart manifest key must be nonnegative: ") + key);
  }
  return value;
}
}  // namespace

std::string restart_compatibility_signature(const CaseConfig& config, const LocalMesh& mesh) {
  std::ostringstream signature;
  signature << std::setprecision(std::numeric_limits<double>::max_digits10)
            << "case_id=" << config.case_id
            << "|mesh_cells=" << mesh.global_cell_count
            << "|mesh_faces=" << mesh.global_face_count
            << "|physics=" << to_string(config.physics_mode)
            << "|reynolds=" << config.reynolds
            << "|gamma=" << config.gas.gamma
            << "|gas_constant=" << config.gas.gas_constant
            << "|prandtl=" << config.gas.prandtl
            << "|mach=" << config.freestream.mach
            << "|aoa=" << config.freestream.aoa_degrees
            << "|rho=" << config.freestream.rho
            << "|velocity=" << config.freestream.velocity_magnitude
            << "|pressure=" << config.freestream.pressure
            << "|reference_length=" << config.reference.length
            << "|reference_area=" << config.reference.area
            << "|reference_moment_x=" << config.reference.moment_center[0]
            << "|reference_moment_y=" << config.reference.moment_center[1]
            << "|reference_reynolds_length=" << config.reference.reynolds_length
            << "|run_type=" << to_string(config.run.type)
            << "|time_integrator=" << config.run.time_integrator
            << "|inviscid_flux=" << config.run.inviscid_flux
            << "|rusanov_dissipation_scale=" << config.run.rusanov_dissipation_scale
            << "|shock_sensor_dissipation=" << config.run.shock_sensor_dissipation
            << "|shock_sensor_threshold=" << config.run.shock_sensor_threshold
            << "|steady_relaxation=" << config.run.steady_relaxation
            << "|reconstruction_gradient_scale=" << config.run.reconstruction_gradient_scale
            << "|steady_newton_only=" << config.run.steady_newton_only
            << "|cfl_initial=" << config.run.cfl_initial
            << "|cfl_max=" << config.run.cfl_max
            << "|pseudo_cfl_ramp_steps=" << config.run.pseudo_cfl_ramp_steps
            << "|min_inner_iterations=" << config.run.min_inner_iterations
            << "|max_inner_iterations=" << config.run.max_inner_iterations
            << "|inner_residual_reduction_target=" << config.run.inner_residual_reduction_target
            << "|time_step=" << config.run.time_step
            << "|final_time=" << config.run.final_time;
  for (const auto& [name, boundary] : config.boundary_conditions) {
    signature << "|boundary:" << name << '=' << to_string(boundary);
  }
  return signature.str();
}

OutputWriter::OutputWriter(std::filesystem::path output_dir, MPI_Comm comm) : output_dir_(std::move(output_dir)), comm_(comm) {
  if (comm_ == MPI_COMM_NULL) throw std::invalid_argument("OutputWriter requires a valid MPI communicator");
  MPI_Comm_rank(comm_, &rank_); MPI_Comm_size(comm_, &size_); ensure_parent(output_dir_);
}

void OutputWriter::write_metadata(const CaseConfig& c, const LocalMesh& mesh, const OutputMetadata& m) const {
  if (rank_ != 0) return;
  std::ofstream out(output_dir_ / "metadata.json"); if (!out) throw std::runtime_error("cannot write metadata.json");
  out << std::setprecision(17) << std::boolalpha << "{\n"
      << "  \"case_id\": \"" << esc(c.case_id) << "\",\n  \"solver_name\": \"" << esc(m.solver_name) << "\",\n  \"solver_version\": \"" << esc(m.solver_version) << "\",\n"
      << "  \"git_revision\": " << (m.git_revision.empty() ? "null" : "\"" + esc(m.git_revision) + "\"") << ",\n"
      << "  \"mpi_ranks\": " << size_ << ",\n  \"mesh_file\": \"" << esc(c.mesh_file.string()) << "\",\n  \"num_cells_global\": " << mesh.global_cell_count << ",\n  \"num_faces_global\": " << mesh.global_face_count << ",\n"
      << "  \"num_cells_owned_local\": " << mesh.owned_cell_count << ",\n  \"num_cells_ghost_local\": " << mesh.ghost_cell_count() << ",\n  \"partitioner\": \"" << esc(m.partitioner) << "\",\n  \"partition_edge_cut\": " << mesh.partition_edge_cut << ",\n"
      << "  \"halo_exchange\": \"" << esc(m.halo_exchange) << "\",\n  \"full_state_replication_during_iterations\": false,\n  \"full_mesh_replication_during_iterations\": false,\n  \"equation_set\": \"compressible_navier_stokes_2d\",\n"
      << "  \"inviscid_flux\": \"" << esc(m.inviscid_flux) << "\",\n  \"entropy_fix\": " << (m.entropy_fix.empty() ? "null" : "\"" + esc(m.entropy_fix) + "\"") << ",\n  \"viscous_flux\": \"" << esc(m.viscous_flux) << "\",\n"
      << "  \"time_integrator\": \"" << esc(c.run.time_integrator) << "\",\n  \"implicit_solver\": \"" << esc(m.implicit_solver) << "\",\n  \"reconstruction\": \"" << esc(m.reconstruction) << "\",\n  \"limiter\": \"" << esc(m.limiter) << "\",\n"
      << "  \"spatial_order_claimed\": " << m.spatial_order_claimed << ",\n  \"positivity_preservation\": \"" << esc(m.positivity_preservation) << "\",\n  \"wall_boundary_output_semantics\": \"" << esc(m.wall_boundary_output_semantics) << "\",\n"
      << "  \"true_bdf2_inner_loop\": " << m.true_bdf2_inner_loop << ",\n  \"typical_inner_iterations\": " << m.typical_inner_iterations << ",\n  \"min_inner_iterations\": " << m.min_inner_iterations << ",\n  \"max_inner_iterations\": " << m.max_inner_iterations << ",\n"
      << "  \"observed_min_inner_iterations\": " << m.observed_min_inner_iterations << ",\n  \"observed_max_inner_iterations\": " << m.observed_max_inner_iterations << ",\n  \"inner_residual_reduction_target\": " << m.inner_residual_reduction_target << ",\n"
      << "  \"inner_target_misses\": " << m.inner_target_misses << ",\n  \"inner_target_converged_fraction\": " << m.inner_target_converged_fraction << ",\n  \"last_inner_residual_ratio\": " << m.last_inner_residual_ratio << ",\n"
      << "  \"run_control\": {\n"
      << "    \"type\": \"" << esc(to_string(c.run.type)) << "\",\n"
      << "    \"max_steps\": " << c.run.max_steps << ",\n"
      << "    \"residual_reduction_target\": " << c.run.residual_reduction_target << ",\n"
      << "    \"cfl_initial\": " << c.run.cfl_initial << ",\n"
      << "    \"cfl_max\": " << c.run.cfl_max << ",\n"
      << "    \"pseudo_cfl_ramp_steps\": " << c.run.pseudo_cfl_ramp_steps << ",\n"
      << "    \"min_inner_iterations\": " << c.run.min_inner_iterations << ",\n"
      << "    \"max_inner_iterations\": " << c.run.max_inner_iterations << ",\n"
      << "    \"inner_residual_reduction_target\": " << c.run.inner_residual_reduction_target << ",\n"
      << "    \"time_step\": ";
  if (c.run.type == RunType::Transient) {
    out << c.run.time_step;
  } else {
    out << "null";
  }
  out << ",\n"
      << "    \"final_time\": ";
  if (c.run.type == RunType::Transient) {
    out << c.run.final_time;
  } else {
    out << "null";
  }
  out << "\n"
      << "  },\n"
      << "  \"actual_controls\": {\n"
      << "    \"inviscid_flux\": \"" << esc(c.run.inviscid_flux) << "\",\n"
      << "    \"rusanov_dissipation_scale\": " << c.run.rusanov_dissipation_scale << ",\n"
      << "    \"reconstruction_gradient_scale\": " << c.run.reconstruction_gradient_scale << ",\n"
      << "    \"shock_sensor_dissipation\": " << c.run.shock_sensor_dissipation << ",\n"
      << "    \"shock_sensor_threshold\": " << c.run.shock_sensor_threshold << ",\n"
      << "    \"steady_relaxation\": " << c.run.steady_relaxation << ",\n"
      << "    \"steady_newton_only\": " << c.run.steady_newton_only << ",\n"
      << "    \"observed_cfl_min\": " << m.observed_cfl_min << ",\n"
      << "    \"observed_cfl_max\": " << m.observed_cfl_max << ",\n"
      << "    \"termination_reason\": \"" << esc(m.termination_reason) << "\"\n"
      << "  },\n"
      << "  \"restart_provenance\": " << provenance_json(m.restart).dump() << ",\n"
      << "  \"start_time_utc\": \"" << esc(m.start_time_utc) << "\",\n  \"end_time_utc\": \"" << esc(m.end_time_utc) << "\",\n  \"completed\": " << m.completed << ",\n  \"convergence_status\": \"" << esc(m.convergence_status) << "\"\n}\n";
}

void OutputWriter::write_partition_diagnostics(const LocalMesh& mesh) const {
  const int values[] = {mesh.rank, mesh.owned_cell_count, mesh.ghost_cell_count(), static_cast<int>(std::count_if(mesh.faces.begin(), mesh.faces.end(), [](const LocalFace& f) { return f.right_cell < 0; })), static_cast<int>(mesh.neighbors.size())};
  std::ostringstream neighbors, send, recv;
  for (size_t i = 0; i < mesh.neighbors.size(); ++i) { if (i) { neighbors << ';'; send << ';'; recv << ';'; } neighbors << mesh.neighbors[i].rank; send << mesh.neighbors[i].send_owned_local_indices.size(); recv << mesh.neighbors[i].recv_ghost_local_indices.size(); }
  const std::string text = std::to_string(values[0]) + "," + std::to_string(values[1]) + "," + std::to_string(values[2]) + "," + std::to_string(values[3]) + "," + std::to_string(values[4]) + ",\"" + neighbors.str() + "\",\"" + send.str() + "\",\"" + recv.str() + "\"\n";
  const int n = static_cast<int>(text.size()); std::vector<int> counts(size_), offsets(size_); MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm_);
  std::vector<char> all; if (rank_ == 0) { for (int i = 1; i < size_; ++i) offsets[i] = offsets[i - 1] + counts[i - 1]; all.resize(offsets.back() + counts.back()); }
  MPI_Gatherv(text.data(), n, MPI_CHAR, all.data(), counts.data(), offsets.data(), MPI_CHAR, 0, comm_);
  if (rank_ == 0) { std::ofstream out(output_dir_ / "partition_diagnostics.csv"); if (!out) throw std::runtime_error("cannot write partition diagnostics"); out << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n"; out.write(all.data(), all.size()); }
}

void OutputWriter::append_residual(const ResidualRecord& r) const { if (rank_ != 0) return; const auto p = output_dir_ / "residuals.csv"; append_header_if_empty(p, kResidualHeader); std::ofstream out(p, std::ios::app); out << std::setprecision(17) << r.step << ',' << r.physical_time << ',' << r.inner_iter << ',' << r.cfl << ',' << r.dt << ',' << r.components[0] << ',' << r.components[1] << ',' << r.components[2] << ',' << r.components[3] << ',' << r.l2 << ',' << r.linf << '\n'; }

void OutputWriter::write_residual_trace(const std::filesystem::path& parent_trace,
                                        const ResidualRecord& initial_reference,
                                        const std::vector<ResidualRecord>& segment_rows,
                                        const RestartProvenance& provenance) const {
  if (rank_ != 0) {
    return;
  }
  if (segment_rows.empty()) {
    throw std::runtime_error("cannot write a cumulative residual trace without a final assembled residual");
  }
  std::ofstream output(output_dir_ / "residuals.csv");
  if (!output) {
    throw std::runtime_error("cannot write residuals.csv");
  }
  ResidualTraceTail tail;
  if (parent_trace.empty()) {
    if (provenance.restarted || initial_reference.step != 0 || initial_reference.physical_time != 0.0 ||
        !std::isfinite(initial_reference.l2) || initial_reference.l2 <= 0.0 ||
        !std::isfinite(initial_reference.linf) || initial_reference.linf < 0.0) {
      throw std::runtime_error("invalid fresh-run residual reference");
    }
    output << kResidualHeader;
    write_residual_row(output, initial_reference);
    tail = {true, initial_reference.step, initial_reference.physical_time, initial_reference.l2,
            initial_reference.linf};
  } else {
    if (!provenance.restarted) {
      throw std::runtime_error("a parent residual trace requires restart provenance");
    }
    tail = validate_and_copy_parent_trace(parent_trace, output);
    if (tail.step != provenance.segment_start_step ||
        !approximately_equal(tail.physical_time, provenance.segment_start_physical_time) ||
        !approximately_equal(tail.l2, provenance.segment_start_residual_l2) ||
        !approximately_equal(tail.linf, provenance.segment_start_residual_linf)) {
      throw std::runtime_error("parent residual trace does not end at the validated restart checkpoint");
    }
  }
  for (const ResidualRecord& row : segment_rows) {
    if (row.step < tail.step || !std::isfinite(row.physical_time) || row.physical_time < tail.physical_time ||
        !std::isfinite(row.l2) || row.l2 <= 0.0 || !std::isfinite(row.linf) || row.linf < 0.0) {
      throw std::runtime_error("invalid segment row in cumulative residual trace");
    }
    write_residual_row(output, row);
    tail = {true, row.step, row.physical_time, row.l2, row.linf};
  }
  if (tail.step != provenance.checkpoint_step ||
      !approximately_equal(tail.physical_time, provenance.checkpoint_physical_time) ||
      !approximately_equal(tail.l2, provenance.checkpoint_residual_l2) ||
      !approximately_equal(tail.linf, provenance.checkpoint_residual_linf)) {
    throw std::runtime_error("cumulative residual trace does not end at the restart checkpoint");
  }
}

void OutputWriter::append_force(const ForceRecord& r) const { if (rank_ != 0) return; const auto p = output_dir_ / "forces.csv"; append_header_if_empty(p, kForceHeader); std::ofstream out(p, std::ios::app); out << std::setprecision(17) << r.step << ',' << r.physical_time << ',' << r.cl << ',' << r.cd << ',' << r.cmz << ',' << r.pressure_drag << ',' << r.viscous_drag << ',' << r.pressure_lift << ',' << r.viscous_lift << '\n'; }

void OutputWriter::write_surface(const std::vector<SurfaceRecord>& rows) const {
  std::ostringstream part; part << std::setprecision(17); for (const auto& r : rows) part << r.x << ',' << r.y << ',' << r.nx << ',' << r.ny << ',' << r.pressure << ',' << r.cp << ',' << r.cf << ',' << r.rho << ',' << r.u << ',' << r.v << ',' << r.mach << ",\"" << esc(r.tag) << "\"\n";
  const std::string bytes = part.str(); const int n = static_cast<int>(bytes.size()); std::vector<int> counts(size_), offsets(size_); MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm_); std::vector<char> all; if (rank_ == 0) { for (int i = 1; i < size_; ++i) offsets[i] = offsets[i-1] + counts[i-1]; all.resize(offsets.back() + counts.back()); } MPI_Gatherv(bytes.data(), n, MPI_CHAR, all.data(), counts.data(), offsets.data(), MPI_CHAR, 0, comm_);
  if (rank_ == 0) { std::ofstream out(output_dir_ / "surface.csv"); if (!out) throw std::runtime_error("cannot write surface.csv"); out << kSurfaceHeader; out.write(all.data(), all.size()); }
}

void OutputWriter::write_run_status(const CaseConfig& c, const RunStatus& s) const { if (rank_ != 0) return; std::ofstream out(output_dir_ / "run_status.json"); if (!out) throw std::runtime_error("cannot write run_status.json"); out << std::setprecision(17) << "{\n  \"case_id\": \"" << esc(c.case_id) << "\",\n  \"command\": \"" << esc(s.command) << "\",\n  \"mpi_ranks\": " << size_ << ",\n  \"wall_time_seconds\": " << s.wall_time_seconds << ",\n  \"final_step\": " << s.final_step << ",\n  \"final_physical_time\": " << s.final_physical_time << ",\n  \"convergence_status\": \"" << esc(s.convergence_status) << "\",\n  \"residual_reduction_orders\": " << s.residual_reduction_orders << ",\n  \"residual_reference_scope\": \"cumulative_fully_assembled\",\n  \"restart_provenance\": " << provenance_json(s.restart).dump() << ",\n  \"notes\": \"" << esc(s.notes) << "\"\n}\n"; }

void OutputWriter::write_field_final(const LocalMesh& mesh, const std::vector<double>& state, const PerfectGas& gas) const {
  require_local_state(mesh, state);
  std::ostringstream local_piece;
  local_piece << std::setprecision(17)
              << "<Piece NumberOfPoints=\"" << mesh.nodes.size() << "\" NumberOfCells=\""
              << mesh.owned_cell_count << "\">\n"
              << "<Points><DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
  for (const auto& point : mesh.nodes) {
    local_piece << point.x << ' ' << point.y << " 0 ";
  }
  local_piece << "\n</DataArray></Points>\n"
              << "<Cells><DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
  int offset = 0;
  for (int cell = 0; cell < mesh.owned_cell_count; ++cell) {
    for (const int node : mesh.cells[static_cast<std::size_t>(cell)].nodes) {
      local_piece << node << ' ';
    }
  }
  local_piece << "\n</DataArray><DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
  for (int cell = 0; cell < mesh.owned_cell_count; ++cell) {
    offset += static_cast<int>(mesh.cells[static_cast<std::size_t>(cell)].nodes.size());
    local_piece << offset << ' ';
  }
  local_piece << "\n</DataArray><DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
  for (int cell = 0; cell < mesh.owned_cell_count; ++cell) {
    local_piece << (mesh.cells[static_cast<std::size_t>(cell)].type == CellType::triangle ? 5 : 9) << ' ';
  }
  local_piece << "\n</DataArray></Cells>\n<CellData>\n";
  const auto array = [&](const char* name, const auto& value) {
    local_piece << "<DataArray type=\"Float64\" Name=\"" << name << "\" format=\"ascii\">\n";
    for (int cell = 0; cell < mesh.owned_cell_count; ++cell) {
      local_piece << value(cell) << ' ';
    }
    local_piece << "\n</DataArray>\n";
  };
  array("density", [&](const int cell) { return gas.primitive(state_at(state, cell)).rho; });
  array("u", [&](const int cell) { return gas.primitive(state_at(state, cell)).u; });
  array("v", [&](const int cell) { return gas.primitive(state_at(state, cell)).v; });
  array("pressure", [&](const int cell) { return gas.primitive(state_at(state, cell)).pressure; });
  array("mach", [&](const int cell) {
    const auto primitive = gas.primitive(state_at(state, cell));
    return std::sqrt(primitive.u * primitive.u + primitive.v * primitive.v) / primitive.sound_speed;
  });
  array("total_energy", [&](const int cell) { return state_at(state, cell)[3]; });
  local_piece << "<DataArray type=\"Int32\" Name=\"rank\" format=\"ascii\">\n";
  for (int cell = 0; cell < mesh.owned_cell_count; ++cell) {
    local_piece << rank_ << ' ';
  }
  local_piece << "\n</DataArray>\n</CellData></Piece>\n";

  const std::string piece_text = local_piece.str();
  const auto piece_path = rank_path(output_dir_, "field", rank_, ".vtu");
  std::ofstream piece_file(piece_path);
  if (!piece_file) {
    throw std::runtime_error("cannot write " + piece_path.string());
  }
  piece_file << "<?xml version=\"1.0\"?>\n<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n"
             << "<UnstructuredGrid>\n" << piece_text << "</UnstructuredGrid>\n</VTKFile>\n";
  piece_file.close();

  const int local_size = static_cast<int>(piece_text.size());
  std::vector<int> sizes(static_cast<std::size_t>(size_), 0);
  std::vector<int> offsets(static_cast<std::size_t>(size_), 0);
  MPI_Gather(&local_size, 1, MPI_INT, sizes.data(), 1, MPI_INT, 0, comm_);
  std::vector<char> gathered;
  if (rank_ == 0) {
    for (int rank = 1; rank < size_; ++rank) {
      offsets[static_cast<std::size_t>(rank)] = offsets[static_cast<std::size_t>(rank - 1)] +
                                                sizes[static_cast<std::size_t>(rank - 1)];
    }
    const int total_size = offsets.back() + sizes.back();
    gathered.resize(static_cast<std::size_t>(total_size));
  }
  MPI_Gatherv(piece_text.data(), local_size, MPI_CHAR, gathered.data(), sizes.data(), offsets.data(), MPI_CHAR, 0,
              comm_);
  MPI_Barrier(comm_);
  if (rank_ != 0) {
    return;
  }

  std::ofstream final_vtu(output_dir_ / "field_final.vtu");
  if (!final_vtu) {
    throw std::runtime_error("cannot write field_final.vtu");
  }
  final_vtu << "<?xml version=\"1.0\"?>\n<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n"
            << "<UnstructuredGrid>\n";
  for (int rank = 0; rank < size_; ++rank) {
    final_vtu.write(gathered.data() + offsets[static_cast<std::size_t>(rank)], sizes[static_cast<std::size_t>(rank)]);
  }
  final_vtu << "</UnstructuredGrid>\n</VTKFile>\n";

  std::ofstream pvtu(output_dir_ / "field_final.pvtu");
  if (!pvtu) {
    throw std::runtime_error("cannot write field_final.pvtu");
  }
  pvtu << "<?xml version=\"1.0\"?><VTKFile type=\"PUnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">"
       << "<PUnstructuredGrid GhostLevel=\"0\"><PPoints><PDataArray type=\"Float64\" NumberOfComponents=\"3\"/>"
       << "</PPoints><PCellData><PDataArray type=\"Float64\" Name=\"density\"/><PDataArray type=\"Float64\" Name=\"u\"/>"
       << "<PDataArray type=\"Float64\" Name=\"v\"/><PDataArray type=\"Float64\" Name=\"pressure\"/>"
       << "<PDataArray type=\"Float64\" Name=\"mach\"/><PDataArray type=\"Float64\" Name=\"total_energy\"/>"
       << "<PDataArray type=\"Int32\" Name=\"rank\"/></PCellData>";
  for (int rank = 0; rank < size_; ++rank) {
    const auto piece = rank_path(output_dir_, "field", rank, ".vtu");
    pvtu << "<Piece Source=\"" << xml(piece.filename().string()) << "\"/>";
  }
  pvtu << "</PUnstructuredGrid></VTKFile>\n";
}

void OutputWriter::write_restart_final(const CaseConfig& config, const LocalMesh& mesh,
                                       const std::vector<double>& state,
                                       const RestartProvenance& provenance) const {
  require_local_state(mesh, state);
  if (provenance.compatibility_signature != restart_compatibility_signature(config, mesh) ||
      provenance.checkpoint_step < 0 || !std::isfinite(provenance.checkpoint_physical_time) ||
      provenance.checkpoint_physical_time < 0.0 || !std::isfinite(provenance.residual_reference_l2) ||
      provenance.residual_reference_l2 <= 0.0 || !std::isfinite(provenance.checkpoint_residual_l2) ||
      provenance.checkpoint_residual_l2 <= 0.0) {
    throw std::runtime_error("invalid restart provenance supplied for output");
  }
  const auto path = rank_path(output_dir_, "restart_final", rank_, ".bin");
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("cannot write restart " + path.string());
  }
  out.write(kRestartMagic, sizeof(kRestartMagic));
  const std::uint32_t version = kRestartBinaryVersion;
  const std::uint64_t count = static_cast<std::uint64_t>(mesh.owned_cell_count);
  write_pod(out, version);
  write_pod(out, provenance.checkpoint_step);
  write_pod(out, provenance.checkpoint_physical_time);
  write_pod(out, count);
  for (int cell = 0; cell < mesh.owned_cell_count; ++cell) {
    const int global = mesh.local_to_global_cell[cell];
    write_pod(out, global);
    for (const double value : state_at(state, cell)) {
      write_pod(out, value);
    }
  }
  out.close();
  MPI_Barrier(comm_);
  if (rank_ != 0) {
    return;
  }
  const Json manifest = {{"format", kRestartManifestFormat},
                         {"binary_version", kRestartBinaryVersion},
                         {"case_id", config.case_id},
                         {"mpi_ranks", size_},
                         {"mesh", {{"global_cells", mesh.global_cell_count},
                                   {"global_faces", mesh.global_face_count}}},
                         {"step", provenance.checkpoint_step},
                         {"physical_time", provenance.checkpoint_physical_time},
                         {"state_layout", "owned_cells_only; global_cell_id plus rho,rhou,rhov,rhoE"},
                         {"provenance", provenance_json(provenance)}};
  std::ofstream manifest_file(output_dir_ / "restart_final.manifest.json");
  if (!manifest_file) {
    throw std::runtime_error("cannot write restart_final.manifest.json");
  }
  manifest_file << manifest.dump(2) << '\n';
}

RestartProvenance OutputWriter::read_restart_provenance(const std::filesystem::path& manifest_path,
                                                         const CaseConfig& config,
                                                         const LocalMesh& mesh) const {
  try {
    std::ifstream input(manifest_path);
    if (!input) {
      throw std::runtime_error("cannot read restart manifest " + manifest_path.string());
    }
    Json manifest;
    input >> manifest;
    if (!manifest.is_object() || required_member(manifest, "format").get<std::string>() != kRestartManifestFormat ||
        required_member(manifest, "binary_version").get<int>() != kRestartBinaryVersion) {
      throw std::runtime_error("restart manifest is not a supported provenance-aware v2 restart");
    }
    if (required_member(manifest, "case_id").get<std::string>() != config.case_id ||
        nonnegative_integer(manifest, "mpi_ranks") != size_) {
      throw std::runtime_error("restart manifest case ID or MPI rank count is incompatible with this run");
    }
    const Json& restart_mesh = required_member(manifest, "mesh");
    if (!restart_mesh.is_object() || nonnegative_integer(restart_mesh, "global_cells") != mesh.global_cell_count ||
        nonnegative_integer(restart_mesh, "global_faces") != mesh.global_face_count) {
      throw std::runtime_error("restart manifest mesh is incompatible with this run");
    }
    const Json& data = required_member(manifest, "provenance");
    if (!data.is_object()) {
      throw std::runtime_error("restart manifest provenance must be an object");
    }
    RestartProvenance provenance;
    provenance.restarted = required_member(data, "restarted").get<bool>();
    provenance.chain_depth = nonnegative_integer(data, "chain_depth");
    const Json& parent_manifest = required_member(data, "parent_manifest");
    provenance.parent_manifest = parent_manifest.is_null() ? std::string{} : parent_manifest.get<std::string>();
    provenance.cumulative_residual_trace = required_member(data, "cumulative_residual_trace").get<std::string>();
    provenance.compatibility_signature = required_member(data, "compatibility_signature").get<std::string>();
    provenance.segment_start_step = nonnegative_integer(data, "segment_start_step");
    provenance.segment_start_physical_time = finite_nonnegative(data, "segment_start_physical_time");
    provenance.residual_reference_l2 = finite_nonnegative(data, "residual_reference_l2");
    provenance.residual_reference_linf = finite_nonnegative(data, "residual_reference_linf");
    provenance.segment_start_residual_l2 = finite_nonnegative(data, "segment_start_residual_l2");
    provenance.segment_start_residual_linf = finite_nonnegative(data, "segment_start_residual_linf");
    provenance.checkpoint_step = nonnegative_integer(data, "checkpoint_step");
    provenance.checkpoint_physical_time = finite_nonnegative(data, "checkpoint_physical_time");
    provenance.checkpoint_residual_l2 = finite_nonnegative(data, "checkpoint_residual_l2");
    provenance.checkpoint_residual_linf = finite_nonnegative(data, "checkpoint_residual_linf");
    if (provenance.residual_reference_l2 <= 0.0 || provenance.segment_start_residual_l2 <= 0.0 ||
        provenance.checkpoint_residual_l2 <= 0.0 || provenance.segment_start_step > provenance.checkpoint_step ||
        provenance.segment_start_physical_time > provenance.checkpoint_physical_time ||
        nonnegative_integer(manifest, "step") != provenance.checkpoint_step ||
        !approximately_equal(finite_nonnegative(manifest, "physical_time"), provenance.checkpoint_physical_time)) {
      throw std::runtime_error("restart manifest has inconsistent checkpoint provenance");
    }
    const std::filesystem::path trace(provenance.cumulative_residual_trace);
    if (trace.is_absolute() || trace.has_parent_path() || trace.filename() != "residuals.csv") {
      throw std::runtime_error("restart manifest names an unsafe cumulative residual trace");
    }
    if (provenance.compatibility_signature != restart_compatibility_signature(config, mesh)) {
      throw std::runtime_error("restart manifest numerical compatibility signature does not match this run");
    }
    std::ostringstream checked_trace;
    const ResidualTraceTail tail =
        validate_and_copy_parent_trace(manifest_path.parent_path() / trace, checked_trace);
    if (tail.step != provenance.checkpoint_step ||
        !approximately_equal(tail.physical_time, provenance.checkpoint_physical_time) ||
        !approximately_equal(tail.l2, provenance.checkpoint_residual_l2) ||
        !approximately_equal(tail.linf, provenance.checkpoint_residual_linf)) {
      throw std::runtime_error("restart manifest checkpoint does not match its cumulative residual trace");
    }
    return provenance;
  } catch (const std::exception& error) {
    throw std::runtime_error("invalid restart manifest " + manifest_path.string() + ": " + error.what());
  }
}

std::vector<double> OutputWriter::read_restart_local(const LocalMesh& mesh, int& step, double& time) const {
  const auto path = rank_path(output_dir_, "restart_final", rank_, ".bin");
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read restart " + path.string());
  }
  std::array<char, sizeof(kRestartMagic)> magic{};
  input.read(magic.data(), magic.size());
  if (!input || !std::equal(magic.begin(), magic.end(), kRestartMagic)) {
    throw std::runtime_error("invalid restart magic");
  }
  std::uint32_t version = 0;
  std::uint64_t count = 0;
  read_pod(input, version);
  read_pod(input, step);
  read_pod(input, time);
  read_pod(input, count);
  if (version != kRestartBinaryVersion || count != static_cast<std::uint64_t>(mesh.owned_cell_count) ||
      step < 0 || !std::isfinite(time) || time < 0.0) {
    throw std::runtime_error("restart partition does not match current local mesh or format");
  }
  std::vector<double> result(static_cast<std::size_t>(mesh.owned_cell_count) * 4U);
  for (int cell = 0; cell < mesh.owned_cell_count; ++cell) {
    int global = -1;
    read_pod(input, global);
    if (global != mesh.local_to_global_cell[cell]) {
      throw std::runtime_error("restart global-cell ordering does not match current partition");
    }
    for (int component = 0; component < 4; ++component) {
      read_pod(input, result[static_cast<std::size_t>(cell) * 4U + static_cast<std::size_t>(component)]);
    }
  }
  return result;
}

}  // namespace cfd
