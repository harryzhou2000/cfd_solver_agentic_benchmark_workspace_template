#include "mesh.hpp"
#include <cgnslib.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

static void cg_check(int err, const char* msg) {
    if (err != CG_OK) throw std::runtime_error(std::string(msg) + ": " + cg_get_error());
}

struct RawZone {
    int nnodes;
    int ncells;
    std::vector<double> x, y;
    struct Section {
        std::string name;
        int etype;
        cgsize_t start, end;
        std::vector<cgsize_t> conn;
    };
    std::vector<Section> sections;
    std::vector<std::pair<std::string, std::string>> bc_family;
};

Mesh read_cgns_mesh(const std::string& filename,
                    const std::map<std::string, BCType>& bc_map) {
    int fn;
    cg_check(cg_open(filename.c_str(), CG_MODE_READ, &fn), "cg_open");

    int nbases;
    cg_check(cg_nbases(fn, &nbases), "cg_nbases");
    if (nbases < 1) throw std::runtime_error("No bases in CGNS file");

    char basename[256];
    int cdim, pdim;
    cg_check(cg_base_read(fn, 1, basename, &cdim, &pdim), "cg_base_read");

    int nzones;
    cg_check(cg_nzones(fn, 1, &nzones), "cg_nzones");

    std::vector<RawZone> zones(nzones);
    for (int Z = 1; Z <= nzones; Z++) {
        auto& rz = zones[Z - 1];
        char zname[256];
        cgsize_t sizes[9];
        cg_check(cg_zone_read(fn, 1, Z, zname, sizes), "cg_zone_read");
        rz.nnodes = (int)sizes[0];
        rz.ncells = (int)sizes[1];

        rz.x.resize(rz.nnodes);
        rz.y.resize(rz.nnodes);
        cgsize_t rmin = 1, rmax = rz.nnodes;
        cg_check(cg_coord_read(fn, 1, Z, "CoordinateX", CGNS_ENUMV(RealDouble), &rmin, &rmax, rz.x.data()), "coord_x");
        cg_check(cg_coord_read(fn, 1, Z, "CoordinateY", CGNS_ENUMV(RealDouble), &rmin, &rmax, rz.y.data()), "coord_y");

        int nsec;
        cg_check(cg_nsections(fn, 1, Z, &nsec), "cg_nsections");
        rz.sections.resize(nsec);
        for (int S = 1; S <= nsec; S++) {
            auto& sec = rz.sections[S - 1];
            char sname[256];
            CGNS_ENUMT(ElementType_t) et;
            cgsize_t estart, eend;
            int nbnd, pflag;
            cg_check(cg_section_read(fn, 1, Z, S, sname, &et, &estart, &eend, &nbnd, &pflag), "sec_read");
            sec.name = sname;
            sec.etype = (int)et;
            sec.start = estart;
            sec.end = eend;
            cgsize_t esize;
            cg_check(cg_ElementDataSize(fn, 1, Z, S, &esize), "edata_size");
            sec.conn.resize(esize);
            cgsize_t* parent = nullptr;
            cg_check(cg_elements_read(fn, 1, Z, S, sec.conn.data(), parent), "elements_read");
        }

        int nbocos;
        cg_check(cg_nbocos(fn, 1, Z, &nbocos), "cg_nbocos");
        for (int BC = 1; BC <= nbocos; BC++) {
            char bcname[256];
            CGNS_ENUMT(BCType_t) bctype;
            CGNS_ENUMT(PointSetType_t) pstype;
            cgsize_t npts;
            int normalidx[3], ndataset;
            cgsize_t normallistsize;
            CGNS_ENUMT(DataType_t) ndt;
            cg_check(cg_boco_info(fn, 1, Z, BC, bcname, &bctype, &pstype, &npts, normalidx, &normallistsize, &ndt, &ndataset), "boco_info");

            char famname[256] = "";
            if (cg_goto(fn, 1, "Zone_t", Z, "ZoneBC_t", 1, "BC_t", BC, "end") == CG_OK) {
                cg_famname_read(famname);
            }
            std::string fname = strlen(famname) > 0 ? famname : bcname;
            rz.bc_family.emplace_back(bcname, fname);
        }
    }
    cg_close(fn);

    Mesh mesh;

    int global_node_offset = 0;
    int global_cell_id = 0;

    struct NodeKey {
        double x, y;
        bool operator<(const NodeKey& o) const {
            if (std::abs(x - o.x) > 1e-12) return x < o.x;
            return y < o.y;
        }
    };
    std::map<NodeKey, int> node_dedup;

    std::vector<int> zone_node_map;

    for (int zi = 0; zi < nzones; zi++) {
        auto& rz = zones[zi];
        int base_node = (int)mesh.nodes.size();
        std::vector<int> local_to_global(rz.nnodes);

        for (int i = 0; i < rz.nnodes; i++) {
            NodeKey key{rz.x[i], rz.y[i]};
            auto it = node_dedup.find(key);
            if (it != node_dedup.end()) {
                local_to_global[i] = it->second;
            } else {
                int gid = (int)mesh.nodes.size();
                mesh.nodes.push_back(Vec2(rz.x[i], rz.y[i]));
                node_dedup[key] = gid;
                local_to_global[i] = gid;
            }
        }

        for (auto& sec : rz.sections) {
            bool is_bar = (sec.etype == CGNS_ENUMV(BAR_2));
            bool is_tri = (sec.etype == CGNS_ENUMV(TRI_3));
            bool is_quad = (sec.etype == CGNS_ENUMV(QUAD_4));

            if (is_bar) continue;
            if (!is_tri && !is_quad) {
                bool is_conn = sec.name.substr(0, 3) == "con";
                if (is_conn || is_bar) continue;
                throw std::runtime_error("Unsupported element type " + std::to_string(sec.etype) + " in section " + sec.name);
            }

            int npn = is_tri ? 3 : 4;
            int nelem = (int)(sec.end - sec.start + 1);
            for (int e = 0; e < nelem; e++) {
                Cell cell;
                cell.global_id = global_cell_id++;
                cell.nodes.resize(npn);
                for (int k = 0; k < npn; k++) {
                    int local_node = (int)sec.conn[e * npn + k] - 1;
                    cell.nodes[k] = local_to_global[local_node];
                }
                mesh.cells.push_back(std::move(cell));
            }
        }

        for (auto& sec : rz.sections) {
            bool is_bar = (sec.etype == CGNS_ENUMV(BAR_2));
            if (!is_bar) continue;

            std::string family_name = sec.name;
            for (auto& [bcname, famname] : rz.bc_family) {
                if (bcname == sec.name || famname == sec.name) {
                    family_name = famname;
                    break;
                }
            }

            auto it = bc_map.find(family_name);
            if (it == bc_map.end()) {
                bool is_conn = family_name.substr(0, 3) == "con" || family_name.find("Unspecified") != std::string::npos;
                if (is_conn) continue;
                continue;
            }

            int bg_idx = -1;
            for (int i = 0; i < (int)mesh.boundary_groups.size(); i++) {
                if (mesh.boundary_groups[i].family_name == family_name) { bg_idx = i; break; }
            }
            if (bg_idx < 0) {
                bg_idx = (int)mesh.boundary_groups.size();
                BoundaryGroup bg;
                bg.family_name = family_name;
                bg.type = it->second;
                mesh.boundary_groups.push_back(bg);
            }

            int nelem = (int)(sec.end - sec.start + 1);
            for (int e = 0; e < nelem; e++) {
                Face face;
                face.is_boundary = true;
                face.bc_id = bg_idx;
                int n0 = local_to_global[(int)sec.conn[e * 2 + 0] - 1];
                int n1 = local_to_global[(int)sec.conn[e * 2 + 1] - 1];
                face.nodes = {n0, n1};
                int fid = (int)mesh.faces.size();
                mesh.boundary_groups[bg_idx].face_ids.push_back(fid);
                mesh.faces.push_back(std::move(face));
            }
        }
    }

    mesh.num_cells_global = (int)mesh.cells.size();
    return mesh;
}

