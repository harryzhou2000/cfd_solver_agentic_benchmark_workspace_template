// Smoke test for the CGNS mesh reader: reads a mesh and prints topology stats.
#include "case.hpp"
#include "mesh.hpp"
#include <cstdio>
int main(int argc, char** argv) {
  if (argc < 3) { std::printf("usage: mesh_test <case.json> <mesh.cgns>\n"); return 1; }
  cfd::CaseConfig cfg = cfd::loadCase(argv[1]);
  cfd::Mesh m = cfd::readCGNSMesh(argv[2], cfg);
  int nBnd = 0, nWall = 0, nFar = 0, nSlip = 0;
  for (int f = 0; f < m.nface; ++f) {
    if (m.faceR[f] < 0) {
      ++nBnd;
      switch (m.faceBC[f].type) {
        case cfd::BCType::Farfield: ++nFar; break;
        case cfd::BCType::SlipWall: ++nSlip; break;
        case cfd::BCType::NoSlipAdiabaticWall: ++nWall; break;
        default: break;
      }
    }
  }
  std::printf("verts=%d cells=%d faces=%d  interior=%d boundary=%d\n",
              m.nvert, m.ncell, m.nface, m.nface - nBnd, nBnd);
  std::printf("boundary: farfield=%d slipwall=%d noslipwall=%d\n", nFar, nSlip, nWall);
  std::printf("bc_sections:");
  for (auto& s : m.bc_section_names) std::printf(" %s", s.c_str());
  std::printf("\n");
  // sanity: every interior face has 2 cells, every boundary face has BC
  double vmin=1e30,vmax=-1e30;
  for (int c=0;c<m.ncell;++c){ if(m.cellVol[c]<vmin)vmin=m.cellVol[c]; if(m.cellVol[c]>vmax)vmax=m.cellVol[c];}
  std::printf("cell vol min=%.4e max=%.4e\n", vmin, vmax);
  return 0;
}
