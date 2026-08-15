// CGNS mesh import (CGNS 4.5.0, 64-bit cgsize_t).
//
// Reads all bases/zones of a 2-D unstructured CGNS file and concatenates
// them into a single raw MeshData:
//   - volume cells from TRI_3 / QUAD_4 / MIXED sections whose element range
//     falls within [1, zone_size[1]],
//   - boundary faces from boundary-element sections (BAR_2) referenced by
//     ZoneBC entries; the boco family name is used as the face tag,
//   - bocos that reference volume cells directly (FaceCenter point-sets)
//     are resolved to the cell's unshared edge.
//
// All CGNS 1-based indices are converted to 0-based for internal use.

#include "mesh_reader.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <cgnslib.h>

namespace cfd {

namespace {

// Throws std::runtime_error with the CGNS error string if `err` != 0.
void check(int err, const char* what) {
    if (err != 0) {
        const char* msg = cg_get_error();
        throw std::runtime_error(std::string("CGNS error in ") + what +
                                 ": " + (msg ? msg : "unknown error"));
    }
}

// Packed 64-bit key for an undirected edge (node ids are < 2^31).
std::uint64_t edge_key(cgsize_t a, cgsize_t b) {
    const cgsize_t lo = std::min(a, b);
    const cgsize_t hi = std::max(a, b);
    return (static_cast<std::uint64_t>(lo) << 32) |
           static_cast<std::uint64_t>(hi);
}

int nodes_per_element(ElementType_t et) {
    switch (et) {
        case BAR_2: return 2;
        case TRI_3: return 3;
        case QUAD_4: return 4;
        default: return -1;  // MIXED, NGON_n, NFACE_n handled elsewhere
    }
}

// One side of a zonal-interface edge: the edge (global 0-based node ids)
// and the single cell of its zone adjacent to it.
struct OrphanEdge {
    cgsize_t a = -1;
    cgsize_t b = -1;
    cgsize_t cell = -1;
};

// Canonical coordinate key of an undirected edge: the two endpoints sorted
// lexicographically. Zonal interface edges are matched across zones by this
// key (the shared nodes are stored once per zone with identical coordinates).
struct EdgeCoordKey {
    double x1, y1, x2, y2;
    bool operator<(const EdgeCoordKey& o) const {
        if (x1 != o.x1) return x1 < o.x1;
        if (y1 != o.y1) return y1 < o.y1;
        if (x2 != o.x2) return x2 < o.x2;
        return y2 < o.y2;
    }
};

EdgeCoordKey make_edge_coord_key(const std::vector<double>& x,
                                 const std::vector<double>& y, cgsize_t a,
                                 cgsize_t b) {
    double xa = x[static_cast<size_t>(a)], ya = y[static_cast<size_t>(a)];
    double xb = x[static_cast<size_t>(b)], yb = y[static_cast<size_t>(b)];
    if (std::make_pair(xb, yb) < std::make_pair(xa, ya)) {
        std::swap(xa, xb);
        std::swap(ya, yb);
    }
    return EdgeCoordKey{xa, ya, xb, yb};
}

}  // namespace

MeshData read_mesh_cgns(const std::string& filename) {
    MeshData data;

    int fn = 0;
    check(cg_open(filename.c_str(), CG_MODE_READ, &fn), "cg_open");

    // One-sided zonal-interface edges collected from every zone (declared
    // here so the post-read matching pass can consume them).
    std::vector<OrphanEdge> orphan_edges;

    try {
        int nbases = 0;
        check(cg_nbases(fn, &nbases), "cg_nbases");

        cgsize_t node_offset = 0;  // running 0-based node offset across zones
        cgsize_t cell_offset = 0;  // running 0-based cell offset across zones

        for (int B = 1; B <= nbases; ++B) {
            char base_name[33];
            int cell_dim = 0, phys_dim = 0;
            check(cg_base_read(fn, B, base_name, &cell_dim, &phys_dim),
                  "cg_base_read");
            if (cell_dim != 2) {
                throw std::runtime_error(
                    std::string("unsupported base '") + base_name +
                    "': cell_dim=" + std::to_string(cell_dim) +
                    " (expected 2)");
            }

            int nzones = 0;
            check(cg_nzones(fn, B, &nzones), "cg_nzones");

            for (int Z = 1; Z <= nzones; ++Z) {
                char zone_name[33];
                cgsize_t zsize[3] = {0, 0, 0};
                check(cg_zone_read(fn, B, Z, zone_name, zsize), "cg_zone_read");
                ZoneType_t ztype = ZoneTypeNull;
                check(cg_zone_type(fn, B, Z, &ztype), "cg_zone_type");
                if (ztype != Unstructured) {
                    throw std::runtime_error(
                        std::string("zone '") + zone_name +
                        "' is not unstructured");
                }

                const cgsize_t n_nodes = zsize[0];
                const cgsize_t n_cells = zsize[1];

                // --- Coordinates -------------------------------------------------
                std::vector<double> zx(n_nodes), zy(n_nodes);
                const cgsize_t one = 1;
                const cgsize_t rmax = n_nodes;
                int ncoords = 0;
                check(cg_ncoords(fn, B, Z, &ncoords), "cg_ncoords");
                for (int c = 1; c <= ncoords; ++c) {
                    char cname[33];
                    DataType_t cdt;
                    check(cg_coord_info(fn, B, Z, c, &cdt, cname), "cg_coord_info");
                    if (std::string(cname) == "CoordinateX") {
                        check(cg_coord_read(fn, B, Z, cname, RealDouble, &one,
                                            &rmax, zx.data()),
                              "cg_coord_read(CoordinateX)");
                    } else if (std::string(cname) == "CoordinateY") {
                        check(cg_coord_read(fn, B, Z, cname, RealDouble, &one,
                                            &rmax, zy.data()),
                              "cg_coord_read(CoordinateY)");
                    } else if (std::string(cname) == "CoordinateZ") {
                        // 2-D mesh: read and discard (completeness).
                        std::vector<double> zz(n_nodes);
                        check(cg_coord_read(fn, B, Z, cname, RealDouble, &one,
                                            &rmax, zz.data()),
                              "cg_coord_read(CoordinateZ)");
                    }
                }
                data.x.insert(data.x.end(), zx.begin(), zx.end());
                data.y.insert(data.y.end(), zy.begin(), zy.end());
                data.n_nodes += n_nodes;

                // --- Element sections --------------------------------------------
                int nsections = 0;
                check(cg_nsections(fn, B, Z, &nsections), "cg_nsections");

                // boundary elements by their global (zone) element index
                std::unordered_map<cgsize_t, std::vector<cgsize_t>>
                    bnd_elements;

                for (int S = 1; S <= nsections; ++S) {
                    char sname[33];
                    ElementType_t et;
                    cgsize_t start = 0, end = 0;
                    int nbndry = 0, parent_flag = 0;
                    check(cg_section_read(fn, B, Z, S, sname, &et, &start, &end,
                                          &nbndry, &parent_flag),
                          "cg_section_read");

                    // Split into the volume-cell part [start, min(end, n_cells)]
                    // and the boundary-element part [max(start, n_cells+1), end].
                    const cgsize_t cell_end = std::min(end, n_cells);
                    const cgsize_t bnd_start = std::max(start, n_cells + 1);

                    if (start <= cell_end) {
                        const cgsize_t n = cell_end - start + 1;
                        const int npe = nodes_per_element(et);
                        if (et == MIXED) {
                            // Each element: type code prefix + nodes.
                            cgsize_t dsize = 0;
                            check(cg_ElementDataSize(fn, B, Z, S, &dsize),
                                  "cg_ElementDataSize");
                            std::vector<cgsize_t> buf(dsize);
                            check(cg_elements_read(fn, B, Z, S, buf.data(),
                                                   nullptr),
                                  "cg_elements_read(MIXED)");
                            size_t p = 0;
                            for (cgsize_t e = start; e <= end && p < buf.size();) {
                                const int mtype = static_cast<int>(buf[p++]);
                                const int mpe = nodes_per_element(
                                    static_cast<ElementType_t>(mtype));
                                if (mpe < 0) {
                                    throw std::runtime_error(
                                        std::string("MIXED section '") + sname +
                                        "': unsupported element type " +
                                        std::to_string(mtype));
                                }
                                if (e >= start && e <= cell_end) {
                                    std::vector<cgsize_t> nodes(mpe);
                                    for (int k = 0; k < mpe; ++k) {
                                        nodes[k] = buf[p + k] - 1 + node_offset;
                                    }
                                    data.cell_nodes.push_back(std::move(nodes));
                                } else if (e >= bnd_start && e <= end) {
                                    std::vector<cgsize_t> nodes(mpe);
                                    for (int k = 0; k < mpe; ++k) {
                                        nodes[k] = buf[p + k] - 1 + node_offset;
                                    }
                                    bnd_elements[e] = std::move(nodes);
                                }
                                p += mpe;
                                ++e;
                            }
                        } else if (npe > 0) {
                            std::vector<cgsize_t> buf(
                                static_cast<size_t>(n) * npe);
                            check(cg_elements_read(fn, B, Z, S, buf.data(),
                                                   nullptr),
                                  "cg_elements_read");
                            for (cgsize_t e = start; e <= cell_end; ++e) {
                                const size_t base =
                                    static_cast<size_t>(e - start) * npe;
                                std::vector<cgsize_t> nodes(npe);
                                for (int k = 0; k < npe; ++k) {
                                    nodes[k] = buf[base + k] - 1 + node_offset;
                                }
                                data.cell_nodes.push_back(std::move(nodes));
                            }
                        } else {
                            // NGON_n / NFACE_n: skipped (unsupported layout).
                        }
                    }

                    if (bnd_start <= end) {
                        const int npe = nodes_per_element(et);
                        if (et == MIXED) {
                            cgsize_t dsize = 0;
                            check(cg_ElementDataSize(fn, B, Z, S, &dsize),
                                  "cg_ElementDataSize");
                            std::vector<cgsize_t> buf(dsize);
                            check(cg_elements_read(fn, B, Z, S, buf.data(),
                                                   nullptr),
                                  "cg_elements_read(MIXED bnd)");
                            size_t p = 0;
                            for (cgsize_t e = start; e <= end && p < buf.size();) {
                                const int mtype = static_cast<int>(buf[p++]);
                                const int mpe = nodes_per_element(
                                    static_cast<ElementType_t>(mtype));
                                if (mpe < 0 || p + mpe > buf.size()) break;
                                if (e >= bnd_start && e <= end) {
                                    std::vector<cgsize_t> nodes(mpe);
                                    for (int k = 0; k < mpe; ++k) {
                                        nodes[k] = buf[p + k] - 1 + node_offset;
                                    }
                                    bnd_elements[e] = std::move(nodes);
                                }
                                p += mpe;
                                ++e;
                            }
                        } else if (npe > 0) {
                            const cgsize_t n = end - bnd_start + 1;
                            std::vector<cgsize_t> buf(
                                static_cast<size_t>(n) * npe);
                            const cgsize_t bstart = bnd_start;
                            check(cg_elements_partial_read(
                                      fn, B, Z, S, bstart, end, buf.data(),
                                      nullptr),
                                  "cg_elements_partial_read(bnd)");
                            for (cgsize_t e = bnd_start; e <= end; ++e) {
                                const size_t base =
                                    static_cast<size_t>(e - bnd_start) * npe;
                                std::vector<cgsize_t> nodes(npe);
                                for (int k = 0; k < npe; ++k) {
                                    nodes[k] = buf[base + k] - 1 + node_offset;
                                }
                                bnd_elements[e] = std::move(nodes);
                            }
                        }
                    }
                }

                // --- Cell-edge map (to resolve boundary faces to cells) ----------
                // edge key -> cells sharing it (for BAR_2 resolution and the
                // FaceCenter volume-cell boco fallback).
                std::unordered_map<std::uint64_t, std::vector<cgsize_t>>
                    cell_edges;
                cell_edges.reserve(static_cast<size_t>(n_cells) * 4);
                for (cgsize_t c = 0; c < n_cells; ++c) {
                    const auto& nodes = data.cell_nodes[cell_offset + c];
                    const size_t n = nodes.size();
                    for (size_t k = 0; k < n; ++k) {
                        const cgsize_t a = nodes[k];
                        const cgsize_t b = nodes[(k + 1) % n];
                        cell_edges[edge_key(a, b)].push_back(c);
                    }
                }

                // --- Boundary conditions ------------------------------------------
                int nbocos = 0;
                check(cg_nbocos(fn, B, Z, &nbocos), "cg_nbocos");
                // Boundary elements referenced by a boco (per-zone element
                // indices); the rest are zonal-interface candidates.
                std::set<cgsize_t> covered_bnd;
                for (int BC = 1; BC <= nbocos; ++BC) {
                    char bname[33];
                    BCType_t bt;
                    PointSetType_t pst;
                    cgsize_t npnts = 0;
                    int normal_index = 0;
                    cgsize_t normal_list_size = 0;
                    DataType_t normal_dt;
                    int ndataset = 0;
                    check(cg_boco_info(fn, B, Z, BC, bname, &bt, &pst, &npnts,
                                       &normal_index, &normal_list_size,
                                       &normal_dt, &ndataset),
                          "cg_boco_info");

                    // Family name (falls back to the boco name).
                    std::string family = bname;
                    char fam[33] = "";
                    check(cg_goto(fn, B, "Zone_t", Z, "ZoneBC_t", 1, "BC_t",
                                  BC, "end"),
                          "cg_goto(ZoneBC)");
                    if (cg_famname_read(fam) == 0 && fam[0] != '\0') {
                        family = fam;
                    }

                    GridLocation_t loc = Vertex;
                    if (cg_boco_gridlocation_read(fn, B, Z, BC, &loc) != 0) {
                        loc = Vertex;
                    }
                    // Note: `loc` distinguishes FaceCenter boco point-sets
                    // (boundary elements) from volume-cell references. Both
                    // are handled below by checking the element index against
                    // the zone cell count.

                    std::vector<cgsize_t> pnts(npnts);
                    if (npnts > 0) {
                        check(cg_boco_read(fn, B, Z, BC, pnts.data(), nullptr),
                              "cg_boco_read");
                    }

                    // ElementRange / PointRange: pnts = {start, end} inclusive.
                    // PointList: each entry is one element index.
                    const bool is_range = (pst == ElementRange ||
                                           pst == PointRange);
                    const cgsize_t plo = is_range && npnts >= 2 ? pnts[0] : 0;
                    const cgsize_t phi = is_range && npnts >= 2 ? pnts[1] : 0;

                    auto resolve_face = [&](cgsize_t elem) {
                        MeshData::BFaceInfo info;
                        info.family_name = family;
                        covered_bnd.insert(elem);
                        if (elem <= n_cells) {
                            // Volume-cell reference: find the cell's unshared
                            // edge (the one no other cell uses).
                            const auto& nodes =
                                data.cell_nodes[cell_offset + elem - 1];
                            const size_t n = nodes.size();
                            for (size_t k = 0; k < n; ++k) {
                                const cgsize_t a = nodes[k];
                                const cgsize_t b = nodes[(k + 1) % n];
                                auto it = cell_edges.find(edge_key(a, b));
                                if (it != cell_edges.end() &&
                                    it->second.size() == 1) {
                                    info.cell_id = cell_offset + elem - 1;
                                    info.node_ids = {a, b};
                                    break;
                                }
                            }
                            if (!info.node_ids.empty()) {
                                data.boundary_faces.push_back(std::move(info));
                            }
                        } else {
                            auto it = bnd_elements.find(elem);
                            if (it != bnd_elements.end()) {
                                info.node_ids = it->second;
                                if (info.node_ids.size() >= 2) {
                                    const auto eit = cell_edges.find(edge_key(
                                        info.node_ids[0], info.node_ids[1]));
                                    if (eit != cell_edges.end() &&
                                        !eit->second.empty()) {
                                        info.cell_id =
                                            cell_offset + eit->second.front();
                                    }
                                    data.boundary_faces.push_back(
                                        std::move(info));
                                }
                            }
                        }
                    };

                    if (is_range && phi >= plo) {
                        for (cgsize_t elem = plo; elem <= phi; ++elem) {
                            resolve_face(elem);
                        }
                    } else {
                        for (cgsize_t i = 0; i < npnts; ++i) {
                            resolve_face(pnts[i]);
                        }
                    }
                }

                // --- Zonal-interface edges ----------------------------------------
                // Boundary-element sections not referenced by any boco (e.g.
                // 'con-2'..'con-N' in the benchmark meshes) describe the
                // interface between zones. Each such edge has exactly one
                // adjacent cell in its own zone; the coincident edge in the
                // neighbor zone is paired up below.
                for (const auto& [elem, nodes] : bnd_elements) {
                    if (covered_bnd.count(elem)) continue;
                    if (nodes.size() < 2) continue;
                    const auto eit = cell_edges.find(edge_key(nodes[0], nodes[1]));
                    if (eit == cell_edges.end() || eit->second.empty()) continue;
                    orphan_edges.push_back(
                        OrphanEdge{nodes[0], nodes[1],
                                   cell_offset + eit->second.front()});
                }

                data.n_cells += n_cells;
                node_offset += n_nodes;
                cell_offset += n_cells;
            }
        }
    } catch (...) {
        cg_close(fn);
        throw;
    }

    // --- Match zonal-interface edges across zones by coordinates ----------
    // Every interface edge is declared once per adjacent zone (each zone
    // stores its own copy of the shared nodes). Pair up the coincident
    // copies and record each pair as an internal face between the two
    // adjacent cells. Edges left unmatched (non-conforming interfaces) are
    // skipped, preserving the previous behavior for degenerate inputs.
    {
        std::map<EdgeCoordKey, std::vector<OrphanEdge>> by_key;
        for (const OrphanEdge& e : orphan_edges) {
            by_key[make_edge_coord_key(data.x, data.y, e.a, e.b)].push_back(e);
        }
        for (auto& [key, vec] : by_key) {
            (void)key;
            while (vec.size() >= 2) {
                const OrphanEdge e0 = vec.back();
                vec.pop_back();
                const OrphanEdge e1 = vec.back();
                vec.pop_back();
                MeshData::ZonalFace zf;
                zf.cell_l = e0.cell;
                zf.cell_r = e1.cell;
                zf.node_a = e0.a;
                zf.node_b = e0.b;
                data.zonal_faces.push_back(zf);
            }
        }
    }

    check(cg_close(fn), "cg_close");
    return data;
}

}  // namespace cfd
