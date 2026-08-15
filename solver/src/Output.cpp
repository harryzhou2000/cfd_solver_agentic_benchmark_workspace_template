#include "cfd/Output.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace cfd {
namespace {

constexpr const char* kResidualHeader =
    "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
constexpr const char* kForceHeader =
    "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
constexpr const char* kSurfaceHeader = "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
constexpr const char* kPartitionHeader =
    "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";

void check_mpi(int code, const char* context) {
  if (code == MPI_SUCCESS) return;
  char message[MPI_MAX_ERROR_STRING]{};
  int length = 0;
  MPI_Error_string(code, message, &length);
  throw std::runtime_error(std::string(context) + ": " + std::string(message, length));
}

void require_finite(double value, const char* name) {
  if (!std::isfinite(value)) throw std::runtime_error(std::string("non-finite output value: ") + name);
}

std::string number(double value) {
  require_finite(value, "numeric field");
  std::ostringstream out;
  out << std::setprecision(17) << value;
  return out.str();
}

std::string csv_escape(const std::string& value) {
  if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
  std::string result{"\""};
  for (const char c : value) {
    if (c == '\"') result += "\"\"";
    else result += c;
  }
  return result + "\"";
}

std::string json_escape(const std::string& value) {
  std::ostringstream out;
  for (const unsigned char c : value) {
    switch (c) {
      case '\"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (c < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<unsigned int>(c) << std::dec << std::setfill(' ');
        } else {
          out << static_cast<char>(c);
        }
    }
  }
  return out.str();
}

void require_open(const std::ofstream& stream, const std::filesystem::path& path) {
  if (!stream) throw std::runtime_error("cannot write output file: " + path.string());
}

std::string rank_file_name(const char* stem, int rank, const char* extension) {
  std::ostringstream name;
  name << stem << "_rank" << std::setw(4) << std::setfill('0') << rank << extension;
  return name.str();
}

std::string partition_row(const LocalMesh& mesh) {
  int boundary_faces = 0;
  int send_cells = 0;
  int recv_cells = 0;
  std::ostringstream neighbors;
  for (std::size_t i = 0; i < mesh.halo.size(); ++i) {
    if (i != 0) neighbors << ';';
    neighbors << mesh.halo[i].rank;
    send_cells += static_cast<int>(mesh.halo[i].send_cells.size());
    recv_cells += static_cast<int>(mesh.halo[i].receive_ghosts.size());
  }
  for (const LocalFace& face : mesh.faces) {
    if (face.right < 0) ++boundary_faces;
  }
  std::ostringstream out;
  out << mesh.rank << ',' << mesh.owned_count << ',' << mesh.ghost_count() << ','
      << boundary_faces << ',' << mesh.halo.size() << ',' << csv_escape(neighbors.str()) << ','
      << send_cells << ',' << recv_cells << '\n';
  return out.str();
}

std::string surface_rows_csv(const std::vector<SurfaceRow>& rows) {
  std::ostringstream out;
  for (const SurfaceRow& row : rows) {
    out << number(row.x) << ',' << number(row.y) << ',' << number(row.nx) << ',' << number(row.ny)
        << ',' << number(row.pressure) << ',' << number(row.cp) << ',' << number(row.cf) << ','
        << number(row.rho) << ',' << number(row.u) << ',' << number(row.v) << ','
        << number(row.mach) << ',' << csv_escape(row.tag) << '\n';
  }
  return out.str();
}

std::vector<char> gather_text(const std::string& local, int rank, int ranks, MPI_Comm communicator) {
  const int local_size = static_cast<int>(local.size());
  std::vector<int> sizes(rank == 0 ? static_cast<std::size_t>(ranks) : 0);
  check_mpi(MPI_Gather(&local_size, 1, MPI_INT, rank == 0 ? sizes.data() : nullptr, 1, MPI_INT, 0,
                       communicator),
            "MPI_Gather output sizes failed");
  std::vector<int> offsets;
  std::vector<char> all;
  if (rank == 0) {
    offsets.resize(static_cast<std::size_t>(ranks));
    int total = 0;
    for (int i = 0; i < ranks; ++i) {
      offsets[static_cast<std::size_t>(i)] = total;
      if (sizes[static_cast<std::size_t>(i)] < 0 ||
          total > std::numeric_limits<int>::max() - sizes[static_cast<std::size_t>(i)]) {
        throw std::runtime_error("gathered output is too large");
      }
      total += sizes[static_cast<std::size_t>(i)];
    }
    all.resize(static_cast<std::size_t>(total));
  }
  check_mpi(MPI_Gatherv(local.data(), local_size, MPI_CHAR, rank == 0 ? all.data() : nullptr,
                        rank == 0 ? sizes.data() : nullptr, rank == 0 ? offsets.data() : nullptr,
                        MPI_CHAR, 0, communicator),
            "MPI_Gatherv output failed");
  return all;
}

