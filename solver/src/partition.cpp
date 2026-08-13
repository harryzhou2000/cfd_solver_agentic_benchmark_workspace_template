#include "partition.hpp"

#include <metis.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace aerofv {
namespace {

void mpi_check(int status, const char *operation) {
  if (status != MPI_SUCCESS) {
    char message[MPI_MAX_ERROR_STRING]{};
    int length = 0;
    MPI_Error_string(status, message, &length);
    throw std::runtime_error(std::string(operation) + ": " +
                             std::string(message, static_cast<std::size_t>(length)));
  }
}

template <typename T> void append(std::vector<char> &out, const T &value) {
  static_assert(std::is_trivially_copyable_v<T>);
  const auto *bytes = reinterpret_cast<const char *>(&value);
  out.insert(out.end(), bytes, bytes + sizeof(T));
}
template <typename T> T consume(const std::vector<char> &in, std::size_t &offset) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (offset + sizeof(T) > in.size()) {
    throw std::runtime_error("truncated LocalMesh MPI payload");
  }
  T value{};
  std::memcpy(&value, in.data() + offset, sizeof(T));
  offset += sizeof(T);
  return value;
}
void append_string(std::vector<char> &out, const std::string &text) {
  append<std::uint64_t>(out, text.size());
  out.insert(out.end(), text.begin(), text.end());
}
std::string consume_string(const std::vector<char> &in, std::size_t &offset) {
  const auto size = consume<std::uint64_t>(in, offset);
  if (size > in.size() - offset) {
    throw std::runtime_error("truncated string in LocalMesh MPI payload");
  }
  std::string result(in.data() + offset, in.data() + offset + size);
  offset += static_cast<std::size_t>(size);
  return result;
}

std::vector<char> pack(const LocalMesh &mesh) {
  std::vector<char> out;
  append(out, mesh.owned_cell_count);
  append(out, mesh.global_cell_count); append(out, mesh.global_face_count);
  append(out, mesh.global_vertex_count); append(out, mesh.edge_cut);
  append<std::uint64_t>(out, mesh.vertices.size());
  for (const Vec2 &v : mesh.vertices) { append(out, v.x); append(out, v.y); }
  append<std::uint64_t>(out, mesh.global_vertex_ids.size());
  for (auto value : mesh.global_vertex_ids) append(out, value);
  append<std::uint64_t>(out, mesh.cells.size());
  for (const Cell &cell : mesh.cells) {
    append(out, cell.centroid.x); append(out, cell.centroid.y); append(out, cell.area);
    append<std::uint64_t>(out, cell.vertices.size());
    for (int value : cell.vertices) append(out, value);
    append<std::uint64_t>(out, cell.faces.size());
    for (int value : cell.faces) append(out, value);
  }
  append<std::uint64_t>(out, mesh.global_cell_ids.size());
  for (auto value : mesh.global_cell_ids) append(out, value);
  append<std::uint64_t>(out, mesh.faces.size());
  for (const Face &face : mesh.faces) {
    append(out, face.vertices[0]); append(out, face.vertices[1]);
    append(out, face.left_cell); append(out, face.right_cell);
    append(out, face.center.x); append(out, face.center.y);
    append(out, face.normal.x); append(out, face.normal.y); append(out, face.length);
    append_string(out, face.boundary_family);
  }
  append<std::uint64_t>(out, mesh.global_face_ids.size());
  for (auto value : mesh.global_face_ids) append(out, value);
  append<std::uint64_t>(out, mesh.exchanges.size());
  for (const NeighborExchange &exchange : mesh.exchanges) {
    append(out, exchange.rank);
    append<std::uint64_t>(out, exchange.send_owned_local.size());
    for (int value : exchange.send_owned_local) append(out, value);
    append<std::uint64_t>(out, exchange.receive_ghost_local.size());
    for (int value : exchange.receive_ghost_local) append(out, value);
  }
  return out;
}

