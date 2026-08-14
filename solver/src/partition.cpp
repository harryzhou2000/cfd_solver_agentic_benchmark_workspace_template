#include "cfd/partition.hpp"

#include <metis.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace cfd {
namespace {

void mpi_call(int status, const char* operation) {
  if (status == MPI_SUCCESS) return;
  std::array<char, MPI_MAX_ERROR_STRING> text{};
  int length = 0;
  MPI_Error_string(status, text.data(), &length);
  throw std::runtime_error(std::string("MPI ") + operation + " failed: " +
                           std::string(text.data(), static_cast<std::size_t>(length)));
}

int mpi_count(std::size_t count, const char* description) {
  if (count > static_cast<std::size_t>(INT_MAX)) {
    throw std::runtime_error(std::string(description) + " exceeds MPI's int count limit");
  }
  return static_cast<int>(count);
}

class ByteWriter {
 public:
  template <class T>
  void scalar(const T& value) {
    static_assert(std::is_trivially_copyable<T>::value, "binary scalar must be trivial");
    const auto* first = reinterpret_cast<const std::uint8_t*>(&value);
    data_.insert(data_.end(), first, first + sizeof(T));
  }
  void size(std::size_t value) { scalar(static_cast<std::uint64_t>(value)); }
  void string(const std::string& value) {
    size(value.size());
    data_.insert(data_.end(), value.begin(), value.end());
  }
  void vec2(const Vec2& value) {
    scalar(value.x);
    scalar(value.y);
  }
  std::vector<std::uint8_t> take() { return std::move(data_); }

 private:
  std::vector<std::uint8_t> data_;
};

class ByteReader {
 public:
  explicit ByteReader(const std::vector<std::uint8_t>& data) : data_(data) {}
  template <class T>
  T scalar() {
    static_assert(std::is_trivially_copyable<T>::value, "binary scalar must be trivial");
    require(sizeof(T));
    T result{};
    std::memcpy(&result, data_.data() + position_, sizeof(T));
    position_ += sizeof(T);
    return result;
  }
  std::size_t size() {
    const auto value = scalar<std::uint64_t>();
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      throw std::runtime_error("distributed-mesh payload contains an oversized collection");
    }
    return static_cast<std::size_t>(value);
  }
  std::string string() {
    const std::size_t count = size();
    require(count);
    std::string result(reinterpret_cast<const char*>(data_.data() + position_), count);
    position_ += count;
    return result;
  }
  Vec2 vec2() { return Vec2{scalar<double>(), scalar<double>()}; }
  void finish() const {
    if (position_ != data_.size()) throw std::runtime_error("trailing bytes in mesh payload");
  }

 private:
  void require(std::size_t count) const {
    if (count > data_.size() - position_) {
      throw std::runtime_error("truncated distributed-mesh payload");
    }
  }
  const std::vector<std::uint8_t>& data_;
  std::size_t position_{};
};

void write_rank_diagnostics(ByteWriter& writer, const RankPartitionDiagnostics& diagnostics) {
  writer.scalar(diagnostics.rank);
  writer.size(diagnostics.owned_cells);
  writer.size(diagnostics.ghost_cells);
  writer.size(diagnostics.neighbor_ids.size());
  for (int value : diagnostics.neighbor_ids) writer.scalar(value);
  writer.size(diagnostics.send_counts.size());
  for (std::size_t value : diagnostics.send_counts) writer.size(value);
  writer.size(diagnostics.receive_counts.size());
  for (std::size_t value : diagnostics.receive_counts) writer.size(value);
}

RankPartitionDiagnostics read_rank_diagnostics(ByteReader& reader) {
  RankPartitionDiagnostics result;
  result.rank = reader.scalar<int>();
  result.owned_cells = reader.size();
  result.ghost_cells = reader.size();
  result.neighbor_ids.resize(reader.size());
  for (int& value : result.neighbor_ids) value = reader.scalar<int>();
  result.send_counts.resize(reader.size());
  for (std::size_t& value : result.send_counts) value = reader.size();
  result.receive_counts.resize(reader.size());
  for (std::size_t& value : result.receive_counts) value = reader.size();
  return result;
}

