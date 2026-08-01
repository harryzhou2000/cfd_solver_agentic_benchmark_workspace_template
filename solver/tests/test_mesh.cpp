#include "cfd/mesh.hpp"
#include "cfd/partition.hpp"

#include <mpi.h>

#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {

using cfd::Face;
using cfd::Mesh;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
void expect(bool value, const std::string& message) { if (!value) fail(message); }

std::map<std::string, int> boundary_counts(const Mesh& mesh) {
    std::map<std::string, int> counts;
    for (const Face& face : mesh.faces) if (face.neighbor < 0) ++counts[face.tag];
    return counts;
}

void assert_naca_mesh(const std::filesystem::path& path) {
    const Mesh mesh = cfd::read_cgns_mesh(path);
    expect(mesh.cell_dimension == 2 && mesh.physical_dimension == 3, "NACA base dimensions");
    expect(mesh.nodes.size() == 15682U, "NACA vertex count");
    expect(mesh.cells.size() == 20816U, "NACA cell count");
    expect(mesh.faces.size() == 36498U, "NACA face count");
    std::size_t tri = 0, quad = 0;
    for (const auto& c : mesh.cells) { expect(c.area > 0.0, "NACA positive cell area"); tri += c.vertex_count == 3; quad += c.vertex_count == 4; }
    expect(tri == 10752U && quad == 10064U, "NACA TRI/QUAD count");
    const auto tags = boundary_counts(mesh);
    expect(tags.size() == 2U && tags.at("bc-2") == 80 && tags.at("bc-4") == 404, "NACA family edge tags");
}

void assert_cylinder_mesh(const std::filesystem::path& path) {
    const Mesh mesh = cfd::read_cgns_mesh(path);
    expect(mesh.cell_dimension == 2 && mesh.physical_dimension == 2, "cylinder base dimensions");
    expect(mesh.nodes.size() == 9995U, "cylinder PointList-stiched vertex count");
    expect(mesh.cells.size() == 10185U, "cylinder cell count");
    expect(mesh.faces.size() == 20180U, "cylinder global face count");
    std::size_t tri = 0, quad = 0;
    for (const auto& c : mesh.cells) { expect(c.area > 0.0, "cylinder positive cell area"); tri += c.vertex_count == 3; quad += c.vertex_count == 4; }
    expect(tri == 500U && quad == 9685U, "cylinder TRI/QUAD count");
    const auto tags = boundary_counts(mesh);
    expect(tags.size() == 2U && tags.at("WALL") == 100 && tags.at("FAR") == 20, "cylinder family edge tags");
}

void assert_distribution(const std::filesystem::path& mesh_path, MPI_Comm comm) {
    int rank = 0, size = 1;
    MPI_Comm_rank(comm, &rank); MPI_Comm_size(comm, &size);
    Mesh root_mesh;
    cfd::PartitionResult partition;
    if (rank == 0) {
        root_mesh = cfd::read_cgns_mesh(mesh_path);
        partition = cfd::partition_metis(root_mesh, size);
    }
    const auto expected_cells = rank == 0 ? static_cast<long long>(root_mesh.cells.size()) : 0LL;
    long long global_cells = 0;
    MPI_Allreduce(&expected_cells, &global_cells, 1, MPI_LONG_LONG, MPI_SUM, comm);
    const auto local = cfd::distribute_mesh(root_mesh, partition, comm);
    expect(root_mesh.empty(), "rank zero must clear global preprocessing mesh");
    expect(!local.has_global_mesh(), "rank-local mesh must not retain global mesh");
    expect(local.global_cell_count == global_cells, "persistent global cell count after distribution");
    const long long expected_faces = mesh_path.filename() == "NACA0012_H2.cgns" ? 36498LL : 20180LL;
    expect(local.global_face_count == expected_faces, "persistent global face count after distribution");
    expect(local.partition_edge_cut >= 0, "persistent partition edge cut");
    expect(local.owned_count > 0U, "every rank owns cells");
    long long owned_sum = static_cast<long long>(local.owned_count);
    MPI_Allreduce(MPI_IN_PLACE, &owned_sum, 1, MPI_LONG_LONG, MPI_SUM, comm);
    expect(owned_sum == global_cells, "distributed owned-cell total");
    if (size > 1) {
        long long ghost_sum = static_cast<long long>(local.cells.size() - local.owned_count);
        MPI_Allreduce(MPI_IN_PLACE, &ghost_sum, 1, MPI_LONG_LONG, MPI_SUM, comm);
        expect(ghost_sum > 0, "parallel partition must create one-ring ghosts");
    }
    std::vector<cfd::State> values(local.cells.size());
    for (std::size_t i = 0; i < local.owned_count; ++i) values[i].fill(static_cast<double>(local.cells[i].cell.global_id));
    cfd::exchange_halo_packets(local.halo, values, comm, 29101);
    for (std::size_t i = local.owned_count; i < local.cells.size(); ++i) {
        expect(values[i][0] == static_cast<double>(local.cells[i].cell.global_id), "halo state owner/global-id correspondence");
    }
}

} // namespace

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank = 0; MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    int failed = 0;
    try {
        const std::filesystem::path mesh_dir{CFD_BENCHMARK_MESH_DIR};
        if (rank == 0) { assert_naca_mesh(mesh_dir / "NACA0012_H2.cgns"); assert_cylinder_mesh(mesh_dir / "CylinderB1.cgns"); }
        MPI_Barrier(MPI_COMM_WORLD);
        assert_distribution(mesh_dir / "NACA0012_H2.cgns", MPI_COMM_WORLD);
        assert_distribution(mesh_dir / "CylinderB1.cgns", MPI_COMM_WORLD);
        if (rank == 0) std::cout << "mesh/distribution tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "rank " << rank << " mesh test failure: " << error.what() << '\n'; failed = 1;
    }
    int global_failed = 0; MPI_Allreduce(&failed, &global_failed, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    MPI_Finalize();
    return global_failed == 0 ? 0 : 1;
}
