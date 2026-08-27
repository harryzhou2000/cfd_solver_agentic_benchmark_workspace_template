#include <cstdio>
#include <cmath>
#include "physics/boundary_conditions.h"
#include "numerics/riemann_flux.h"
using namespace cns2d;
int main(){
  GasProperties gp; gp.gamma=1.4; gp.R=1.0; gp.prandtl=0.72;
  CaseInput in; in.gas=gp; in.freestream.rho=1.0; in.freestream.velocity_magnitude=1.0;
  in.freestream.mach=0.1; in.freestream.pressure=1.0/(1.4*0.01); in.physics.mode=PhysicsMode::kInviscid;
  FlowContext ctx=makeFlowContext(in);
  const PerfectGas& gas=ctx.gas;
  // A slip wall with a NON-tangential interior state (as happens during transients):
  // the energy flux through a solid wall must be exactly zero regardless.
  Vec2 n{0.0,1.0};
  double worst=0;
  for (double un=-0.5; un<=0.5; un+=0.1) {
   for (double ut=-1.0; ut<=1.0; ut+=0.5) {
    PrimVec Wi{1.0, ut, un, 71.4285714};
    ConsVec Ug=boundaryGhostState(BCType::kSlipWall, gas.consFromPrim(Wi), n, ctx);
    Real s=0;
    ConsVec f=riemannFlux(RiemannFluxType::kRoeEntropyFix,gas,Wi,gas.primFromCons(Ug),n,1.0,s);
    // mass flux and energy flux through a wall must both vanish
    if (std::abs(f[0])>worst||std::abs(f[3])>worst) {
      printf("un=%5.2f ut=%5.2f  massflux=%12.5e  energyflux=%12.5e  momn=%12.5e\n",un,ut,f[0],f[3],f[2]);
    }
    worst=std::max(worst,std::max(std::abs(f[0]),std::abs(f[3])));
   }
  }
  printf("worst |mass or energy flux| through slip wall = %.5e\n", worst);
  return 0;
}
