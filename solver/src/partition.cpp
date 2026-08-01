#include "cfd/partition.hpp"

#include <metis.h>

#include <array>
#include <cstring>
#include <limits>
#include <set>
#include <unordered_map>

namespace cfd {
namespace {

constexpr int kDistributionHeaderTag = 27410;
constexpr int kDistributionPayloadTag = 27411;

class ByteWriter {
public:
    template <class T> void put(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto old = bytes_.size(); bytes_.resize(old + sizeof(T));
        std::memcpy(bytes_.data() + old, &value, sizeof(T));
    }
    void put_string(const std::string& text) {
        put<std::uint64_t>(text.size());
        const auto old = bytes_.size(); bytes_.resize(old + text.size());
        std::memcpy(bytes_.data() + old, text.data(), text.size());
    }
    [[nodiscard]] std::vector<std::byte>&& take() && { return std::move(bytes_); }
private:
    std::vector<std::byte> bytes_;
};

class ByteReader {
public:
    explicit ByteReader(const std::vector<std::byte>& bytes) : bytes_(bytes) {}
    template <class T> T get() {
        static_assert(std::is_trivially_copyable_v<T>);
        if (offset_ + sizeof(T) > bytes_.size()) throw std::runtime_error("truncated distributed mesh payload");
        T value{}; std::memcpy(&value, bytes_.data() + offset_, sizeof(T)); offset_ += sizeof(T); return value;
    }
    std::string get_string() {
        const auto n = get<std::uint64_t>();
        if (n > bytes_.size() - offset_) throw std::runtime_error("truncated distributed mesh string");
        std::string text(reinterpret_cast<const char*>(bytes_.data() + offset_), static_cast<std::size_t>(n));
        offset_ += static_cast<std::size_t>(n); return text;
    }
    [[nodiscard]] bool done() const noexcept { return offset_ == bytes_.size(); }
private:
    const std::vector<std::byte>& bytes_;
    std::size_t offset_{};
};

struct WireHeader {
    std::int64_t global_cells{};
    std::int64_t global_faces{};
    int edge_cut{};
    std::uint64_t nodes{};
    std::uint64_t cells{};
    std::uint64_t owned{};
    std::uint64_t faces{};
    std::uint64_t peers{};
};
struct WireNode { GlobalIndex global_id; Vec2 xy; };
struct WireCell { Cell cell; std::uint8_t owned; };
struct WireFace {
    GlobalIndex global_id;
    std::array<GlobalIndex, 2> vertices;
    GlobalIndex owner;
    GlobalIndex neighbor;
    Vec2 center;
    Vec2 normal;
    double length;
    LocalIndex owner_local;
    LocalIndex neighbor_local;
};

std::vector<std::byte> serialize(const DistributedMesh& local) {
    ByteWriter out;
    out.put(WireHeader{local.global_cell_count, local.global_face_count, local.partition_edge_cut,
                       local.nodes.size(), local.cells.size(), local.owned_count,
                       local.faces.size(), local.halo.size()});
    for (const auto& node : local.nodes) out.put(WireNode{node.global_id, node.xy});
    for (const auto& cell : local.cells) out.put(WireCell{cell.cell, static_cast<std::uint8_t>(cell.owned)});
    for (const auto& face : local.faces) {
        out.put(WireFace{face.face.global_id, face.face.vertices, face.face.owner, face.face.neighbor,
                         face.face.center, face.face.normal, face.face.length,
                         face.owner_local, face.neighbor_local});
        out.put_string(face.face.tag);
    }
    for (const auto& list : local.cell_faces) {
        out.put<std::uint64_t>(list.size()); for (const auto i : list) out.put(i);
    }
    for (const auto& list : local.adjacency) {
        out.put<std::uint64_t>(list.size()); for (const auto i : list) out.put(i);
    }
    for (const auto& peer : local.halo) {
        out.put(peer.rank); out.put<std::uint64_t>(peer.send_owned.size());
        for (const auto i : peer.send_owned) out.put(i);
        out.put<std::uint64_t>(peer.recv_ghost.size());
        for (const auto i : peer.recv_ghost) out.put(i);
    }
    return std::move(out).take();
}

DistributedMesh deserialize(const std::vector<std::byte>& payload, int rank, int size) {
    ByteReader in(payload); const auto header = in.get<WireHeader>();
    DistributedMesh local; local.rank = rank; local.size = size;
    local.global_cell_count = header.global_cells; local.global_face_count = header.global_faces;
    local.partition_edge_cut = header.edge_cut; local.owned_count = static_cast<std::size_t>(header.owned);
    local.nodes.reserve(static_cast<std::size_t>(header.nodes));
    local.cells.reserve(static_cast<std::size_t>(header.cells)); local.faces.reserve(static_cast<std::size_t>(header.faces));
    for (std::uint64_t i = 0; i < header.nodes; ++i) { const auto n = in.get<WireNode>(); local.nodes.push_back({n.global_id, n.xy}); }
    for (std::uint64_t i = 0; i < header.cells; ++i) { const auto c = in.get<WireCell>(); local.cells.push_back({c.cell, c.owned != 0}); }
    for (std::uint64_t i = 0; i < header.faces; ++i) {
        const auto f = in.get<WireFace>();
        Face face; face.global_id = f.global_id; face.vertices = f.vertices; face.owner = f.owner;
        face.neighbor = f.neighbor; face.center = f.center; face.normal = f.normal; face.length = f.length;
        face.tag = in.get_string(); local.faces.push_back({std::move(face), f.owner_local, f.neighbor_local});
    }
    local.cell_faces.resize(local.cells.size());
    for (auto& list : local.cell_faces) { const auto n = in.get<std::uint64_t>(); list.resize(static_cast<std::size_t>(n)); for (auto& i : list) i = in.get<LocalIndex>(); }
    local.adjacency.resize(local.cells.size());
    for (auto& list : local.adjacency) { const auto n = in.get<std::uint64_t>(); list.resize(static_cast<std::size_t>(n)); for (auto& i : list) i = in.get<LocalIndex>(); }
    for (std::uint64_t p = 0; p < header.peers; ++p) {
        HaloPeer peer; peer.rank = in.get<int>(); const auto ns = in.get<std::uint64_t>(); peer.send_owned.resize(static_cast<std::size_t>(ns));
        for (auto& i : peer.send_owned) i = in.get<LocalIndex>();
        const auto nr = in.get<std::uint64_t>();
        peer.recv_ghost.resize(static_cast<std::size_t>(nr));
        for (auto& i : peer.recv_ghost) i = in.get<LocalIndex>();
        local.halo.push_back(std::move(peer));
    }
    if (!in.done()) throw std::runtime_error("extra bytes in distributed mesh payload");
    local.diagnostics.rank = rank; local.diagnostics.num_cells_owned = static_cast<std::int64_t>(local.owned_count);
    local.diagnostics.num_cells_ghost = static_cast<std::int64_t>(local.cells.size() - local.owned_count);
    for (const auto& f : local.faces) if (f.face.neighbor < 0 && f.owner_local >= 0 && local.is_owned(static_cast<std::size_t>(f.owner_local))) ++local.diagnostics.num_boundary_faces;
    for (const auto& peer : local.halo) { local.diagnostics.neighbor_ranks.push_back(peer.rank); local.diagnostics.send_cells.push_back(peer.send_owned.size()); local.diagnostics.recv_cells.push_back(peer.recv_ghost.size()); }
    return local;
}

DistributedMesh make_local(const Mesh& mesh, const PartitionResult& part, int rank, int size) {
    DistributedMesh local; local.rank = rank; local.size = size;
    local.global_cell_count = static_cast<std::int64_t>(mesh.cells.size());
    local.global_face_count = static_cast<std::int64_t>(mesh.faces.size());
    local.partition_edge_cut = part.edge_cut;
    std::set<GlobalIndex> owned, ghosts;
    for (const auto& c : mesh.cells) if (part.cell_owner.at(static_cast<std::size_t>(c.global_id)) == rank) owned.insert(c.global_id);
    for (const auto cell : owned) {
        for (const auto adjacent : mesh.adjacency.at(static_cast<std::size_t>(cell))) {
            if (part.cell_owner.at(static_cast<std::size_t>(adjacent)) != rank) ghosts.insert(adjacent);
        }
    }
    std::unordered_map<GlobalIndex, LocalIndex> index;
    index.reserve(owned.size() + ghosts.size());
    const auto append = [&](GlobalIndex global, bool is_owned) {
        const auto local_index = static_cast<LocalIndex>(local.cells.size());
        index.emplace(global, local_index); local.cells.push_back({mesh.cells.at(static_cast<std::size_t>(global)), is_owned});
    };
    for (const auto global : owned) append(global, true);
    local.owned_count = local.cells.size();
    for (const auto global : ghosts) append(global, false);

    std::set<GlobalIndex> node_ids;
    for (const auto& c : local.cells) for (std::uint8_t i = 0; i < c.cell.vertex_count; ++i) node_ids.insert(c.cell.vertices[i]);
    for (const auto node : node_ids) local.nodes.push_back(mesh.nodes.at(static_cast<std::size_t>(node)));

    local.cell_faces.resize(local.cells.size()); local.adjacency.resize(local.cells.size());
    for (const auto& f : mesh.faces) {
        const auto oi = index.find(f.owner), ni = f.neighbor < 0 ? index.end() : index.find(f.neighbor);
        const bool owns_owner = oi != index.end() && local.cells[static_cast<std::size_t>(oi->second)].owned;
        const bool owns_neighbor = ni != index.end() && local.cells[static_cast<std::size_t>(ni->second)].owned;
        if (!owns_owner && !owns_neighbor) continue;
        const auto face_index = static_cast<LocalIndex>(local.faces.size());
        const LocalIndex owner_local = oi == index.end() ? -1 : oi->second;
        const LocalIndex neighbor_local = ni == index.end() ? -1 : ni->second;
        local.faces.push_back({f, owner_local, neighbor_local});
        if (owner_local >= 0) local.cell_faces[static_cast<std::size_t>(owner_local)].push_back(face_index);
        if (neighbor_local >= 0) local.cell_faces[static_cast<std::size_t>(neighbor_local)].push_back(face_index);
        if (owner_local >= 0 && neighbor_local >= 0) {
            local.adjacency[static_cast<std::size_t>(owner_local)].push_back(neighbor_local);
            local.adjacency[static_cast<std::size_t>(neighbor_local)].push_back(owner_local);
        }
    }

    std::map<int, std::set<std::pair<GlobalIndex, GlobalIndex>>> halo_ids;
    for (const auto& f : mesh.faces) {
        if (f.neighbor < 0) continue;
        const int po = part.cell_owner.at(static_cast<std::size_t>(f.owner));
        const int pn = part.cell_owner.at(static_cast<std::size_t>(f.neighbor));
        if (po == pn) continue;
        if (rank == po) halo_ids[pn].emplace(f.owner, f.neighbor);
        if (rank == pn) halo_ids[po].emplace(f.neighbor, f.owner);
    }
    for (const auto& [peer_rank, pairs] : halo_ids) {
        HaloPeer peer; peer.rank = peer_rank;
        for (const auto& [send, recv] : pairs) peer.send_owned.push_back(index.at(send));
        std::vector<std::pair<GlobalIndex, GlobalIndex>> receive_order(pairs.begin(), pairs.end());
        std::sort(receive_order.begin(), receive_order.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second < b.second : a.first < b.first;
        });
        for (const auto& [send, recv] : receive_order) {
            static_cast<void>(send); peer.recv_ghost.push_back(index.at(recv));
        }
        local.halo.push_back(std::move(peer));
    }
    local.diagnostics.rank = rank; local.diagnostics.num_cells_owned = static_cast<std::int64_t>(local.owned_count);
    local.diagnostics.num_cells_ghost = static_cast<std::int64_t>(local.cells.size() - local.owned_count);
    for (const auto& f : local.faces) if (f.face.neighbor < 0 && f.owner_local >= 0 && local.is_owned(static_cast<std::size_t>(f.owner_local))) ++local.diagnostics.num_boundary_faces;
    for (const auto& peer : local.halo) { local.diagnostics.neighbor_ranks.push_back(peer.rank); local.diagnostics.send_cells.push_back(peer.send_owned.size()); local.diagnostics.recv_cells.push_back(peer.recv_ghost.size()); }
    return local;
}

} // namespace