std::vector<std::uint8_t> serialize(const DistributedMesh& mesh) {
  ByteWriter writer;
  writer.scalar(mesh.rank);
  writer.scalar(mesh.size);
  writer.size(mesh.global_vertex_count);
  writer.size(mesh.global_cell_count);
  writer.size(mesh.global_face_count);
  writer.size(mesh.owned_cell_count);

  writer.size(mesh.vertices.size());
  for (const LocalVertex& vertex : mesh.vertices) {
    writer.scalar(vertex.global_id);
    writer.vec2(vertex.position);
  }
  writer.size(mesh.cells.size());
  for (const LocalCell& cell : mesh.cells) {
    writer.scalar(cell.global_id);
    writer.scalar(cell.owner);
    writer.scalar(static_cast<std::uint8_t>(cell.owned ? 1U : 0U));
    writer.size(cell.vertices.size());
    for (LocalIndex value : cell.vertices) writer.scalar(value);
    writer.size(cell.faces.size());
    for (LocalIndex value : cell.faces) writer.scalar(value);
    writer.vec2(cell.center);
    writer.scalar(cell.area);
  }
  writer.size(mesh.faces.size());
  for (const LocalFace& face : mesh.faces) {
    writer.scalar(face.global_id);
    writer.scalar(face.vertices[0]);
    writer.scalar(face.vertices[1]);
    writer.scalar(face.left_cell);
    writer.scalar(face.right_cell);
    writer.vec2(face.center);
    writer.scalar(face.length);
    writer.vec2(face.normal);
    writer.string(face.boundary);
  }
  writer.size(mesh.halo.size());
  for (const NeighborSchedule& schedule : mesh.halo) {
    writer.scalar(schedule.rank);
    writer.size(schedule.send_global_cells.size());
    for (GlobalId value : schedule.send_global_cells) writer.scalar(value);
    writer.size(schedule.receive_global_cells.size());
    for (GlobalId value : schedule.receive_global_cells) writer.scalar(value);
    writer.size(schedule.send_local_cells.size());
    for (LocalIndex value : schedule.send_local_cells) writer.scalar(value);
    writer.size(schedule.receive_local_cells.size());
    for (LocalIndex value : schedule.receive_local_cells) writer.scalar(value);
  }
  write_rank_diagnostics(writer, mesh.diagnostics);
  writer.scalar(mesh.global_diagnostics.edge_cut);
  writer.size(mesh.global_diagnostics.total_owned_cells);
  writer.size(mesh.global_diagnostics.total_ghost_cells);
  writer.size(mesh.global_diagnostics.ranks.size());
  for (const auto& diagnostics : mesh.global_diagnostics.ranks) {
    write_rank_diagnostics(writer, diagnostics);
  }
  return writer.take();
}

DistributedMesh deserialize(const std::vector<std::uint8_t>& payload) {
  ByteReader reader(payload);
  DistributedMesh mesh;
  mesh.rank = reader.scalar<int>();
  mesh.size = reader.scalar<int>();
  mesh.global_vertex_count = reader.size();
  mesh.global_cell_count = reader.size();
  mesh.global_face_count = reader.size();
  mesh.owned_cell_count = reader.size();

  mesh.vertices.resize(reader.size());
  for (LocalVertex& vertex : mesh.vertices) {
    vertex.global_id = reader.scalar<GlobalId>();
    vertex.position = reader.vec2();
  }
  mesh.cells.resize(reader.size());
  for (LocalCell& cell : mesh.cells) {
    cell.global_id = reader.scalar<GlobalId>();
    cell.owner = reader.scalar<int>();
    cell.owned = reader.scalar<std::uint8_t>() != 0U;
    cell.vertices.resize(reader.size());
    for (LocalIndex& value : cell.vertices) value = reader.scalar<LocalIndex>();
    cell.faces.resize(reader.size());
    for (LocalIndex& value : cell.faces) value = reader.scalar<LocalIndex>();
    cell.center = reader.vec2();
    cell.area = reader.scalar<double>();
  }
  mesh.faces.resize(reader.size());
  for (LocalFace& face : mesh.faces) {
    face.global_id = reader.scalar<GlobalId>();
    face.vertices[0] = reader.scalar<LocalIndex>();
    face.vertices[1] = reader.scalar<LocalIndex>();
    face.left_cell = reader.scalar<LocalIndex>();
    face.right_cell = reader.scalar<LocalIndex>();
    face.center = reader.vec2();
    face.length = reader.scalar<double>();
    face.normal = reader.vec2();
    face.boundary = reader.string();
  }
  mesh.halo.resize(reader.size());
  for (NeighborSchedule& schedule : mesh.halo) {
    schedule.rank = reader.scalar<int>();
    schedule.send_global_cells.resize(reader.size());
    for (GlobalId& value : schedule.send_global_cells) value = reader.scalar<GlobalId>();
    schedule.receive_global_cells.resize(reader.size());
    for (GlobalId& value : schedule.receive_global_cells) value = reader.scalar<GlobalId>();
    schedule.send_local_cells.resize(reader.size());
    for (LocalIndex& value : schedule.send_local_cells) value = reader.scalar<LocalIndex>();
    schedule.receive_local_cells.resize(reader.size());
    for (LocalIndex& value : schedule.receive_local_cells) value = reader.scalar<LocalIndex>();
  }
  mesh.diagnostics = read_rank_diagnostics(reader);
  mesh.global_diagnostics.edge_cut = reader.scalar<std::int64_t>();
  mesh.global_diagnostics.total_owned_cells = reader.size();
  mesh.global_diagnostics.total_ghost_cells = reader.size();
  mesh.global_diagnostics.ranks.resize(reader.size());
  for (auto& diagnostics : mesh.global_diagnostics.ranks) {
    diagnostics = read_rank_diagnostics(reader);
  }
  reader.finish();
  return mesh;
}

