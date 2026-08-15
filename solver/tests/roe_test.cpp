#include "physics.hpp"
#include <cstdio>
int main(){
  cfd::Gas g; g.gamma=1.4; g.R=1.0; g.prandtl=0.72;
  cfd::Prim fs; fs.r()=1.0; fs.u()=1.0; fs.v()=0.0; fs.p()=71.42857142857142;
  cfd::Cons UL=cfd::toCons(fs,g), UR=cfd::toCons(fs,g);
  double nx=0.031, ny=-1.0;
  double F[4];
  for(int trial=0; trial<5; ++trial){
    cfd::roeFlux(UL,UR,nx,ny,g,0.10,F);
    std::printf("trial %d: F=[%.6e %.6e %.6e %.6e]\n", trial, F[0],F[1],F[2],F[3]);
  }
  // physical flux for reference
  double FL[4]; cfd::inviscidFluxDotN(fs,nx,ny,g,FL);
  std::printf("phys : F=[%.6e %.6e %.6e %.6e]\n", FL[0],FL[1],FL[2],FL[3]);
  return 0;
}
