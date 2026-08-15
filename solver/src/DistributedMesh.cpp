#include "cfd/DistributedMesh.hpp"

#include <metis.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cfd {
namespace {

class Buffer {
 public:
  template <class T>
  void put(const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* bytes = reinterpret_cast<const char*>(&value);
    data.insert(data.end(), bytes, bytes + sizeof(T));
  }

  template <class T>
  T get() {
    static_assert(std::is_trivially_copyable_v<T>);
    if (position + sizeof(T) > data.size()) throw std::runtime_error("truncated mesh payload");
    T value{};
    std::memcpy(&value, data.data() + position, sizeof(T));
    position += sizeof(T);
    return value;
  }

  void put_string(const std::string& value) {
    put<int>(static_cast<int>(value.size()));
    data.insert(data.end(), value.begin(), value.end());
  }

  std::string get_string() {
    const int size = get<int>();
    if (size < 0 || position + static_cast<std::size_t>(size) > data.size()) {
      throw std::runtime_error("invalid string in mesh payload");
    }
    std::string value(data.data() + position, data.data() + position + size);
    position += static_cast<std::size_t>(size);
    return value;
  }

  std::vector<char> data;
  std::size_t position = 0;
};

void mpi_check(int code, const char* context) {
  if (code == MPI_SUCCESS) return;
  char message[MPI_MAX_ERROR_STRING]{};
  int length = 0;
  MPI_Error_string(code, message, &length);
  throw std::runtime_error(std::string(context) + ": " + std::string(message, length));
}

std::vector<int> partition_mesh(const GlobalMesh& mesh, int ranks, int& edge_cut) {
  std::vector<int> partition(mesh.cells.size(), 0);
  edge_cut = 0;
  if (ranks == 1) return partition;

  idx_t vertices = static_cast<idx_t>(mesh.cells.size());
  idx_t constraints = 1;
  idx_t parts = static_cast<idx_t>(ranks);
  std::vector<idx_t> offsets(mesh.adjacency_offsets.begin(), mesh.adjacency_offsets.end());
  std::vector<idx_t> adjacency(mesh.adjacency.begin(), mesh.adjacency.end());
  std::vector<idx_t> result(mesh.cells.size(), 0);
  idx_t cut = 0;
  idx_t options[METIS_NOPTIONS];
  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_NUMBERING] = 0;
  options[METIS_OPTION_SEED] = 42;
  const int status = METIS_PartGraphKway(
      &vertices, &constraints, offsets.data(), adjacency.data(), nullptr,
      nullptr, nullptr, &parts, nullptr, nullptr, options, &cut, result.data());
  if (status != METIS_OK) throw std::runtime_error("METIS_PartGraphKway failed");
  edge_cut = static_cast<int>(cut);
  for (std::size_t i = 0; i < result.size(); ++i) partition[i] = static_cast<int>(result[i]);
  return partition;
}

LocalMesh make_local(const GlobalMesh& global, const std::vector<int>& partition, int rank,
                     int ranks, int edge_cut) {
  LocalMesh local;
  local.rank = rank;
  local.ranks = ranks;
  local.global_cells = static_cast<int>(global.cells.size());
  local.global_faces = static_cast<int>(global.faces.size());
  local.partition_edge_cut = edge_cut;

  std::vector<int> globals;
  globals.reserve(global.cells.size() / static_cast<std::size_t>(ranks) + 64);
  for (std::size_t cell = 0; cell < partition.size(); ++cell) {
    if (partition[cell] == rank) globals.push_back(static_cast<int>(cell));
  }
  local.owned_count = static_cast<int>(globals.size());

  std::set<int> ghosts;
  for (const Face& face : global.faces) {
    if (face.right < 0) continue;
    if (partition[static_cast<std::size_t>(face.left)] == rank &&
        partition[static_cast<std::size_t>(face.right)] != rank) {
      ghosts.insert(face.right);
    }
    if (partition[static_cast<std::size_t>(face.right)] == rank &&
        partition[static_cast<std::size_t>(face.left)] != rank) {
      ghosts.insert(face.left);
    }
  }
  globals.insert(globals.end(), ghosts.begin(), ghosts.end());

  std::unordered_map<int, int> local_index;
  for (std::size_t i = 0; i < globals.size(); ++i) local_index.emplace(globals[i], static_cast<int>(i));
  local.cells.reserve(globals.size());
  for (const int global_id : globals) {
    const Cell& source = global.cells[static_cast<std::size_t>(global_id)];
    LocalCell cell;
    cell.global_id = global_id;
    cell.owner_rank = partition[static_cast<std::size_t>(global_id)];
    cell.center = source.center;
    cell.volume = source.volume;
    for (const int node : source.nodes) cell.polygon.push_back(global.nodes[static_cast<std::size_t>(node)]);
    local.cells.push_back(std::move(cell));
  }

  for (const Face& source : global.faces) {
    const bool left_owned = partition[static_cast<std::size_t>(source.left)] == rank;
    const bool right_owned =
        source.right >= 0 && partition[static_cast<std::size_t>(source.right)] == rank;
    if (!left_owned && !right_owned) continue;
    LocalFace face;
    face.left = local_index.at(source.left);
    face.right = source.right < 0 ? -1 : local_index.at(source.right);
    face.node0 = global.nodes[static_cast<std::size_t>(source.nodes[0])];
    face.node1 = global.nodes[static_cast<std::size_t>(source.nodes[1])];
    face.center = source.center;
    face.normal = source.normal;
    face.area = source.area;
    face.boundary_tag = source.boundary_tag;
    local.faces.push_back(std::move(face));
  }

  std::map<int, HaloPeer> peers;
  for (int ghost = local.owned_count; ghost < static_cast<int>(local.cells.size()); ++ghost) {
    const int owner = local.cells[static_cast<std::size_t>(ghost)].owner_rank;
    peers[owner].rank = owner;
    peers[owner].receive_ghosts.push_back(ghost);
  }
  for (int owned = 0; owned < local.owned_count; ++owned) {
    const int global_id = local.cells[static_cast<std::size_t>(owned)].global_id;
    std::set<int> destinations;
    const int begin = global.adjacency_offsets[static_cast<std::size_t>(global_id)];
    const int end = global.adjacency_offsets[static_cast<std::size_t>(global_id + 1)];
    for (int offset = begin; offset < end; ++offset) {
      const int destination = partition[static_cast<std::size_t>(global.adjacency[offset])];
      if (destination != rank) destinations.insert(destination);
    }
    for (const int destination : destinations) {
      peers[destination].rank = destination;
      peers[destination].send_cells.push_back(owned);
    }
  }
  for (auto& [peer, exchange] : peers) {
    (void)peer;
    local.halo.push_back(std::move(exchange));
  }
  return local;
}

