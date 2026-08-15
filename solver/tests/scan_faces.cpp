#include "case.hpp"
#include "mesh.hpp"
#include <cstdio>
int main(int argc, char** argv){
  cfd::CaseConfig cfg=cfd::loadCase(argv[1]);
  cfd::Mesh m=cfd::readCGNSMesh(argv[2],cfg);
  int ca=std::atoi(argv[3]), cb=std::atoi(argv[4]);
  int cnt=0;
  for (int f=0;f<m.nface;++f){
    int a=m.faceL[f], b=m.faceR[f];
    if ((a==ca&&b==cb)||(a==cb&&b==ca)){
      std::printf("global face %d: L=%d R=%d v=(%d,%d) fc=(%.4f,%.4f) len=%.4e\n", f,a,b,m.faceV0[f],m.faceV1[f],m.faceCx[f],m.faceCy[f],m.faceLen[f]);
      cnt++;
    }
  }
  std::printf("total faces connecting %d and %d: %d\n", ca, cb, cnt);
  // also: how many faces does cell ca have in global cellFaces adjacency vs faceL/faceR scan?
  int nfl=0,nfr=0;
  for(int f=0;f<m.nface;++f){ if(m.faceL[f]==ca)nfl++; if(m.faceR[f]==ca)nfr++; }
  std::printf("cell %d: appears as faceL in %d faces, as faceR in %d faces (total %d)\n", ca, nfl, nfr, nfl+nfr);
  return 0;
}