PartitionResult partition_metis(const Mesh& mesh, int partitions) {
    validate_mesh(mesh);
    if (partitions < 1) throw std::invalid_argument("METIS partition count must be positive");
    PartitionResult result; result.cell_owner.assign(mesh.cells.size(), 0);
    if (partitions == 1) return result;
    if (static_cast<std::size_t>(partitions) > mesh.cells.size()) throw std::invalid_argument("more partitions than cells");
    idx_t nvtxs = static_cast<idx_t>(mesh.cells.size()), ncon = 1, nparts = static_cast<idx_t>(partitions), edgecut = 0;
    std::vector<idx_t> xadj(mesh.cells.size() + 1), adjncy;
    for (std::size_t i = 0; i < mesh.adjacency.size(); ++i) {
        xadj[i] = static_cast<idx_t>(adjncy.size());
        for (const auto n : mesh.adjacency[i]) adjncy.push_back(static_cast<idx_t>(n));
    }
    xadj.back() = static_cast<idx_t>(adjncy.size());
    std::vector<idx_t> parts(mesh.cells.size()), options(METIS_NOPTIONS);
    METIS_SetDefaultOptions(options.data()); options[METIS_OPTION_NUMBERING] = 0; options[METIS_OPTION_SEED] = 1729;
    const int status = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(), nullptr, nullptr, nullptr,
                                           &nparts, nullptr, nullptr, options.data(), &edgecut, parts.data());
    if (status != METIS_OK) throw std::runtime_error("METIS_PartGraphKway failed");
    for (std::size_t i = 0; i < parts.size(); ++i) result.cell_owner[i] = static_cast<int>(parts[i]);
    result.edge_cut = static_cast<int>(edgecut); return result;
}

