// Standalone mesh-reader test. Loads a case JSON, builds the global mesh,
// and prints cell/face counts, boundary counts per family, and the bounding
// box so we can verify the reader against the CGNS inspection output.
#include "../case.hpp"
#include "../mesh.hpp"
#include <map>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("usage: mesh_test <case.json> <mesh.cgns>\n"); return 1; }
  try {
    cfd::CaseDef cd = cfd::parse_case(argv[1]);
    cfd::Mesh m = cfd::load_cgns(argv[2], cd.bc_map);
    std::printf("case_id: %s\n", cd.case_id.c_str());
    std::printf("cells: %d\n", (int)m.cells.size());
    std::printf("faces: %d  (internal=%d boundary=%d)\n",
      (int)m.faces.size(), m.num_internal_faces, m.num_boundary_faces);
    std::printf("wall faces: %d  farfield faces: %d\n",
      (int)m.wall_face_ids.size(), (int)m.farfield_face_ids.size());
    double minA = 1e30, maxA = 0, sumA = 0;
    for (const auto& c : m.cells) { minA = std::min(minA, c.area); maxA = std::max(maxA, c.area); sumA += c.area; }
    std::printf("cell area min/max/mean: %.6e %.6e %.6e\n", minA, maxA, sumA / m.cells.size());
    double minLen = 1e30, maxLen = 0;
    int badOrient = 0;
    for (const auto& f : m.faces) {
      minLen = std::min(minLen, f.len);
      maxLen = std::max(maxLen, f.len);
      if (f.lc < 0) badOrient++;
    }
    std::printf("face len min/max: %.6e %.6e  bad_orient=%d\n", minLen, maxLen, badOrient);
    std::printf("bbox: [%.4f,%.4f] x [%.4f,%.4f] diag=%.4f\n",
      m.bbox_min.x, m.bbox_max.x, m.bbox_min.y, m.bbox_max.y, m.diag);
    std::map<std::string, std::pair<int, cfd::BCType>> fam;
    for (const auto& f : m.faces) if (f.rc < 0) {
      auto& e = fam[f.family]; e.first++; e.second = f.bctype;
    }
    for (const auto& kv : fam)
      std::printf("  boundary family '%s': %d faces (%s)\n",
        kv.first.c_str(), kv.second.first, cfd::bcTypeName(kv.second.second));
    return 0;
  } catch (std::exception& e) {
    std::printf("ERROR: %s\n", e.what()); return 2;
  }
}
