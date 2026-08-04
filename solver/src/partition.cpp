#include "cfd/partition.hpp"

#include <metis.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace cfd {
namespace {

class BufferWriter {
 public:
  template <class T>
  void scalar(const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* begin = reinterpret_cast<const std::byte*>(&value);
    data_.insert(data_.end(), begin, begin + sizeof(T));
  }
  void string(const std::string& value) {
    scalar<std::uint64_t>(value.size());
    const auto* begin = reinterpret_cast<const std::byte*>(value.data());
    data_.insert(data_.end(), begin, begin + value.size());
  }
  template <class T>
  void scalar_vector(const std::vector<T>& values) {
    scalar<std::uint64_t>(values.size());
    for (const T& value : values) scalar(value);
  }
  [[nodiscard]] std::vector<std::byte> finish() && { return std::move(data_); }

 private:
  std::vector<std::byte> data_;
};

class BufferReader {
 public:
  explicit BufferReader(const std::vector<std::byte>& data) : data_(data) {}
  template <class T>
  T scalar() {
    static_assert(std::is_trivially_copyable_v<T>);
    if (position_ + sizeof(T) > data_.size()) throw std::runtime_error("truncated serialized local mesh");
    T value{};
    std::memcpy(&value, data_.data() + position_, sizeof(T));
    position_ += sizeof(T);
    return value;
  }
  std::string string() {
    const auto size = scalar<std::uint64_t>();
    if (size > data_.size() - position_) throw std::runtime_error("invalid serialized string size");
    const char* begin = reinterpret_cast<const char*>(data_.data() + position_);
    std::string value(begin, begin + size);
    position_ += static_cast<std::size_t>(size);
    return value;
  }
  template <class T>
  std::vector<T> scalar_vector() {
    const auto size = scalar<std::uint64_t>();
    if (size > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) throw std::runtime_error("serialized vector is too large");
    std::vector<T> values(static_cast<std::size_t>(size));
    for (T& value : values) value = scalar<T>();
    return values;
  }
  void require_end() const {
    if (position_ != data_.size()) throw std::runtime_error("unused bytes remain in serialized local mesh");
  }

