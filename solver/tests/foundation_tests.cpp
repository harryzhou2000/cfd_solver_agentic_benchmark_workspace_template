#include "cfd/config.hpp"
#include "cfd/mesh.hpp"
#include "cfd/partition.hpp"
#include "cfd/types.hpp"

#include <mpi.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error("test assertion failed: " + message);
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  require(static_cast<bool>(input), "open " + path.string());
  std::ostringstream result;
  result << input.rdbuf();
  return result.str();
}

void test_types() {
  const cfd::Vec2 a{3.0, 4.0};
  const cfd::Vec2 b{-1.0, 2.0};
  require(std::abs(cfd::norm(a) - 5.0) < 1.0e-14, "Vec2 norm");
  require(cfd::dot(a, b) == 5.0, "Vec2 dot product");
  require(cfd::cross(a, b) == 10.0, "Vec2 cross product");
  require((a + b).x == 2.0 && (a + b).y == 6.0, "Vec2 addition");
  require(cfd::is_nearly_planar_for_xy_projection(100.0, 9.0e-6),
          "documented relative planarity tolerance accepts small export noise");
  require(!cfd::is_nearly_planar_for_xy_projection(100.0, 2.0e-5),
          "materially nonplanar geometry is rejected before XY projection");
  require(cfd::resolve_cgns_boundary_family("wall", std::nullopt, false) == "wall",
          "non-FamilySpecified BC may fall back to its BC_t node name");
  require(cfd::resolve_cgns_boundary_family("wall", std::string_view("wall-family"), true) ==
              "wall-family",
          "FamilySpecified BC resolves its FamilyName_t");
  bool rejected_missing_family = false;
  try {
    (void)cfd::resolve_cgns_boundary_family("wall", std::nullopt, true);
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    rejected_missing_family = message.find("FamilySpecified") != std::string::npos &&
                              message.find("FamilyName_t") != std::string::npos;
  }
  require(rejected_missing_family,
          "FamilySpecified BC without FamilyName_t receives a clear error");
}

void test_config(const std::filesystem::path& benchmark) {
  const auto cases = benchmark / "inputs" / "cases";
  const std::vector<std::string> names{
      "naca0012_m015_inviscid.json",       "naca0012_m080_inviscid.json",
      "naca0012_m200_inviscid.json",       "naca0012_m015_laminar_re5000.json",
      "naca0012_m080_laminar_re5000.json", "naca0012_m200_laminar_re5000.json",
      "cylinder_m010_laminar_re20.json",   "cylinder_m010_laminar_re200.json"};
  for (const std::string& name : names) {
    const cfd::CaseConfig config = cfd::parse_case_file(cases / name);
    require(config.schema_version == 1, "schema version for " + name);
    require(config.mesh.file.is_absolute(), "absolute resolved mesh path for " + name);
    require(std::filesystem::is_regular_file(config.mesh.file), "resolved mesh exists for " + name);
    require(config.gas.gamma == 1.4 && config.gas.gas_constant == 1.0,
            "gas data for " + name);
    require(config.freestream.rho == 1.0 && config.reference.length == 1.0,
            "freestream/reference data for " + name);
    require(config.boundary_conditions.size() == 2U, "boundary map for " + name);
  }

  const cfd::CaseConfig transient =
      cfd::parse_case_file(cases / "cylinder_m010_laminar_re200.json");
  require(transient.run_control.type == cfd::RunType::transient, "transient run type");
  require(transient.run_control.time_step == 0.01, "transient time step");
  require(transient.run_control.final_time == 300.0, "transient final time");
  require(transient.run_control.inner_residual_norm == "total_spatial_plus_physical_time",
          "transient residual norm");
  require(transient.run_control.bdf2_history_update == "after_inner_convergence",
          "BDF2 history semantics");
  require(transient.outputs.recommended_vorticity_clip_range.has_value(),
          "transient clip range");

  std::string invalid = read_text(cases / "naca0012_m015_inviscid.json");
  const std::string marker = "\"schema_version\": 1,";
  const auto position = invalid.find(marker);
  require(position != std::string::npos, "schema marker in fixture");
  invalid.insert(position + marker.size(), "\n  \"unexpected\": 7,");
  bool rejected = false;
  try {
    (void)cfd::parse_case_json(invalid, cases, "strict-test.json");
  } catch (const std::runtime_error& error) {
    rejected = std::string(error.what()).find("unknown field") != std::string::npos &&
               std::string(error.what()).find("$.unexpected") != std::string::npos;
  }
  require(rejected, "strict parser rejects and locates unknown fields");

  std::string future = read_text(cases / "naca0012_m015_inviscid.json");
  const auto version_position = future.find(marker);
  require(version_position != std::string::npos, "version marker in fixture");
  future.replace(version_position, marker.size(), "\"schema_version\": 2,");
  rejected = false;
  try {
    (void)cfd::parse_case_json(future, cases, "future-schema.json");
  } catch (const std::runtime_error& error) {
    rejected = std::string(error.what()).find("unsupported schema version 2") != std::string::npos;
  }
  require(rejected, "future schema receives a clear unsupported-version error");
}