void write_json_string(std::ostream& out, const char* key, const std::string& value, bool comma = true) {
  out << "  \"" << key << "\": \"" << json_escape(value) << "\"" << (comma ? ",\n" : "\n");
}

void write_json_number(std::ostream& out, const char* key, double value, bool comma = true) {
  out << "  \"" << key << "\": " << number(value) << (comma ? ",\n" : "\n");
}

void write_json_integer(std::ostream& out, const char* key, int value, bool comma = true) {
  out << "  \"" << key << "\": " << value << (comma ? ",\n" : "\n");
}

void write_json_bool(std::ostream& out, const char* key, bool value, bool comma = true) {
  out << "  \"" << key << "\": " << (value ? "true" : "false") << (comma ? ",\n" : "\n");
}

}  // namespace

OutputWriter::OutputWriter(std::filesystem::path directory, const CaseConfig& config,
                           const LocalMesh& mesh, MPI_Comm communicator)
    : directory_(std::move(directory)), config_(config), mesh_(mesh), communicator_(communicator) {
  check_mpi(MPI_Comm_rank(communicator_, &rank_), "MPI_Comm_rank failed");
  check_mpi(MPI_Comm_size(communicator_, &ranks_), "MPI_Comm_size failed");
  if (rank_ != mesh_.rank || ranks_ != mesh_.ranks) {
    throw std::runtime_error("output communicator does not match LocalMesh rank layout");
  }
}

void OutputWriter::initialize() {
  if (initialized_) return;
  int success = 1;
  std::string error;
  if (rank_ == 0) {
    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);
    if (ec) {
      success = 0;
      error = "cannot create output directory " + directory_.string() + ": " + ec.message();
    } else {
      residual_stream_.open(directory_ / "residuals.csv", std::ios::trunc);
      force_stream_.open(directory_ / "forces.csv", std::ios::trunc);
      if (!residual_stream_ || !force_stream_) {
        success = 0;
        error = "cannot create shared CSV outputs in " + directory_.string();
      } else {
        residual_stream_ << kResidualHeader;
        force_stream_ << kForceHeader;
      }
    }
  }
  check_mpi(MPI_Bcast(&success, 1, MPI_INT, 0, communicator_), "MPI_Bcast output status failed");
  if (success == 0) {
    if (rank_ != 0) error = "rank zero could not create output directory";
    throw std::runtime_error(error);
  }
  check_mpi(MPI_Barrier(communicator_), "MPI_Barrier output initialization failed");
  initialized_ = true;
}

void OutputWriter::write_partition_diagnostics() {
  initialize();
  const std::vector<char> gathered = gather_text(partition_row(mesh_), rank_, ranks_, communicator_);
  if (rank_ == 0) {
    const auto path = directory_ / "partition_diagnostics.csv";
    std::ofstream out(path, std::ios::trunc);
    require_open(out, path);
    out << kPartitionHeader;
    out.write(gathered.data(), static_cast<std::streamsize>(gathered.size()));
  }
}

void OutputWriter::write_residual(const ResidualRow& row) {
  initialize();
  require_finite(row.physical_time, "physical_time");
  require_finite(row.cfl, "cfl");
  require_finite(row.dt, "dt");
  require_finite(row.residual_l2, "residual_l2");
  require_finite(row.residual_linf, "residual_linf");
  for (const double value : row.component_l2) require_finite(value, "component residual");
  if (rank_ != 0) return;
  residual_stream_ << row.step << ',' << number(row.physical_time) << ',' << row.inner_iter << ','
      << number(row.cfl) << ',' << number(row.dt);
  for (const double value : row.component_l2) residual_stream_ << ',' << number(value);
  residual_stream_ << ',' << number(row.residual_l2) << ',' << number(row.residual_linf) << '\n';
}

void OutputWriter::write_force(const ForceRow& row) {
  initialize();
  require_finite(row.physical_time, "physical_time");
  const std::array<double, 7> values{row.cl, row.cd, row.cmz, row.pressure_drag,
                                     row.viscous_drag, row.pressure_lift, row.viscous_lift};
  for (const double value : values) require_finite(value, "force value");
  if (rank_ != 0) return;
  force_stream_ << row.step << ',' << number(row.physical_time);
  for (const double value : values) force_stream_ << ',' << number(value);
  force_stream_ << '\n';
}

