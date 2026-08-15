#include "cfd/output.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace cfd {

CsvHistory::CsvHistory(const fs::path& path) { open(path); }

void CsvHistory::open(const fs::path& path) {
  out_.open(path);
  if (!out_) throw CfdError("failed to open CSV for writing: " + path.string());
}

void ensure_output_dir(const fs::path& output_dir) {
  std::error_code ec;
  fs::create_directories(output_dir, ec);
  if (ec) throw CfdError("failed to create output directory " + output_dir.string() + ": " +
                         ec.message());
}

std::string json_escape(const std::string& value) {
  std::ostringstream os;
  for (char c : value) {
    switch (c) {
      case '\\':
        os << "\\\\";
        break;
      case '"':
        os << "\\\"";
        break;
      case '\n':
        os << "\\n";
        break;
      case '\r':
        os << "\\r";
        break;
      case '\t':
        os << "\\t";
        break;
      default:
        os << c;
    }
  }
  return os.str();
}

namespace {

std::string iso_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&t, &tm);
  std::ostringstream os;
  os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return os.str();
}

std::string join_ints(const std::vector<int>& values, char sep = ' ') {
  std::ostringstream os;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) os << sep;
    os << values[i];
  }
  return os.str();
}

}  // namespace

void write_metadata(const fs::path& output_dir, const CaseConfig& cfg, const LocalMesh& local,
                    const RunSummary& summary, const PartitionDiagnostics& diag) {
  std::ofstream out(output_dir / "metadata.json");
  if (!out) throw CfdError("failed to write metadata.json");
  const bool transient = cfg.run_control.type == RunType::Transient;
  const std::string viscous_flux =
      cfg.mode == PhysicsMode::Laminar ? "green_gauss_gradient_newtonian_stress_fourier_heat"
                                       : "disabled_zero_viscosity";
  out << std::setprecision(16);
  out << "{\n";
  out << "  \"case_id\": \"" << json_escape(cfg.case_id) << "\",\n";
  out << "  \"solver_name\": \"agentic_cfd_solver\",\n";
  out << "  \"solver_version\": \"0.1.0\",\n";
  out << "  \"git_revision\": null,\n";
  out << "  \"mpi_ranks\": " << summary.mpi_ranks << ",\n";
  out << "  \"mesh_file\": \"" << json_escape(cfg.mesh_file.string()) << "\",\n";
  out << "  \"num_cells_global\": " << local.global_num_cells << ",\n";
  out << "  \"num_faces_global\": " << local.global_num_faces << ",\n";
  out << "  \"num_cells_owned_local\": " << diag.num_cells_owned << ",\n";
  out << "  \"num_cells_ghost_local\": " << diag.num_cells_ghost << ",\n";
  out << "  \"partitioner\": \"metis_kway_graph_partition\",\n";
  out << "  \"partition_edge_cut\": " << summary.edge_cut << ",\n";
  out << "  \"halo_exchange\": \"neighbor_isend_irecv_conservative_state\",\n";
  out << "  \"full_state_replication_during_iterations\": false,\n";
  out << "  \"full_mesh_replication_during_iterations\": false,\n";
  out << "  \"equation_set\": \"compressible_navier_stokes_2d\",\n";
  out << "  \"inviscid_flux\": \"rusanov_local_lax_friedrichs\",\n";
  out << "  \"entropy_fix\": \"rusanov_scalar_dissipation\",\n";
  out << "  \"viscous_flux\": \"" << viscous_flux << "\",\n";
  out << "  \"time_integrator\": \""
      << (transient ? "bdf2_dual_time_outer_physical_inner_relaxation"
                    : "steady_local_pseudo_time") << "\",\n";
  out << "  \"implicit_solver\": \"block_jacobi_diagonal_lusgs_style_inner_relaxation\",\n";
  out << "  \"reconstruction\": \"least_squares_piecewise_linear_cell_centered\",\n";
  out << "  \"limiter\": \"barth_jespersen_scalar_limiter_with_positivity_fallback\",\n";
  out << "  \"spatial_order_claimed\": 2,\n";
  out << "  \"positivity_preservation\": \"density_pressure_floor_and_limited_face_values\",\n";
  out << "  \"wall_boundary_output_semantics\": \"boundary_value\",\n";
  out << "  \"true_bdf2_inner_loop\": " << (transient ? "true" : "false") << ",\n";
  out << "  \"typical_inner_iterations\": " << summary.mean_inner_iterations << ",\n";
  out << "  \"min_inner_iterations\": " << summary.min_inner_iterations << ",\n";
  out << "  \"max_inner_iterations\": " << summary.max_inner_iterations << ",\n";
  out << "  \"observed_min_inner_iterations\": " << summary.observed_min_inner_iterations << ",\n";
  out << "  \"observed_max_inner_iterations\": " << summary.observed_max_inner_iterations << ",\n";
  out << "  \"inner_residual_reduction_target\": "
      << cfg.run_control.inner_residual_reduction_target << ",\n";
  out << "  \"inner_target_misses\": " << summary.inner_target_misses << ",\n";
  out << "  \"inner_target_converged_fraction\": "
      << summary.inner_target_converged_fraction << ",\n";
  out << "  \"last_inner_residual_ratio\": " << summary.last_inner_residual_ratio << ",\n";
  out << "  \"start_time_utc\": \"" << iso_now() << "\",\n";
  out << "  \"end_time_utc\": \"" << iso_now() << "\",\n";
  out << "  \"completed\": " << (summary.convergence_status == "failed" ? "false" : "true")
      << ",\n";
  out << "  \"convergence_status\": \"" << json_escape(summary.convergence_status) << "\"\n";
  out << "}\n";
}