Buffer pack(const LocalMesh& mesh) {
  Buffer out;
  out.put(mesh.rank);
  out.put(mesh.ranks);
  out.put(mesh.global_cells);
  out.put(mesh.global_faces);
  out.put(mesh.partition_edge_cut);
  out.put(mesh.owned_count);
  out.put<int>(static_cast<int>(mesh.cells.size()));
  for (const LocalCell& cell : mesh.cells) {
    out.put(cell.global_id);
    out.put(cell.owner_rank);
    out.put(cell.center);
    out.put(cell.volume);
    out.put<int>(static_cast<int>(cell.polygon.size()));
    for (const Vec2 point : cell.polygon) out.put(point);
  }
  out.put<int>(static_cast<int>(mesh.faces.size()));
  for (const LocalFace& face : mesh.faces) {
    out.put(face.left);
    out.put(face.right);
    out.put(face.node0);
    out.put(face.node1);
    out.put(face.center);
    out.put(face.normal);
    out.put(face.area);
    out.put_string(face.boundary_tag);
  }
  out.put<int>(static_cast<int>(mesh.halo.size()));
  for (const HaloPeer& peer : mesh.halo) {
    out.put(peer.rank);
    out.put<int>(static_cast<int>(peer.send_cells.size()));
    for (const int index : peer.send_cells) out.put(index);
    out.put<int>(static_cast<int>(peer.receive_ghosts.size()));
    for (const int index : peer.receive_ghosts) out.put(index);
  }
  return out;
}

LocalMesh unpack(Buffer& in) {
  LocalMesh mesh;
  mesh.rank = in.get<int>();
  mesh.ranks = in.get<int>();
  mesh.global_cells = in.get<int>();
  mesh.global_faces = in.get<int>();
  mesh.partition_edge_cut = in.get<int>();
  mesh.owned_count = in.get<int>();
  const int cells = in.get<int>();
  if (cells < 0) throw std::runtime_error("invalid local cell count");
  mesh.cells.resize(static_cast<std::size_t>(cells));
  for (LocalCell& cell : mesh.cells) {
    cell.global_id = in.get<int>();
    cell.owner_rank = in.get<int>();
    cell.center = in.get<Vec2>();
    cell.volume = in.get<double>();
    const int points = in.get<int>();
    if (points < 3) throw std::runtime_error("invalid cell polygon payload");
    cell.polygon.resize(static_cast<std::size_t>(points));
    for (Vec2& point : cell.polygon) point = in.get<Vec2>();
  }
  const int faces = in.get<int>();
  if (faces < 0) throw std::runtime_error("invalid local face count");
  mesh.faces.resize(static_cast<std::size_t>(faces));
  for (LocalFace& face : mesh.faces) {
    face.left = in.get<int>();
    face.right = in.get<int>();
    face.node0 = in.get<Vec2>();
    face.node1 = in.get<Vec2>();
    face.center = in.get<Vec2>();
    face.normal = in.get<Vec2>();
    face.area = in.get<double>();
    face.boundary_tag = in.get_string();
  }
  const int peers = in.get<int>();
  if (peers < 0) throw std::runtime_error("invalid halo peer count");
  mesh.halo.resize(static_cast<std::size_t>(peers));
  for (HaloPeer& peer : mesh.halo) {
    peer.rank = in.get<int>();
    const int sends = in.get<int>();
    if (sends < 0) throw std::runtime_error("invalid halo send count");
    peer.send_cells.resize(static_cast<std::size_t>(sends));
    for (int& index : peer.send_cells) index = in.get<int>();
    const int receives = in.get<int>();
    if (receives < 0) throw std::runtime_error("invalid halo receive count");
    peer.receive_ghosts.resize(static_cast<std::size_t>(receives));
    for (int& index : peer.receive_ghosts) index = in.get<int>();
  }
  return mesh;
}

}  // namespace