LocalMesh unpack(const std::vector<char> &in) {
  std::size_t offset = 0;
  LocalMesh mesh;
  mesh.owned_cell_count = consume<int>(in, offset);
  mesh.global_cell_count = consume<std::int64_t>(in, offset);
  mesh.global_face_count = consume<std::int64_t>(in, offset);
  mesh.global_vertex_count = consume<std::int64_t>(in, offset);
  mesh.edge_cut = consume<std::int64_t>(in, offset);
  const auto vertices = consume<std::uint64_t>(in, offset);
  mesh.vertices.resize(static_cast<std::size_t>(vertices));
  for (Vec2 &v : mesh.vertices) { v.x = consume<double>(in, offset); v.y = consume<double>(in, offset); }
  const auto global_vertices = consume<std::uint64_t>(in, offset);
  mesh.global_vertex_ids.resize(static_cast<std::size_t>(global_vertices));
  for (auto &value : mesh.global_vertex_ids) value = consume<std::int64_t>(in, offset);
  const auto cells = consume<std::uint64_t>(in, offset);
  mesh.cells.resize(static_cast<std::size_t>(cells));
  for (Cell &cell : mesh.cells) {
    cell.centroid.x = consume<double>(in, offset); cell.centroid.y = consume<double>(in, offset);
    cell.area = consume<double>(in, offset);
    const auto nvertices = consume<std::uint64_t>(in, offset);
    cell.vertices.resize(static_cast<std::size_t>(nvertices));
    for (int &value : cell.vertices) value = consume<int>(in, offset);
    const auto nfaces = consume<std::uint64_t>(in, offset);
    cell.faces.resize(static_cast<std::size_t>(nfaces));
    for (int &value : cell.faces) value = consume<int>(in, offset);
  }
  const auto global_cells = consume<std::uint64_t>(in, offset);
  mesh.global_cell_ids.resize(static_cast<std::size_t>(global_cells));
  for (auto &value : mesh.global_cell_ids) value = consume<std::int64_t>(in, offset);
  const auto faces = consume<std::uint64_t>(in, offset);
  mesh.faces.resize(static_cast<std::size_t>(faces));
  for (Face &face : mesh.faces) {
    face.vertices[0] = consume<int>(in, offset); face.vertices[1] = consume<int>(in, offset);
    face.left_cell = consume<int>(in, offset); face.right_cell = consume<int>(in, offset);
    face.center.x = consume<double>(in, offset); face.center.y = consume<double>(in, offset);
    face.normal.x = consume<double>(in, offset); face.normal.y = consume<double>(in, offset);
    face.length = consume<double>(in, offset); face.boundary_family = consume_string(in, offset);
  }
  const auto global_faces = consume<std::uint64_t>(in, offset);
  mesh.global_face_ids.resize(static_cast<std::size_t>(global_faces));
  for (auto &value : mesh.global_face_ids) value = consume<std::int64_t>(in, offset);
  const auto exchanges = consume<std::uint64_t>(in, offset);
  mesh.exchanges.resize(static_cast<std::size_t>(exchanges));
  for (NeighborExchange &exchange : mesh.exchanges) {
    exchange.rank = consume<int>(in, offset);
    const auto sends = consume<std::uint64_t>(in, offset);
    exchange.send_owned_local.resize(static_cast<std::size_t>(sends));
    for (int &value : exchange.send_owned_local) value = consume<int>(in, offset);
    const auto receives = consume<std::uint64_t>(in, offset);
    exchange.receive_ghost_local.resize(static_cast<std::size_t>(receives));
    for (int &value : exchange.receive_ghost_local) value = consume<int>(in, offset);
  }
  if (offset != in.size()) throw std::runtime_error("extra bytes in LocalMesh MPI payload");
  return mesh;
}

} // namespace