void write_run_status(const fs::path& output_dir, const CaseConfig& cfg, const RunSummary& summary) {
  std::ofstream out(output_dir / "run_status.json");
  if (!out) throw CfdError("failed to write run_status.json");
  out << std::setprecision(16);
  out << "{\n";
  out << "  \"case_id\": \"" << json_escape(cfg.case_id) << "\",\n";
  out << "  \"command\": \"" << json_escape(summary.command) << "\",\n";
  out << "  \"mpi_ranks\": " << summary.mpi_ranks << ",\n";
  out << "  \"wall_time_seconds\": " << summary.wall_time_seconds << ",\n";
  out << "  \"final_step\": " << summary.final_step << ",\n";
  out << "  \"final_physical_time\": " << summary.final_physical_time << ",\n";
  out << "  \"convergence_status\": \"" << json_escape(summary.convergence_status) << "\",\n";
  out << "  \"residual_reduction_orders\": " << summary.residual_reduction_orders << ",\n";
  const std::string notes = summary.notes.empty() ? "finite-volume run completed by agentic_cfd_solver"
                                                  : summary.notes;
  out << "  \"notes\": \"" << json_escape(notes) << "\"\n";
  out << "}\n";
}

void write_partition_diagnostics(const fs::path& output_dir,
                                 const std::vector<PartitionDiagnostics>& diagnostics) {
  std::ofstream out(output_dir / "partition_diagnostics.csv");
  if (!out) throw CfdError("failed to write partition_diagnostics.csv");
  out << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,"
         "neighbor_ranks,send_cells,recv_cells\n";
  for (const PartitionDiagnostics& d : diagnostics) {
    std::vector<std::string> sends;
    std::vector<std::string> recvs;
    for (const auto& [r, n] : d.send_cells) sends.push_back(std::to_string(r) + ":" + std::to_string(n));
    for (const auto& [r, n] : d.recv_cells) recvs.push_back(std::to_string(r) + ":" + std::to_string(n));
    auto join_str = [](const std::vector<std::string>& xs) {
      std::ostringstream os;
      for (std::size_t i = 0; i < xs.size(); ++i) {
        if (i) os << ' ';
        os << xs[i];
      }
      return os.str();
    };
    out << d.rank << ',' << d.num_cells_owned << ',' << d.num_cells_ghost << ','
        << d.num_boundary_faces << ',' << d.neighbor_ranks.size() << ",\""
        << join_ints(d.neighbor_ranks) << "\",\"" << join_str(sends) << "\",\""
        << join_str(recvs) << "\"\n";
  }
}

void write_surface_csv(const fs::path& output_dir, const std::vector<std::string>& rows) {
  std::ofstream out(output_dir / "surface.csv");
  if (!out) throw CfdError("failed to write surface.csv");
  out << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
  for (const std::string& row : rows) out << row << '\n';
}

