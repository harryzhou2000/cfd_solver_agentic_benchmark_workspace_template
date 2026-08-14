#include "mesh/cgns_reader.hpp"
#include <cgnslib.h>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <set>
#include <map>

namespace omo {

// Face key for face matching
struct FaceKey {
    int n0, n1;
    FaceKey(int a, int b) : n0(std::min(a,b)), n1(std::max(a,b)) {}
    bool operator<(const FaceKey& o) const {
        if (n0 != o.n0) return n0 < o.n0;
        return n1 < o.n1;
    }
    bool operator==(const FaceKey& o) const { return n0 == o.n0 && n1 == o.n1; }
};

MeshData CGNSReader::read(const std::string& filename) {
    MeshData mesh;
    int file_id;

    if (cg_open(filename.c_str(), CG_MODE_READ, &file_id) != CG_OK) {
        throw std::runtime_error("Failed to open CGNS file: " + filename);
    }

    int nbases;
    cg_nbases(file_id, &nbases);
    if (nbases < 1) { cg_close(file_id); throw std::runtime_error("No bases in CGNS file"); }

    int base_id = 1;
    int cell_dim, phys_dim;
    char base_name[128];
    cg_base_read(file_id, base_id, base_name, &cell_dim, &phys_dim);

    int nzones;
    cg_nzones(file_id, base_id, &nzones);
    if (nzones < 1) { cg_close(file_id); throw std::runtime_error("No zones"); }

    int zone_id = 1;
    cgsize_t sizes[9];
    char zone_name[128];
    cg_zone_read(file_id, base_id, zone_id, zone_name, sizes);

    cgsize_t n_nodes = sizes[0];
    cgsize_t n_cells = sizes[1];
    mesh.n_nodes_global = static_cast<int>(n_nodes);
    mesh.n_cells_global = static_cast<int>(n_cells);
    mesh.nodes.resize(n_nodes);

    read_nodes(file_id, base_id, zone_id, mesh);
    read_elements(file_id, base_id, zone_id, mesh);

    mesh.n_boundary_faces_total = 0;
    for (auto& [tag, faces] : mesh.boundary_faces) {
        mesh.n_boundary_faces_total += faces.size();
    }

    std::cout << "[CGNS] Loaded mesh: " << mesh.n_nodes_global << " nodes, "
              << mesh.n_cells_global << " cells, "
              << mesh.n_faces_global << " faces, "
              << mesh.n_boundary_faces_total << " boundary faces, "
              << mesh.boundary_faces.size() << " BC families" << std::endl;

    cg_close(file_id);
    return mesh;
}

void CGNSReader::read_nodes(int file_id, int base_id, int zone_id, MeshData& mesh) {
    int n_nodes = mesh.n_nodes_global;
    double* coords_x = new double[n_nodes];
    double* coords_y = new double[n_nodes];

    cgsize_t rmin = 1, rmax = static_cast<cgsize_t>(n_nodes);

    if (cg_coord_read(file_id, base_id, zone_id, "CoordinateX", RealDouble,
                      &rmin, &rmax, coords_x) != CG_OK) {
        // Try "CoordinateX     " (padded)
        if (cg_coord_read(file_id, base_id, zone_id, "CoordinateX    ", RealDouble,
                          &rmin, &rmax, coords_x) != CG_OK) {
            delete[] coords_x; delete[] coords_y;
            throw std::runtime_error("Failed to read CoordinateX");
        }
    }

    if (cg_coord_read(file_id, base_id, zone_id, "CoordinateY", RealDouble,
                      &rmin, &rmax, coords_y) != CG_OK) {
        if (cg_coord_read(file_id, base_id, zone_id, "CoordinateY    ", RealDouble,
                          &rmin, &rmax, coords_y) != CG_OK) {
            delete[] coords_x; delete[] coords_y;
            throw std::runtime_error("Failed to read CoordinateY");
        }
    }

    for (int i = 0; i < n_nodes; i++) {
        mesh.nodes[i].x(0) = coords_x[i];
        mesh.nodes[i].x(1) = coords_y[i];
    }

    delete[] coords_x;
    delete[] coords_y;
}

void CGNSReader::read_elements(int file_id, int base_id, int zone_id, MeshData& mesh) {
    int n_cells = mesh.n_cells_global;
    int nsections;
    cg_nsections(file_id, base_id, zone_id, &nsections);

    mesh.cells.resize(n_cells);
    mesh.cell_faces.resize(n_cells);
    mesh.cell_neighbors.resize(n_cells);

    // ---- Pass 1: Build cell->node connectivity from TRI_3 and QUAD_4 sections ----
    std::vector<std::vector<int>> cell_nodes(n_cells);

    // Also store BAR_2 sections for later BC matching
    struct Bar2Section {
        std::string name;
        std::vector<std::pair<int,int>> edges; // (node0, node1) pairs
    };
    std::vector<Bar2Section> bar2_sections;

    for (int sec = 1; sec <= nsections; sec++) {
        char sec_name[128];
        ElementType_t etype;
        cgsize_t start, end;
        int nbndry, parent_flag;
        cg_section_read(file_id, base_id, zone_id, sec,
                        sec_name, &etype, &start, &end, &nbndry, &parent_flag);
        if (parent_flag != 0) continue;

        if (etype == BAR_2) {
            // Store BAR_2 edges for BC assignment
            Bar2Section b2s;
            b2s.name = sec_name;
            cgsize_t nelem = end - start + 1;
            cgsize_t* conn = new cgsize_t[nelem * 2];
            cg_elements_partial_read(file_id, base_id, zone_id, sec,
                                     start, end, conn, nullptr);
            for (cgsize_t e = 0; e < nelem; e++) {
                int n0 = static_cast<int>(conn[e*2] - 1);
                int n1 = static_cast<int>(conn[e*2+1] - 1);
                b2s.edges.push_back({n0, n1});
            }
            delete[] conn;
            bar2_sections.push_back(std::move(b2s));
            continue;
        }

        // Determine nodes per element
        int npe = 0;
        cgsize_t* conn = nullptr;
        cgsize_t nelem = end - start + 1;

        if (etype == TRI_3) npe = 3;
        else if (etype == QUAD_4) npe = 4;
        else if (etype == MIXED) {
            // Read mixed elements
            cgsize_t data_size = 0;
            cg_ElementDataSize(file_id, base_id, zone_id, sec, &data_size);
            cgsize_t* edata = new cgsize_t[data_size];
            cg_elements_read(file_id, base_id, zone_id, sec, edata, nullptr);
            cgsize_t pos = 0;
            for (cgsize_t e = 0; e < nelem && pos < data_size; e++) {
                int mtype = static_cast<int>(edata[pos]);
                int mnpe = 0;
                if (mtype == TRI_3) mnpe = 3;
                else if (mtype == QUAD_4) mnpe = 4;
                else { pos += 1; continue; } // skip unknown types
                int cidx = static_cast<int>(start - 1 + e);
                for (int k = 0; k < mnpe; k++) {
                    cell_nodes[cidx].push_back(static_cast<int>(edata[pos+1+k] - 1));
                }
                pos += 1 + mnpe;
            }
            delete[] edata;
            continue;
        } else {
            continue; // skip unknown element types
        }

        conn = new cgsize_t[nelem * npe];
        cg_elements_partial_read(file_id, base_id, zone_id, sec,
                                 start, end, conn, nullptr);
        for (cgsize_t e = 0; e < nelem; e++) {
            int cidx = static_cast<int>(start - 1 + e);
            for (int k = 0; k < npe; k++) {
                cell_nodes[cidx].push_back(static_cast<int>(conn[e*npe + k] - 1));
            }
        }
        delete[] conn;
    }

    std::cout << "[CGNS] Read " << bar2_sections.size() << " BAR_2 boundary sections" << std::endl;

    // ---- Pass 2: Build faces from cell-node connectivity ----
    struct RawFace {
        FaceKey key;
        int cell_idx;
    };
    std::vector<RawFace> raw_faces;
    for (int c = 0; c < n_cells; c++) {
        auto& nodes = cell_nodes[c];
        int nn = static_cast<int>(nodes.size());
        for (int k = 0; k < nn; k++) {
            int n0 = nodes[k], n1 = nodes[(k+1)%nn];
            raw_faces.push_back({FaceKey(n0, n1), c});
        }
    }

    std::sort(raw_faces.begin(), raw_faces.end(),
              [](const RawFace& a, const RawFace& b) { return a.key < b.key; });

    // Build faces and face_map
    mesh.faces.clear();
    mesh.face_bc_names.clear();
    std::map<FaceKey, int> face_map;
    std::vector<std::vector<int>> cell_faces_vec(n_cells);

    size_t i = 0;
    while (i < raw_faces.size()) {
        FaceKey fk = raw_faces[i].key;
        size_t j = i;
        while (j < raw_faces.size() && raw_faces[j].key == fk) j++;

        int count = static_cast<int>(j - i);
        int fidx = static_cast<int>(mesh.faces.size());

        FaceData fd;
        fd.left_cell = raw_faces[i].cell_idx;
        fd.right_cell = (count == 2) ? raw_faces[i+1].cell_idx : -1;

        fd.centroid = 0.5 * (mesh.nodes[fk.n0].x + mesh.nodes[fk.n1].x);
        Vector2 edge = mesh.nodes[fk.n1].x - mesh.nodes[fk.n0].x;
        fd.normal(0) = edge(1);
        fd.normal(1) = -edge(0);
        fd.area = edge.norm();
        Real len = fd.normal.norm();
        if (len > 0) fd.normal /= len;

        fd.bc_tag = -1;
        mesh.faces.push_back(fd);
        mesh.face_bc_names.push_back("");
        face_map[fk] = fidx;

        for (size_t k = i; k < j; k++) {
            cell_faces_vec[raw_faces[k].cell_idx].push_back(fidx);
        }
        i = j;
    }
    mesh.n_faces_global = static_cast<int>(mesh.faces.size());

    // ---- Pass 3: Match BAR_2 edges to faces for BC assignment ----
    for (auto& b2s : bar2_sections) {
        for (auto& [n0, n1] : b2s.edges) {
            FaceKey fk(n0, n1);
            auto it = face_map.find(fk);
            if (it != face_map.end()) {
                int fidx = it->second;
                mesh.face_bc_names[fidx] = b2s.name;
            }
        }
    }

    // ---- Pass 4: Build boundary_faces map and cell_neighbors ----
    for (int f = 0; f < mesh.n_faces_global; f++) {
        if (mesh.faces[f].right_cell < 0) {
            std::string tag = mesh.face_bc_names[f];
            if (tag.empty()) tag = "unknown";
            mesh.boundary_faces[tag].push_back(f);
        } else {
            int left = mesh.faces[f].left_cell;
            int right = mesh.faces[f].right_cell;
            mesh.cell_neighbors[left].push_back(right);
            mesh.cell_neighbors[right].push_back(left);
        }
    }

    mesh.cell_faces = std::move(cell_faces_vec);
    std::cout << "[CGNS] Built " << mesh.n_faces_global << " faces, "
              << mesh.boundary_faces.size() << " BC groups" << std::endl;

    // Print BC groups
    for (auto& [name, faces] : mesh.boundary_faces) {
        std::cout << "  BC '" << name << "': " << faces.size() << " faces" << std::endl;
    }
}

int CGNSReader::get_cgns_element_type(int npe) {
    switch (npe) { case 3: return TRI_3; case 4: return QUAD_4; default: return -1; }
}

} // namespace omo
