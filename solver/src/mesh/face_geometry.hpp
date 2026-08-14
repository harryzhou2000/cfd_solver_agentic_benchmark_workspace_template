#pragma once
#include "types.hpp"

namespace omo {

class FaceGeometry {
public:
    // Compute cell volumes and face normals/areas for a mesh
    // Cell volumes are computed using Green's theorem (sum of face contributions)
    static void compute(MeshData& mesh);
    
    // Compute for a local mesh
    static void compute_local(LocalMeshData& local, const MeshData* global_mesh);
};

} // namespace omo