struct EdgeKey {
    int n0, n1;
    EdgeKey(int a, int b) : n0(std::min(a, b)), n1(std::max(a, b)) {}
    bool operator==(const EdgeKey& o) const { return n0 == o.n0 && n1 == o.n1; }
};

struct EdgeHash {
    size_t operator()(const EdgeKey& e) const {
        return std::hash<long long>()(((long long)e.n0 << 32) | e.n1);
    }
};

void build_cell_face_adjacency(Mesh& mesh) {
    std::unordered_map<EdgeKey, int, EdgeHash> bc_face_map;
    for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
        auto& f = mesh.faces[fi];
        EdgeKey ek(f.nodes[0], f.nodes[1]);
        bc_face_map[ek] = fi;
    }

    std::unordered_map<EdgeKey, int, EdgeHash> edge_to_face;

    for (int ci = 0; ci < (int)mesh.cells.size(); ci++) {
        auto& cell = mesh.cells[ci];
        int nn = (int)cell.nodes.size();
        for (int k = 0; k < nn; k++) {
            int n0 = cell.nodes[k];
            int n1 = cell.nodes[(k + 1) % nn];
            EdgeKey ek(n0, n1);

            auto bc_it = bc_face_map.find(ek);
            if (bc_it != bc_face_map.end()) {
                int fi = bc_it->second;
                auto& f = mesh.faces[fi];
                if (f.left_cell < 0) {
                    f.left_cell = ci;
                    cell.faces.push_back(fi);
                }
                continue;
            }

            auto it = edge_to_face.find(ek);
            if (it == edge_to_face.end()) {
                int fi = (int)mesh.faces.size();
                Face face;
                face.nodes = {n0, n1};
                face.left_cell = ci;
                face.is_boundary = false;
                face.bc_id = -1;
                mesh.faces.push_back(face);
                edge_to_face[ek] = fi;
                cell.faces.push_back(fi);
            } else {
                int fi = it->second;
                auto& f = mesh.faces[fi];
                f.right_cell = ci;
                cell.faces.push_back(fi);
            }
        }
    }

    mesh.num_faces_global = (int)mesh.faces.size();
}

