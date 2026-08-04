#include "cfd/output.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cfd {
namespace {

using json = nlohmann::json;

std::string rank_suffix(int rank) {
  std::ostringstream stream;
  stream << "rank" << std::setw(4) << std::setfill('0') << rank;
  return stream.str();
}

std::string time_stem(int step) {
  std::ostringstream stream;
  stream << "field_step" << std::setw(8) << std::setfill('0') << step;
  return stream.str();
}

void require_stream(const std::ios& stream, const std::filesystem::path& path) {
  if (!stream) throw std::runtime_error("failed writing " + path.string());
}

std::string gather_text(const std::string& local, int root, MPI_Comm communicator) {
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(communicator, &rank);
  MPI_Comm_size(communicator, &size);
  const int local_size = static_cast<int>(local.size());
  std::vector<int> sizes(static_cast<std::size_t>(size));
  MPI_Gather(&local_size, 1, MPI_INT, sizes.data(), 1, MPI_INT, root, communicator);
  std::vector<int> offsets(static_cast<std::size_t>(size), 0);
  std::vector<char> all;
  if (rank == root) {
    for (int i = 1; i < size; ++i) offsets[static_cast<std::size_t>(i)] = offsets[static_cast<std::size_t>(i - 1)] + sizes[static_cast<std::size_t>(i - 1)];
    const int total = offsets.back() + sizes.back();
    all.resize(static_cast<std::size_t>(total));
  }
  MPI_Gatherv(local.data(), local_size, MPI_CHAR, all.data(), sizes.data(), offsets.data(), MPI_CHAR, root, communicator);
  if (rank != root) return {};
  return std::string(all.begin(), all.end());
}

template <class T>
void write_binary(std::ofstream& stream, const T& value) {
  stream.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
}

template <class T>
T read_binary(std::ifstream& stream) {
  T value{};
  stream.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
  if (!stream) throw std::runtime_error("truncated restart file");
  return value;
}

}  // namespace

OutputManager::OutputManager(const std::filesystem::path& output_directory, const CaseConfig& config,
                             const LocalMesh& mesh, MPI_Comm communicator)
    : directory_(std::filesystem::absolute(output_directory).lexically_normal()),
      config_(config), mesh_(mesh), communicator_(communicator), physics_(config) {
  MPI_Comm_rank(communicator_, &rank_);
  MPI_Comm_size(communicator_, &rank_count_);
  if (rank_ == 0) {
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) throw std::runtime_error("cannot create output directory " + directory_.string() + ": " + error.message());
    residual_stream_.open(directory_ / "residuals.csv");
    force_stream_.open(directory_ / "forces.csv");
    log_stream_.open(directory_ / "stdout.log");
    require_stream(residual_stream_, directory_ / "residuals.csv");
    require_stream(force_stream_, directory_ / "forces.csv");
    require_stream(log_stream_, directory_ / "stdout.log");
    residual_stream_ << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
    force_stream_ << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
    residual_stream_ << std::setprecision(17);
    force_stream_ << std::setprecision(17);
    log_stream_ << "SaturnCFD case " << config_.case_id << " on " << rank_count_ << " MPI ranks\n";
  }
  MPI_Barrier(communicator_);
}

void OutputManager::residual(const ResidualRow& row) {
  if (rank_ != 0) return;
  residual_stream_ << row.step << ',' << row.physical_time << ',' << row.inner_iteration << ','
                   << row.cfl << ',' << row.dt;
  for (double value : row.component_l2) residual_stream_ << ',' << value;
  residual_stream_ << ',' << row.residual_l2 << ',' << row.residual_linf << '\n';
  if (row.step % 100 == 0) residual_stream_.flush();
}

void OutputManager::force(const ForceRow& row) {
  if (rank_ != 0) return;
  force_stream_ << row.step << ',' << row.physical_time << ',' << row.cl << ',' << row.cd << ','
                << row.cmz << ',' << row.pressure_drag << ',' << row.viscous_drag << ','
                << row.pressure_lift << ',' << row.viscous_lift << '\n';
  if (row.step % 100 == 0) force_stream_.flush();
}

void OutputManager::progress(const std::string& line) {
  if (rank_ != 0) return;
  std::cout << line << '\n';
  log_stream_ << line << '\n';
  log_stream_.flush();
}