void OutputWriter::write_surface(const std::vector<SurfaceRow>& local_rows) {
  initialize();
  const std::vector<char> gathered = gather_text(surface_rows_csv(local_rows), rank_, ranks_, communicator_);
  if (rank_ == 0) {
    const auto path = directory_ / "surface.csv";
    std::ofstream out(path, std::ios::trunc);
    require_open(out, path);
    out << kSurfaceHeader;
    out.write(gathered.data(), static_cast<std::streamsize>(gathered.size()));
  }
}

void OutputWriter::write_final_field(const std::vector<Conserved>& states) const {
  if (!initialized_) throw std::runtime_error("OutputWriter::initialize must be called before field output");
  if (states.size() < static_cast<std::size_t>(mesh_.owned_count)) {
    throw std::runtime_error("field state array does not cover all owned cells");
  }
  const auto path = directory_ / rank_file_name("field_final", rank_, ".vtu");
  std::ofstream out(path, std::ios::trunc);
  require_open(out, path);
  const int cells = mesh_.owned_count;
  std::size_t points = 0;
  for (int cell = 0; cell < cells; ++cell) points += mesh_.cells[static_cast<std::size_t>(cell)].polygon.size();

  out << "<?xml version=\"1.0\"?>\n"
      << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n"
      << "  <UnstructuredGrid>\n"
      << "    <Piece NumberOfPoints=\"" << points << "\" NumberOfCells=\"" << cells << "\">\n"
      << "      <CellData Scalars=\"density\">\n";
  const auto scalar = [&](const char* name, const auto& value) {
    out << "        <DataArray type=\"Float64\" Name=\"" << name << "\" format=\"ascii\">\n          ";
    for (int cell = 0; cell < cells; ++cell) out << number(value(cell)) << ' ';
    out << "\n        </DataArray>\n";
  };
  const auto primitive = [&](int cell) {
    return conservative_to_primitive(states[static_cast<std::size_t>(cell)], config_.gas.gamma,
                                     config_.gas.gas_constant);
  };
  scalar("density", [&](int cell) { return primitive(cell).rho; });
  scalar("u", [&](int cell) { return primitive(cell).u; });
  scalar("v", [&](int cell) { return primitive(cell).v; });
  scalar("pressure", [&](int cell) { return primitive(cell).p; });
  scalar("mach", [&](int cell) { return primitive(cell).mach; });
  scalar("temperature", [&](int cell) { return primitive(cell).temperature; });
  scalar("owner", [&](int cell) { return static_cast<double>(mesh_.cells[static_cast<std::size_t>(cell)].owner_rank); });
  out << "      </CellData>\n      <Points>\n"
      << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n          ";
  for (int cell = 0; cell < cells; ++cell) {
    for (const Vec2 point : mesh_.cells[static_cast<std::size_t>(cell)].polygon) {
      out << number(point.x) << ' ' << number(point.y) << " 0 ";
    }
  }
  out << "\n        </DataArray>\n      </Points>\n      <Cells>\n"
      << "        <DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n          ";
  int point_offset = 0;
  for (int cell = 0; cell < cells; ++cell) {
    const int count = static_cast<int>(mesh_.cells[static_cast<std::size_t>(cell)].polygon.size());
    for (int point = 0; point < count; ++point) out << point_offset + point << ' ';
    point_offset += count;
  }
  out << "\n        </DataArray>\n"
      << "        <DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n          ";
  point_offset = 0;
  for (int cell = 0; cell < cells; ++cell) {
    point_offset += static_cast<int>(mesh_.cells[static_cast<std::size_t>(cell)].polygon.size());
    out << point_offset << ' ';
  }
  out << "\n        </DataArray>\n"
      << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n          ";
  for (int cell = 0; cell < cells; ++cell) out << "7 ";  // VTK_POLYGON
  out << "\n        </DataArray>\n      </Cells>\n    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n";
  require_open(out, path);

  check_mpi(MPI_Barrier(communicator_), "MPI_Barrier field pieces failed");
  if (rank_ == 0) {
    const auto master_path = directory_ / "field_final.pvtu";
    std::ofstream master(master_path, std::ios::trunc);
    require_open(master, master_path);
    master << "<?xml version=\"1.0\"?>\n"
           << "<VTKFile type=\"PUnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n"
           << "  <PUnstructuredGrid GhostLevel=\"0\">\n"
           << "    <PCellData Scalars=\"density\">\n";
    for (const char* name : {"density", "u", "v", "pressure", "mach", "temperature", "owner"}) {
      master << "      <PDataArray type=\"Float64\" Name=\"" << name << "\"/>\n";
    }
    master << "    </PCellData>\n    <PPoints>\n"
           << "      <PDataArray type=\"Float64\" NumberOfComponents=\"3\"/>\n"
           << "    </PPoints>\n";
    for (int rank = 0; rank < ranks_; ++rank) {
      master << "    <Piece Source=\"" << rank_file_name("field_final", rank, ".vtu") << "\"/>\n";
    }
    master << "  </PUnstructuredGrid>\n</VTKFile>\n";
  }
}