struct ScheduleSets {
  std::set<GlobalId> send;
  std::set<GlobalId> receive;
};

DistributedMesh build_local_mesh(const Mesh& global, const Partitioning& partition,
                                 int rank, int size,
                                 const std::vector<std::map<int, ScheduleSets>>& schedules,
                                 const GlobalPartitionDiagnostics& global_diagnostics) {
  std::vector<GlobalId> owned;
  std::set<GlobalId> ghosts;
  std::vector<GlobalId> needed_faces;
  for (const Cell& cell : global.cells) {
    if (partition.cell_owner[static_cast<std::size_t>(cell.id)] == rank) owned.push_back(cell.id);
  }
  for (const Face& face : global.faces) {
    const int left_owner = partition.cell_owner[static_cast<std::size_t>(face.left)];
    const int right_owner = face.right == invalid_global_id
                                ? -1
                                : partition.cell_owner[static_cast<std::size_t>(face.right)];
    if (left_owner == rank || right_owner == rank) {
      needed_faces.push_back(face.id);
      if (left_owner != rank) ghosts.insert(face.left);
      if (face.right != invalid_global_id && right_owner != rank) ghosts.insert(face.right);
    }
  }

  DistributedMesh local;
  local.rank = rank;
  local.size = size;
  local.global_vertex_count = global.vertices.size();
  local.global_cell_count = global.cells.size();
  local.global_face_count = global.faces.size();
  local.owned_cell_count = owned.size();
  local.global_diagnostics = global_diagnostics;

  std::vector<GlobalId> local_cell_ids = owned;
  local_cell_ids.insert(local_cell_ids.end(), ghosts.begin(), ghosts.end());
  std::unordered_map<GlobalId, LocalIndex> cell_local;
  for (std::size_t i = 0; i < local_cell_ids.size(); ++i) {
    cell_local.emplace(local_cell_ids[i], static_cast<LocalIndex>(i));
  }
  std::unordered_map<GlobalId, LocalIndex> face_local;
  for (std::size_t i = 0; i < needed_faces.size(); ++i) {
    face_local.emplace(needed_faces[i], static_cast<LocalIndex>(i));
  }

  std::set<GlobalId> needed_vertices;
  for (GlobalId cell_id : local_cell_ids) {
    const Cell& cell = global.cells[static_cast<std::size_t>(cell_id)];
    needed_vertices.insert(cell.vertices.begin(), cell.vertices.end());
  }
  for (GlobalId face_id : needed_faces) {
    const Face& face = global.faces[static_cast<std::size_t>(face_id)];
    needed_vertices.insert(face.vertices.begin(), face.vertices.end());
  }
  std::unordered_map<GlobalId, LocalIndex> vertex_local;
  for (GlobalId vertex_id : needed_vertices) {
    const LocalIndex index = static_cast<LocalIndex>(local.vertices.size());
    vertex_local.emplace(vertex_id, index);
    const Vertex& vertex = global.vertices[static_cast<std::size_t>(vertex_id)];
    local.vertices.push_back(LocalVertex{vertex.id, vertex.position});
  }

  local.cells.reserve(local_cell_ids.size());
  for (GlobalId cell_id : local_cell_ids) {
    const Cell& source = global.cells[static_cast<std::size_t>(cell_id)];
    LocalCell cell;
    cell.global_id = source.id;
    cell.owner = partition.cell_owner[static_cast<std::size_t>(source.id)];
    cell.owned = cell.owner == rank;
    for (GlobalId vertex : source.vertices) cell.vertices.push_back(vertex_local.at(vertex));
    for (GlobalId face : source.faces) {
      const auto found = face_local.find(face);
      if (found != face_local.end()) cell.faces.push_back(found->second);
    }
    cell.center = source.center;
    cell.area = source.area;
    local.cells.push_back(std::move(cell));
  }

  local.faces.reserve(needed_faces.size());
  for (GlobalId face_id : needed_faces) {
    const Face& source = global.faces[static_cast<std::size_t>(face_id)];
    LocalFace face;
    face.global_id = source.id;
    face.vertices = {{vertex_local.at(source.vertices[0]), vertex_local.at(source.vertices[1])}};
    face.left_cell = cell_local.at(source.left);
    if (source.right != invalid_global_id) face.right_cell = cell_local.at(source.right);
    face.center = source.center;
    face.length = source.length;
    face.normal = source.normal;
    face.boundary = source.boundary;
    local.faces.push_back(std::move(face));
  }

  for (const auto& neighbor_entry : schedules[static_cast<std::size_t>(rank)]) {
    NeighborSchedule schedule;
    schedule.rank = neighbor_entry.first;
    schedule.send_global_cells.assign(neighbor_entry.second.send.begin(),
                                      neighbor_entry.second.send.end());
    schedule.receive_global_cells.assign(neighbor_entry.second.receive.begin(),
                                         neighbor_entry.second.receive.end());
    for (GlobalId cell : schedule.send_global_cells) {
      schedule.send_local_cells.push_back(cell_local.at(cell));
    }
    for (GlobalId cell : schedule.receive_global_cells) {
      schedule.receive_local_cells.push_back(cell_local.at(cell));
    }
    local.halo.push_back(std::move(schedule));
  }
  local.diagnostics = global_diagnostics.ranks.at(static_cast<std::size_t>(rank));
  return local;
}

}  // namespace