void OutputManager::field(int step, double physical_time, const std::vector<Conserved>& state,
                          const std::vector<double>& vorticity, bool final) {
  (void)physical_time;
  const std::string stem = final ? "field_final" : time_stem(step);
  const std::string piece_name = stem + "_" + rank_suffix(rank_) + ".vtu";
  write_vtu_piece(directory_ / piece_name, state, vorticity);
  MPI_Barrier(communicator_);
  if (rank_ == 0) write_pvtu(directory_ / (stem + ".pvtu"), stem + "_rank%04d.vtu");
  MPI_Barrier(communicator_);
}

void OutputManager::write_vtu_piece(const std::filesystem::path& path,
                                    const std::vector<Conserved>& state,
                                    const std::vector<double>& vorticity) const {
  if (state.size() < static_cast<std::size_t>(mesh_.owned_cell_count) ||
      vorticity.size() < static_cast<std::size_t>(mesh_.owned_cell_count)) {
    throw std::runtime_error("field snapshot does not contain all owned cells");
  }
  std::size_t point_count = 0;
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) point_count += mesh_.cells[static_cast<std::size_t>(cell)].vertices.size();
  std::ofstream stream(path);
  require_stream(stream, path);
  stream << std::setprecision(17);
  stream << "<?xml version=\"1.0\"?>\n"
         << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n"
         << "<UnstructuredGrid><Piece NumberOfPoints=\"" << point_count
         << "\" NumberOfCells=\"" << mesh_.owned_cell_count << "\">\n"
         << "<Points><DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    for (const Vec2 vertex : mesh_.cells[static_cast<std::size_t>(cell)].vertices) stream << vertex.x << ' ' << vertex.y << " 0\n";
  }
  stream << "</DataArray></Points>\n<Cells>\n"
         << "<DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n";
  std::size_t point_offset = 0;
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    const std::size_t count = mesh_.cells[static_cast<std::size_t>(cell)].vertices.size();
    for (std::size_t vertex = 0; vertex < count; ++vertex) stream << point_offset + vertex << ' ';
    stream << '\n';
    point_offset += count;
  }
  stream << "</DataArray>\n<DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n";
  point_offset = 0;
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    point_offset += mesh_.cells[static_cast<std::size_t>(cell)].vertices.size();
    stream << point_offset << '\n';
  }
  stream << "</DataArray>\n<DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    const std::size_t count = mesh_.cells[static_cast<std::size_t>(cell)].vertices.size();
    stream << (count == 3 ? 5 : count == 4 ? 9 : 7) << '\n';
  }
  stream << "</DataArray></Cells>\n<CellData>\n";

  std::vector<Primitive> primitive(static_cast<std::size_t>(mesh_.owned_cell_count));
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) primitive[static_cast<std::size_t>(cell)] = physics_.to_primitive(state[static_cast<std::size_t>(cell)]);
  auto write_double_array = [&](const char* name, auto getter) {
    stream << "<DataArray type=\"Float64\" Name=\"" << name << "\" format=\"ascii\">\n";
    for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) stream << getter(cell, primitive[static_cast<std::size_t>(cell)]) << '\n';
    stream << "</DataArray>\n";
  };
  write_double_array("Density", [](int, const Primitive& p) { return p.rho; });
  write_double_array("VelocityX", [](int, const Primitive& p) { return p.u; });
  write_double_array("VelocityY", [](int, const Primitive& p) { return p.v; });
  write_double_array("Pressure", [](int, const Primitive& p) { return p.p; });
  write_double_array("Mach", [&](int, const Primitive& p) {
    return std::sqrt(p.u * p.u + p.v * p.v) / physics_.sound_speed(p);
  });
  write_double_array("Temperature", [&](int, const Primitive& p) { return physics_.temperature(p); });
  write_double_array("TotalEnergy", [&](int cell, const Primitive&) { return state[static_cast<std::size_t>(cell)][3]; });
  write_double_array("Vorticity", [&](int cell, const Primitive&) { return vorticity[static_cast<std::size_t>(cell)]; });
  stream << "<DataArray type=\"Int32\" Name=\"OwnerRank\" format=\"ascii\">\n";
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) stream << rank_ << '\n';
  stream << "</DataArray>\n<DataArray type=\"Int64\" Name=\"GlobalCellId\" format=\"ascii\">\n";
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) stream << mesh_.cells[static_cast<std::size_t>(cell)].global_id << '\n';
  stream << "</DataArray>\n</CellData></Piece></UnstructuredGrid></VTKFile>\n";
  require_stream(stream, path);
}

