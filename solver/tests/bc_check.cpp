#include "case.hpp"
#include "mesh.hpp"
#include <cstdio>
#include <cmath>
int main(int argc, char** argv){
  cfd::CaseConfig cfg=cfd::loadCase(argv[1]);
  cfd::Mesh m=cfd::readCGNSMesh(argv[2],cfg);
  // Print boundary face locations by BC type
  double ff_xmin=1e30,ff_xmax=-1e30,ff_ymin=1e30,ff_ymax=-1e30;
  double sw_xmin=1e30,sw_xmax=-1e30,sw_ymin=1e30,sw_ymax=-1e30;
  int ff=0,sw=0;
  for(int f=0;f<m.nface;++f){
    if(m.faceR[f]>=0) continue;
    double x=m.faceCx[f],y=m.faceCy[f];
    if(m.faceBC[f].type==cfd::BCType::Farfield){
      ff++; if(x<ff_xmin)ff_xmin=x; if(x>ff_xmax)ff_xmax=x; if(y<ff_ymin)ff_ymin=y; if(y>ff_ymax)ff_ymax=y;
    } else if(m.faceBC[f].type==cfd::BCType::SlipWall){
      sw++; if(x<sw_xmin)sw_xmin=x; if(x>sw_xmax)sw_xmax=x; if(y<sw_ymin)sw_ymin=y; if(y>sw_ymax)sw_ymax=y;
    }
  }
  printf("farfield: %d faces, x[%.4f,%.4f] y[%.4f,%.4f]\n",ff,ff_xmin,ff_xmax,ff_ymin,ff_ymax);
  printf("slipwall: %d faces, x[%.4f,%.4f] y[%.4f,%.4f]\n",sw,sw_xmin,sw_xmax,sw_ymin,sw_ymax);
  return 0;
}
