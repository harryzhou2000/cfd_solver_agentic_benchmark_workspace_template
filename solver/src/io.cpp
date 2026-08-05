#include "cfd/io.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace cfd {
namespace {
constexpr const char* kResidualHeader = "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
constexpr const char* kForceHeader = "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
constexpr const char* kSurfaceHeader = "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
constexpr const char kRestartMagic[] = "CFDRST01";

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
}  // namespace

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
void OutputWriter::append_force(const ForceRecord& r) const { if (rank_ != 0) return; const auto p = output_dir_ / "forces.csv"; append_header_if_empty(p, kForceHeader); std::ofstream out(p, std::ios::app); out << std::setprecision(17) << r.step << ',' << r.physical_time << ',' << r.cl << ',' << r.cd << ',' << r.cmz << ',' << r.pressure_drag << ',' << r.viscous_drag << ',' << r.pressure_lift << ',' << r.viscous_lift << '\n'; }

void OutputWriter::write_surface(const std::vector<SurfaceRecord>& rows) const {
  std::ostringstream part; part << std::setprecision(17); for (const auto& r : rows) part << r.x << ',' << r.y << ',' << r.nx << ',' << r.ny << ',' << r.pressure << ',' << r.cp << ',' << r.cf << ',' << r.rho << ',' << r.u << ',' << r.v << ',' << r.mach << ",\"" << esc(r.tag) << "\"\n";
  const std::string bytes = part.str(); const int n = static_cast<int>(bytes.size()); std::vector<int> counts(size_), offsets(size_); MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm_); std::vector<char> all; if (rank_ == 0) { for (int i = 1; i < size_; ++i) offsets[i] = offsets[i-1] + counts[i-1]; all.resize(offsets.back() + counts.back()); } MPI_Gatherv(bytes.data(), n, MPI_CHAR, all.data(), counts.data(), offsets.data(), MPI_CHAR, 0, comm_);
  if (rank_ == 0) { std::ofstream out(output_dir_ / "surface.csv"); if (!out) throw std::runtime_error("cannot write surface.csv"); out << kSurfaceHeader; out.write(all.data(), all.size()); }
}

void OutputWriter::write_run_status(const CaseConfig& c, const RunStatus& s) const { if (rank_ != 0) return; std::ofstream out(output_dir_ / "run_status.json"); if (!out) throw std::runtime_error("cannot write run_status.json"); out << std::setprecision(17) << "{\n  \"case_id\": \"" << esc(c.case_id) << "\",\n  \"command\": \"" << esc(s.command) << "\",\n  \"mpi_ranks\": " << size_ << ",\n  \"wall_time_seconds\": " << s.wall_time_seconds << ",\n  \"final_step\": " << s.final_step << ",\n  \"final_physical_time\": " << s.final_physical_time << ",\n  \"convergence_status\": \"" << esc(s.convergence_status) << "\",\n  \"residual_reduction_orders\": " << s.residual_reduction_orders << ",\n  \"notes\": \"" << esc(s.notes) << "\"\n}\n"; }

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

void OutputWriter::write_restart_final(const LocalMesh& mesh, const std::vector<double>& state, int step, double time) const { require_local_state(mesh,state); const auto path=rank_path(output_dir_,"restart_final",rank_,".bin"); std::ofstream out(path,std::ios::binary); if(!out) throw std::runtime_error("cannot write restart"); out.write(kRestartMagic,sizeof(kRestartMagic)); const std::uint32_t version=1; const std::uint64_t count=mesh.owned_cell_count; write_pod(out,version); write_pod(out,step); write_pod(out,time); write_pod(out,count); for(int c=0;c<mesh.owned_cell_count;++c) { const int global=mesh.local_to_global_cell[c]; write_pod(out,global); for(double x:state_at(state,c)) write_pod(out,x); } out.close(); MPI_Barrier(comm_); if(rank_==0) { std::ofstream manifest(output_dir_/"restart_final.manifest.json"); manifest << "{\n  \"format\": \"cfd_rank_local_restart_v1\",\n  \"mpi_ranks\": " << size_ << ",\n  \"step\": " << step << ",\n  \"physical_time\": " << std::setprecision(17) << time << ",\n  \"state_layout\": \"owned_cells_only; global_cell_id plus rho,rhou,rhov,rhoE\"\n}\n"; } }

std::vector<double> OutputWriter::read_restart_local(const LocalMesh& mesh, int& step, double& time) const { const auto path=rank_path(output_dir_,"restart_final",rank_,".bin"); std::ifstream in(path,std::ios::binary); if(!in) throw std::runtime_error("cannot read restart " + path.string()); std::array<char,sizeof(kRestartMagic)> magic{}; in.read(magic.data(),magic.size()); if(!in || !std::equal(magic.begin(),magic.end(),kRestartMagic)) throw std::runtime_error("invalid restart magic"); std::uint32_t version=0; std::uint64_t count=0; read_pod(in,version); read_pod(in,step); read_pod(in,time); read_pod(in,count); if(version!=1 || count!=static_cast<std::uint64_t>(mesh.owned_cell_count)) throw std::runtime_error("restart partition does not match current local mesh"); std::vector<double> result(static_cast<size_t>(mesh.owned_cell_count)*4); for(int c=0;c<mesh.owned_cell_count;++c) { int global=-1; read_pod(in,global); if(global!=mesh.local_to_global_cell[c]) throw std::runtime_error("restart global-cell ordering does not match current partition"); for(int k=0;k<4;++k) read_pod(in,result[static_cast<size_t>(c)*4+k]); } return result; }

}  // namespace cfd