void OutputManager::write_pvtu(const std::filesystem::path& path, const std::string& piece_pattern) const {
  std::ofstream stream(path);
  require_stream(stream, path);
  stream << "<?xml version=\"1.0\"?>\n"
         << "<VTKFile type=\"PUnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n"
         << "<PUnstructuredGrid GhostLevel=\"0\"><PPoints><PDataArray type=\"Float64\" NumberOfComponents=\"3\"/></PPoints>\n"
         << "<PCellData>\n";
  for (const char* name : {"Density", "VelocityX", "VelocityY", "Pressure", "Mach", "Temperature", "TotalEnergy", "Vorticity"}) {
    stream << "<PDataArray type=\"Float64\" Name=\"" << name << "\"/>\n";
  }
  stream << "<PDataArray type=\"Int32\" Name=\"OwnerRank\"/>\n"
         << "<PDataArray type=\"Int64\" Name=\"GlobalCellId\"/>\n</PCellData>\n";
  for (int rank = 0; rank < rank_count_; ++rank) {
    char name[256]{};
    std::snprintf(name, sizeof(name), piece_pattern.c_str(), rank);
    stream << "<Piece Source=\"" << name << "\"/>\n";
  }
  stream << "</PUnstructuredGrid></VTKFile>\n";
  require_stream(stream, path);
}

void OutputManager::write_partition_diagnostics() const {
  std::ostringstream row;
  row << mesh_.rank << ',' << mesh_.owned_cell_count << ',' << mesh_.ghost_cell_count() << ','
      << mesh_.boundary_face_count << ',' << mesh_.halo_links.size() << ",\"";
  for (std::size_t i = 0; i < mesh_.halo_links.size(); ++i) {
    if (i) row << ';';
    row << mesh_.halo_links[i].rank;
  }
  row << "\",\"";
  for (std::size_t i = 0; i < mesh_.halo_links.size(); ++i) {
    if (i) row << ';';
    row << mesh_.halo_links[i].send_cells.size();
  }
  row << "\",\"";
  for (std::size_t i = 0; i < mesh_.halo_links.size(); ++i) {
    if (i) row << ';';
    row << mesh_.halo_links[i].receive_cells.size();
  }
  row << "\"\n";
  const std::string gathered = gather_text(row.str(), 0, communicator_);
  if (rank_ == 0) {
    const auto path = directory_ / "partition_diagnostics.csv";
    std::ofstream stream(path);
    stream << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n"
           << gathered;
    require_stream(stream, path);
  }
}

void OutputManager::write_surface(const std::vector<SurfaceRow>& local_surface) const {
  std::ostringstream rows;
  rows << std::setprecision(17);
  for (const SurfaceRow& row : local_surface) {
    rows << row.x << ',' << row.y << ',' << row.nx << ',' << row.ny << ',' << row.pressure << ','
         << row.cp << ',' << row.cf << ',' << row.rho << ',' << row.u << ',' << row.v << ','
         << row.mach << ',' << row.tag << '\n';
  }
  const std::string gathered = gather_text(rows.str(), 0, communicator_);
  if (rank_ == 0) {
    const auto path = directory_ / "surface.csv";
    std::ofstream stream(path);
    stream << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n" << gathered;
    require_stream(stream, path);
  }
}

void OutputManager::write_restart(const RunResult& result) const {
  const std::string filename = "restart_final_" + rank_suffix(rank_) + ".bin";
  const auto path = directory_ / filename;
  std::ofstream stream(path, std::ios::binary);
  require_stream(stream, path);
  constexpr std::array<char, 8> magic{{'S', 'A', 'T', 'C', 'F', 'D', 'R', '1'}};
  stream.write(magic.data(), static_cast<std::streamsize>(magic.size()));
  write_binary<std::int32_t>(stream, rank_count_);
  write_binary<std::int32_t>(stream, rank_);
  write_binary<std::int64_t>(stream, mesh_.owned_cell_count);
  write_binary<std::int64_t>(stream, result.final_step);
  write_binary<double>(stream, result.final_physical_time);
  for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    const auto i = static_cast<std::size_t>(cell);
    write_binary<std::int64_t>(stream, mesh_.cells[i].global_id);
    for (double value : result.state[i]) write_binary<double>(stream, value);
    for (double value : result.previous_state[i]) write_binary<double>(stream, value);
  }
  require_stream(stream, path);
  MPI_Barrier(communicator_);
  if (rank_ == 0) {
    json manifest{{"format", "saturn_cfd_rank_local_restart_v1"},
                  {"case_id", config_.case_id}, {"mpi_ranks", rank_count_},
                  {"final_step", result.final_step}, {"final_physical_time", result.final_physical_time},
                  {"file_pattern", "restart_final_rank%04d.bin"}};
    const auto manifest_path = directory_ / "restart_final.json";
    std::ofstream manifest_stream(manifest_path);
    manifest_stream << std::setw(2) << manifest << '\n';
    require_stream(manifest_stream, manifest_path);
  }
}