Partitioning partition_cells(const Mesh& mesh, int number_of_parts) {
  if (number_of_parts < 1) throw std::invalid_argument("number_of_parts must be positive");
  if (mesh.cells.empty()) throw std::invalid_argument("cannot partition an empty mesh");
  if (static_cast<std::size_t>(number_of_parts) > mesh.cells.size()) {
    throw std::invalid_argument("number_of_parts exceeds the number of cells");
  }
  if (mesh.cells.size() > static_cast<std::size_t>(std::numeric_limits<idx_t>::max())) {
    throw std::overflow_error("mesh is too large for this METIS idx_t build");
  }

  Partitioning result;
  result.cell_owner.assign(mesh.cells.size(), 0);
  if (number_of_parts == 1) return result;

  std::vector<std::vector<idx_t>> adjacency(mesh.cells.size());
  for (const Face& face : mesh.faces) {
    if (face.right == invalid_global_id) continue;
    adjacency[static_cast<std::size_t>(face.left)].push_back(static_cast<idx_t>(face.right));
    adjacency[static_cast<std::size_t>(face.right)].push_back(static_cast<idx_t>(face.left));
  }
  std::vector<idx_t> offsets(mesh.cells.size() + 1U, 0);
  std::vector<idx_t> neighbors;
  for (std::size_t cell = 0; cell < adjacency.size(); ++cell) {
    auto& list = adjacency[cell];
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
    neighbors.insert(neighbors.end(), list.begin(), list.end());
    offsets[cell + 1U] = static_cast<idx_t>(neighbors.size());
  }

  idx_t vertices = static_cast<idx_t>(mesh.cells.size());
  idx_t constraints = 1;
  idx_t parts = static_cast<idx_t>(number_of_parts);
  idx_t edge_cut = 0;
  std::vector<idx_t> assignment(mesh.cells.size(), 0);
  std::array<idx_t, METIS_NOPTIONS> options{};
  METIS_SetDefaultOptions(options.data());
  options[METIS_OPTION_NUMBERING] = 0;
  options[METIS_OPTION_SEED] = 0;
  options[METIS_OPTION_CONTIG] = 1;
  const int status = METIS_PartGraphKway(&vertices, &constraints, offsets.data(), neighbors.data(),
                                         nullptr, nullptr, nullptr, &parts, nullptr, nullptr,
                                         options.data(), &edge_cut, assignment.data());
  if (status != METIS_OK) {
    throw std::runtime_error("METIS_PartGraphKway failed with status " +
                             std::to_string(status));
  }
  result.edge_cut = static_cast<std::int64_t>(edge_cut);
  for (std::size_t i = 0; i < assignment.size(); ++i) {
    if (assignment[i] < 0 || assignment[i] >= parts) {
      throw std::runtime_error("METIS returned an invalid partition ID");
    }
    result.cell_owner[i] = static_cast<int>(assignment[i]);
  }
  return result;
}

