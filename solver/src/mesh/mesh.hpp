#pragma once

#include "common/types.hpp"
#include <vector>
#include <map>
#include <string>

// Unstructured mesh data (full serial mesh, before partitioning)
struct Mesh {
    // Nodes
    std::vector<Vec2> nodes;

    // Cells: each cell is a list of face IDs that bound it
    std::vector<Cell> cells;
    Index num_cells = 0;

    // Faces (internal + boundary)
    std::vector<Face> faces;
    Index num_faces = 0;
    Index num_internal_faces = 0;

    // Boundary info: face_id -> boundary condition
    std::map<FaceID, BoundaryFace> boundary_faces;

    // Wall face IDs (for force computation)
    std::vector<FaceID> wall_faces;

    // CGNS zone name
    std::string zone_name;

    // Bounding box
    Vec2 bbox_min, bbox_max;
};

// Distributed mesh view for a single MPI rank
struct RankMesh {
    // Owned cells (this rank's partition)
    std::vector<Cell> owned_cells;
    std::vector<StateVector> owned_state;
    std::vector<Vec2> owned_grad_rho;    // gradient of rho
    std::vector<Grad2> owned_grad_u;     // gradient of u
    std::vector<Grad2> owned_grad_v;     // gradient of v
    std::vector<Grad2> owned_grad_T;     // gradient of T
    std::vector<StateVector> owned_residual;

    // Ghost cells (from neighbor ranks)
    std::vector<Cell> ghost_cells;
    std::vector<StateVector> ghost_state;
    std::vector<int>    ghost_owner_rank;  // which rank owns this ghost

    // Faces: all internal faces involving owned cells
    std::vector<Face> internal_faces;       // faces between owned-* cells
    std::vector<BoundaryFace> internal_boundary_faces;  // boundary faces owned here

    // Node list for cells on this rank
    // We store nodes globally for simplicity; MPI doesn't require node partition
    // (only cell data is partitioned)
    const std::vector<Vec2>* global_nodes = nullptr;

    // Face-to-cell mapping for owned cells (redundant but convenient for residual)
    // face_id -> (left_cell_local, right_cell_local)
    // -1 for boundary or ghost side
    struct FaceConn {
        Index left;
        Index right;
    };
    std::vector<FaceConn> face_conn;

    // MPI neighbor info
    struct Neighbor {
        RankID rank;
        std::vector<Index> ghost_cell_ids;   // local ghost indices for this neighbor
        std::vector<Index> send_cell_ids;    // local owned indices to send
    };
    std::vector<Neighbor> neighbors;

    // Wall face map: local boundary face index -> global FaceID
    std::vector<FaceID> wall_face_ids;
};