CellPartition partition_cells_metis(const GlobalMesh &mesh, int ranks) {
  if (ranks < 1) throw std::invalid_argument("partition rank count must be positive");
  mesh.validate_topology();
  CellPartition partition;
  partition.owner_by_cell.assign(mesh.cells.size(), 0);
  if (ranks == 1) return partition;
  std::vector<std::vector<idx_t>> neighbors(mesh.cells.size());
  for (const Face &face : mesh.faces) {
    if (face.right_cell >= 0) {
      neighbors[face.left_cell].push_back(static_cast<idx_t>(face.right_cell));
      neighbors[face.right_cell].push_back(static_cast<idx_t>(face.left_cell));
    }
  }
  std::vector<idx_t> xadj(mesh.cells.size() + 1, 0), adjncy;
  for (std::size_t i = 0; i < neighbors.size(); ++i) {
    std::sort(neighbors[i].begin(), neighbors[i].end());
    neighbors[i].erase(std::unique(neighbors[i].begin(), neighbors[i].end()), neighbors[i].end());
    xadj[i + 1] = xadj[i] + static_cast<idx_t>(neighbors[i].size());
    adjncy.insert(adjncy.end(), neighbors[i].begin(), neighbors[i].end());
  }
  idx_t nvtxs = static_cast<idx_t>(mesh.cells.size()), ncon = 1, nparts = static_cast<idx_t>(ranks);
  idx_t edgecut = 0;
  std::vector<idx_t> assignment(mesh.cells.size(), 0);
  idx_t options[METIS_NOPTIONS];
  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_SEED] = 42;
  const int status = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(), nullptr,
                                         nullptr, nullptr, &nparts, nullptr, nullptr, options,
                                         &edgecut, assignment.data());
  if (status != METIS_OK) throw std::runtime_error("METIS_PartGraphKway failed");
  for (std::size_t i = 0; i < assignment.size(); ++i) partition.owner_by_cell[i] = assignment[i];
  for (const Face &face : mesh.faces) {
    if (face.right_cell >= 0 && partition.owner_by_cell[face.left_cell] != partition.owner_by_cell[face.right_cell]) {
      ++partition.edge_cut;
    }
  }
  return partition;
}

LocalMesh build_local_mesh(const GlobalMesh &mesh, const CellPartition &partition, int rank) {
  if (partition.owner_by_cell.size() != mesh.cells.size()) throw std::invalid_argument("partition size mismatch");
  LocalMesh local;
  local.global_cell_count = static_cast<std::int64_t>(mesh.cells.size());
  local.global_face_count = static_cast<std::int64_t>(mesh.faces.size());
  local.global_vertex_count = static_cast<std::int64_t>(mesh.vertices.size());
  local.edge_cut = partition.edge_cut;
  std::vector<int> selected;
  for (std::size_t cell = 0; cell < mesh.cells.size(); ++cell)
    if (partition.owner_by_cell[cell] == rank) selected.push_back(static_cast<int>(cell));
  local.owned_cell_count = static_cast<int>(selected.size());
  std::set<int> ghosts;
  for (int cell : selected) for (int face_id : mesh.cells[cell].faces) {
    const Face &face = mesh.faces[face_id]; const int other = face.left_cell == cell ? face.right_cell : face.left_cell;
    if (other >= 0 && partition.owner_by_cell[other] != rank) ghosts.insert(other);
  }
  selected.insert(selected.end(), ghosts.begin(), ghosts.end());
  std::map<int, int> local_cell;
  for (std::size_t i = 0; i < selected.size(); ++i) local_cell.emplace(selected[i], static_cast<int>(i));
  local.cells.resize(selected.size()); local.global_cell_ids.reserve(selected.size());
  std::set<int> global_faces;
  std::set<int> global_vertices;
  for (int cell : selected) {
    for (int vertex : mesh.cells[cell].vertices) global_vertices.insert(vertex);
  }
  for (int cell : selected) if (partition.owner_by_cell[cell] == rank)
    for (int face : mesh.cells[cell].faces) global_faces.insert(face);
  std::map<int, int> local_vertex;
  for (int vertex : global_vertices) {
    local_vertex.emplace(vertex, static_cast<int>(local.vertices.size()));
    local.vertices.push_back(mesh.vertices[vertex]); local.global_vertex_ids.push_back(vertex);
  }
  std::map<int, int> local_face;
  int next_local_face = 0;
  for (int face : global_faces) {
    local_face.emplace(face, next_local_face++);
  }
  for (std::size_t local_id = 0; local_id < selected.size(); ++local_id) {
    const Cell &source = mesh.cells[selected[local_id]]; Cell copy = source;
    for (int &vertex : copy.vertices) vertex = local_vertex.at(vertex);
    copy.faces.clear();
    for (int face : source.faces) { const auto found = local_face.find(face); if (found != local_face.end()) copy.faces.push_back(found->second); }
    local.cells[local_id] = std::move(copy); local.global_cell_ids.push_back(selected[local_id]);
  }
  for (int global_face : global_faces) {
    const Face &source = mesh.faces[global_face]; Face copy = source;
    copy.vertices = {{local_vertex.at(source.vertices[0]), local_vertex.at(source.vertices[1])}};
    copy.left_cell = local_cell.count(source.left_cell) ? local_cell.at(source.left_cell) : -1;
    copy.right_cell = source.right_cell >= 0 && local_cell.count(source.right_cell) ? local_cell.at(source.right_cell) : -1;
    local.faces.push_back(std::move(copy)); local.global_face_ids.push_back(global_face);
  }
  std::map<int, NeighborExchange> exchanges;
  for (int owned = 0; owned < local.owned_cell_count; ++owned) {
    const int global = selected[owned];
    for (int face_id : mesh.cells[global].faces) {
      const Face &face = mesh.faces[face_id]; const int other = face.left_cell == global ? face.right_cell : face.left_cell;
      if (other < 0 || partition.owner_by_cell[other] == rank) continue;
      const int peer = partition.owner_by_cell[other];
      auto &exchange = exchanges[peer]; exchange.rank = peer; exchange.send_owned_local.push_back(owned);
      exchange.receive_ghost_local.push_back(local_cell.at(other));
    }
  }
  for (auto &[peer, exchange] : exchanges) {
    std::sort(exchange.send_owned_local.begin(), exchange.send_owned_local.end());
    exchange.send_owned_local.erase(std::unique(exchange.send_owned_local.begin(), exchange.send_owned_local.end()), exchange.send_owned_local.end());
    std::sort(exchange.receive_ghost_local.begin(), exchange.receive_ghost_local.end());
    exchange.receive_ghost_local.erase(std::unique(exchange.receive_ghost_local.begin(), exchange.receive_ghost_local.end()), exchange.receive_ghost_local.end());
    local.exchanges.push_back(std::move(exchange));
  }
  return local;
}