DistributedMesh partition_and_distribute(std::unique_ptr<Mesh> root_mesh,
                                         MPI_Comm communicator, int root) {
  int rank = 0;
  int size = 0;
  mpi_call(MPI_Comm_rank(communicator, &rank), "Comm_rank");
  mpi_call(MPI_Comm_size(communicator, &size), "Comm_size");
  if (root < 0 || root >= size) throw std::invalid_argument("invalid distribution root rank");
  if ((rank == root) != static_cast<bool>(root_mesh)) {
    throw std::invalid_argument("exactly the root rank must supply the global mesh");
  }

  DistributedMesh local;
  constexpr int size_tag = 4101;
  constexpr int payload_tag = 4102;
  if (rank == root) {
    validate_mesh(*root_mesh);
    const Partitioning partition = partition_cells(*root_mesh, size);
    std::vector<std::map<int, ScheduleSets>> schedules(static_cast<std::size_t>(size));
    std::int64_t crossing_faces = 0;
    for (const Face& face : root_mesh->faces) {
      if (face.right == invalid_global_id) continue;
      const int left_rank = partition.cell_owner[static_cast<std::size_t>(face.left)];
      const int right_rank = partition.cell_owner[static_cast<std::size_t>(face.right)];
      if (left_rank == right_rank) continue;
      ++crossing_faces;
      schedules[static_cast<std::size_t>(left_rank)][right_rank].send.insert(face.left);
      schedules[static_cast<std::size_t>(left_rank)][right_rank].receive.insert(face.right);
      schedules[static_cast<std::size_t>(right_rank)][left_rank].send.insert(face.right);
      schedules[static_cast<std::size_t>(right_rank)][left_rank].receive.insert(face.left);
    }
    if (crossing_faces != partition.edge_cut) {
      throw std::runtime_error("METIS edge cut disagrees with the constructed cell graph");
    }

    GlobalPartitionDiagnostics global_diagnostics;
    global_diagnostics.edge_cut = partition.edge_cut;
    global_diagnostics.total_owned_cells = root_mesh->cells.size();
    global_diagnostics.ranks.resize(static_cast<std::size_t>(size));
    for (int target = 0; target < size; ++target) {
      RankPartitionDiagnostics diagnostics;
      diagnostics.rank = target;
      diagnostics.owned_cells = static_cast<std::size_t>(std::count(
          partition.cell_owner.begin(), partition.cell_owner.end(), target));
      std::set<GlobalId> ghosts;
      for (const auto& item : schedules[static_cast<std::size_t>(target)]) {
        diagnostics.neighbor_ids.push_back(item.first);
        diagnostics.send_counts.push_back(item.second.send.size());
        diagnostics.receive_counts.push_back(item.second.receive.size());
        ghosts.insert(item.second.receive.begin(), item.second.receive.end());
      }
      diagnostics.ghost_cells = ghosts.size();
      global_diagnostics.total_ghost_cells += ghosts.size();
      global_diagnostics.ranks[static_cast<std::size_t>(target)] = std::move(diagnostics);
    }

    for (int target = 0; target < size; ++target) {
      DistributedMesh target_mesh = build_local_mesh(*root_mesh, partition, target, size,
                                                     schedules, global_diagnostics);
      if (target == root) {
        local = std::move(target_mesh);
      } else {
        std::vector<std::uint8_t> payload = serialize(target_mesh);
        const std::uint64_t bytes = static_cast<std::uint64_t>(payload.size());
        mpi_call(MPI_Send(&bytes, 1, MPI_UINT64_T, target, size_tag, communicator), "Send size");
        mpi_call(MPI_Send(payload.data(), mpi_count(payload.size(), "mesh payload"), MPI_BYTE,
                          target, payload_tag, communicator),
                 "Send payload");
      }
    }
    root_mesh.reset();
  } else {
    std::uint64_t bytes = 0;
    mpi_call(MPI_Recv(&bytes, 1, MPI_UINT64_T, root, size_tag, communicator, MPI_STATUS_IGNORE),
             "Recv size");
    if (bytes > static_cast<std::uint64_t>(INT_MAX) ||
        bytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      throw std::runtime_error("received distributed-mesh payload is too large");
    }
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(bytes));
    mpi_call(MPI_Recv(payload.data(), static_cast<int>(bytes), MPI_BYTE, root, payload_tag,
                      communicator, MPI_STATUS_IGNORE),
             "Recv payload");
    local = deserialize(payload);
  }
  if (local.rank != rank || local.size != size) {
    throw std::runtime_error("distributed-mesh payload rank metadata is inconsistent");
  }
  validate_distributed_mesh(local);
  return local;
}