void LocalMesh::validate() const {
  if (owned_count <= 0 || owned_count > static_cast<int>(cells.size())) {
    throw std::runtime_error("partition has no owned cells or invalid owned count");
  }
  for (int i = 0; i < owned_count; ++i) {
    if (cells[static_cast<std::size_t>(i)].owner_rank != rank) {
      throw std::runtime_error("owned local cell has incorrect owner");
    }
  }
  for (int i = owned_count; i < static_cast<int>(cells.size()); ++i) {
    if (cells[static_cast<std::size_t>(i)].owner_rank == rank) {
      throw std::runtime_error("ghost local cell is marked owned");
    }
  }
  for (const LocalFace& face : faces) {
    if (face.left < 0 || face.left >= static_cast<int>(cells.size()) ||
        face.right >= static_cast<int>(cells.size())) {
      throw std::runtime_error("local face has invalid cell index");
    }
  }
}

LocalMesh MeshDistributor::load_partition(const std::string& mesh_path, MPI_Comm communicator) {
  int rank = 0;
  int ranks = 1;
  mpi_check(MPI_Comm_rank(communicator, &rank), "MPI_Comm_rank failed");
  mpi_check(MPI_Comm_size(communicator, &ranks), "MPI_Comm_size failed");

  constexpr int payload_size_tag = 4101;
  constexpr int payload_tag = 4102;
  LocalMesh result;
  std::vector<Buffer> payloads;
  int preparation_ok = 1;
  std::string preparation_error;
  if (rank == 0) {
    try {
      GlobalMesh global = GlobalMesh::read_cgns(mesh_path);
      int edge_cut = 0;
      const std::vector<int> partition = partition_mesh(global, ranks, edge_cut);
      payloads.resize(static_cast<std::size_t>(ranks));
      for (int destination = 0; destination < ranks; ++destination) {
        LocalMesh local = make_local(global, partition, destination, ranks, edge_cut);
        local.validate();
        if (destination == 0) {
          result = std::move(local);
        } else {
          payloads[static_cast<std::size_t>(destination)] = pack(local);
        }
      }
    } catch (const std::exception& error) {
      preparation_ok = 0;
      preparation_error = error.what();
    }
  }

  // All ranks wait here before any point-to-point receive.  Without this
  // handshake, a rank-zero CGNS/METIS failure leaves every other rank blocked
  // in MPI_Recv and prevents the CLI from returning its documented error.
  mpi_check(MPI_Bcast(&preparation_ok, 1, MPI_INT, 0, communicator),
            "MPI_Bcast partition preparation status failed");
  int error_size = rank == 0 ? static_cast<int>(preparation_error.size()) : 0;
  mpi_check(MPI_Bcast(&error_size, 1, MPI_INT, 0, communicator),
            "MPI_Bcast partition error size failed");
  if (error_size < 0) throw std::runtime_error("invalid partition preparation error size");
  if (rank != 0) preparation_error.resize(static_cast<std::size_t>(error_size));
  if (error_size > 0) {
    mpi_check(MPI_Bcast(preparation_error.data(), error_size, MPI_CHAR, 0, communicator),
              "MPI_Bcast partition error message failed");
  }
  if (preparation_ok == 0) {
    throw std::runtime_error("mesh partition preparation failed: " + preparation_error);
  }

  if (rank == 0) {
    for (int destination = 1; destination < ranks; ++destination) {
      const Buffer& payload = payloads[static_cast<std::size_t>(destination)];
      const auto size = static_cast<unsigned long long>(payload.data.size());
      mpi_check(MPI_Send(&size, 1, MPI_UNSIGNED_LONG_LONG, destination, payload_size_tag,
                         communicator),
                "MPI_Send mesh size failed");
      mpi_check(MPI_Send(payload.data.data(), static_cast<int>(payload.data.size()), MPI_BYTE,
                         destination, payload_tag, communicator),
                "MPI_Send mesh payload failed");
    }
  } else {
    unsigned long long size = 0;
    mpi_check(MPI_Recv(&size, 1, MPI_UNSIGNED_LONG_LONG, 0, payload_size_tag, communicator,
                       MPI_STATUS_IGNORE),
              "MPI_Recv mesh size failed");
    if (size == 0 || size > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
      throw std::runtime_error("invalid mesh payload size");
    }
    Buffer payload;
    payload.data.resize(static_cast<std::size_t>(size));
    mpi_check(MPI_Recv(payload.data.data(), static_cast<int>(payload.data.size()), MPI_BYTE, 0,
                       payload_tag, communicator, MPI_STATUS_IGNORE),
              "MPI_Recv mesh payload failed");
    result = unpack(payload);
  }
  result.validate();
  return result;
}

}  // namespace cfd
