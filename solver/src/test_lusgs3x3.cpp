#include <cmath>
#include <cstdio>
#include <vector>
#include "common.hpp"
using namespace cfd;
static MatN jac(const Primitive& q, const Vec2& n, const GasModel& g) {
    const double gm=g.gamma,u=q.u,v=q.v,rho=std::max(q.rho,1e-300);
    const double H=q.p/((gm-1.0)*rho)+q.p/rho+0.5*(u*u+v*v);
    const double ke=0.5*(u*u+v*v);
    MatN Ax,Ay,A;
    Ax[0][1]=1.0; Ax[1][0]=0.5*(gm-3.0)*u*u+0.5*(gm-1.0)*v*v;
    Ax[1][1]=(3.0-gm)*u; Ax[1][2]=-(gm-1.0)*v; Ax[1][3]=gm-1.0;
    Ax[2][0]=-u*v; Ax[2][1]=v; Ax[2][2]=u;
    Ax[3][0]=u*((gm-1.0)*ke-H); Ax[3][1]=H-(gm-1.0)*u*u;
    Ax[3][2]=-(gm-1.0)*u*v; Ax[3][3]=gm*u;
    Ay[0][2]=1.0; Ay[1][0]=-u*v; Ay[1][1]=v; Ay[1][2]=u;
    Ay[2][0]=0.5*(gm-1.0)*u*u+0.5*(gm-3.0)*v*v;
    Ay[2][1]=-(gm-1.0)*u; Ay[2][2]=(3.0-gm)*v; Ay[2][3]=gm-1.0;
    Ay[3][0]=v*((gm-1.0)*ke-H); Ay[3][1]=-(gm-1.0)*u*v;
    Ay[3][2]=H-(gm-1.0)*v*v; Ay[3][3]=gm*v;
    for(int i=0;i<kNC;i++)for(int j=0;j<kNC;j++)A[i][j]=Ax[i][j]*n.x+Ay[i][j]*n.y;
    return A;
}
struct Face{int L,R;Vec2 n;double area;};
int main(int argc,char**argv){
    const double cfl=argc>1?atof(argv[1]):16.0;
    const int dmode=argc>2?atoi(argv[2]):0;
    const double relax=argc>3?atof(argv[3]):1.0;
    const int bmode=argc>4?atoi(argv[4]):0;  // 0=A+ upper, 1=A- upper
    GasModel gas{1.4,1.0,0.72};
    const Primitive q0{1.0,1.0,0.0,31.746031746031743};
    const double vol=1.0;
    // 3x3 cell grid with 4-face interior cells, ordered row-major.
    std::vector<Face> F;
    auto add=[&](int L,int R,Vec2 n){F.push_back({L,R,n,1.0});};
    add(0,1,{1,0}); add(1,2,{1,0});
    add(3,4,{1,0}); add(4,5,{1,0});
    add(6,7,{1,0}); add(7,8,{1,0});
    add(0,3,{0,1}); add(3,6,{0,1});
    add(1,4,{0,1}); add(4,7,{0,1});
    add(2,5,{0,1}); add(5,8,{0,1});
    std::vector<std::vector<int>> cf(9);
    for(int f=0;f<(int)F.size();f++){cf[F[f].L].push_back(f);cf[F[f].R].push_back(f);}
    // Slip-wall faces on the outer boundary of the 3x3 patch.
    std::vector<std::pair<int,Vec2>> walls = {
        {0,{-1,0}},{1,{0,1}},{2,{1,0}},
        {3,{-1,0}},{5,{1,0}},
        {6,{-1,0}},{7,{0,-1}},{8,{1,0}}};
    std::vector<VecN> U(9,prim_to_cons(q0,gas));
    U[4][0]*=1.01; U[4][3]*=1.01;
    for(int it=0;it<600;it++){
        std::vector<VecN> R(9);
        for(const auto&f:F){
            Primitive qL=cons_to_prim(U[f.L],gas),qR=cons_to_prim(U[f.R],gas);
            double aL=sound_speed(qL,gas),aR=sound_speed(qR,gas);
            double lam=std::max(std::fabs(qL.u*f.n.x+qL.v*f.n.y)+aL,std::fabs(qR.u*f.n.x+qR.v*f.n.y)+aR);
            VecN FL=inviscid_flux_normal(qL,f.n,gas),FR=inviscid_flux_normal(qR,f.n,gas),fx;
            for(int i=0;i<kNC;i++)fx[i]=0.5*(FL[i]+FR[i])-0.5*lam*(U[f.R][i]-U[f.L][i]);
            for(int i=0;i<kNC;i++){R[f.L][i]+=fx[i];R[f.R][i]-=fx[i];}
        }
        for(const auto&[c,nw]:walls){
            Primitive qc=cons_to_prim(U[c],gas);
            R[c][1]+=qc.p*nw.x; R[c][2]+=qc.p*nw.y;
        }
        std::vector<double> D(9),dU(36,0.0);
        for(int c=0;c<9;c++){
            Primitive qc=cons_to_prim(U[c],gas);double a=sound_speed(qc,gas);
            double lam=0,rs=0;
            for(int f:cf[c]){
                Vec2 no=(F[f].L==c)?F[f].n:Vec2{-F[f].n.x,-F[f].n.y};
                lam+=std::fabs(qc.u*no.x+qc.v*no.y)+a;
                rs+=std::fabs(qc.u*no.x+qc.v*no.y)+a;
            }
            for(const auto&[wc,nw]:walls) if(wc==c)
                rs+=std::fabs(qc.u*nw.x+qc.v*nw.y)+a;
            D[c]=lam/(cfl*vol)*vol;
            if(dmode==0)D[c]+=0.5*rs;
            else if(dmode==1)D[c]+=rs;
            else D[c]+=2.0*rs;
        }
        for(int c=0;c<9;c++){
            VecN rhs; for(int i=0;i<kNC;i++)rhs[i]=-R[c][i];
            for(int f:cf[c]){
                int j=(F[f].L==c)?F[f].R:F[f].L;
                if(j>=c)continue;
                Primitive qc=cons_to_prim(U[c],gas),qn=cons_to_prim(U[j],gas);
                Primitive qa{0.5*(qc.rho+qn.rho),0.5*(qc.u+qn.u),0.5*(qc.v+qn.v),0.5*(qc.p+qn.p)};
                double a=sound_speed(qa,gas);
                Vec2 no=(F[f].L==c)?F[f].n:Vec2{-F[f].n.x,-F[f].n.y};
                double rho=std::fabs(qa.u*no.x+qa.v*no.y)+a;
                MatN A=jac(qa,no,gas);
                for(int i=0;i<kNC;i++){double s=0;for(int k=0;k<kNC;k++)s+=(A[i][k]-(i==k?rho:0.0))*dU[j*kNC+k];rhs[i]-=0.5*s;}
            }
            for(int i=0;i<kNC;i++)dU[c*kNC+i]=rhs[i]/D[c]*relax;
        }
        for(int c=8;c>=0;c--){
            VecN corr;
            for(int f:cf[c]){
                int j=(F[f].L==c)?F[f].R:F[f].L;
                if(j<=c)continue;
                Primitive qc=cons_to_prim(U[c],gas),qn=cons_to_prim(U[j],gas);
                Primitive qa{0.5*(qc.rho+qn.rho),0.5*(qc.u+qn.u),0.5*(qc.v+qn.v),0.5*(qc.p+qn.p)};
                double a=sound_speed(qa,gas);
                Vec2 no=(F[f].L==c)?F[f].n:Vec2{-F[f].n.x,-F[f].n.y};
                double rho=std::fabs(qa.u*no.x+qa.v*no.y)+a;
                MatN A=jac(qa,no,gas);
                for(int i=0;i<kNC;i++){double s=0;for(int k=0;k<kNC;k++){
                    double aij=A[i][k];
                    if(bmode==1)aij=A[i][k];
                    s+=(aij+((bmode==1)?-(i==k?rho:0.0):(i==k?rho:0.0)))*dU[j*kNC+k];
                    }corr[i]+=0.5*s;}
            }
            for(int i=0;i<kNC;i++)dU[c*kNC+i]-=corr[i]/D[c]*relax;
        }
        double rn=0;for(int c=0;c<9;c++){for(int i=0;i<kNC;i++){U[c][i]+=dU[c*kNC+i];rn+=R[c][i]*R[c][i];}}
        if(it%100==0)printf("  iter %d residual %.4e\n",it,sqrt(rn));
        if(!std::isfinite(sqrt(rn))||sqrt(rn)>1e6){printf("DIVERGED cfl=%.1f at %d\n",cfl,it);return 1;}
    }
    printf("stable cfl=%.1f\n",cfl);
    return 0;
}