void OutputManager::write_metadata(const RunResult& result, const std::string& start_time_utc,
                                   const std::string& end_time_utc) const {
  if (rank_ != 0) return;
  const auto& inner = result.inner_statistics;
  json metadata{
      {"case_id", config_.case_id}, {"solver_name", "SaturnCFD"}, {"solver_version", "0.1.0"},
      {"git_revision", nullptr}, {"mpi_ranks", rank_count_}, {"mesh_file", config_.mesh_path.string()},
      {"num_cells_global", mesh_.global_cell_count}, {"num_faces_global", mesh_.global_face_count},
      {"num_cells_owned_local", mesh_.owned_cell_count}, {"num_cells_ghost_local", mesh_.ghost_cell_count()},
      {"partitioner", "metis_kway"}, {"partition_edge_cut", mesh_.partition_edge_cut},
      {"halo_exchange", "neighbor_isend_irecv"},
      {"full_state_replication_during_iterations", false}, {"full_mesh_replication_during_iterations", false},
      {"equation_set", "compressible_navier_stokes_2d"},
      {"inviscid_flux", config_.run.transient ? "HLLC_with_Rusanov_positivity_fallback"
                                               : "Rusanov_local_Lax_Friedrichs"},
      {"entropy_fix", nullptr},
      {"viscous_flux", config_.viscous ? "corrected_primitive_gradient_Newton_Fourier" : "disabled"},
      {"time_integrator", config_.run.transient ? "BDF2_frozen_history_block_Newton" : "implicit_steady_pseudo_time"},
      {"implicit_solver", "distributed_4x4_block_Jacobi_Newton_defect_correction"},
      {"reconstruction", "weighted_least_squares_piecewise_linear"},
      {"limiter", "Barth-Jespersen_primitive_component_limiter"}, {"spatial_order_claimed", 2},
      {"positivity_preservation", "face_scaling_and_conservative_update_line_search"},
      {"wall_boundary_output_semantics", "boundary_value"},
      {"initial_condition", config_.run.transient
          ? "freestream_plus_deterministic_localized_crossflow_perturbation_1e-3"
          : "uniform_freestream"},
      {"true_bdf2_inner_loop", config_.run.transient},
      {"typical_inner_iterations", inner.observed_mean}, {"min_inner_iterations", config_.run.min_inner_iterations},
      {"max_inner_iterations", config_.run.max_inner_iterations},
      {"observed_min_inner_iterations", inner.observed_min}, {"observed_max_inner_iterations", inner.observed_max},
      {"observed_mean_inner_iterations", inner.observed_mean},
      {"inner_residual_reduction_target", config_.run.inner_residual_reduction_target},
      {"inner_target_misses", inner.target_misses},
      {"inner_target_converged_fraction", inner.target_converged_fraction},
      {"last_inner_residual_ratio", inner.last_ratio},
      {"rusanov_flux_face_evaluations", result.hllc_fallback_faces},
      {"reconstruction_positivity_fallbacks", result.reconstruction_positivity_fallbacks},
      {"damped_conservative_updates", result.damped_updates},
      {"start_time_utc", start_time_utc}, {"end_time_utc", end_time_utc},
      {"completed", result.convergence_status == "converged" || result.convergence_status == "statistically_periodic"},
      {"convergence_status", result.convergence_status}};
  const auto path = directory_ / "metadata.json";
  std::ofstream stream(path);
  stream << std::setw(2) << metadata << '\n';
  require_stream(stream, path);
}