DistributedMesh distribute_mesh(Mesh& root_mesh, const PartitionResult& partition, MPI_Comm comm) {
    int rank = 0, size = 1; MPI_Comm_rank(comm, &rank); MPI_Comm_size(comm, &size);
    if (rank == 0) {
        if (partition.cell_owner.size() != root_mesh.cells.size()) throw std::invalid_argument("partition does not match root mesh");
        std::vector<std::vector<std::byte>> payloads(static_cast<std::size_t>(size));
        DistributedMesh own;
        for (int destination = 0; destination < size; ++destination) {
            auto local = make_local(root_mesh, partition, destination, size);
            if (destination == 0) own = std::move(local); else payloads[static_cast<std::size_t>(destination)] = serialize(local);
        }
        for (int destination = 1; destination < size; ++destination) {
            const auto n = static_cast<std::uint64_t>(payloads[static_cast<std::size_t>(destination)].size());
            MPI_Send(&n, 1, MPI_UINT64_T, destination, kDistributionHeaderTag, comm);
            MPI_Send(payloads[static_cast<std::size_t>(destination)].data(), static_cast<int>(n), MPI_BYTE, destination, kDistributionPayloadTag, comm);
        }
        root_mesh.clear(); // Global preprocessing data must not survive solver distribution.
        return own;
    }
    std::uint64_t n = 0; MPI_Recv(&n, 1, MPI_UINT64_T, 0, kDistributionHeaderTag, comm, MPI_STATUS_IGNORE);
    if (n > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) throw std::runtime_error("distributed mesh payload is too large");
    std::vector<std::byte> payload(static_cast<std::size_t>(n));
    MPI_Recv(payload.data(), static_cast<int>(n), MPI_BYTE, 0, kDistributionPayloadTag, comm, MPI_STATUS_IGNORE);
    return deserialize(payload, rank, size);
}

} // namespace cfd