void validate_distributed_mesh(const DistributedMesh& mesh) {
  if (mesh.rank < 0 || mesh.rank >= mesh.size || mesh.size < 1 ||
      mesh.owned_cell_count > mesh.cells.size() ||
      mesh.diagnostics.rank != mesh.rank ||
      mesh.diagnostics.owned_cells != mesh.owned_cell_count ||
      mesh.diagnostics.ghost_cells != mesh.cells.size() - mesh.owned_cell_count ||
      mesh.global_diagnostics.ranks.size() != static_cast<std::size_t>(mesh.size) ||
      mesh.global_diagnostics.total_owned_cells != mesh.global_cell_count) {
    throw std::runtime_error("distributed-mesh counts or diagnostics are inconsistent");
  }
  for (const LocalVertex& vertex : mesh.vertices) {
    if (vertex.global_id < 0 || !finite(vertex.position)) {
      throw std::runtime_error("distributed mesh contains an invalid vertex");
    }
  }
  for (std::size_t i = 0; i < mesh.cells.size(); ++i) {
    const LocalCell& cell = mesh.cells[i];
    const bool should_be_owned = i < mesh.owned_cell_count;
    if (cell.global_id < 0 || cell.owner < 0 || cell.owner >= mesh.size ||
        cell.owned != should_be_owned || cell.owned != (cell.owner == mesh.rank) ||
        !(cell.area > 0.0) || !std::isfinite(cell.area) || !finite(cell.center) ||
        (cell.vertices.size() != 3U && cell.vertices.size() != 4U)) {
      throw std::runtime_error("distributed mesh contains invalid cell metadata or geometry");
    }
    for (LocalIndex vertex : cell.vertices) {
      if (vertex < 0 || static_cast<std::size_t>(vertex) >= mesh.vertices.size()) {
        throw std::runtime_error("distributed cell references an invalid local vertex");
      }
    }
    double cross_sum = 0.0;
    Vec2 centroid_sum{};
    for (std::size_t vertex = 0; vertex < cell.vertices.size(); ++vertex) {
      const Vec2 p = mesh.vertices[static_cast<std::size_t>(cell.vertices[vertex])].position;
      const Vec2 q =
          mesh.vertices[static_cast<std::size_t>(
              cell.vertices[(vertex + 1U) % cell.vertices.size()])]
              .position;
      const double weight = cross(p, q);
      cross_sum += weight;
      centroid_sum += (p + q) * weight;
    }
    if (!(cross_sum > 0.0)) {
      throw std::runtime_error("distributed cell lost its positive vertex orientation");
    }
    const double reconstructed_area = 0.5 * cross_sum;
    const Vec2 reconstructed_center = centroid_sum / (3.0 * cross_sum);
    if (std::abs(reconstructed_area - cell.area) >
            1.0e-11 * std::max(1.0, cell.area) ||
        norm(reconstructed_center - cell.center) >
            1.0e-11 * std::max(1.0, std::sqrt(cell.area))) {
      throw std::runtime_error("distributed cell geometry was not preserved by serialization");
    }
    for (LocalIndex face : cell.faces) {
      if (face < 0 || static_cast<std::size_t>(face) >= mesh.faces.size()) {
        throw std::runtime_error("distributed cell references an invalid local face");
      }
    }
  }
  for (const LocalFace& face : mesh.faces) {
    if (face.global_id < 0 || face.vertices[0] < 0 || face.vertices[1] < 0 ||
        static_cast<std::size_t>(face.vertices[0]) >= mesh.vertices.size() ||
        static_cast<std::size_t>(face.vertices[1]) >= mesh.vertices.size() ||
        face.left_cell < 0 || static_cast<std::size_t>(face.left_cell) >= mesh.cells.size() ||
        (face.right_cell != invalid_local_index &&
         (face.right_cell < 0 || static_cast<std::size_t>(face.right_cell) >= mesh.cells.size())) ||
        !(face.length > 0.0) || !std::isfinite(face.length) || !finite(face.center) ||
        !finite(face.normal) || std::abs(norm(face.normal) - 1.0) > 1.0e-10) {
      throw std::runtime_error("distributed mesh contains invalid face metadata or geometry");
    }
    const Vec2 p = mesh.vertices[static_cast<std::size_t>(face.vertices[0])].position;
    const Vec2 q = mesh.vertices[static_cast<std::size_t>(face.vertices[1])].position;
    const Vec2 edge = q - p;
    const double edge_length = norm(edge);
    const double geometry_scale = std::max(1.0, edge_length);
    if (!(edge_length > 0.0) ||
        std::abs(edge_length - face.length) > 1.0e-11 * geometry_scale ||
        norm(face.center - (p + q) * 0.5) > 1.0e-11 * geometry_scale ||
        std::abs(dot(face.normal, edge)) > 1.0e-12 * edge_length) {
      throw std::runtime_error("distributed face geometry was not preserved by serialization");
    }
    const bool left_owned = mesh.cells[static_cast<std::size_t>(face.left_cell)].owned;
    const bool right_owned = face.right_cell != invalid_local_index &&
                             mesh.cells[static_cast<std::size_t>(face.right_cell)].owned;
    if (!left_owned && !right_owned) {
      throw std::runtime_error("distributed mesh retained a face unused by owned residuals");
    }
    const Vec2 target =
        face.right_cell == invalid_local_index
            ? face.center - mesh.cells[static_cast<std::size_t>(face.left_cell)].center
            : mesh.cells[static_cast<std::size_t>(face.right_cell)].center -
                  mesh.cells[static_cast<std::size_t>(face.left_cell)].center;
    if (!(dot(face.normal, target) > 0.0)) {
      throw std::runtime_error("distributed face normal orientation is invalid");
    }
    if ((face.right_cell == invalid_local_index) != !face.boundary.empty()) {
      throw std::runtime_error("distributed face boundary tag and incidence disagree");
    }
  }
  if (mesh.halo.size() != mesh.diagnostics.neighbor_ids.size() ||
      mesh.halo.size() != mesh.diagnostics.send_counts.size() ||
      mesh.halo.size() != mesh.diagnostics.receive_counts.size()) {
    throw std::runtime_error("distributed halo and neighbor diagnostics disagree");
  }
  for (std::size_t i = 0; i < mesh.halo.size(); ++i) {
    const NeighborSchedule& schedule = mesh.halo[i];
    if (schedule.rank != mesh.diagnostics.neighbor_ids[i] ||
        schedule.send_global_cells.size() != schedule.send_local_cells.size() ||
        schedule.receive_global_cells.size() != schedule.receive_local_cells.size() ||
        schedule.send_global_cells.size() != mesh.diagnostics.send_counts[i] ||
        schedule.receive_global_cells.size() != mesh.diagnostics.receive_counts[i]) {
      throw std::runtime_error("distributed halo schedule sizes are inconsistent");
    }
    for (std::size_t cell = 0; cell < schedule.send_local_cells.size(); ++cell) {
      const LocalIndex local = schedule.send_local_cells[cell];
      if (local < 0 || static_cast<std::size_t>(local) >= mesh.owned_cell_count ||
          mesh.cells[static_cast<std::size_t>(local)].global_id !=
              schedule.send_global_cells[cell]) {
        throw std::runtime_error("distributed halo send schedule is invalid");
      }
    }
    for (std::size_t cell = 0; cell < schedule.receive_local_cells.size(); ++cell) {
      const LocalIndex local = schedule.receive_local_cells[cell];
      if (local < 0 || static_cast<std::size_t>(local) < mesh.owned_cell_count ||
          static_cast<std::size_t>(local) >= mesh.cells.size() ||
          mesh.cells[static_cast<std::size_t>(local)].global_id !=
              schedule.receive_global_cells[cell]) {
        throw std::runtime_error("distributed halo receive schedule is invalid");
      }
    }
  }
}

