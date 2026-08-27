#include "io/restart_io.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "core/exceptions.h"
#include "core/logging.h"

namespace cns2d {
namespace {

// Simple, explicitly versioned header so a mismatched file is rejected with a
// clear message rather than being misread.
struct RestartHeader {
  char magic[8];      // "CNS2DRST"
  std::int32_t version;
  std::int32_t num_vars;
  std::int64_t num_cells_global;
  std::int32_t step;
  std::int32_t padding;
  double physical_time;
};

constexpr std::int32_t kRestartVersion = 1;

}  // namespace

void writeRestart(const std::string &path, const DistributedMesh &mesh, const StateField &U,
                  MPI_Comm comm, int step, Real physical_time) {
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  const Index num_owned = mesh.numOwned();
  // Pack (global id, state) pairs for the owned cells.
  std::vector<GlobalIndex> ids(static_cast<std::size_t>(num_owned));
  std::vector<Real> values(static_cast<std::size_t>(num_owned) * kNumVars);
  for (Index c = 0; c < num_owned; ++c) {
    ids[static_cast<std::size_t>(c)] = mesh.globalCellId()[static_cast<std::size_t>(c)];
    const Real *p = U.cell(c);
    for (int k = 0; k < kNumVars; ++k) {
      values[static_cast<std::size_t>(c) * kNumVars + k] = p[k];
    }
  }

  std::vector<int> id_counts(static_cast<std::size_t>(size), 0);
  const int my_ids = static_cast<int>(num_owned);
  MPI_Gather(&my_ids, 1, MPI_INT, id_counts.data(), 1, MPI_INT, 0, comm);

  std::vector<int> id_displs(static_cast<std::size_t>(size), 0);
  std::vector<int> val_counts(static_cast<std::size_t>(size), 0);
  std::vector<int> val_displs(static_cast<std::size_t>(size), 0);
  GlobalIndex total = 0;
  if (rank == 0) {
    int acc_id = 0;
    int acc_val = 0;
    for (int r = 0; r < size; ++r) {
      id_displs[static_cast<std::size_t>(r)] = acc_id;
      val_counts[static_cast<std::size_t>(r)] = id_counts[static_cast<std::size_t>(r)] * kNumVars;
      val_displs[static_cast<std::size_t>(r)] = acc_val;
      acc_id += id_counts[static_cast<std::size_t>(r)];
      acc_val += val_counts[static_cast<std::size_t>(r)];
    }
    total = acc_id;
  }

  std::vector<GlobalIndex> all_ids;
  std::vector<Real> all_values;
  if (rank == 0) {
    all_ids.resize(static_cast<std::size_t>(total));
    all_values.resize(static_cast<std::size_t>(total) * kNumVars);
  }
  MPI_Gatherv(ids.data(), my_ids, MPI_INT64_T, rank == 0 ? all_ids.data() : nullptr,
              id_counts.data(), id_displs.data(), MPI_INT64_T, 0, comm);
  MPI_Gatherv(values.data(), my_ids * kNumVars, MPI_DOUBLE, rank == 0 ? all_values.data() : nullptr,
              val_counts.data(), val_displs.data(), MPI_DOUBLE, 0, comm);

  if (rank == 0) {
    if (total != mesh.numCellsGlobal()) {
      throw CnsError("restart write: gathered " + std::to_string(total) + " cells but the mesh has " +
                     std::to_string(mesh.numCellsGlobal()));
    }
    // Reorder into global cell order so the file layout is rank-count independent.
    std::vector<Real> ordered(static_cast<std::size_t>(total) * kNumVars, 0.0);
    for (GlobalIndex i = 0; i < total; ++i) {
      const GlobalIndex gid = all_ids[static_cast<std::size_t>(i)];
      if (gid < 0 || gid >= total) {
        throw CnsError("restart write: global cell id out of range");
      }
      for (int k = 0; k < kNumVars; ++k) {
        ordered[static_cast<std::size_t>(gid) * kNumVars + k] =
            all_values[static_cast<std::size_t>(i) * kNumVars + k];
      }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw CnsError("cannot open restart file for writing: " + path);
    RestartHeader header{};
    std::memcpy(header.magic, "CNS2DRST", 8);
    header.version = kRestartVersion;
    header.num_vars = kNumVars;
    header.num_cells_global = total;
    header.step = step;
    header.padding = 0;
    header.physical_time = physical_time;
    out.write(reinterpret_cast<const char *>(&header), sizeof(header));
    out.write(reinterpret_cast<const char *>(ordered.data()),
              static_cast<std::streamsize>(ordered.size() * sizeof(Real)));
    if (!out.good()) throw CnsError("failed while writing restart file: " + path);
  }
  MPI_Barrier(comm);
}

void readRestart(const std::string &path, const DistributedMesh &mesh, StateField &U, MPI_Comm comm) {
  int rank = 0;
  MPI_Comm_rank(comm, &rank);

  const GlobalIndex num_global = mesh.numCellsGlobal();
  std::vector<Real> ordered;
  if (rank == 0) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw CnsError("cannot open restart file for reading: " + path);
    RestartHeader header{};
    in.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!in.good() || std::strncmp(header.magic, "CNS2DRST", 8) != 0) {
      throw CnsError("restart file '" + path + "' is not a cns2d restart file");
    }
    if (header.version != kRestartVersion) {
      throw CnsError("restart file '" + path + "' has version " + std::to_string(header.version) +
                     " but this build reads version " + std::to_string(kRestartVersion));
    }
    if (header.num_vars != kNumVars) {
      throw CnsError("restart file '" + path + "' stores " + std::to_string(header.num_vars) +
                     " variables per cell, expected " + std::to_string(kNumVars));
    }
    if (header.num_cells_global != num_global) {
      throw CnsError("restart file '" + path + "' holds " +
                     std::to_string(header.num_cells_global) + " cells but the mesh has " +
                     std::to_string(num_global) + "; the restart does not match this mesh");
    }
    ordered.resize(static_cast<std::size_t>(num_global) * kNumVars);
    in.read(reinterpret_cast<char *>(ordered.data()),
            static_cast<std::streamsize>(ordered.size() * sizeof(Real)));
    if (!in.good()) throw CnsError("restart file '" + path + "' is truncated");
  }

  // Broadcast the ordered state, then each rank extracts only its owned cells.
  // This is a one-off setup broadcast, not an iteration-time operation.
  if (rank != 0) ordered.resize(static_cast<std::size_t>(num_global) * kNumVars);
  MPI_Bcast(ordered.data(), static_cast<int>(ordered.size()), MPI_DOUBLE, 0, comm);

  const Index num_local = mesh.numLocal();
  for (Index c = 0; c < num_local; ++c) {
    const GlobalIndex gid = mesh.globalCellId()[static_cast<std::size_t>(c)];
    ConsVec u{};
    for (int k = 0; k < kNumVars; ++k) {
      u[static_cast<std::size_t>(k)] = ordered[static_cast<std::size_t>(gid) * kNumVars + k];
    }
    U.set(c, u);
  }
}

}  // namespace cns2d
