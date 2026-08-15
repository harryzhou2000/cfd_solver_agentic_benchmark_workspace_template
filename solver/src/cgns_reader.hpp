#pragma once

/// @file cgns_reader.hpp
/// Read 2-D unstructured CGNS meshes into a `cfd::Mesh`.

#include "common.hpp"

#include <string>
#include <unordered_map>

namespace cfd {

/// Read a 2-D unstructured CGNS mesh into a single merged `cfd::Mesh`.
///
/// Reads EVERY zone of the first base: node coordinates, cell connectivity
/// (TRI_3 / QUAD_4 / NGON_n sections), and boundary conditions. Zones are
/// merged into one conformal mesh via their Abutting1to1 zone-interface
/// connectivity (vertex-based PointList/PointListDonor pairs, read with
/// cg_nconns/cg_conn_info/cg_conn_read — the benchmark meshes store their
/// interfaces as generic GridConnectivity_t nodes, which cg_n1to1 does not
/// count). Interface vertices of non-first zones are remapped onto their
/// donor-zone counterparts; all other nodes are appended.
///
/// Boundary-family names are mapped to integer `bc_tag`s through `bc_map`
/// (family name -> BC type string as used in case files, e.g. "farfield",
/// "slip_wall", "no_slip_adiabatic_wall"); tags follow the BoundaryType enum
/// values (0 = farfield, 1 = slip_wall, 2 = no_slip_adiabatic_wall). CGNS
/// 1-based indices are converted to 0-based internally. BAR_2 interface
/// sections (e.g. "con-*") are never instantiated as faces — only elements
/// referenced by a ZoneBC become boundary faces.
///
/// Boundary faces are appended to `Mesh::faces` (with `is_boundary == true`)
/// and their indices are recorded in `Mesh::boundary_faces[bc_tag]`.
///
/// @throws std::runtime_error on CGNS read failures, unsupported layouts, or
///         inconsistent zone-interface mappings.
Mesh read_cgns_mesh(const std::string& mesh_file_path,
                    const std::unordered_map<std::string, std::string>& bc_map);

} // namespace cfd