void compute_geometry(Mesh& mesh) {
    for (auto& cell : mesh.cells) {
        int nn = (int)cell.nodes.size();
        Vec2 centroid(0, 0);
        for (int n : cell.nodes) centroid += mesh.nodes[n];
        centroid /= nn;
        cell.centroid = centroid;

        double vol = 0.0;
        for (int k = 0; k < nn; k++) {
            Vec2 a = mesh.nodes[cell.nodes[k]];
            Vec2 b = mesh.nodes[cell.nodes[(k + 1) % nn]];
            vol += (a.x() * b.y() - b.x() * a.y());
        }
        cell.volume = std::abs(vol) * 0.5;
    }

    for (auto& face : mesh.faces) {
        Vec2 p0 = mesh.nodes[face.nodes[0]];
        Vec2 p1 = mesh.nodes[face.nodes[1]];
        face.midpoint = 0.5 * (p0 + p1);
        Vec2 edge = p1 - p0;
        face.area = edge.norm();
        face.normal = Vec2(edge.y(), -edge.x());
        if (face.area > 1e-30) face.normal /= face.area;

        if (face.left_cell >= 0) {
            Vec2 c = mesh.cells[face.left_cell].centroid;
            if ((face.midpoint - c).dot(face.normal) < 0) {
                face.normal = -face.normal;
            }
        }
    }
}