void OutputWriter::write_restart(const std::vector<Conserved>& states) const {
  if (!initialized_) throw std::runtime_error("OutputWriter::initialize must be called before restart output");
  if (states.size() < static_cast<std::size_t>(mesh_.owned_count)) {
    throw std::runtime_error("restart state array does not cover all owned cells");
  }
  std::ostringstream restart_name;
  restart_name << "restart_final.rank" << std::setw(4) << std::setfill('0') << rank_ << ".bin";
  const auto path = directory_ / restart_name.str();
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  require_open(out, path);
  constexpr std::array<char, 8> magic{{'C', 'F', 'D', 'R', 'S', 'T', '1', '\0'}};
  const std::uint32_t version = 1;
  const std::int32_t rank = rank_;
  const std::int32_t ranks = ranks_;
  const std::int32_t owned = mesh_.owned_count;
  out.write(magic.data(), static_cast<std::streamsize>(magic.size()));
  out.write(reinterpret_cast<const char*>(&version), sizeof(version));
  out.write(reinterpret_cast<const char*>(&rank), sizeof(rank));
  out.write(reinterpret_cast<const char*>(&ranks), sizeof(ranks));
  out.write(reinterpret_cast<const char*>(&owned), sizeof(owned));
  for (int cell = 0; cell < mesh_.owned_count; ++cell) {
    const std::int32_t global_id = mesh_.cells[static_cast<std::size_t>(cell)].global_id;
    out.write(reinterpret_cast<const char*>(&global_id), sizeof(global_id));
    out.write(reinterpret_cast<const char*>(states[static_cast<std::size_t>(cell)].data()),
              static_cast<std::streamsize>(sizeof(double) * states[static_cast<std::size_t>(cell)].size()));
  }
  require_open(out, path);
}

