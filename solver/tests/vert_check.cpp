#include "case.hpp"
#include "mesh.hpp"
#include <cstdio>
int main(int argc, char** argv){
  cfd::CaseConfig cfg=cfd::loadCase(argv[1]);
  cfd::Mesh m=cfd::readCGNSMesh(argv[2],cfg);
  for(int i=2;i<argc;++i){ int g=std::atoi(argv[i]); if(g<m.nvert) std::printf("vert %d: x=%.17g y=%.17g\n", g, m.vx[g], m.vy[g]); }
  // count duplicate coords
  int dup=0;
  for(int a=0;a<m.nvert;++a) for(int b=a+1;b<m.nvert;++b) if(m.vx[a]==m.vx[b]&&m.vy[a]==m.vy[b]) dup++;
  std::printf("exact-duplicate vertex pairs: %d (nvert=%d)\n", dup, m.nvert);
  return 0;
}