 private:
  const std::vector<std::byte>& data_;
  std::size_t position_ = 0;
};

std::vector<std::byte> serialize(const LocalMesh& mesh) {
  BufferWriter writer;
  writer.scalar(mesh.rank);
  writer.scalar(mesh.rank_count);
  writer.scalar(mesh.global_cell_count);
  writer.scalar(mesh.global_face_count);
  writer.scalar(mesh.partition_edge_cut);
  writer.scalar(mesh.owned_cell_count);
  writer.scalar(mesh.boundary_face_count);
  writer.scalar<std::uint64_t>(mesh.cells.size());
  for (const LocalCell& cell : mesh.cells) {
    writer.scalar(cell.global_id);
    writer.scalar(cell.center.x);
    writer.scalar(cell.center.y);
    writer.scalar(cell.area);
    writer.scalar(cell.owner_rank);
    writer.scalar<std::uint64_t>(cell.vertices.size());
    for (const Vec2 vertex : cell.vertices) {
      writer.scalar(vertex.x);
      writer.scalar(vertex.y);
    }
  }
  writer.scalar<std::uint64_t>(mesh.faces.size());
  for (const LocalFace& face : mesh.faces) {
    writer.scalar(face.global_id);
    writer.scalar(face.global_nodes[0]);
    writer.scalar(face.global_nodes[1]);
    writer.scalar(face.point0.x);
    writer.scalar(face.point0.y);
    writer.scalar(face.point1.x);
    writer.scalar(face.point1.y);
    writer.scalar(face.center.x);
    writer.scalar(face.center.y);
    writer.scalar(face.normal.x);
    writer.scalar(face.normal.y);
    writer.scalar(face.length);
    writer.scalar(face.left);
    writer.scalar(face.right);
    writer.scalar(static_cast<std::uint8_t>(face.boundary_type));
    writer.string(face.boundary_tag);
  }
  writer.scalar<std::uint64_t>(mesh.halo_links.size());
  for (const HaloLink& link : mesh.halo_links) {
    writer.scalar(link.rank);
    writer.scalar_vector(link.send_cells);
    writer.scalar_vector(link.receive_cells);
  }
  return std::move(writer).finish();
}

LocalMesh deserialize(const std::vector<std::byte>& bytes) {
  BufferReader reader(bytes);
  LocalMesh mesh;
  mesh.rank = reader.scalar<int>();
  mesh.rank_count = reader.scalar<int>();
  mesh.global_cell_count = reader.scalar<std::int64_t>();
  mesh.global_face_count = reader.scalar<std::int64_t>();
  mesh.partition_edge_cut = reader.scalar<std::int64_t>();
  mesh.owned_cell_count = reader.scalar<int>();
  mesh.boundary_face_count = reader.scalar<int>();
  const auto cell_count = reader.scalar<std::uint64_t>();
  mesh.cells.resize(static_cast<std::size_t>(cell_count));
  for (LocalCell& cell : mesh.cells) {
    cell.global_id = reader.scalar<std::int64_t>();
    cell.center.x = reader.scalar<double>();
    cell.center.y = reader.scalar<double>();
    cell.area = reader.scalar<double>();
    cell.owner_rank = reader.scalar<int>();
    const auto vertex_count = reader.scalar<std::uint64_t>();
    cell.vertices.resize(static_cast<std::size_t>(vertex_count));
    for (Vec2& vertex : cell.vertices) {
      vertex.x = reader.scalar<double>();
      vertex.y = reader.scalar<double>();
    }
  }
  const auto face_count = reader.scalar<std::uint64_t>();
  mesh.faces.resize(static_cast<std::size_t>(face_count));
  for (LocalFace& face : mesh.faces) {
    face.global_id = reader.scalar<std::int64_t>();
    face.global_nodes[0] = reader.scalar<std::int64_t>();
    face.global_nodes[1] = reader.scalar<std::int64_t>();
    face.point0.x = reader.scalar<double>();
    face.point0.y = reader.scalar<double>();
    face.point1.x = reader.scalar<double>();
    face.point1.y = reader.scalar<double>();
    face.center.x = reader.scalar<double>();
    face.center.y = reader.scalar<double>();
    face.normal.x = reader.scalar<double>();
    face.normal.y = reader.scalar<double>();
    face.length = reader.scalar<double>();
    face.left = reader.scalar<int>();
    face.right = reader.scalar<int>();
    face.boundary_type = static_cast<BoundaryType>(reader.scalar<std::uint8_t>());
    face.boundary_tag = reader.string();
  }
  const auto link_count = reader.scalar<std::uint64_t>();
  mesh.halo_links.resize(static_cast<std::size_t>(link_count));
  for (HaloLink& link : mesh.halo_links) {
    link.rank = reader.scalar<int>();
    link.send_cells = reader.scalar_vector<int>();
    link.receive_cells = reader.scalar_vector<int>();
  }
  reader.require_end();
  mesh.rebuild_cell_faces();
  return mesh;
}

}  // namespace

void LocalMesh::rebuild_cell_faces() {
  for (LocalCell& cell : cells) cell.faces.clear();
  for (std::size_t face_index = 0; face_index < faces.size(); ++face_index) {
    const LocalFace& face = faces[face_index];
    if (face.left < 0 || static_cast<std::size_t>(face.left) >= cells.size() ||
        (face.right >= 0 && static_cast<std::size_t>(face.right) >= cells.size())) {
      throw std::runtime_error("local face references an invalid cell");
    }
    cells[static_cast<std::size_t>(face.left)].faces.push_back(static_cast<int>(face_index));
    if (face.right >= 0) cells[static_cast<std::size_t>(face.right)].faces.push_back(static_cast<int>(face_index));
  }
}

