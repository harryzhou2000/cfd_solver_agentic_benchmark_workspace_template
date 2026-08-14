#include "cgns_reader.hpp"
#include <pcgnslib.h>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <map>
#include <set>
#include <cmath>

#undef Integer

static void check_cgns(int ier) {
    if (ier != CG_OK) {
        const char* err = cg_get_error();
        throw std::runtime_error(std::string("CGNS error: ") + (err ? err : "unknown"));
    }
}

static inline Index edge_hash(Index a, Index b) {
    if (a > b) std::swap(a, b);
    return (a << 32) | b;
}

Mesh read_cgns_mesh(const std::string& filepath) {
    Mesh mesh;
    int fn, B, Z, nBases, nZones;

    int ier_open = cgp_open(filepath.c_str(), CG_MODE_READ, &fn);
    if (ier_open != CG_OK) {
        if (cg_open(filepath.c_str(), CG_MODE_READ, &fn) != CG_OK)
            throw std::runtime_error("Cannot open CGNS file: " + filepath);
    }

    check_cgns(cg_nbases(fn, &nBases));
    B = 1;
    check_cgns(cg_nzones(fn, B, &nZones));
    if (nZones < 1) throw std::runtime_error("No zones in CGNS mesh");
    Z = 1;

    cgsize_t sizes[9];
    char zone_name[128];
    check_cgns(cg_zone_read(fn, B, Z, zone_name, sizes));
    mesh.zone_name = zone_name;

    cgsize_t nNodes = sizes[0];
    cgsize_t nCells = sizes[1];

    std::cout << "CGNS mesh: " << mesh.zone_name
              << " nodes=" << nNodes << " cells=" << nCells << std::endl;

    // Read coordinates
    mesh.nodes.resize(nNodes);
    {
        std::vector<double> cx(nNodes), cy(nNodes);
        cgsize_t rmin = 1, rmax = nNodes;
        check_cgns(cgp_coord_read_data(fn, B, Z, 1, &rmin, &rmax, cx.data()));
        check_cgns(cgp_coord_read_data(fn, B, Z, 2, &rmin, &rmax, cy.data()));
        for (cgsize_t i = 0; i < nNodes; i++) {
            mesh.nodes[i] = Vec2(cx[i], cy[i]);
        }
    }

    // Read element sections
    int nSections;
    check_cgns(cg_nsections(fn, B, Z, &nSections));

    mesh.num_cells = nCells;
    std::vector<std::vector<cgsize_t>> cell_nodes(nCells);

    // Store BAR_2 elements per section with family name for boundary tagging
    struct BarSection {
        std::string family_name;
        std::vector<std::pair<cgsize_t, cgsize_t>> edges; // (n1, n2) node pairs
    };
    std::vector<BarSection> bar_sections;

    for (int S = 1; S <= nSections; S++) {
        char section_name[128];
        CGNS_ENUMT(ElementType_t) etype;
        cgsize_t start, end;
        int nbndry, parent_flag;
        check_cgns(cg_section_read(fn, B, Z, S, section_name, &etype, &start, &end,
                                    &nbndry, &parent_flag));

        bool is_tri  = (etype == CGNS_ENUMV(TRI_3));
        bool is_quad = (etype == CGNS_ENUMV(QUAD_4));
        bool is_bar  = (etype == CGNS_ENUMV(BAR_2));
        bool is_mixed = (etype == CGNS_ENUMV(MIXED));

        int nn = 0;
        if (is_tri) nn = 3;
        else if (is_quad) nn = 4;
        else if (is_bar) nn = 2;

        // Read element data
        cgsize_t ne = end - start + 1;
        if (ne <= 0) continue;

        if (nn >= 2) {
            cgsize_t elem_data_size;
            check_cgns(cg_ElementDataSize(fn, B, Z, S, &elem_data_size));
            std::vector<cgsize_t> ed(elem_data_size);
            check_cgns(cgp_elements_read_data(fn, B, Z, S, start, end, ed.data()));

            if (is_bar) {
                // BAR_2 sections define boundary edges with family names
                BarSection bs;
                bs.family_name = section_name;
                cgsize_t pos = 0;
                for (cgsize_t e = 0; e < ne; e++) {
                    bs.edges.emplace_back(ed[pos] - 1, ed[pos+1] - 1);
                    pos += 2;
                }
                bar_sections.push_back(bs);
                std::cout << "  BAR section: " << section_name << " n=" << ne << std::endl;
            } else {
                // TRI_3 or QUAD_4
                cgsize_t pos = 0;
                for (cgsize_t e = start; e <= end; e++) {
                    auto& cn = cell_nodes[e - 1];
                    cn.resize(nn);
                    for (int k = 0; k < nn; k++) cn[k] = ed[pos++] - 1;
                }
                std::cout << "  Cell section: " << section_name << " nn=" << nn << " n=" << ne << std::endl;
            }
        } else if (is_mixed) {
            // MIXED section
            cgsize_t elem_data_size;
            check_cgns(cg_ElementDataSize(fn, B, Z, S, &elem_data_size));
            std::vector<cgsize_t> ed(elem_data_size);
            check_cgns(cgp_elements_read_data(fn, B, Z, S, start, end, ed.data()));
            std::cout << "  MIXED section: " << section_name << " n=" << ne << std::endl;

            cgsize_t pos = 0;
            for (cgsize_t e = start; e <= end; e++) {
                if (pos >= elem_data_size) break;
                cgsize_t elem_type = ed[pos++];
                int npe = 0;
                if (elem_type == CGNS_ENUMV(TRI_3)) npe = 3;
                else if (elem_type == CGNS_ENUMV(QUAD_4)) npe = 4;
                if (npe == 0) { pos += 1; continue; }
                auto& cn = cell_nodes[e - 1];
                cn.resize(npe);
                for (int k = 0; k < npe; k++) cn[k] = ed[pos++] - 1;
            }
        }
    }

    // Build edge->cell map
    struct EdgeInfo { Index cell; int local_idx; };
    std::map<Index, std::vector<EdgeInfo>> edge_cells;
    for (Index ic = 0; ic < nCells; ic++) {
        const auto& nd = cell_nodes[ic];
        int nv = (int)nd.size();
        if (nv < 3) continue;
        for (int k = 0; k < nv; k++) {
            cgsize_t a = nd[k], b = nd[(k+1)%nv];
            Index eid = edge_hash(a, b);
            edge_cells[eid].push_back({ic, k});
        }
    }

    // Build faces from edges
    std::map<Index, FaceID> edge_face_map;
    mesh.num_internal_faces = 0;

    for (const auto& [ehash, evec] : edge_cells) {
        Face face; face.left = -1; face.right = -1;
        if (evec.size() == 2) {
            face.left = evec[0].cell; face.right = evec[1].cell;
            mesh.num_internal_faces++;
        } else if (evec.size() == 1) {
            face.left = evec[0].cell; face.right = -1;
        } else continue;

        int kidx = evec[0].local_idx;
        const auto& cn = cell_nodes[evec[0].cell];
        int nv = (int)cn.size();
        cgsize_t n1 = cn[kidx], n2 = cn[(kidx+1)%nv];
        Vec2 p1 = mesh.nodes[n1], p2 = mesh.nodes[n2];
        face.centroid = (p1 + p2) * 0.5;
        face.area = (p2 - p1).norm();
        Vec2 out_normal(-(p2.y() - p1.y()), p2.x() - p1.x());
        out_normal.normalize();
        face.normal = out_normal;

        FaceID fid = (FaceID)mesh.faces.size();
        mesh.faces.push_back(face);
        edge_face_map[ehash] = fid;

        if (face.right < 0) {
            BoundaryFace bf; bf.global_face_id = fid; bf.family_name = "";
            bf.bc_type = BCType::Undefined;
            mesh.boundary_faces[fid] = bf;
        }
    }

    // Tag boundary faces from BAR_2 sections
    // Each BAR_2 element edge maps to a face via the edge hash
    for (const auto& bs : bar_sections) {
        for (const auto& [n1, n2] : bs.edges) {
            Index eid = edge_hash(n1, n2);
            auto it = edge_face_map.find(eid);
            if (it != edge_face_map.end()) {
                FaceID fid = it->second;
                auto bfit = mesh.boundary_faces.find(fid);
                if (bfit != mesh.boundary_faces.end()) {
                    bfit->second.family_name = bs.family_name;
                }
            }
        }
    }

    // Assign faces to cells
    mesh.cells.resize(nCells);
    for (Index ic = 0; ic < nCells; ic++) {
        Cell cell;
        const auto& nd = cell_nodes[ic];
        int nv = (int)nd.size();
        for (int k = 0; k < nv; k++) {
            Index eid = edge_hash(nd[k], nd[(k+1)%nv]);
            auto it = edge_face_map.find(eid);
            if (it != edge_face_map.end()) cell.faces.push_back(it->second);
        }
        mesh.cells[ic] = cell;
    }

    mesh.num_faces = (Index)mesh.faces.size();
    cgp_close(fn);

    std::cout << "Mesh loaded: " << mesh.num_cells << " cells, "
              << mesh.num_faces << " faces (" << mesh.num_internal_faces
              << " internal, " << mesh.boundary_faces.size() << " boundary)" << std::endl;
    return mesh;
}
