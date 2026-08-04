#include "cfd/partition.hpp"

#include <metis.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <type_traits>

namespace cfd {
namespace {

class ByteWriter {
public:
    template <class T> void pod(const T& value) { static_assert(std::is_trivially_copyable_v<T>); const auto* p = reinterpret_cast<const char*>(&value); bytes_.insert(bytes_.end(), p, p + sizeof(T)); }
    void string(const std::string& value) { const std::uint64_t n = value.size(); pod(n); bytes_.insert(bytes_.end(), value.begin(), value.end()); }
    template <class T> void vector_pod(const std::vector<T>& values) { const std::uint64_t n = values.size(); pod(n); for (const auto& v : values) pod(v); }
    std::vector<char> take() { return std::move(bytes_); }
private: std::vector<char> bytes_;
};
class ByteReader {
public:
    explicit ByteReader(const std::vector<char>& bytes) : p_(bytes.data()), end_(bytes.data() + bytes.size()) {}
    template <class T> T pod() { static_assert(std::is_trivially_copyable_v<T>); if (end_ - p_ < static_cast<std::ptrdiff_t>(sizeof(T))) throw std::runtime_error("truncated distributed mesh payload"); T v; std::memcpy(&v, p_, sizeof(T)); p_ += sizeof(T); return v; }
    std::string string() { const auto n = pod<std::uint64_t>(); if (n > static_cast<std::uint64_t>(end_ - p_)) throw std::runtime_error("invalid distributed string"); std::string v(p_, p_ + n); p_ += n; return v; }
    template <class T> std::vector<T> vector_pod() { const auto n = pod<std::uint64_t>(); if (n > (1ULL << 31)) throw std::runtime_error("invalid distributed vector"); std::vector<T> v(static_cast<size_t>(n)); for (auto& x : v) x = pod<T>(); return v; }
private: const char* p_; const char* end_;
};
void write_vec(ByteWriter& w, Vec2 v) { w.pod(v.x); w.pod(v.y); }
Vec2 read_vec(ByteReader& r) { return {r.pod<double>(), r.pod<double>()}; }
void write_cell(ByteWriter& w, const Cell& c) { w.vector_pod(c.nodes); w.vector_pod(c.faces); w.vector_pod(c.neighbors); write_vec(w,c.centroid); w.pod(c.area); w.pod(c.type); }
Cell read_cell(ByteReader& r) { Cell c; c.nodes=r.vector_pod<int>(); c.faces=r.vector_pod<int>(); c.neighbors=r.vector_pod<int>(); c.centroid=read_vec(r); c.area=r.pod<double>(); c.type=r.pod<CellType>(); return c; }
void write_face(ByteWriter& w, const LocalFace& f) { w.pod(f.global_id); w.pod(f.nodes[0]); w.pod(f.nodes[1]); w.pod(f.left_cell); w.pod(f.right_cell); write_vec(w,f.centroid); write_vec(w,f.normal); write_vec(w,f.unit_normal); w.pod(f.length); w.pod(f.boundary_type); w.string(f.boundary_name); }
LocalFace read_face(ByteReader& r) { LocalFace f; f.global_id=r.pod<int>(); f.nodes={{r.pod<int>(),r.pod<int>()}}; f.left_cell=r.pod<int>(); f.right_cell=r.pod<int>(); f.centroid=read_vec(r); f.normal=read_vec(r); f.unit_normal=read_vec(r); f.length=r.pod<double>(); f.boundary_type=r.pod<BoundaryType>(); f.boundary_name=r.string(); return f; }
std::vector<char> pack(const LocalMesh& m) {
    ByteWriter w; w.pod(m.rank); w.pod(m.size); w.pod(m.global_cell_count); w.pod(m.global_face_count); w.pod(m.partition_edge_cut); w.pod(m.owned_cell_count); w.vector_pod(m.local_to_global_cell); w.vector_pod(m.local_to_global_node);
    const std::uint64_t nn=m.nodes.size(); w.pod(nn); for(auto v:m.nodes) write_vec(w,v);
    const std::uint64_t nc=m.cells.size(); w.pod(nc); for(const auto& c:m.cells) write_cell(w,c);
    const std::uint64_t nf=m.faces.size(); w.pod(nf); for(const auto& f:m.faces) write_face(w,f);
    const std::uint64_t np=m.neighbors.size(); w.pod(np); for(const auto& n:m.neighbors) { w.pod(n.rank); w.vector_pod(n.send_owned_local_indices); w.vector_pod(n.recv_ghost_local_indices); } return w.take();
}
LocalMesh unpack(const std::vector<char>& b) {
    ByteReader r(b); LocalMesh m; m.rank=r.pod<int>(); m.size=r.pod<int>(); m.global_cell_count=r.pod<int>(); m.global_face_count=r.pod<int>(); m.partition_edge_cut=r.pod<int>(); m.owned_cell_count=r.pod<int>(); m.local_to_global_cell=r.vector_pod<int>(); m.local_to_global_node=r.vector_pod<int>();
    for(auto n=r.pod<std::uint64_t>(); n--;) m.nodes.push_back(read_vec(r));
    for(auto n=r.pod<std::uint64_t>(); n--;) m.cells.push_back(read_cell(r));
    for(auto n=r.pod<std::uint64_t>(); n--;) m.faces.push_back(read_face(r));
    for(auto n=r.pod<std::uint64_t>(); n--;) { NeighborPlan p; p.rank=r.pod<int>(); p.send_owned_local_indices=r.vector_pod<int>(); p.recv_ghost_local_indices=r.vector_pod<int>(); m.neighbors.push_back(std::move(p)); }
    for(int i=0;i<static_cast<int>(m.local_to_global_cell.size());++i) m.global_to_local_cell.emplace(m.local_to_global_cell[i],i);
    return m;
}

LocalMesh make_local(const Mesh& mesh, const PartitionResult& p, int rank, int size) {
    LocalMesh out; out.rank=rank; out.size=size; out.global_cell_count=static_cast<int>(mesh.cells.size()); out.global_face_count=static_cast<int>(mesh.faces.size()); out.partition_edge_cut=p.edge_cut;
    for(int c=0;c<(int)mesh.cells.size();++c) if(p.cell_owner[c]==rank) out.local_to_global_cell.push_back(c);
    out.owned_cell_count=static_cast<int>(out.local_to_global_cell.size());
    for(const Face& f:mesh.faces) if(f.right_cell>=0 && ((p.cell_owner[f.left_cell]==rank)!=(p.cell_owner[f.right_cell]==rank))) { const int g=p.cell_owner[f.left_cell]==rank?f.right_cell:f.left_cell; if(out.global_to_local_cell.find(g)==out.global_to_local_cell.end() && std::find(out.local_to_global_cell.begin(),out.local_to_global_cell.end(),g)==out.local_to_global_cell.end()) out.local_to_global_cell.push_back(g); }
    for(int i=0;i<(int)out.local_to_global_cell.size();++i) out.global_to_local_cell.emplace(out.local_to_global_cell[i],i);
    std::map<int,int> node_map;
    for(int gc:out.local_to_global_cell) for(int gn:mesh.cells[gc].nodes) if(node_map.emplace(gn,node_map.size()).second) { out.local_to_global_node.push_back(gn); out.nodes.push_back(mesh.nodes[gn]); }
    out.cells.reserve(out.local_to_global_cell.size());
    for(int gc:out.local_to_global_cell) { Cell c=mesh.cells[gc]; for(int& n:c.nodes) n=node_map.at(n); for(int& nb:c.neighbors) { const auto it=out.global_to_local_cell.find(nb); nb=it==out.global_to_local_cell.end()?-1:it->second; } out.cells.push_back(std::move(c)); }
    std::map<int,int> local_face;
    for(int gc=0;gc<(int)mesh.cells.size();++gc) if(p.cell_owner[gc]==rank) for(int gf:mesh.cells[gc].faces) if(local_face.emplace(gf,local_face.size()).second) {
        const Face& f=mesh.faces[gf]; const bool flip=f.left_cell!=gc; LocalFace lf; lf.global_id=gf; lf.nodes={{node_map.at(f.nodes[flip]),node_map.at(f.nodes[!flip])}}; lf.left_cell=out.global_to_local_cell.at(gc); const int other=flip?f.left_cell:f.right_cell; lf.right_cell=other<0?-1:out.global_to_local_cell.at(other); lf.centroid=f.centroid; lf.length=f.length; lf.normal=flip?Vec2{-f.normal.x,-f.normal.y}:f.normal; lf.unit_normal=flip?Vec2{-f.unit_normal.x,-f.unit_normal.y}:f.unit_normal; lf.boundary_type=f.boundary_type; lf.boundary_name=f.boundary_name; out.faces.push_back(std::move(lf));
    }
    for(Cell& c:out.cells) for(int& gf:c.faces) { const auto it=local_face.find(gf); gf=it==local_face.end()?-1:it->second; }
    return out;
}
} // namespace

PartitionResult partition_cells_metis(const Mesh& mesh, int parts) {
    if(parts<1) throw std::invalid_argument("METIS partition count must be positive");
    PartitionResult out; out.cell_owner.assign(mesh.cells.size(),0); if(parts==1 || mesh.cells.empty()) return out;
    const auto adj=cell_adjacency(mesh); std::vector<idx_t> xadj(mesh.cells.size()+1,0), adjncy;
    for(size_t i=0;i<adj.size();++i) { xadj[i+1]=xadj[i]+static_cast<idx_t>(adj[i].size()); for(int n:adj[i]) adjncy.push_back(n); }
    idx_t nvtxs=static_cast<idx_t>(mesh.cells.size()), ncon=1, nparts=parts, edgecut=0; std::vector<idx_t> part(mesh.cells.size()); int options[METIS_NOPTIONS]; METIS_SetDefaultOptions(options); options[METIS_OPTION_NUMBERING]=0;
    const int rc=METIS_PartGraphKway(&nvtxs,&ncon,xadj.data(),adjncy.data(),nullptr,nullptr,nullptr,&nparts,nullptr,nullptr,options,&edgecut,part.data()); if(rc!=METIS_OK) throw std::runtime_error("METIS_PartGraphKway failed"); for(size_t i=0;i<part.size();++i) out.cell_owner[i]=part[i]; out.edge_cut=edgecut; return out;
}

HaloExchange::HaloExchange(const LocalMesh& mesh, MPI_Comm comm) : comm_(comm), local_cell_count_(mesh.cells.size()), neighbors_(mesh.neighbors) {}
void HaloExchange::begin(const std::vector<double>& values, int components) {
    if(active_) throw std::logic_error("halo exchange already active");
    if(components<=0 || values.size()!=local_cell_count_*static_cast<size_t>(components)) throw std::invalid_argument("invalid halo state layout");
    requests_.clear(); send_buffers_.clear(); recv_buffers_.clear(); requests_.reserve(neighbors_.size()*2); send_buffers_.resize(neighbors_.size()); recv_buffers_.resize(neighbors_.size());
    for(size_t i=0;i<neighbors_.size();++i) { const auto& n=neighbors_[i]; recv_buffers_[i].resize(n.recv_ghost_local_indices.size()*static_cast<size_t>(components)); MPI_Request q; MPI_Irecv(recv_buffers_[i].data(),static_cast<int>(recv_buffers_[i].size()),MPI_DOUBLE,n.rank,913,comm_,&q); requests_.push_back(q); }
    for(size_t i=0;i<neighbors_.size();++i) { const auto& n=neighbors_[i]; auto& b=send_buffers_[i]; b.resize(n.send_owned_local_indices.size()*static_cast<size_t>(components)); for(size_t j=0;j<n.send_owned_local_indices.size();++j) std::copy_n(values.data()+static_cast<size_t>(n.send_owned_local_indices[j])*components,components,b.data()+j*components); MPI_Request q; MPI_Isend(b.data(),static_cast<int>(b.size()),MPI_DOUBLE,n.rank,913,comm_,&q); requests_.push_back(q); } active_=true;
}
void HaloExchange::finish(std::vector<double>& values, int components) { if(!active_) throw std::logic_error("no active halo exchange"); MPI_Waitall(static_cast<int>(requests_.size()),requests_.data(),MPI_STATUSES_IGNORE); for(size_t i=0;i<neighbors_.size();++i) for(size_t j=0;j<neighbors_[i].recv_ghost_local_indices.size();++j) std::copy_n(recv_buffers_[i].data()+j*components,components,values.data()+static_cast<size_t>(neighbors_[i].recv_ghost_local_indices[j])*components); active_=false; }
void HaloExchange::exchange(std::vector<double>& values,int components) { begin(values,components); finish(values,components); }

LocalMesh read_partition_distribute(const std::string& path,const BoundaryMap& map,MPI_Comm comm,double tol) {
    int rank=0,size=1; MPI_Comm_rank(comm,&rank); MPI_Comm_size(comm,&size); std::vector<LocalMesh> all;
    if(rank==0) { Mesh global=read_cgns_mesh(path,map,tol); const auto part=partition_cells_metis(global,size); all.reserve(size); for(int r=0;r<size;++r) all.push_back(make_local(global,part,r,size)); for(int r=0;r<size;++r) for(size_t gi=static_cast<size_t>(all[r].owned_cell_count);gi<all[r].local_to_global_cell.size();++gi) { const int owner=part.cell_owner[all[r].local_to_global_cell[gi]]; auto& recv=all[r].neighbors; auto it=std::find_if(recv.begin(),recv.end(),[&](const NeighborPlan& n){return n.rank==owner;}); if(it==recv.end()) { recv.push_back({owner,{},{}}); it=std::prev(recv.end()); } it->recv_ghost_local_indices.push_back(static_cast<int>(gi)); auto& send=all[owner].neighbors; auto jt=std::find_if(send.begin(),send.end(),[&](const NeighborPlan& n){return n.rank==r;}); if(jt==send.end()) { send.push_back({r,{},{}}); jt=std::prev(send.end()); } jt->send_owned_local_indices.push_back(all[owner].global_to_local_cell.at(all[r].local_to_global_cell[gi])); } }
    if(rank==0) for(int r=1;r<size;++r) { auto bytes=pack(all[r]); const int n=static_cast<int>(bytes.size()); MPI_Send(&n,1,MPI_INT,r,911,comm); MPI_Send(bytes.data(),n,MPI_BYTE,r,912,comm); }
    if(rank==0) return all[0];
    int n=0; MPI_Recv(&n,1,MPI_INT,0,911,comm,MPI_STATUS_IGNORE); std::vector<char> bytes(static_cast<size_t>(n)); MPI_Recv(bytes.data(),n,MPI_BYTE,0,912,comm,MPI_STATUS_IGNORE); return unpack(bytes);
}
} // namespace cfd