void check_mesh(const cfd::Mesh& mesh, std::size_t vertices, std::size_t cells,
                std::size_t faces, const std::vector<std::pair<std::string, std::size_t>>& bcs) {
  require(mesh.vertices.size() == vertices, "exact vertex count");
  require(mesh.cells.size() == cells, "exact cell count");
  require(mesh.faces.size() == faces, "exact face count");
  require(mesh.boundary_face_counts.size() == bcs.size(), "exact boundary family count");
  require(mesh.boundary_metadata.size() == bcs.size(), "one metadata row per supplied BC_t");
  for (const auto& item : bcs) {
    const auto found = mesh.boundary_face_counts.find(item.first);
    require(found != mesh.boundary_face_counts.end(), "boundary family " + item.first);
    require(found->second == item.second, "boundary face count for " + item.first);
    require(std::find(mesh.family_names.begin(), mesh.family_names.end(), item.first) !=
                mesh.family_names.end(),
            "base Family_t discovery for " + item.first);
    const auto metadata =
        std::find_if(mesh.boundary_metadata.begin(), mesh.boundary_metadata.end(),
                     [&](const cfd::BoundaryMetadata& value) {
                       return value.bc_name == item.first && value.family == item.first;
                     });
    require(metadata != mesh.boundary_metadata.end(),
            "BC_t to FamilyName_t mapping for " + item.first);
    require(metadata->family_specified, "supplied BCType is FamilySpecified for " + item.first);
    require(metadata->family_name_node_present,
            "supplied BC explicitly carries FamilyName_t for " + item.first);
  }

  std::size_t cell_incidences = 0;
  for (const cfd::Cell& cell : mesh.cells) {
    require(cell.area > 0.0 && std::isfinite(cell.area), "strictly positive cell area");
    require(cell.faces.size() == cell.vertices.size(), "cell face incidence count");
    cell_incidences += cell.faces.size();
  }
  std::size_t face_incidences = 0;
  std::size_t boundary_faces = 0;
  for (const cfd::Face& face : mesh.faces) {
    require(face.left >= 0, "face left incidence");
    ++face_incidences;
    if (face.right >= 0) {
      ++face_incidences;
      require(face.boundary.empty(), "interior face has no physical tag");
    } else {
      ++boundary_faces;
      require(!face.boundary.empty(), "boundary face is physically tagged");
    }
    const cfd::Vec2 edge =
        mesh.vertices[static_cast<std::size_t>(face.vertices[1])].position -
        mesh.vertices[static_cast<std::size_t>(face.vertices[0])].position;
    require(std::abs(cfd::dot(face.normal, edge)) <= 1.0e-12 * face.length,
            "face normal is perpendicular to its edge");
  }
  require(cell_incidences == face_incidences, "cell/face incidence totals agree");
  std::size_t expected_boundaries = 0;
  for (const auto& item : bcs) expected_boundaries += item.second;
  require(boundary_faces == expected_boundaries, "closed geometry has only tagged boundaries");
  cfd::validate_mesh(mesh);
}

