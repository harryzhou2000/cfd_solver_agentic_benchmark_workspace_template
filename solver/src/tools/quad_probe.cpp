// Probe: read the QUAD_4 section of a CGNS mesh and report vertex coords +
// shoelace area for the first few quads and an area histogram, to diagnose
// degenerate/bowtie elements from node ordering.
#include <cgnslib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: quad_probe <file.cgns>\n"); return 1; }
  int fn; if (cg_open(argv[1], CG_MODE_READ, &fn) != CG_OK) { std::printf("open fail\n"); return 1; }
  int nbases=0; cg_nbases(fn,&nbases);
  for (int b=1;b<=nbases;++b) {
    char bn[64]; int cd=0,pd=0; cg_base_read(fn,b,bn,&cd,&pd);
    int nz=0; cg_nzones(fn,b,&nz);
    for (int z=1;z<=nz;++z) {
      char zn[64]; cgsize_t sz[9]; for(int i=0;i<9;++i) sz[i]=0; cg_zone_read(fn,b,z,zn,sz);
      int nv=(int)sz[0];
      std::vector<double> X(nv),Y(nv);
      int nco=0; cg_ncoords(fn,b,z,&nco);
      cgsize_t rmin[3]={1,1,1}, rmax[3]={nv,1,1};
      for(int c=1;c<=nco;++c){char cn[64];DataType_t dt;cg_coord_info(fn,b,z,c,&dt,cn);
        if(std::string(cn).find("X")!=std::string::npos) cg_coord_read(fn,b,z,cn,RealDouble,rmin,rmax,X.data());
        else if(std::string(cn).find("Y")!=std::string::npos) cg_coord_read(fn,b,z,cn,RealDouble,rmin,rmax,Y.data());}
      int ns=0; cg_nsections(fn,b,z,&ns);
      for(int s=1;s<=ns;++s){
        char secn[64];ElementType_t et;cgsize_t st=0,en=0;int nb=0,par=0;
        cg_section_read(fn,b,z,s,secn,&et,&st,&en,&nb,&par);
        if(!(et==QUAD_4||et==TRI_3)) continue;
        cgsize_t ds=0; cg_ElementDataSize(fn,b,z,s,&ds);
        std::vector<cgsize_t> conn(ds,0); if(ds>0) cg_elements_read(fn,b,z,s,conn.data(),nullptr);
        int npe = (et==QUAD_4)?4:3;
        int nel=(int)(en-st+1);
        std::vector<double> areas;
        for(int e=0;e<nel;++e){
          std::vector<int> idx(npe); for(int k=0;k<npe;++k) idx[k]=(int)conn[e*npe+k]-1;
          double a=0; int n=npe;
          for(int i=0;i<n;++i){int j=(i+1)%n; double xi=X[idx[i]],yi=Y[idx[i]],xj=X[idx[j]],yj=Y[idx[j]]; a+=xi*yj-xj*yi;}
          a*=0.5; areas.push_back(std::fabs(a));
        }
        std::sort(areas.begin(),areas.end());
        std::printf("zone%d sec %s %s nel=%d npe=%d\n",z,secn,(et==QUAD_4?"QUAD_4":"TRI_3"),nel,npe);
        auto pct=[&](double p){ return areas[(int)(p*(nel-1))]; };
        std::printf("  area min=%.3e p1=%.3e p10=%.3e p50=%.3e p90=%.3e p99=%.3e max=%.3e\n",
          areas[0],pct(0.01),pct(0.10),pct(0.50),pct(0.90),pct(0.99),areas.back());
        // dump first quad + a small-area one
        for(int e=0;e<2 && e<nel;++e){
          std::printf("  elem%d verts:",e);
          for(int k=0;k<npe;++k){int id=(int)conn[e*npe+k]; std::printf(" %d(%.4f,%.4f)",id,X[id-1],Y[id-1]);}
          std::printf(" area=%.3e\n",areas[e]);
        }
      }
    }
  }
  cg_close(fn); return 0;
}
