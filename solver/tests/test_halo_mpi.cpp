#include "halo.hpp"

#include <mpi.h>

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace aerofv;

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

LocalMesh synthetic_mesh(int rank, int ranks) {
  LocalMesh mesh;
  mesh.owned_cell_count = 1;
  mesh.cells.resize(static_cast<std::size_t>(ranks));
  mesh.global_cell_ids.push_back(rank);
  for (int peer = 0; peer < ranks; ++peer) {
    if (peer == rank) {
      continue;
    }
    mesh.global_cell_ids.push_back(peer);
    NeighborExchange exchange;
    exchange.rank = peer;
    exchange.send_owned_local = {0};
    exchange.receive_ghost_local = {
        static_cast<int>(mesh.global_cell_ids.size() - 1)};
    mesh.exchanges.push_back(std::move(exchange));
  }
  return mesh;
}

void verify_values(const LocalMesh &mesh, const std::vector<Conservative> &values,
                   double base) {
  require(values.size() == mesh.cells.size(), "incorrect state vector size");
  for (std::size_t i = 1; i < values.size(); ++i) {
    const double peer = static_cast<double>(mesh.global_cell_ids[i]);
    require(std::abs(values[i][0] - (base + peer)) < 1.0e-12,
            "ghost conservative value differs from neighbor owned value");
    require(std::abs(values[i][3] - (base + 30.0 + peer)) < 1.0e-12,
            "ghost conservative component differs from neighbor owned value");
  }
}

} // namespace

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  int ranks = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &ranks);
  try {
    const LocalMesh mesh = synthetic_mesh(rank, ranks);
    std::vector<Conservative> state(mesh.cells.size(), {-1.0, -1.0, -1.0, -1.0});
    state[0] = {10.0 + rank, 20.0 + rank, 30.0 + rank, 40.0 + rank};
    exchange_conservative_halo(mesh, state, MPI_COMM_WORLD);
    verify_values(mesh, state, 10.0);

    std::vector<PrimitiveGradient> gradients(mesh.cells.size());
    for (int component = 0; component < 4; ++component) {
      gradients[0][component] = {100.0 * component + rank,
                                 100.0 * component + 50.0 + rank};
    }
    exchange_primitive_gradient_halo(mesh, gradients, MPI_COMM_WORLD);
    for (std::size_t i = 1; i < gradients.size(); ++i) {
      const double peer = static_cast<double>(mesh.global_cell_ids[i]);
      require(std::abs(gradients[i][2].x - (200.0 + peer)) < 1.0e-12,
              "ghost gradient x differs from neighbor value");
      require(std::abs(gradients[i][2].y - (250.0 + peer)) < 1.0e-12,
              "ghost gradient y differs from neighbor value");
    }

    std::vector<PrimitiveLimiter> limiters(mesh.cells.size(), {-1.0, -1.0, -1.0, -1.0});
    limiters[0] = {1.0 + rank, 2.0 + rank, 3.0 + rank, 4.0 + rank};
    exchange_limiter_halo(mesh, limiters, MPI_COMM_WORLD);
    for (std::size_t i = 1; i < limiters.size(); ++i) {
      require(std::abs(limiters[i][3] - (4.0 + mesh.global_cell_ids[i])) < 1.0e-12,
              "ghost limiter differs from neighbor value");
    }

    std::vector<Conservative> increments(mesh.cells.size(), {-1.0, -1.0, -1.0, -1.0});
    increments[0] = {-10.0 - rank, -20.0 - rank, -30.0 - rank, -40.0 - rank};
    exchange_conservative_increment_halo(mesh, increments, MPI_COMM_WORLD);
    for (std::size_t i = 1; i < increments.size(); ++i) {
      require(std::abs(increments[i][1] - (-20.0 - mesh.global_cell_ids[i])) < 1.0e-12,
              "ghost increment differs from neighbor value");
    }

    // A serial local mesh with no neighbors is a valid no-op, including when
    // this test is launched under multiple ranks.
    LocalMesh no_neighbor;
    no_neighbor.owned_cell_count = 1;
    no_neighbor.cells.resize(1);
    std::vector<Conservative> serial_state{{7.0, 8.0, 9.0, 10.0}};
    exchange_conservative_halo(no_neighbor, serial_state, MPI_COMM_WORLD);
    require(serial_state[0][0] == 7.0, "no-neighbor halo exchange changed data");

    if (rank == 0) {
      std::cout << "halo MPI test passed on " << ranks << " ranks\n";
    }
    MPI_Finalize();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rank " << rank << " halo test failed: " << error.what() << '\n';
    MPI_Abort(MPI_COMM_WORLD, 1);
    return 1;
  }
}
