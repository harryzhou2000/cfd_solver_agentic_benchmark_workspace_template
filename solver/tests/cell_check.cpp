#include "case.hpp"
#include "mesh.hpp"
#include <cstdio>
#include <cmath>
int main(int argc, char** argv){
  cfd::CaseConfig cfg=cfd::loadCase(argv[1]);
  cfd::Mesh m=cfd::readCGNSMesh(argv[2],cfg);
  int target = argc>3 ? std::atoi(argv[3]) : 218;
  if (target>=m.ncell){ std::printf("cell %d out of range\n",target); return 1; }
  int off=m.cellOffset[target], nv=m.cellNv[target];
  std::printf("cell %d nv=%d center=(%.4f,%.4f) vol=%.4e\n", target, nv, m.cellCx[target],m.cellCy[target],m.cellVol[target]);
  std::printf("  verts:");
  for(int v=0;v<nv;++v){ int g=m.cellVerts[off+v]; std::printf(" %d(%.4f,%.4f)", g, m.vx[g], m.vy[g]); }
  std::printf("\n  faces:\n");
  int fo=m.cellFaceOffset[target],fe=m.cellFaceOffset[target+1];
  for(int k=fo;k<fe;++k){ int f=m.cellFaces[k]; int nb=(m.faceL[f]==target)?m.faceR[f]:m.faceL[f];
    std::printf("    f%d v=(%d,%d) len=%.4e fc=(%.4f,%.4f) nb=%d", f, m.faceV0[f],m.faceV1[f], m.faceLen[f], m.faceCx[f],m.faceCy[f], nb);
    if(nb>=0) std::printf(" nbcenter=(%.4f,%.4f)", m.cellCx[nb],m.cellCy[nb]);
    std::printf("\n");
  }
  return 0;
}