void write_restart(const fs::path& output_dir, const LocalMesh& local,
                   const std::vector<Conserved>& state) {
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  std::ostringstream name;
  name << "restart_final.rank" << std::setw(4) << std::setfill('0') << rank << ".json";
  std::ofstream out(output_dir / name.str());
  if (!out) throw CfdError("failed to write restart file");
  out << std::setprecision(16);
  out << "{\n  \"rank\": " << rank << ",\n  \"cells\": [\n";
  bool first = true;
  for (int idx : local.owned_local_indices) {
    if (!first) out << ",\n";
    first = false;
    const Conserved& u = state[static_cast<std::size_t>(idx)];
    out << "    {\"global_id\": " << local.cells[static_cast<std::size_t>(idx)].global_id
        << ", \"U\": [" << u[0] << ", " << u[1] << ", " << u[2] << ", " << u[3] << "]}";
  }
  out << "\n  ]\n}\n";
}

void write_vtu(const fs::path& output_dir, const LocalMesh& local, const CaseConfig& cfg,
               const std::vector<Conserved>& state, int rank) {
  std::ostringstream name;
  name << "field_final.rank" << std::setw(4) << std::setfill('0') << rank << ".vtu";
  std::ofstream out(output_dir / name.str());
  if (!out) throw CfdError("failed to write VTU file");

  std::vector<int> point_used(local.vertices.size(), -1);
  std::vector<int> owned_cells = local.owned_local_indices;
  for (int idx : owned_cells) {
    for (int v : local.cells[static_cast<std::size_t>(idx)].vertices) {
      point_used[static_cast<std::size_t>(v)] = -2;
    }
  }
  int npoints = 0;
  for (int& p : point_used) {
    if (p == -2) p = npoints++;
  }

  out << std::setprecision(16);
  out << "<?xml version=\"1.0\"?>\n";
  out << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
  out << "<UnstructuredGrid><Piece NumberOfPoints=\"" << npoints << "\" NumberOfCells=\""
      << owned_cells.size() << "\">\n";
  out << "<Points><DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
  for (int i = 0; i < static_cast<int>(local.vertices.size()); ++i) {
    if (point_used[static_cast<std::size_t>(i)] >= 0) {
      const Vec2 p = local.vertices[static_cast<std::size_t>(i)];
      out << p.x << ' ' << p.y << " 0\n";
    }
  }
  out << "</DataArray></Points>\n";
  out << "<Cells>\n";
  out << "<DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
  for (int idx : owned_cells) {
    for (int v : local.cells[static_cast<std::size_t>(idx)].vertices) {
      out << point_used[static_cast<std::size_t>(v)] << ' ';
    }
    out << '\n';
  }
  out << "</DataArray>\n";
  out << "<DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
  int offset = 0;
  for (int idx : owned_cells) {
    offset += static_cast<int>(local.cells[static_cast<std::size_t>(idx)].vertices.size());
    out << offset << '\n';
  }
  out << "</DataArray>\n";
  out << "<DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
  for (int idx : owned_cells) {
    const int nv = static_cast<int>(local.cells[static_cast<std::size_t>(idx)].vertices.size());
    out << (nv == 3 ? 5 : 9) << '\n';
  }
  out << "</DataArray>\n";
  out << "</Cells>\n";

  out << "<CellData Scalars=\"pressure\">\n";
  auto write_scalar = [&](const std::string& n, auto fn) {
    out << "<DataArray type=\"Float64\" Name=\"" << n << "\" format=\"ascii\">\n";
    for (int idx : owned_cells) out << fn(idx) << '\n';
    out << "</DataArray>\n";
  };
  write_scalar("density", [&](int idx) {
    return conserved_to_primitive(state[static_cast<std::size_t>(idx)], cfg.gas).rho;
  });
  write_scalar("u", [&](int idx) {
    return conserved_to_primitive(state[static_cast<std::size_t>(idx)], cfg.gas).u;
  });
  write_scalar("v", [&](int idx) {
    return conserved_to_primitive(state[static_cast<std::size_t>(idx)], cfg.gas).v;
  });
  write_scalar("pressure", [&](int idx) {
    return conserved_to_primitive(state[static_cast<std::size_t>(idx)], cfg.gas).p;
  });
  write_scalar("mach", [&](int idx) {
    return thermo_from_primitive(conserved_to_primitive(state[static_cast<std::size_t>(idx)],
                                                        cfg.gas),
                                 cfg.gas)
        .mach;
  });
  write_scalar("total_energy", [&](int idx) {
    const Primitive q = conserved_to_primitive(state[static_cast<std::size_t>(idx)], cfg.gas);
    return thermo_from_primitive(q, cfg.gas).E;
  });
  write_scalar("owner_rank", [&](int) { return static_cast<Real>(rank); });
  out << "</CellData>\n";
  out << "</Piece></UnstructuredGrid></VTKFile>\n";
}

}  // namespace cfd