void OutputWriter::write_metadata_and_status(const RunSummary& summary) const {
  if (!initialized_) throw std::runtime_error("OutputWriter::initialize must be called before JSON output");
  if (summary.convergence_status != "converged" && summary.convergence_status != "statistically_periodic" &&
      summary.convergence_status != "failed") {
    throw std::runtime_error("invalid convergence status");
  }
  if (rank_ != 0) return;
  residual_stream_.flush();
  force_stream_.flush();
  const auto metadata_path = directory_ / "metadata.json";
  std::ofstream metadata(metadata_path, std::ios::trunc);
  require_open(metadata, metadata_path);
  metadata << "{\n";
  write_json_string(metadata, "case_id", config_.case_id);
  write_json_string(metadata, "solver_name", summary.solver_name);
  write_json_string(metadata, "solver_version", summary.solver_version);
  if (summary.git_revision.empty()) metadata << "  \"git_revision\": null,\n";
  else write_json_string(metadata, "git_revision", summary.git_revision);
  write_json_integer(metadata, "mpi_ranks", ranks_);
  write_json_string(metadata, "mesh_file", config_.mesh.file.string());
  write_json_integer(metadata, "num_cells_global", mesh_.global_cells);
  write_json_integer(metadata, "num_faces_global", mesh_.global_faces);
  write_json_integer(metadata, "num_cells_owned_local", mesh_.owned_count);
  write_json_integer(metadata, "num_cells_ghost_local", mesh_.ghost_count());
  write_json_string(metadata, "partitioner", "METIS_Kway");
  write_json_integer(metadata, "partition_edge_cut", mesh_.partition_edge_cut);
  write_json_string(metadata, "halo_exchange", "neighbor_isend_irecv");
  write_json_bool(metadata, "full_state_replication_during_iterations", false);
  write_json_bool(metadata, "full_mesh_replication_during_iterations", false);
  write_json_string(metadata, "equation_set", "compressible_navier_stokes_2d");
  write_json_string(metadata, "inviscid_flux", "rusanov_local_lax_friedrichs");
  metadata << "  \"entropy_fix\": null,\n";
  write_json_string(metadata, "viscous_flux",
                    config_.physics.mode == "laminar"
                        ? "newtonian_stress_fourier_heat_flux_constant_viscosity"
                        : "disabled");
  write_json_string(metadata, "time_integrator",
                    config_.run_control.type == "transient"
                        ? "bdf2_with_backward_euler_startup"
                        : "local_pseudo_time");
  write_json_string(metadata, "implicit_solver",
                    "distributed_additive_schwarz_lu_sgs_scalar_spectral_jacobian");
  write_json_number(metadata, "rusanov_dissipation_scale",
                    config_.run_control.rusanov_dissipation_scale.value_or(1.0));
  if (config_.physics.mode == "laminar") {
    write_json_string(metadata, "viscosity_model", "constant_from_freestream_reynolds_number");
    const double viscosity = config_.freestream.rho * config_.freestream.velocity_magnitude *
                             config_.reference.reynolds_length /
                             config_.physics.reynolds.value();
    write_json_number(metadata, "dynamic_viscosity", viscosity);
  } else {
    metadata << "  \"viscosity_model\": null,\n"
             << "  \"dynamic_viscosity\": 0,\n";
  }
  if (config_.run_control.type == "transient") {
    write_json_number(metadata, "physical_time_step", config_.run_control.time_step.value());
    write_json_number(metadata, "requested_final_time", config_.run_control.final_time.value());
    write_json_string(metadata, "inner_residual_norm",
                      config_.run_control.inner_residual_norm.value());
    write_json_string(metadata, "bdf2_history_update",
                      config_.run_control.bdf2_history_update.value());
  } else {
    metadata << "  \"physical_time_step\": null,\n"
             << "  \"requested_final_time\": null,\n"
             << "  \"inner_residual_norm\": null,\n"
             << "  \"bdf2_history_update\": null,\n";
  }
  if (config_.run_control.type == "steady" && config_.freestream.mach < 0.3) {
    write_json_string(metadata, "steady_convergence_acceleration",
                      "distributed_schwarz_lu_sgs_with_cycle_mean_history_filter");
  } else {
    metadata << "  \"steady_convergence_acceleration\": null,\n";
  }
  write_json_string(metadata, "reconstruction", summary.reconstruction);
  write_json_string(metadata, "limiter", summary.limiter);
  write_json_integer(metadata, "spatial_order_claimed", config_.numerics_required.spatial_order);
  write_json_string(metadata, "positivity_preservation", summary.positivity_preservation);
  write_json_string(metadata, "wall_boundary_output_semantics", summary.wall_boundary_output_semantics);
  write_json_bool(metadata, "true_bdf2_inner_loop", summary.true_bdf2_inner_loop);
  write_json_integer(metadata, "typical_inner_iterations",
                     static_cast<int>(std::lround(summary.inner_solve.observed_mean_inner_iterations)));
  write_json_integer(metadata, "min_inner_iterations", config_.run_control.min_inner_iterations);
  write_json_integer(metadata, "max_inner_iterations", config_.run_control.max_inner_iterations);
  write_json_integer(metadata, "observed_min_inner_iterations", summary.inner_solve.observed_min_inner_iterations);
  write_json_integer(metadata, "observed_max_inner_iterations", summary.inner_solve.observed_max_inner_iterations);
  write_json_number(metadata, "observed_mean_inner_iterations", summary.inner_solve.observed_mean_inner_iterations);
  write_json_number(metadata, "inner_residual_reduction_target", config_.run_control.inner_residual_reduction_target);
  write_json_integer(metadata, "inner_target_misses", summary.inner_solve.inner_target_misses);
  write_json_number(metadata, "inner_target_converged_fraction", summary.inner_solve.inner_target_converged_fraction);
  write_json_number(metadata, "last_inner_residual_ratio", summary.inner_solve.last_inner_residual_ratio);
  write_json_string(metadata, "start_time_utc", summary.start_time_utc);
  write_json_string(metadata, "end_time_utc", summary.end_time_utc);
  write_json_bool(metadata, "completed", summary.completed);
  write_json_string(metadata, "convergence_status", summary.convergence_status, false);
  metadata << "}\n";

  const auto status_path = directory_ / "run_status.json";
  std::ofstream status(status_path, std::ios::trunc);
  require_open(status, status_path);
  status << "{\n";
  write_json_string(status, "case_id", config_.case_id);
  write_json_string(status, "command", summary.command);
  write_json_integer(status, "mpi_ranks", ranks_);
  write_json_number(status, "wall_time_seconds", summary.wall_time_seconds);
  write_json_integer(status, "final_step", summary.final_step);
  write_json_number(status, "final_physical_time", summary.final_physical_time);
  write_json_string(status, "convergence_status", summary.convergence_status);
  write_json_number(status, "residual_reduction_orders", summary.residual_reduction_orders);
  write_json_string(status, "notes", summary.notes, false);
  status << "}\n";
}

}  // namespace cfd