PartitionResult partition_global_mesh(const GlobalMesh& mesh, int rank_count) {
  if (rank_count <= 0) throw std::runtime_error("MPI rank count must be positive");
  if (mesh.cells.size() < static_cast<std::size_t>(rank_count)) throw std::runtime_error("more MPI ranks than mesh cells");

  std::vector<std::vector<idx_t>> adjacency(mesh.cells.size());
  for (const GlobalFace& face : mesh.faces) {
    if (face.right >= 0) {
      adjacency[static_cast<std::size_t>(face.left)].push_back(static_cast<idx_t>(face.right));
      adjacency[static_cast<std::size_t>(face.right)].push_back(static_cast<idx_t>(face.left));
    }
  }
  std::vector<idx_t> offsets(mesh.cells.size() + 1, 0);
  std::vector<idx_t> neighbors;
  for (std::size_t cell = 0; cell < adjacency.size(); ++cell) {
    auto& row = adjacency[cell];
    std::sort(row.begin(), row.end());
    row.erase(std::unique(row.begin(), row.end()), row.end());
    neighbors.insert(neighbors.end(), row.begin(), row.end());
    offsets[cell + 1] = static_cast<idx_t>(neighbors.size());
  }

  PartitionResult result;
  result.owner.assign(mesh.cells.size(), 0);
  if (rank_count > 1) {
    idx_t vertex_count = static_cast<idx_t>(mesh.cells.size());
    idx_t constraints = 1;
    idx_t partitions = static_cast<idx_t>(rank_count);
    idx_t edge_cut = 0;
    std::vector<idx_t> owner_idx(mesh.cells.size(), 0);
    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_NUMBERING] = 0;
    options[METIS_OPTION_SEED] = 42;
    options[METIS_OPTION_CONTIG] = 1;
    const int status = METIS_PartGraphKway(&vertex_count, &constraints, offsets.data(), neighbors.data(), nullptr,
                                           nullptr, nullptr, &partitions, nullptr, nullptr, options, &edge_cut,
                                           owner_idx.data());
    if (status != METIS_OK) throw std::runtime_error("METIS_PartGraphKway failed with code " + std::to_string(status));
    std::transform(owner_idx.begin(), owner_idx.end(), result.owner.begin(), [](idx_t value) { return static_cast<int>(value); });
    result.edge_cut = static_cast<std::int64_t>(edge_cut);
  }

  std::vector<std::vector<std::int64_t>> owned(static_cast<std::size_t>(rank_count));
  std::vector<std::set<std::int64_t>> ghosts(static_cast<std::size_t>(rank_count));
  std::vector<std::map<int, std::set<std::int64_t>>> send_gids(static_cast<std::size_t>(rank_count));
  std::vector<std::map<int, std::set<std::int64_t>>> receive_gids(static_cast<std::size_t>(rank_count));
  for (std::size_t cell = 0; cell < result.owner.size(); ++cell) {
    const int owner = result.owner[cell];
    if (owner < 0 || owner >= rank_count) throw std::runtime_error("METIS returned an invalid partition id");
    owned[static_cast<std::size_t>(owner)].push_back(static_cast<std::int64_t>(cell));
  }
  for (const GlobalFace& face : mesh.faces) {
    if (face.right < 0) continue;
    const int left_owner = result.owner[static_cast<std::size_t>(face.left)];
    const int right_owner = result.owner[static_cast<std::size_t>(face.right)];
    if (left_owner == right_owner) continue;
    ghosts[static_cast<std::size_t>(left_owner)].insert(face.right);
    ghosts[static_cast<std::size_t>(right_owner)].insert(face.left);
    send_gids[static_cast<std::size_t>(left_owner)][right_owner].insert(face.left);
    receive_gids[static_cast<std::size_t>(left_owner)][right_owner].insert(face.right);
    send_gids[static_cast<std::size_t>(right_owner)][left_owner].insert(face.right);
    receive_gids[static_cast<std::size_t>(right_owner)][left_owner].insert(face.left);
  }

  result.local_meshes.resize(static_cast<std::size_t>(rank_count));
  for (int rank = 0; rank < rank_count; ++rank) {
    LocalMesh& local = result.local_meshes[static_cast<std::size_t>(rank)];
    local.rank = rank;
    local.rank_count = rank_count;
    local.global_cell_count = static_cast<std::int64_t>(mesh.cells.size());
    local.global_face_count = static_cast<std::int64_t>(mesh.faces.size());
    local.partition_edge_cut = result.edge_cut;
    local.owned_cell_count = static_cast<int>(owned[static_cast<std::size_t>(rank)].size());
    std::unordered_map<std::int64_t, int> global_to_local;
    auto append_cell = [&](std::int64_t gid) {
      const int local_id = static_cast<int>(local.cells.size());
      global_to_local.emplace(gid, local_id);
      const GlobalCell& cell = mesh.cells[static_cast<std::size_t>(gid)];
      std::vector<Vec2> vertices;
      vertices.reserve(cell.nodes.size());
      for (const std::int64_t node : cell.nodes) vertices.push_back(mesh.nodes[static_cast<std::size_t>(node)].position);
      local.cells.push_back({gid, cell.center, cell.area, result.owner[static_cast<std::size_t>(gid)],
                             std::move(vertices), {}});
    };
    for (std::int64_t gid : owned[static_cast<std::size_t>(rank)]) append_cell(gid);
    for (std::int64_t gid : ghosts[static_cast<std::size_t>(rank)]) append_cell(gid);

    for (const GlobalFace& face : mesh.faces) {
      const bool left_owned = result.owner[static_cast<std::size_t>(face.left)] == rank;
      const bool right_owned = face.right >= 0 && result.owner[static_cast<std::size_t>(face.right)] == rank;
      if (!left_owned && !right_owned) continue;
      LocalFace out;
      out.global_id = face.global_id;
      out.global_nodes = face.nodes;
      out.point0 = mesh.nodes[static_cast<std::size_t>(face.nodes[0])].position;
      out.point1 = mesh.nodes[static_cast<std::size_t>(face.nodes[1])].position;
      out.center = face.center;
      out.normal = face.normal;
      out.length = face.length;
      out.left = global_to_local.at(face.left);
      out.right = face.right < 0 ? -1 : global_to_local.at(face.right);
      out.boundary_type = face.boundary_type;
      out.boundary_tag = face.boundary_tag;
      if (face.right < 0) ++local.boundary_face_count;
      local.faces.push_back(std::move(out));
    }
    for (const auto& [neighbor_rank, send_set] : send_gids[static_cast<std::size_t>(rank)]) {
      HaloLink link;
      link.rank = neighbor_rank;
      for (std::int64_t gid : send_set) link.send_cells.push_back(global_to_local.at(gid));
      const auto receive_it = receive_gids[static_cast<std::size_t>(rank)].find(neighbor_rank);
      if (receive_it == receive_gids[static_cast<std::size_t>(rank)].end()) throw std::runtime_error("asymmetric halo map");
      for (std::int64_t gid : receive_it->second) link.receive_cells.push_back(global_to_local.at(gid));
      local.halo_links.push_back(std::move(link));
    }
    local.rebuild_cell_faces();
  }
  return result;
}