HaloExchangeWorkspaceStatistics HaloExchangeWorkspace::statistics() const noexcept {
  HaloExchangeWorkspaceStatistics result;
  result.cached_widths = widths_.size();
  result.growth_events = growth_events_;
  for (const WidthBuffers& width : widths_) {
    result.neighbor_buffer_capacity += width.neighbors.capacity();
    result.request_capacity += width.requests.capacity();
    for (const NeighborBuffers& neighbor : width.neighbors) {
      result.send_value_capacity += neighbor.send.capacity();
      result.receive_value_capacity += neighbor.receive.capacity();
    }
  }
  return result;
}

void exchange_halo(const DistributedMesh& mesh, std::vector<double>& values, std::size_t width,
                   MPI_Comm communicator) {
  if (width == 0U) throw std::invalid_argument("halo value width must be positive");
  if (mesh.cells.size() > std::numeric_limits<std::size_t>::max() / width ||
      values.size() != mesh.cells.size() * width) {
    throw std::invalid_argument("halo array size does not equal local cell count times width");
  }
  int rank = 0;
  int size = 0;
  mpi_call(MPI_Comm_rank(communicator, &rank), "Comm_rank");
  mpi_call(MPI_Comm_size(communicator, &size), "Comm_size");
  if (rank != mesh.rank || size != mesh.size) {
    throw std::invalid_argument("communicator does not match the distributed mesh");
  }

  HaloExchangeWorkspace& workspace = mesh.halo_exchange_workspace;
  auto reserve = [&workspace](auto& values_to_grow, std::size_t capacity) {
    if (values_to_grow.capacity() >= capacity) return;
    values_to_grow.reserve(capacity);
    ++workspace.growth_events_;
  };
  auto found = std::find_if(workspace.widths_.begin(), workspace.widths_.end(),
                            [width](const HaloExchangeWorkspace::WidthBuffers& item) {
                              return item.width == width;
                            });
  if (found == workspace.widths_.end()) {
    reserve(workspace.widths_, workspace.widths_.size() + 1U);
    workspace.widths_.push_back({});
    found = std::prev(workspace.widths_.end());
    found->width = width;
  }
  HaloExchangeWorkspace::WidthBuffers& buffers = *found;
  reserve(buffers.neighbors, mesh.halo.size());
  buffers.neighbors.resize(mesh.halo.size());
  if (mesh.halo.size() > std::numeric_limits<std::size_t>::max() / 2U) {
    throw std::overflow_error("halo neighbor count exceeds request storage");
  }
  reserve(buffers.requests, mesh.halo.size() * 2U);
  buffers.requests.resize(mesh.halo.size() * 2U);

  constexpr int halo_tag = 4200;
  for (std::size_t i = 0; i < mesh.halo.size(); ++i) {
    const NeighborSchedule& schedule = mesh.halo[i];
    HaloExchangeWorkspace::NeighborBuffers& data = buffers.neighbors[i];
    const std::size_t receive_count = schedule.receive_local_cells.size() * width;
    reserve(data.receive, receive_count);
    data.receive.resize(receive_count);
    MPI_Request& request = buffers.requests[i];
    request = MPI_REQUEST_NULL;
    mpi_call(MPI_Irecv(data.receive.data(), mpi_count(data.receive.size(), "halo receive"),
                        MPI_DOUBLE, schedule.rank, halo_tag, communicator, &request),
              "Irecv halo");
  }
  for (std::size_t i = 0; i < mesh.halo.size(); ++i) {
    const NeighborSchedule& schedule = mesh.halo[i];
    HaloExchangeWorkspace::NeighborBuffers& data = buffers.neighbors[i];
    const std::size_t send_count = schedule.send_local_cells.size() * width;
    reserve(data.send, send_count);
    data.send.resize(send_count);
    for (std::size_t cell = 0; cell < schedule.send_local_cells.size(); ++cell) {
      const auto local_cell = static_cast<std::size_t>(schedule.send_local_cells[cell]);
      std::copy_n(values.data() + local_cell * width, width, data.send.data() + cell * width);
    }
    MPI_Request& request = buffers.requests[mesh.halo.size() + i];
    request = MPI_REQUEST_NULL;
    mpi_call(MPI_Isend(data.send.data(), mpi_count(data.send.size(), "halo send"), MPI_DOUBLE,
                        schedule.rank, halo_tag, communicator, &request),
              "Isend halo");
  }
  if (!buffers.requests.empty()) {
    mpi_call(MPI_Waitall(mpi_count(buffers.requests.size(), "halo request list"),
                          buffers.requests.data(),
                          MPI_STATUSES_IGNORE),
              "Waitall halo");
  }
  for (std::size_t i = 0; i < mesh.halo.size(); ++i) {
    const NeighborSchedule& schedule = mesh.halo[i];
    const HaloExchangeWorkspace::NeighborBuffers& data = buffers.neighbors[i];
    for (std::size_t cell = 0; cell < schedule.receive_local_cells.size(); ++cell) {
      const auto local_cell = static_cast<std::size_t>(schedule.receive_local_cells[cell]);
      std::copy_n(data.receive.data() + cell * width, width, values.data() + local_cell * width);
    }
  }
}

}  // namespace cfd
