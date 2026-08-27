#include <cstdio>
#include <cmath>
#include <random>
#include "solve/lusgs.h"
#include "numerics/riemann_flux.h"
using namespace cns2d;
int main(){
  GasProperties gp; gp.gamma=1.4; gp.R=1.0; gp.prandtl=0.72;
  PerfectGas gas(gp);
  std::mt19937 rng(7); std::uniform_real_distribution<double> U01(0.0,1.0);
  double worst=0;
  for(int trial=0;trial<20000;++trial){
    PrimVec W{0.3+2.0*U01(rng), -2.0+4.0*U01(rng), -2.0+4.0*U01(rng), 0.3+3.0*U01(rng)};
    double th=6.283185307*U01(rng); Vec2 n{std::cos(th),std::sin(th)};
    ConsVec Uc=gas.consFromPrim(W);
    ConsVec dU; double s=0;
    for(int k=0;k<4;++k) dU[k]=(-1.0+2.0*U01(rng));
    // normalize dU
    double nn=0; for(int k=0;k<4;++k) nn+=dU[k]*dU[k]; nn=std::sqrt(nn);
    for(int k=0;k<4;++k) dU[k]/=nn;
    ConsVec analytic=fluxJacobianTimesVector(gas,Uc,n,dU);
    // central finite difference of the exact normal flux
    double eps=1e-7;
    ConsVec Up,Um;
    for(int k=0;k<4;++k){Up[k]=Uc[k]+eps*dU[k];Um[k]=Uc[k]-eps*dU[k];}
    // require positive pressure both sides
    if(gas.pressureFromCons(Up)<=0||gas.pressureFromCons(Um)<=0||Up[0]<=0||Um[0]<=0) continue;
    ConsVec fp=eulerNormalFlux(gas,gas.primFromCons(Up),n);
    ConsVec fm=eulerNormalFlux(gas,gas.primFromCons(Um),n);
    double scale=0;
    for(int k=0;k<4;++k) scale=std::max(scale,std::abs(analytic[k]));
    scale=std::max(scale,1e-6);
    for(int k=0;k<4;++k){
      double fd=(fp[k]-fm[k])/(2*eps);
      double err=std::abs(fd-analytic[k])/scale;
      if(err>worst){worst=err;
        if(err>1e-4) printf("trial %d comp %d: analytic=%.8e fd=%.8e relerr=%.3e  W=(%.3f,%.3f,%.3f,%.3f)\n",trial,k,analytic[k],fd,err,W[0],W[1],W[2],W[3]);
      }
    }
    (void)s;
  }
  printf("worst relative error of Jacobian-vector product vs central finite difference: %.4e\n",worst);
  return 0;
}