LocalMesh partition_and_distribute(const GlobalMesh *global_on_root, MPI_Comm communicator) {
  int rank = 0, ranks = 0;
  mpi_check(MPI_Comm_rank(communicator, &rank), "MPI_Comm_rank");
  mpi_check(MPI_Comm_size(communicator, &ranks), "MPI_Comm_size");
  if ((rank == 0) != (global_on_root != nullptr)) {
    throw std::invalid_argument("only rank zero supplies GlobalMesh to partition_and_distribute");
  }
  if (rank == 0) {
    const CellPartition partition = partition_cells_metis(*global_on_root, ranks);
    LocalMesh mine;
    for (int destination = 0; destination < ranks; ++destination) {
      LocalMesh local = build_local_mesh(*global_on_root, partition, destination);
      if (destination == 0) { mine = std::move(local); continue; }
      const std::vector<char> bytes = pack(local);
      const std::uint64_t count = bytes.size();
      mpi_check(MPI_Send(&count, 1, MPI_UINT64_T, destination, 700, communicator), "MPI_Send(payload size)");
      mpi_check(MPI_Send(bytes.data(), static_cast<int>(bytes.size()), MPI_BYTE, destination, 701, communicator), "MPI_Send(payload)");
    }
    return mine;
  }
  std::uint64_t count = 0;
  mpi_check(MPI_Recv(&count, 1, MPI_UINT64_T, 0, 700, communicator, MPI_STATUS_IGNORE), "MPI_Recv(payload size)");
  if (count > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) throw std::runtime_error("LocalMesh MPI payload too large");
  std::vector<char> bytes(static_cast<std::size_t>(count));
  mpi_check(MPI_Recv(bytes.data(), static_cast<int>(bytes.size()), MPI_BYTE, 0, 701, communicator, MPI_STATUS_IGNORE), "MPI_Recv(payload)");
  return unpack(bytes);
}

} // namespace aerofv