void test_distributed(cfd::DistributedMesh& mesh, MPI_Comm communicator) {
  cfd::validate_distributed_mesh(mesh);
  cfd::DistributedMesh second_mesh = mesh;
  require(mesh.owned_cell_count == mesh.diagnostics.owned_cells, "local owned diagnostics");
  require(mesh.cells.size() == mesh.owned_cell_count + mesh.diagnostics.ghost_cells,
          "owned plus one-ring ghost count");
  require(mesh.global_diagnostics.total_owned_cells == mesh.global_cell_count,
          "global owned diagnostics");
  require(mesh.global_diagnostics.ranks.size() == static_cast<std::size_t>(mesh.size),
          "one diagnostic row per rank");
  require(mesh.diagnostics.neighbor_ids.size() == mesh.halo.size(), "neighbor diagnostics");

  for (std::size_t i = 0; i < mesh.owned_cell_count; ++i) {
    require(mesh.cells[i].owned && mesh.cells[i].owner == mesh.rank, "owned cells are first");
  }
  for (std::size_t i = mesh.owned_cell_count; i < mesh.cells.size(); ++i) {
    require(!mesh.cells[i].owned && mesh.cells[i].owner != mesh.rank, "ghost ownership");
    bool touches_owned = false;
    for (const cfd::LocalFace& face : mesh.faces) {
      if (face.left_cell == static_cast<cfd::LocalIndex>(i) && face.right_cell >= 0) {
        touches_owned = mesh.cells[static_cast<std::size_t>(face.right_cell)].owned;
      }
      if (face.right_cell == static_cast<cfd::LocalIndex>(i)) {
        touches_owned = mesh.cells[static_cast<std::size_t>(face.left_cell)].owned;
      }
      if (touches_owned) break;
    }
    require(touches_owned, "every ghost is exactly one ring from an owned cell");
  }
  for (const cfd::LocalFace& face : mesh.faces) {
    const bool left_owned = mesh.cells[static_cast<std::size_t>(face.left_cell)].owned;
    const bool right_owned = face.right_cell >= 0 &&
                             mesh.cells[static_cast<std::size_t>(face.right_cell)].owned;
    require(left_owned || right_owned, "local faces are needed by an owned residual");
    const cfd::Vec2 edge =
        mesh.vertices[static_cast<std::size_t>(face.vertices[1])].position -
        mesh.vertices[static_cast<std::size_t>(face.vertices[0])].position;
    require(std::abs(cfd::dot(face.normal, edge)) <= 1.0e-12 * face.length,
            "serialized local face normal remains perpendicular");
  }

  for (std::size_t i = 0; i < mesh.halo.size(); ++i) {
    const cfd::NeighborSchedule& schedule = mesh.halo[i];
    require(schedule.rank == mesh.diagnostics.neighbor_ids[i], "ordered neighbor IDs");
    require(schedule.send_global_cells.size() == mesh.diagnostics.send_counts[i],
            "exact send diagnostic");
    require(schedule.receive_global_cells.size() == mesh.diagnostics.receive_counts[i],
            "exact receive diagnostic");
    require(schedule.send_global_cells.size() == schedule.send_local_cells.size(),
            "send global/local schedule widths");
    require(schedule.receive_global_cells.size() == schedule.receive_local_cells.size(),
            "receive global/local schedule widths");
    for (cfd::LocalIndex cell : schedule.send_local_cells) {
      require(mesh.cells[static_cast<std::size_t>(cell)].owned, "only owned cells are sent");
    }
    for (cfd::LocalIndex cell : schedule.receive_local_cells) {
      require(!mesh.cells[static_cast<std::size_t>(cell)].owned, "only ghosts are received");
    }
    const auto& remote = mesh.global_diagnostics.ranks[static_cast<std::size_t>(schedule.rank)];
    const auto remote_it = std::find(remote.neighbor_ids.begin(), remote.neighbor_ids.end(), mesh.rank);
    require(remote_it != remote.neighbor_ids.end(), "neighbor relation is symmetric");
    const std::size_t remote_index = static_cast<std::size_t>(remote_it - remote.neighbor_ids.begin());
    require(schedule.send_global_cells.size() == remote.receive_counts[remote_index],
            "send/remote-receive counts agree");
    require(schedule.receive_global_cells.size() == remote.send_counts[remote_index],
            "receive/remote-send counts agree");
  }

  unsigned long long local_owned = static_cast<unsigned long long>(mesh.owned_cell_count);
  unsigned long long owned_sum = 0;
  MPI_Allreduce(&local_owned, &owned_sum, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, communicator);
  require(owned_sum == mesh.global_cell_count, "METIS owned-cell sum");
  if (mesh.size > 1) {
    require(mesh.cells.size() < mesh.global_cell_count, "no rank retains the full global cell mesh");
    require(!mesh.halo.empty(), "connected partition has halo neighbors");
  }

  const auto exchange_and_verify = [](cfd::DistributedMesh& target, std::size_t width,
                                      MPI_Comm target_communicator) {
    std::vector<double> values(target.cells.size() * width, -1.0);
    for (std::size_t cell = 0; cell < target.owned_cell_count; ++cell) {
      for (std::size_t component = 0; component < width; ++component) {
        values[cell * width + component] = static_cast<double>(
            target.cells[cell].global_id * 10 + static_cast<cfd::GlobalId>(component));
      }
    }
    cfd::exchange_halo(target, values, width, target_communicator);
    for (std::size_t cell = 0; cell < target.cells.size(); ++cell) {
      for (std::size_t component = 0; component < width; ++component) {
        const double expected = static_cast<double>(
            target.cells[cell].global_id * 10 + static_cast<cfd::GlobalId>(component));
        require(values[cell * width + component] == expected, "fixed-width halo value");
      }
    }
  };
  const auto same_capacities = [](const cfd::HaloExchangeWorkspaceStatistics& lhs,
                                  const cfd::HaloExchangeWorkspaceStatistics& rhs) {
    return lhs.cached_widths == rhs.cached_widths &&
           lhs.neighbor_buffer_capacity == rhs.neighbor_buffer_capacity &&
           lhs.send_value_capacity == rhs.send_value_capacity &&
           lhs.receive_value_capacity == rhs.receive_value_capacity &&
           lhs.request_capacity == rhs.request_capacity &&
           lhs.growth_events == rhs.growth_events;
  };

  exchange_and_verify(mesh, 4U, communicator);
  exchange_and_verify(mesh, 8U, communicator);
  const auto warmed = mesh.halo_exchange_workspace.statistics();
  exchange_and_verify(mesh, 4U, communicator);
  exchange_and_verify(mesh, 8U, communicator);
  require(same_capacities(warmed, mesh.halo_exchange_workspace.statistics()),
          "alternating halo widths allocate no storage after warmup");

  MPI_Comm duplicate_communicator = MPI_COMM_NULL;
  MPI_Comm_dup(communicator, &duplicate_communicator);
  exchange_and_verify(second_mesh, 4U, duplicate_communicator);
  exchange_and_verify(second_mesh, 8U, duplicate_communicator);
  const auto second_warmed = second_mesh.halo_exchange_workspace.statistics();
  exchange_and_verify(second_mesh, 8U, duplicate_communicator);
  exchange_and_verify(second_mesh, 4U, duplicate_communicator);
  require(same_capacities(second_warmed, second_mesh.halo_exchange_workspace.statistics()),
          "second mesh and communicator have stable independent halo storage");
  require(same_capacities(warmed, mesh.halo_exchange_workspace.statistics()),
          "second mesh exchange does not alter first mesh workspace");
  MPI_Comm_free(&duplicate_communicator);
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  try {
    const std::filesystem::path benchmark =
        std::filesystem::absolute(std::filesystem::path(BENCHMARK_ROOT)).lexically_normal();
    std::unique_ptr<cfd::Mesh> naca;
    if (rank == 0) {
      test_types();
      test_config(benchmark);
      naca = std::make_unique<cfd::Mesh>(
          cfd::read_cgns_mesh(benchmark / "inputs" / "meshes" / "NACA0012_H2.cgns"));
      check_mesh(*naca, 15682U, 20816U, 36498U, {{"bc-2", 80U}, {"bc-4", 404U}});
    }
    cfd::DistributedMesh local_naca =
        cfd::partition_and_distribute(std::move(naca), MPI_COMM_WORLD, 0);
    test_distributed(local_naca, MPI_COMM_WORLD);

    std::unique_ptr<cfd::Mesh> cylinder;
    if (rank == 0) {
      cylinder = std::make_unique<cfd::Mesh>(
          cfd::read_cgns_mesh(benchmark / "inputs" / "meshes" / "CylinderB1.cgns"));
      check_mesh(*cylinder, 9995U, 10185U, 20180U, {{"FAR", 20U}, {"WALL", 100U}});
      require(cylinder->zone_names.size() == 2U, "cylinder dynamically discovered two zones");
    }
    cfd::DistributedMesh local_cylinder =
        cfd::partition_and_distribute(std::move(cylinder), MPI_COMM_WORLD, 0);
    test_distributed(local_cylinder, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) std::cout << "foundation tests passed\n";
    MPI_Finalize();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "rank " << rank << ": " << error.what() << '\n';
    MPI_Abort(MPI_COMM_WORLD, 1);
    return 1;
  }
}
