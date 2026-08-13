#include "mesh.hpp"
#include <cgnslib.h>
#include <hdf5.h>
#include <cassert>
#include <cstring>
#include <map>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <iostream>

namespace cfd {

static void cgns_check(int ierr) {
    if (ierr != CG_OK) {
        cg_error_print();
        throw std::runtime_error("CGNS error");
    }
}

Mesh read_cgns_mesh(const std::string& filename) {
    Mesh mesh;
    int fn, B, Z;
    char basename[128];
    int cell_dim, phys_dim;
    
    cgns_check(cg_open(filename.c_str(), CG_MODE_READ, &fn));
    
    // First get number of bases
    int nbases;
    cgns_check(cg_nbases(fn, &nbases));
    B = 1;
    cgns_check(cg_base_read(fn, B, basename, &cell_dim, &phys_dim));
    
    // Get number of zones
    int nzones;
    cgns_check(cg_nzones(fn, B, &nzones));
    Z = 1;
    
    cgsize_t sizes[9];
    cgns_check(cg_zone_read(fn, B, Z, basename, sizes));
    cgsize_t n_vertices = sizes[0];
    cgsize_t n_cells = sizes[1];
    mesh.n_cells = n_cells;
    
    // Read coordinates
    std::vector<real_t> x(n_vertices), y(n_vertices);
    cgsize_t rmin = 1, rmax = n_vertices;
    cgns_check(cg_coord_read(fn, B, Z, "CoordinateX", CGNS_ENUMV(RealDouble),
                             &rmin, &rmax, x.data()));
    cgns_check(cg_coord_read(fn, B, Z, "CoordinateY", CGNS_ENUMV(RealDouble),
                             &rmin, &rmax, y.data()));
    
    // Read sections
    int nsections;
    cgns_check(cg_nsections(fn, B, Z, &nsections));
    
    struct SectionInfo {
        std::string name;
        std::string family_name;
        CGNS_ENUMT(ElementType_t) etype;
        cgsize_t start, end;
        std::vector<cgsize_t> connectivity;
        int nodes_per_elem;
    };
    std::vector<SectionInfo> all_sections;
    
    for (int S = 1; S <= nsections; S++) {
        char sname[128];
        CGNS_ENUMT(ElementType_t) etype;
        cgsize_t start, end;
        int nbndry, pflag;
        cgns_check(cg_section_read(fn, B, Z, S, sname, &etype, &start, &end,
                                    &nbndry, &pflag));
        
        cgsize_t data_size;
        cgns_check(cg_ElementDataSize(fn, B, Z, S, &data_size));
        std::vector<cgsize_t> conn(data_size);
        cgns_check(cg_elements_read(fn, B, Z, S, conn.data(), nullptr));
        
        SectionInfo si;
        si.name = sname;
        si.etype = etype;
        si.start = start;
        si.end = end;
        si.connectivity = conn;
        
        // Try reading family name from the element section
        cgns_check(cg_goto(fn, B, "Zone_t", Z, "Elements_t", S, nullptr));
        char fam_name[128] = {0};
        if (cg_famname_read(fam_name) == CG_OK) {
            si.family_name = fam_name;
        }
        
        if (etype == CGNS_ENUMV(TRI_3)) si.nodes_per_elem = 3;
        else if (etype == CGNS_ENUMV(QUAD_4)) si.nodes_per_elem = 4;
        else if (etype == CGNS_ENUMV(BAR_2)) si.nodes_per_elem = 2;
        else if (etype == CGNS_ENUMV(TETRA_4)) si.nodes_per_elem = 4;
        else if (etype == CGNS_ENUMV(HEXA_8)) si.nodes_per_elem = 8;
        else if (etype == CGNS_ENUMV(PENTA_6)) si.nodes_per_elem = 6;
        else if (etype == CGNS_ENUMV(MIXED)) si.nodes_per_elem = -1;
        else si.nodes_per_elem = 0;
        
        all_sections.push_back(si);
    }
    
    cg_close(fn);
    
    // Parse elements
    struct ElementData {
        cgsize_t elem_id;
        std::vector<cgsize_t> nodes;
        std::string family_name;
    };
    std::vector<ElementData> volume_elements;
    std::map<std::string, std::vector<ElementData>> boundary_elements;
    
    for (auto& si : all_sections) {
        if (si.nodes_per_elem == 3 || si.nodes_per_elem == 4) {
            cgsize_t pos = 0;
            cgsize_t npe = si.nodes_per_elem;
            for (cgsize_t e = si.start; e <= si.end; e++) {
                ElementData ed;
                ed.elem_id = e;
                for (int k = 0; k < npe; k++) ed.nodes.push_back(si.connectivity[pos++]);
                ed.family_name = si.family_name;
                volume_elements.push_back(ed);
            }
        } else if (si.nodes_per_elem == 2) {
            std::string tag = si.family_name.empty() ? si.name : si.family_name;
            cgsize_t pos = 0;
            for (cgsize_t e = si.start; e <= si.end; e++) {
                ElementData ed;
                ed.elem_id = e;
                ed.nodes.push_back(si.connectivity[pos++]);
                ed.nodes.push_back(si.connectivity[pos++]);
                ed.family_name = si.family_name;
                boundary_elements[tag].push_back(ed);
            }
        } else if (si.nodes_per_elem == -1) {
            // MIXED section - parse manually
            const auto& conn = si.connectivity;
            cgsize_t pos = 0;
            cgsize_t data_size = conn.size();
            cgsize_t elem_idx = si.start;
            while (pos < data_size) {
                CGNS_ENUMT(ElementType_t) mixed_type = (CGNS_ENUMT(ElementType_t))conn[pos];
                pos++;
                int mnpe = 0;
                if (mixed_type == CGNS_ENUMV(TRI_3)) mnpe = 3;
                else if (mixed_type == CGNS_ENUMV(QUAD_4)) mnpe = 4;
                else if (mixed_type == CGNS_ENUMV(TETRA_4)) mnpe = 4;
                else if (mixed_type == CGNS_ENUMV(HEXA_8)) mnpe = 8;
                else if (mixed_type == CGNS_ENUMV(PENTA_6)) mnpe = 6;
                else if (mixed_type == CGNS_ENUMV(BAR_2)) mnpe = 2;
                else {
                    // Unknown - skip
                    break;
                }
                if (mnpe > 0 && pos + mnpe <= data_size) {
                    ElementData ed;
                    ed.elem_id = elem_idx++;
                    for (int k = 0; k < mnpe; k++) ed.nodes.push_back(conn[pos++]);
                    ed.family_name = si.family_name;
                    if (mnpe == 2) {
                        std::string tag = si.family_name.empty() ? si.name : si.family_name;
                        boundary_elements[tag].push_back(ed);
                    } else {
                        volume_elements.push_back(ed);
                    }
                } else {
                    break;
                }
            }
        }
    }
    
    // Build cell data
    mesh.cells.clear();
    mesh.cells.resize(n_cells);
    for (cgsize_t i = 0; i < n_cells; i++) {
        mesh.cells[i].id = i;
    }
    
    for (auto& ed : volume_elements) {
        cgsize_t cid = ed.elem_id - 1;
        if (cid < 0 || cid >= n_cells) continue;
        
        auto& cell = mesh.cells[cid];
        int nn = ed.nodes.size();
        
        std::vector<Vec2> vertices;
        for (int k = 0; k < nn; k++) {
            vertices.push_back(Vec2(x[ed.nodes[k]-1], y[ed.nodes[k]-1]));
        }
        
        Vec2 centroid(0, 0);
        real_t volume = 0;
        
        if (nn == 3) {
            centroid = (vertices[0] + vertices[1] + vertices[2]) / 3.0;
            volume = 0.5 * std::abs((vertices[1][0]-vertices[0][0])*(vertices[2][1]-vertices[0][1]) 
                                   - (vertices[1][1]-vertices[0][1])*(vertices[2][0]-vertices[0][0]));
        } else if (nn == 4) {
            centroid = (vertices[0] + vertices[1] + vertices[2] + vertices[3]) / 4.0;
            real_t a1 = 0.5 * std::abs((vertices[1][0]-vertices[0][0])*(vertices[2][1]-vertices[0][1])
                                      - (vertices[1][1]-vertices[0][1])*(vertices[2][0]-vertices[0][0]));
            real_t a2 = 0.5 * std::abs((vertices[2][0]-vertices[0][0])*(vertices[3][1]-vertices[0][1])
                                      - (vertices[2][1]-vertices[0][1])*(vertices[3][0]-vertices[0][0]));
            volume = a1 + a2;
        }
        
        cell.centroid = centroid;
        cell.volume = volume;
    }
    
    // Build edge-to-cell mapping for all volume elements
    struct EdgeKey {
        cgsize_t n1, n2;
        EdgeKey(cgsize_t a, cgsize_t b) : n1(std::min(a,b)), n2(std::max(a,b)) {}
        bool operator<(const EdgeKey& o) const {
            return n1 < o.n1 || (n1 == o.n1 && n2 < o.n2);
        }
    };
    std::map<EdgeKey, std::vector<cgsize_t>> edge_to_cells;
    
    for (auto& ed : volume_elements) {
        cgsize_t cid = ed.elem_id - 1;
        int nn = ed.nodes.size();
        for (int k = 0; k < nn; k++) {
            EdgeKey ek(ed.nodes[k], ed.nodes[(k+1)%nn]);
            edge_to_cells[ek].push_back(cid);
        }
    }
    
    // Build boundary edge set from boundary elements
    std::map<EdgeKey, std::string> boundary_edge_family;
    for (auto& [tag, elements] : boundary_elements) {
        for (auto& ed : elements) {
            EdgeKey ek(ed.nodes[0], ed.nodes[1]);
            boundary_edge_family[ek] = tag;
        }
    }
    
    // If no boundary elements found via BAR_2, use fallback: identify boundary edges
    // as edges that only appear once in volume elements
    if (boundary_edge_family.empty()) {
        for (auto& [ek, cells] : edge_to_cells) {
            std::set<cgsize_t> uc(cells.begin(), cells.end());
            if (uc.size() == 1) {
                // Boundary edge without a tag - classify by geometric location
                // Use the bounding box to guess farfield vs wall
                Vec2 pt(x[ek.n1-1], y[ek.n1-1]);
                // Farfield guessing: points near domain boundary
                // This is a fallback - the NACA/Cylinder meshes should have BAR_2 boundary elements
                boundary_edge_family[ek] = "unknown";
            }
        }
    }
    
    // Identify interior/boundary edges and build faces
    std::map<EdgeKey, std::vector<cgsize_t>> interior_edges;
    std::map<EdgeKey, std::pair<cgsize_t, std::string>> boundary_edges;
    
    for (auto& [ek, cells] : edge_to_cells) {
        std::set<cgsize_t> uc(cells.begin(), cells.end());
        std::vector<cgsize_t> ucv(uc.begin(), uc.end());
        
        auto bit = boundary_edge_family.find(ek);
        if (uc.size() >= 2) {
            interior_edges[ek] = ucv;
        } else if (uc.size() == 1) {
            std::string tag = (bit != boundary_edge_family.end()) ? bit->second : "unknown";
            boundary_edges[ek] = {ucv[0], tag};
        }
    }
    
    // Build faces
    mesh.faces.clear();
    cgsize_t face_counter = 0;
    
    std::map<std::string, idx_t> tag_to_bc_id;
    idx_t next_tag = 1;
    auto get_tag_id = [&](const std::string& tag) -> idx_t {
        auto it = tag_to_bc_id.find(tag);
        if (it != tag_to_bc_id.end()) return it->second;
        tag_to_bc_id[tag] = next_tag;
        return next_tag++;
    };
    
    // Interior faces
    for (auto& [ek, cells_pair] : interior_edges) {
        cgsize_t c0 = cells_pair[0];
        cgsize_t c1 = cells_pair[1];
        if (c0 >= n_cells || c1 >= n_cells) continue;
        
        Face face;
        face.id = face_counter++;
        face.left_cell = c0;
        face.right_cell = c1;
        
        Vec2 p1(x[ek.n1-1], y[ek.n1-1]);
        Vec2 p2(x[ek.n2-1], y[ek.n2-1]);
        face.centroid = (p1 + p2) * 0.5;
        
        Vec2 edge_vec = p2 - p1;
        Vec2 edge_normal(-edge_vec[1], edge_vec[0]);
        
        Vec2 to_right = mesh.cells[c1].centroid - mesh.cells[c0].centroid;
        if (edge_normal.dot(to_right) < 0) {
            edge_normal = -edge_normal;
        }
        face.normal = edge_normal;
        face.bc_tag = 0;
        
        mesh.faces.push_back(face);
        mesh.cells[c0].face_ids.push_back(face.id);
        mesh.cells[c0].neighbor_ids.push_back(c1);
        mesh.cells[c1].face_ids.push_back(face.id);
        mesh.cells[c1].neighbor_ids.push_back(c0);
    }
    
    // Boundary faces
    for (auto& [ek, cell_tag] : boundary_edges) {
        cgsize_t cell_id = cell_tag.first;
        std::string tag = cell_tag.second;
        if (cell_id >= n_cells) continue;
        
        Face face;
        face.id = face_counter++;
        face.left_cell = cell_id;
        face.right_cell = -1;
        
        Vec2 p1(x[ek.n1-1], y[ek.n1-1]);
        Vec2 p2(x[ek.n2-1], y[ek.n2-1]);
        face.centroid = (p1 + p2) * 0.5;
        
        Vec2 edge_vec = p2 - p1;
        Vec2 edge_normal(-edge_vec[1], edge_vec[0]);
        
        // Ensure normal points outward
        Vec2 to_face = face.centroid - mesh.cells[cell_id].centroid;
        if (edge_normal.dot(to_face) < 0) {
            edge_normal = -edge_normal;
        }
        face.normal = edge_normal;
        face.bc_tag = get_tag_id(tag);
        
        mesh.faces.push_back(face);
        mesh.cells[cell_id].face_ids.push_back(face.id);
        mesh.boundary_families[tag].push_back(face.id);
    }
    
    mesh.n_faces = mesh.faces.size();
    mesh.n_boundary_faces = 0;
    for (auto& [tag, fids] : mesh.boundary_families) {
        mesh.n_boundary_faces += fids.size();
    }
    mesh.bc_tag_map = tag_to_bc_id;
    
    std::cout << "Mesh loaded: " << mesh.n_cells << " cells, " 
              << mesh.n_faces << " faces, "
              << mesh.n_boundary_faces << " boundary faces" << std::endl;
    for (auto& [tag, fids] : mesh.boundary_families) {
        std::cout << "  Boundary family '" << tag << "': " << fids.size() << " faces" << std::endl;
    }
    
    return mesh;
}

void compute_face_geometry(Mesh&) {}
void compute_cell_geometry(Mesh&) {}

} // namespace cfd