LocalMesh distribute_local_mesh(const std::vector<LocalMesh>* root_meshes, MPI_Comm communicator) {
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(communicator, &rank);
  MPI_Comm_size(communicator, &size);
  constexpr int size_tag = 7310;
  constexpr int data_tag = 7311;
  if (rank == 0) {
    if (root_meshes == nullptr || root_meshes->size() != static_cast<std::size_t>(size)) {
      throw std::runtime_error("rank zero has an invalid set of local meshes to distribute");
    }
    for (int target = 1; target < size; ++target) {
      const auto bytes = serialize(root_meshes->at(static_cast<std::size_t>(target)));
      const auto byte_count = static_cast<std::uint64_t>(bytes.size());
      MPI_Send(&byte_count, 1, MPI_UINT64_T, target, size_tag, communicator);
      if (byte_count > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) throw std::runtime_error("local mesh message exceeds MPI count range");
      MPI_Send(bytes.data(), static_cast<int>(byte_count), MPI_BYTE, target, data_tag, communicator);
    }
    LocalMesh local = root_meshes->front();
    local.rebuild_cell_faces();
    return local;
  }
  std::uint64_t byte_count = 0;
  MPI_Recv(&byte_count, 1, MPI_UINT64_T, 0, size_tag, communicator, MPI_STATUS_IGNORE);
  if (byte_count > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) throw std::runtime_error("received local mesh exceeds MPI count range");
  std::vector<std::byte> bytes(static_cast<std::size_t>(byte_count));
  MPI_Recv(bytes.data(), static_cast<int>(byte_count), MPI_BYTE, 0, data_tag, communicator, MPI_STATUS_IGNORE);
  return deserialize(bytes);
}

