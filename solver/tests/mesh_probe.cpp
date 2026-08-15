#include "mesh.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: mesh_probe <mesh.cgns> [--bc <case.json>]\n");
    return 1;
  }
  try {
    cfd::GlobalMesh mesh = cfd::loadCgnsMesh(argv[1]);
    std::printf("zones: %zu\n", mesh.zoneNames.size());
    for (const auto& z : mesh.zoneNames) std::printf("  zone: %s\n", z.c_str());
    std::printf("points: %zu\n", mesh.points.size());
    std::printf("cells: %ld\n", mesh.numCells());
    std::printf("faces: %ld (interior %ld, boundary %ld)\n", mesh.numFaces(),
                mesh.numFaces() - mesh.numBoundaryFaces(), mesh.numBoundaryFaces());
    std::printf("1to1 connections: %ld\n", mesh.n1to1);
    std::printf("families:\n");
    for (const auto& f : mesh.familyNames) std::printf("  %s\n", f.c_str());
    double vol = 0;
    for (double v : mesh.cellVolume) vol += v;
    std::printf("total volume: %.12g\n", vol);
    double cmin[2] = {1e30, 1e30}, cmax[2] = {-1e30, -1e30};
    for (const auto& p : mesh.points) {
      cmin[0] = std::min(cmin[0], p[0]);
      cmin[1] = std::min(cmin[1], p[1]);
      cmax[0] = std::max(cmax[0], p[0]);
      cmax[1] = std::max(cmax[1], p[1]);
    }
    std::printf("bounds: [%g,%g] x [%g,%g]\n", cmin[0], cmax[0], cmin[1], cmax[1]);
    if (argc > 3 && std::string(argv[2]) == "--bc") {
      cfd::Case c = cfd::loadCase(argv[3]);
      cfd::assignBoundaryConditions(mesh, c);
      int counts[3] = {0, 0, 0};
      for (int bc : mesh.faceBcType)
        if (bc >= 0) counts[bc]++;
    std::printf("BC counts: farfield=%d slip=%d noslip=%d\n", counts[0], counts[1],
                counts[2]);
    if (argc > 4) {
      // dump geometry for specific global cell ids (debugging)
      std::map<int, int> g2l;
      for (long c = 0; c < mesh.numCells(); ++c) g2l[static_cast<int>(c)] = static_cast<int>(c);
      for (int a = 4; a < argc; ++a) {
        int gid = std::atoi(argv[a]);
        const auto& cell = mesh.cells[gid];
        std::printf("cell %d: nverts=%zu centroid=(%.6g,%.6g) V=%.6g\n", gid, cell.size(),
                    mesh.cellCentroid[gid][0], mesh.cellCentroid[gid][1],
                    mesh.cellVolume[gid]);
        for (int fi : mesh.cellFaces[gid]) {
          const auto& f = mesh.faces[fi];
          std::printf("  face %d: v=(%d,%d) len=%.6g n=(%.4g,%.4g) cen=(%.6g,%.6g) "
                      "c=(%d,%d) bc=%d\n",
                      fi, f.v0, f.v1, f.len, f.normal[0], f.normal[1], f.centroid[0],
                      f.centroid[1], f.c0, f.c1, mesh.faceBcType[fi]);
        }
      }
    }
  }
  return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "ERROR: %s\n", e.what());
    return 2;
  }
}
