#include "partition.hpp"

#include <mpi.h>

#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>

using namespace aerofv;

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  try {
    if (argc != 2) {
      throw std::runtime_error("usage: test_partition_mpi mesh.cgns");
    }
    std::optional<GlobalMesh> global;
    if (rank == 0) {
      global.emplace(read_cgns_unstructured_2d(argv[1]));
    }
    LocalMesh local = partition_and_distribute(
        rank == 0 ? &*global : nullptr, MPI_COMM_WORLD);
    global.reset(); // The full mesh is not retained into solver execution.

    assert(local.owned_cell_count > 0);
    assert(static_cast<std::size_t>(local.owned_cell_count) <= local.cells.size());
    assert(local.cells.size() == local.global_cell_ids.size());
    assert(local.faces.size() == local.global_face_ids.size());
    assert(local.vertices.size() == local.global_vertex_ids.size());
    for (std::size_t cell_id = 0; cell_id < local.cells.size(); ++cell_id) {
      const Cell &cell = local.cells[cell_id];
      assert(cell.area > 0.0);
      for (int vertex : cell.vertices) {
        assert(vertex >= 0 && static_cast<std::size_t>(vertex) < local.vertices.size());
      }
      if (cell_id < static_cast<std::size_t>(local.owned_cell_count)) {
        for (int face_id : cell.faces) {
          assert(face_id >= 0 && static_cast<std::size_t>(face_id) < local.faces.size());
          const Face &face = local.faces[static_cast<std::size_t>(face_id)];
          assert(face.left_cell == static_cast<int>(cell_id) ||
                 face.right_cell == static_cast<int>(cell_id));
        }
      }
    }
    for (const Face &face : local.faces) {
      assert(face.left_cell >= 0 || face.right_cell >= 0);
      assert(face.left_cell < static_cast<int>(local.cells.size()));
      assert(face.right_cell < static_cast<int>(local.cells.size()));
    }
    for (const NeighborExchange &exchange : local.exchanges) {
      assert(exchange.rank >= 0 && exchange.rank != rank);
      for (int index : exchange.send_owned_local) {
        assert(local.is_owned_cell(index));
      }
      for (int index : exchange.receive_ghost_local) {
        assert(index >= local.owned_cell_count &&
               static_cast<std::size_t>(index) < local.cells.size());
      }
    }

    const std::int64_t owned = local.owned_cell_count;
    std::int64_t owned_global = 0;
    MPI_Allreduce(&owned, &owned_global, 1, MPI_INT64_T, MPI_SUM, MPI_COMM_WORLD);
    assert(owned_global == local.global_cell_count);
    if (rank == 0) {
      std::cout << "distributed " << owned_global << " cells; METIS edge cut "
                << local.edge_cut << '\n';
    }
    MPI_Finalize();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rank " << rank << ": " << error.what() << '\n';
    MPI_Abort(MPI_COMM_WORLD, 1);
    return 1;
  }
}