void validate_distributed_mesh(const LocalMesh& mesh, MPI_Comm communicator) {
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(communicator, &rank);
  MPI_Comm_size(communicator, &size);
  if (mesh.rank != rank || mesh.rank_count != size) throw std::runtime_error("local mesh MPI identity mismatch");
  if (mesh.owned_cell_count <= 0 || mesh.owned_cell_count > static_cast<int>(mesh.cells.size())) {
    throw std::runtime_error("invalid local owned-cell count");
  }
  std::int64_t local_owned = mesh.owned_cell_count;
  std::int64_t global_owned = 0;
  MPI_Allreduce(&local_owned, &global_owned, 1, MPI_INT64_T, MPI_SUM, communicator);
  if (global_owned != mesh.global_cell_count) throw std::runtime_error("distributed owned-cell total does not match global mesh");
  std::vector<int> remote_send_counts(mesh.halo_links.size(), 0);
  std::vector<int> local_send_counts(mesh.halo_links.size(), 0);
  std::vector<MPI_Request> count_requests;
  count_requests.reserve(mesh.halo_links.size() * 2);
  for (std::size_t link_index = 0; link_index < mesh.halo_links.size(); ++link_index) {
    MPI_Request request{};
    MPI_Irecv(&remote_send_counts[link_index], 1, MPI_INT, mesh.halo_links[link_index].rank,
              7320, communicator, &request);
    count_requests.push_back(request);
  }
  for (std::size_t link_index = 0; link_index < mesh.halo_links.size(); ++link_index) {
    const HaloLink& link = mesh.halo_links[link_index];
    if (link.rank < 0 || link.rank >= size || link.rank == rank) throw std::runtime_error("invalid halo neighbor rank");
    local_send_counts[link_index] = static_cast<int>(link.send_cells.size());
    MPI_Request request{};
    MPI_Isend(&local_send_counts[link_index], 1, MPI_INT, link.rank, 7320, communicator, &request);
    count_requests.push_back(request);
    for (int cell : link.send_cells) {
      if (!mesh.is_owned(cell)) throw std::runtime_error("halo send list contains a non-owned cell");
    }
    for (int cell : link.receive_cells) {
      if (cell < mesh.owned_cell_count || cell >= static_cast<int>(mesh.cells.size())) {
        throw std::runtime_error("halo receive list contains a non-ghost cell");
      }
    }
  }
  if (!count_requests.empty()) {
    MPI_Waitall(static_cast<int>(count_requests.size()), count_requests.data(), MPI_STATUSES_IGNORE);
  }
  for (std::size_t link_index = 0; link_index < mesh.halo_links.size(); ++link_index) {
    if (remote_send_counts[link_index] !=
        static_cast<int>(mesh.halo_links[link_index].receive_cells.size())) {
      throw std::runtime_error("halo send/receive counts are asymmetric");
    }
  }
}

}  // namespace cfd