void OutputManager::write_run_status(const RunResult& result, const std::string& command,
                                     double wall_time_seconds) const {
  if (rank_ != 0) return;
  json status{{"case_id", config_.case_id}, {"command", command}, {"mpi_ranks", rank_count_},
              {"wall_time_seconds", wall_time_seconds}, {"final_step", result.final_step},
              {"final_physical_time", result.final_physical_time},
              {"convergence_status", result.convergence_status},
              {"residual_reduction_orders", result.residual_reduction_orders}, {"notes", result.notes}};
  const auto path = directory_ / "run_status.json";
  std::ofstream stream(path);
  stream << std::setw(2) << status << '\n';
  require_stream(stream, path);
}

void OutputManager::finalize(const RunResult& result, const std::string& command, double wall_time_seconds,
                             const std::string& start_time_utc, const std::string& end_time_utc) {
  if (rank_ == 0) {
    residual_stream_.flush();
    force_stream_.flush();
  }
  write_partition_diagnostics();
  write_surface(result.local_surface);
  write_restart(result);
  write_metadata(result, start_time_utc, end_time_utc);
  write_run_status(result, command, wall_time_seconds);
  if (rank_ == 0) {
    log_stream_ << "final status=" << result.convergence_status << " step=" << result.final_step
                << " time=" << result.final_physical_time << " wall_seconds=" << wall_time_seconds << '\n';
    log_stream_.flush();
  }
  MPI_Barrier(communicator_);
}

InitialState load_restart(const std::filesystem::path& input_manifest_path, const LocalMesh& mesh,
                          MPI_Comm communicator) {
  int rank = 0;
  int rank_count = 1;
  MPI_Comm_rank(communicator, &rank);
  MPI_Comm_size(communicator, &rank_count);
  const auto manifest_path = std::filesystem::absolute(input_manifest_path).lexically_normal();
  std::ifstream manifest_stream(manifest_path);
  if (!manifest_stream) throw std::runtime_error("cannot open restart manifest: " + manifest_path.string());
  json manifest;
  manifest_stream >> manifest;
  if (manifest.value("format", std::string{}) != "saturn_cfd_rank_local_restart_v1") {
    throw std::runtime_error("unsupported restart manifest format");
  }
  if (manifest.value("mpi_ranks", 0) != rank_count) throw std::runtime_error("restart MPI rank count differs from current run");
  const std::string pattern = manifest.value("file_pattern", std::string{});
  if (pattern.empty()) throw std::runtime_error("restart manifest has no file_pattern");
  char filename[512]{};
  std::snprintf(filename, sizeof(filename), pattern.c_str(), rank);
  const auto rank_path = manifest_path.parent_path() / filename;
  std::ifstream stream(rank_path, std::ios::binary);
  if (!stream) throw std::runtime_error("cannot open rank restart file: " + rank_path.string());
  std::array<char, 8> magic{};
  stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  constexpr std::array<char, 8> expected{{'S', 'A', 'T', 'C', 'F', 'D', 'R', '1'}};
  if (magic != expected) throw std::runtime_error("invalid restart magic in " + rank_path.string());
  if (read_binary<std::int32_t>(stream) != rank_count || read_binary<std::int32_t>(stream) != rank) {
    throw std::runtime_error("rank restart identity mismatch");
  }
  const auto cell_count = read_binary<std::int64_t>(stream);
  InitialState initial;
  initial.step = static_cast<int>(read_binary<std::int64_t>(stream));
  initial.physical_time = read_binary<double>(stream);
  initial.state.assign(mesh.cells.size(), zeros());
  initial.previous_state.assign(mesh.cells.size(), zeros());
  std::unordered_map<std::int64_t, int> local_by_gid;
  for (int cell = 0; cell < mesh.owned_cell_count; ++cell) local_by_gid.emplace(mesh.cells[static_cast<std::size_t>(cell)].global_id, cell);
  if (cell_count != mesh.owned_cell_count) throw std::runtime_error("restart owned-cell count differs from current partition");
  for (std::int64_t record = 0; record < cell_count; ++record) {
    const std::int64_t gid = read_binary<std::int64_t>(stream);
    const auto found = local_by_gid.find(gid);
    if (found == local_by_gid.end()) throw std::runtime_error("restart contains a cell absent from current partition");
    const auto local = static_cast<std::size_t>(found->second);
    for (double& value : initial.state[local]) value = read_binary<double>(stream);
    for (double& value : initial.previous_state[local]) value = read_binary<double>(stream);
  }
  return initial;
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

}  // namespace cfd
