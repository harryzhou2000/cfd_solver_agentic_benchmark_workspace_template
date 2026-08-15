#include "case.hpp"
#include "mesh.hpp"
#include <cstdio>
#include <cmath>
int main(int argc, char** argv){
  cfd::CaseConfig cfg=cfd::loadCase(argv[1]);
  cfd::Mesh m=cfd::readCGNSMesh(argv[2],cfg);
  double maxsum=0; int wcell=-1;
  for (int c=0;c<m.ncell;++c){
    double sx=0,sy=0;
    int fo=m.cellFaceOffset[c],fe=m.cellFaceOffset[c+1];
    for (int k=fo;k<fe;++k){
      int f=m.cellFaces[k];
      double nx,ny;
      if (m.faceL[f]==c){ nx=m.faceNx[f]; ny=m.faceNy[f]; }
      else { nx=-m.faceNx[f]; ny=-m.faceNy[f]; }
      sx+=nx*m.faceLen[f]; sy+=ny*m.faceLen[f];
    }
    double mag=std::sqrt(sx*sx+sy*sy);
    if (mag>maxsum){maxsum=mag;wcell=c;}
  }
  std::printf("max |sum n*len| over cells = %.6e  (cell %d, vol=%.4e)\n", maxsum, wcell, m.cellVol[wcell]);
  return 0;
}
