#include "cfd.hpp"
Gas make_gas(const CaseInput &cs){ Gas g; g.gamma=cs.gamma; g.Rgas=cs.Rgas; g.prandtl=cs.prandtl; g.cpmcv=cs.gamma-1.0; g.mach=cs.mach; g.Uinf=cs.U_inf; g.rinf=cs.rho_inf; g.pinf=cs.p_inf;
  if(cs.mode=="laminar"&&cs.reynolds>0) g.mu=cs.rho_inf*cs.U_inf*cs.reynolds_length/cs.reynolds; else g.mu=0.0; return g; }
void primitive(const Vec4 &U, const Gas &g, double &rho, double &u, double &v, double &p, double &T){
  rho=U[0]; double ir=1.0/rho; u=U[1]*ir; v=U[2]*ir; double e=U[3]-0.5*(u*u+v*v); p=g.cpmcv*rho*e; T=p/(rho*g.Rgas); }
double sound_speed(double p, double rho, double gamma){ return std::sqrt(std::max(1e-30,gamma*p/std::max(rho,1e-30))); }
static inline Vec4 phys_flux(double rho,double u,double v,double p,double rhoE,double nx,double ny){
  double un=u*nx+v*ny; Vec4 F; F[0]=rho*un; F[1]=rho*u*un+p*nx; F[2]=rho*v*un+p*ny; F[3]=(rhoE+p)*un; return F; }
// Roe flux with Harten-Yee entropy fix
Vec4 roe_flux(const Vec4 &UL, const Vec4 &UR, double nx, double ny, const Gas &g, double /*scale*/){
  double ggm=g.cpmcv;
  double rhoL=UL[0],uL=UL[1]/rhoL,vL=UL[2]/rhoL,pL=ggm*rhoL*(UL[3]-0.5*(uL*uL+vL*vL));
  double rhoR=UR[0],uR=UR[1]/rhoR,vR=UR[2]/rhoR,pR=ggm*rhoR*(UR[3]-0.5*(uR*uR+vR*vR));
  double HL=(UL[3]+pL)/rhoL, HR=(UR[3]+pR)/rhoR;
  double srL=std::sqrt(rhoL), srR=std::sqrt(rhoR), den=1.0/(srL+srR);
  double rho=srL*srR, u=(srL*uL+srR*uR)*den, v=(srL*vL+srR*vR)*den, H=(srL*HL+srR*HR)*den;
  double q2=u*u+v*v, a=std::sqrt(std::max(1e-30,ggm*(H-0.5*q2)));
  double un=u*nx+v*ny, ut=u*ny-v*nx;
  Vec4 FL=phys_flux(rhoL,uL,vL,pL,UL[3],nx,ny), FR=phys_flux(rhoR,uR,vR,pR,UR[3],nx,ny);
  double dr=rhoR-rhoL, du=uR-uL, dv=vR-vL, dp=pR-pL;
  double dun=du*nx+dv*ny, dut=du*ny-dv*nx, a2=a*a;
  double w1=(dp-rho*a*dun)/(2.0*a2), w4=(dp+rho*a*dun)/(2.0*a2), w2=dut, w3=dr-dp/a2;
  double delta=0.1*a;
  auto efix=[&](double lam)->double{ double al=std::fabs(lam); if(al<delta) return 0.5*(lam*lam/delta+delta); return al; };
  double L1=efix(un-a), L4=efix(un+a), L2=efix(un), L3=L2;
  double D[4]={0,0,0,0};
  D[0]+=L1*w1; D[1]+=L1*w1*(u-rho*a*nx); D[2]+=L1*w1*(v-rho*a*ny); D[3]+=L1*w1*(H-rho*a*un);
  D[0]+=L4*w4; D[1]+=L4*w4*(u+rho*a*nx); D[2]+=L4*w4*(v+rho*a*ny); D[3]+=L4*w4*(H+rho*a*un);
  D[1]+=L2*w2*(rho*ny); D[2]+=L2*w2*(-rho*nx); D[3]+=L2*w2*(rho*ut);
  D[0]+=L3*w3; D[1]+=L3*w3*u; D[2]+=L3*w3*v; D[3]+=L3*w3*(0.5*q2);
  Vec4 F; for(int k=0;k<4;k++) F[k]=0.5*(FL[k]+FR[k])-0.5*D[k]; return F;
}
Vec4 rusanov_flux(const Vec4 &UL, const Vec4 &UR, double nx, double ny, const Gas &g, double scale){
  double ggm=g.cpmcv;
  double rhoL=UL[0],uL=UL[1]/rhoL,vL=UL[2]/rhoL,pL=ggm*rhoL*(UL[3]-0.5*(uL*uL+vL*vL));
  double rhoR=UR[0],uR=UR[1]/rhoR,vR=UR[2]/rhoR,pR=ggm*rhoR*(UR[3]-0.5*(uR*uR+vR*vR));
  double aL=sound_speed(pL,rhoL,g.gamma),aR=sound_speed(pR,rhoR,g.gamma);
  double unL=uL*nx+vL*ny,unR=uR*nx+vR*ny;
  static double smooth_abs_delta=0.0; static bool sa_init=false; if(!sa_init){ const char*e=getenv("CFDD_SMOOTH_ABS"); if(e) smooth_abs_delta=std::atof(e); sa_init=true; }
  double sL,sR;
  if(smooth_abs_delta>0.0){ double ad=smooth_abs_delta; sL=std::sqrt(unL*unL+ad*ad)+aL; sR=std::sqrt(unR*unR+ad*ad)+aR; }
  else { sL=std::fabs(unL)+aL; sR=std::fabs(unR)+aR; }
  double smax;
  static double smooth_delta=0.0; static bool sm_init=false; if(!sm_init){ const char*e=getenv("CFDD_SMOOTH_SMAX"); if(e) smooth_delta=std::atof(e); sm_init=true; }
  if(smooth_delta>0.0){ double avg=0.5*(sL+sR); double d=smooth_delta*avg; smax=scale*0.5*(sL+sR+std::sqrt((sL-sR)*(sL-sR)+d*d)); }
  else smax=scale*std::max(sL,sR);
  Vec4 FL=phys_flux(rhoL,uL,vL,pL,UL[3],nx,ny),FR=phys_flux(rhoR,uR,vR,pR,UR[3],nx,ny);
  Vec4 F; for(int k=0;k<4;k++) F[k]=0.5*(FL[k]+FR[k])-0.5*smax*(UR[k]-UL[k]); return F;
}
