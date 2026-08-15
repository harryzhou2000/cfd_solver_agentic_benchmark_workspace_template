#include "cfd.hpp"
#include <cgnslib.h>
#include <metis.h>
#include <unordered_set>
#include <fstream>

static BCType parse_bc(const std::string &s) {
  if (s == "farfield") return BC_FARFIELD;
  if (s == "slip_wall") return BC_SLIPWALL;
  if (s == "no_slip_adiabatic_wall") return BC_NOSLIP_ADIABATIC;
  return BC_NONE;
}
struct VHash {
  std::unordered_map<long long, int> tab; double scale = 1e7;
  void add(double x, double y, int &gid, std::vector<double> &vx, std::vector<double> &vy) {
    long long kx=(long long)std::llround(x*scale), ky=(long long)std::llround(y*scale);
    long long key=(kx & 0xffffffffLL)|(ky<<32);
    auto it=tab.find(key); if(it!=tab.end()){ gid=it->second; return; }
    gid=(int)vx.size(); vx.push_back(x); vy.push_back(y); tab[key]=gid;
  }
};
void read_cgns_global(const std::string &path, const CaseInput &cs, GlobalMesh &g, int rank) {
  if (rank != 0) return;
  int fn, nBases;
  if (cg_open(path.c_str(), CG_MODE_READ, &fn) != CG_OK) throw std::runtime_error("cg_open failed: "+path);
  cg_nbases(fn, &nBases);
  VHash vh; std::map<std::pair<int,int>, std::string> barFam;
  for (int b=1;b<=nBases;b++) {
    char bname[128]; int cellDim,physDim; cg_base_read(fn,b,bname,&cellDim,&physDim);
    int nZones; cg_nzones(fn,b,&nZones);
    for (int z=1;z<=nZones;z++) {
      char zname[128]; cgsize_t sizes[3]; cg_zone_read(fn,b,z,zname,sizes);
      std::vector<double> zx(sizes[0]), zy(sizes[0]); cgsize_t rmin=1, rmax=sizes[0];
      cg_coord_read(fn,b,z,"CoordinateX",RealDouble,&rmin,&rmax,zx.data());
      cg_coord_read(fn,b,z,"CoordinateY",RealDouble,&rmin,&rmax,zy.data());
      std::vector<int> vmap(sizes[0]);
      for (cgsize_t i=0;i<sizes[0];i++){ int gid; vh.add(zx[i],zy[i],gid,g.vx,g.vy); vmap[i]=gid; }
      int nSec; cg_nsections(fn,b,z,&nSec);
      for (int s=1;s<=nSec;s++) {
        char sname[128]; ElementType_t etype; cgsize_t start,end; int nbnd,parent;
        cg_section_read(fn,b,z,s,sname,&etype,&start,&end,&nbnd,&parent);
        int nvpe=(etype==TRI_3)?3:(etype==QUAD_4)?4:(etype==BAR_2)?2:0;
        if(nvpe==0) continue;
        cgsize_t connSize; cg_ElementDataSize(fn,b,z,s,&connSize);
        std::vector<cgsize_t> conn(connSize); cg_elements_read(fn,b,z,s,conn.data(),nullptr);
        int nelem=(int)(end-start+1); std::string name(sname);
        if (etype==TRI_3||etype==QUAD_4) {
          for(int e=0;e<nelem;e++){ std::array<int,4> v{}; for(int k=0;k<nvpe;k++) v[k]=vmap[conn[e*nvpe+k]-1]; g.cellV.push_back(v); g.cellNv.push_back(nvpe); }
        } else if (etype==BAR_2) {
          auto it=cs.bc_map.find(name);
          if(it!=cs.bc_map.end()) for(int e=0;e<nelem;e++){ int a=vmap[conn[e*2]-1], b2=vmap[conn[e*2+1]-1]; barFam[{std::min(a,b2),std::max(a,b2)}]=name; }
        }
      }
    }
  }
  cg_close(fn);
  g.nVerts=(long)g.vx.size(); g.nCells=(long)g.cellV.size();
  g.cx.resize(g.nCells); g.cy.resize(g.nCells); g.vol.resize(g.nCells);
  for (long c=0;c<g.nCells;c++) {
    int n=g.cellNv[c]; double area=0,cx=0,cy=0;
    for(int k=0;k<n;k++){ int i0=g.cellV[c][k], i1=g.cellV[c][(k+1)%n]; double x0=g.vx[i0],y0=g.vy[i0],x1=g.vx[i1],y1=g.vy[i1]; double cr=x0*y1-x1*y0; area+=cr; cx+=(x0+x1)*cr; cy+=(y0+y1)*cr; }
    area*=0.5; if(std::fabs(area)<1e-18) area=1e-18; double inv=1.0/(6.0*area);
    g.cx[c]=cx*inv; g.cy[c]=cy*inv; g.vol[c]=std::fabs(area);
  }
  std::unordered_map<long long,int> faceMap; faceMap.reserve(g.nCells*4); long nv1=g.nVerts+1;
  auto fkey=[&](int a,int b)->long long{ return (long long)a*nv1+(long long)b; };
  g.cellFace.resize(g.nCells); g.cellNF.resize(g.nCells);
  for (long c=0;c<g.nCells;c++) {
    int n=g.cellNv[c];
    for(int k=0;k<n;k++){ int a=g.cellV[c][k], b=g.cellV[c][(k+1)%n]; int lo=std::min(a,b), hi=std::max(a,b); long long key=fkey(lo,hi);
      auto it=faceMap.find(key); int fid;
      if(it==faceMap.end()){ fid=(int)g.fv0.size(); g.fv0.push_back(lo); g.fv1.push_back(hi); g.fL.push_back((int)c); g.fR.push_back(-1); g.fFam.push_back(""); faceMap[key]=fid; }
      else { fid=it->second; g.fR[fid]=(int)c; }
      g.cellFace[c][k]=fid;
    }
    g.cellNF[c]=n;
  }
  g.nFaces=(long)g.fv0.size();
  g.fnx.resize(g.nFaces); g.fny.resize(g.nFaces); g.fa.resize(g.nFaces); g.fcx.resize(g.nFaces); g.fcy.resize(g.nFaces); g.fBC.resize(g.nFaces);
  for (long f=0;f<g.nFaces;f++) {
    int v0=g.fv0[f], v1=g.fv1[f]; double x0=g.vx[v0],y0=g.vy[v0],x1=g.vx[v1],y1=g.vy[v1];
    double ex=x1-x0, ey=y1-y0; double len=std::sqrt(ex*ex+ey*ey); if(len<1e-18)len=1e-18;
    g.fa[f]=len; g.fcx[f]=0.5*(x0+x1); g.fcy[f]=0.5*(y0+y1);
    double n1x=ey/len, n1y=-ex/len; int cL=g.fL[f]; double dx=g.fcx[f]-g.cx[cL], dy=g.fcy[f]-g.cy[cL];
    double nx,ny; if(n1x*dx+n1y*dy>=0){nx=n1x;ny=n1y;}else{nx=-n1x;ny=-n1y;}
    g.fnx[f]=nx; g.fny[f]=ny;
    if (g.fR[f]<0) {
      auto it=barFam.find({std::min(v0,v1),std::max(v0,v1)}); std::string fam=(it!=barFam.end())?it->second:"";
      g.fFam[f]=fam; BCType bc=BC_NONE;
      if(!fam.empty()){ auto jt=cs.bc_map.find(fam); if(jt!=cs.bc_map.end()) bc=parse_bc(jt->second); }
      if(bc==BC_NONE) bc=BC_FARFIELD; g.fBC[f]=bc;
    } else g.fBC[f]=BC_NONE;
  }
}
static void build_local_mesh(const GlobalMesh &g, const std::vector<idx_t> &part, int r, LocalMesh &m, int nprocs) {
  m.rank=r; m.nprocs=nprocs; long nc=g.nCells;
  std::vector<int> localIdx(nc,-1); std::vector<int> ownedList;
  for (long c=0;c<nc;c++) if(part[c]==r) ownedList.push_back((int)c);
  std::vector<int> ghostList; std::unordered_set<int> ghostSet;
  for (int c:ownedList){ int n=g.cellNF[c]; for(int k=0;k<n;k++){ int f=g.cellFace[c][k]; int other=(g.fL[f]==c)?g.fR[f]:g.fL[f]; if(other>=0&&part[other]!=r&&ghostSet.find(other)==ghostSet.end()){ ghostSet.insert(other); ghostList.push_back(other); } } }
  m.nOwn=(int)ownedList.size(); m.nGhost=(int)ghostList.size();
  m.gid.resize(m.nOwn+m.nGhost); m.gRank.resize(m.nOwn+m.nGhost); m.cx.resize(m.nOwn+m.nGhost); m.cy.resize(m.nOwn+m.nGhost); m.vol.resize(m.nOwn+m.nGhost);
  for (int i=0;i<m.nOwn;i++){ int c=ownedList[i]; localIdx[c]=i; m.gid[i]=c; m.gRank[i]=r; m.cx[i]=g.cx[c]; m.cy[i]=g.cy[c]; m.vol[i]=g.vol[c]; }
  for (int i=0;i<m.nGhost;i++){ int c=ghostList[i]; localIdx[c]=m.nOwn+i; m.gid[m.nOwn+i]=c; m.gRank[m.nOwn+i]=(int)part[c]; m.cx[m.nOwn+i]=g.cx[c]; m.cy[m.nOwn+i]=g.cy[c]; m.vol[m.nOwn+i]=g.vol[c]; }
  for (int i=0;i<m.nOwn;i++) {
    int c=ownedList[i], n=g.cellNF[c];
    for(int k=0;k<n;k++){ int f=g.cellFace[c][k]; int other=(g.fL[f]==c)?g.fR[f]:g.fL[f];
      int fpart,fL=i,fR;
      if(other<0){fpart=2;fR=-1;} else if(part[other]==r){fpart=0;fR=localIdx[other];} else {fpart=1;fR=localIdx[other];}
      double nx=g.fnx[f],ny=g.fny[f]; if(!(g.fL[f]==c)){nx=-nx;ny=-ny;}
      m.fL.push_back(fL); m.fR.push_back(fR); m.fnx.push_back(nx); m.fny.push_back(ny); m.fa.push_back(g.fa[f]); m.fcx.push_back(g.fcx[f]); m.fcy.push_back(g.fcy[f]); m.fBC.push_back(g.fBC[f]); m.fV0.push_back(g.fv0[f]); m.fV1.push_back(g.fv1[f]); m.fPart.push_back(fpart);
    }
  }
  std::map<int,std::vector<int>> recvByRank;
  for (int i=0;i<m.nGhost;i++) recvByRank[m.gRank[m.nOwn+i]].push_back(m.nOwn+i);
  std::map<int,std::unordered_set<int>> sendByRank;
  for (int i=0;i<m.nGhost;i++){ int gc=m.gid[m.nOwn+i], owner=m.gRank[m.nOwn+i], n=g.cellNF[gc]; for(int k=0;k<n;k++){ int f=g.cellFace[gc][k]; int other=(g.fL[f]==gc)?g.fR[f]:g.fL[f]; if(other>=0&&part[other]==r) sendByRank[owner].insert(localIdx[other]); } }
  for (auto &kv:recvByRank) {
    int nb=kv.first; m.neighbors.push_back(nb); m.recvCells.push_back(kv.second); m.recvGid.resize(m.recvCells.size()); m.sendCells.resize(m.recvCells.size());
    for(int li:kv.second) m.recvGid.back().push_back(m.gid[li]);
    // Sort recv cells by global id so the receive order matches the sender's send
    // order: sendCells are sorted by local index, and for owned cells local index
    // is monotonic with global id, so they are in ascending-global-id order. The
    // receiver must use the same ascending-global-id order or ghost values are
    // scrambled across partition boundaries.
    { auto &rc=m.recvCells.back(); auto &rg=m.recvGid.back();
      std::vector<int> idx(rc.size()); for(size_t i=0;i<idx.size();i++) idx[i]=(int)i;
      std::sort(idx.begin(),idx.end(),[&](int a,int b){ return rg[a]<rg[b]; });
      std::vector<int> rc2(rc.size()),rg2(rg.size());
      for(size_t i=0;i<idx.size();i++){ rc2[i]=rc[idx[i]]; rg2[i]=rg[idx[i]]; }
      rc.swap(rc2); rg.swap(rg2); }
    auto &sc=m.sendCells.back(); for(int v:sendByRank[nb]) sc.push_back(v); std::sort(sc.begin(),sc.end());
  }
}
void partition_and_scatter(const GlobalMesh &g, int nparts, LocalMesh &m, int rank, int nprocs) {
  std::vector<idx_t> part; idx_t edgeCut=0;
  if (rank==0) {
    idx_t nv=(idx_t)g.nCells;
    std::vector<idx_t> xadj(nv+1,0); std::vector<int> deg(nv,0);
    for (long f=0;f<g.nFaces;f++) if(g.fR[f]>=0){ deg[g.fL[f]]++; deg[g.fR[f]]++; }
    for (idx_t i=0;i<nv;i++) xadj[i+1]=xadj[i]+deg[i];
    std::vector<idx_t> pos(nv,0); std::vector<idx_t> adj(xadj[nv]);
    for (long f=0;f<g.nFaces;f++) if(g.fR[f]>=0){ int a=g.fL[f],b=g.fR[f]; adj[xadj[a]+pos[a]++]=b; adj[xadj[b]+pos[b]++]=a; }
    part.resize(nv); idx_t objval=0;
    if (nparts<=1) { for(idx_t i=0;i<nv;i++) part[i]=0; }
    else {
      idx_t ncon=1, np=nparts, options[METIS_NOPTIONS]; METIS_SetDefaultOptions(options); options[METIS_OPTION_CONTIG]=1;
      int ok=METIS_PartGraphKway(&nv,&ncon,xadj.data(),adj.data(),nullptr,nullptr,nullptr,&np,nullptr,nullptr,options,&objval,part.data());
      if(ok!=METIS_OK){ for(idx_t i=0;i<nv;i++) part[i]=i%np; objval=0; }
    }
    edgeCut=objval;
  }
  MPI_Bcast(&edgeCut,1,MPI_INT,0,MPI_COMM_WORLD);
  if (rank==0) {
    build_local_mesh(g,part,0,m,nprocs); m.edgeCut=edgeCut; m.nCellsGlobal=g.nCells; m.nFacesGlobal=g.nFaces;
    for (int r=1;r<nprocs;r++) {
      LocalMesh rm; build_local_mesh(g,part,r,rm,nprocs); rm.edgeCut=edgeCut; rm.nCellsGlobal=g.nCells; rm.nFacesGlobal=g.nFaces;
      std::vector<long long> ibuf; std::vector<double> dbuf;
      auto puti=[&](long long v){ ibuf.push_back(v); }; auto putd=[&](double v){ dbuf.push_back(v); };
      puti(rm.nOwn); puti(rm.nGhost); puti((long long)rm.gid.size()); puti((long long)rm.fL.size()); puti((long long)rm.neighbors.size()); puti(edgeCut); puti(g.nCells); puti(g.nFaces);
      for(int v:rm.gid) puti(v); for(int v:rm.gRank) puti(v);
      for(size_t i=0;i<rm.cx.size();i++){ putd(rm.cx[i]); putd(rm.cy[i]); putd(rm.vol[i]); }
      for(int v:rm.fL) puti(v); for(int v:rm.fR) puti(v);
      for(size_t i=0;i<rm.fnx.size();i++){ putd(rm.fnx[i]); putd(rm.fny[i]); putd(rm.fa[i]); putd(rm.fcx[i]); putd(rm.fcy[i]); }
      for(int v:rm.fBC) puti(v); for(int v:rm.fV0) puti(v); for(int v:rm.fV1) puti(v); for(int v:rm.fPart) puti(v);
      for(int nb:rm.neighbors) puti(nb); puti((long long)rm.sendCells.size());
      for(auto&sv:rm.sendCells){ puti((long long)sv.size()); for(int v:sv) puti(v); }
      for(auto&sv:rm.recvCells){ puti((long long)sv.size()); for(int v:sv) puti(v); }
      for(auto&sv:rm.recvGid){ puti((long long)sv.size()); for(int v:sv) puti(v); }
      long long ni=ibuf.size(), nd=dbuf.size();
      MPI_Send(&ni,1,MPI_LONG_LONG,r,0,MPI_COMM_WORLD); MPI_Send(&nd,1,MPI_LONG_LONG,r,1,MPI_COMM_WORLD);
      MPI_Send(ibuf.data(),ni,MPI_LONG_LONG,r,2,MPI_COMM_WORLD); MPI_Send(dbuf.data(),nd,MPI_DOUBLE,r,3,MPI_COMM_WORLD);
    }
  } else {
    long long ni,nd; MPI_Recv(&ni,1,MPI_LONG_LONG,0,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE); MPI_Recv(&nd,1,MPI_LONG_LONG,0,1,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
    std::vector<long long> ibuf(ni); std::vector<double> dbuf(nd);
    MPI_Recv(ibuf.data(),ni,MPI_LONG_LONG,0,2,MPI_COMM_WORLD,MPI_STATUS_IGNORE); MPI_Recv(dbuf.data(),nd,MPI_DOUBLE,0,3,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
    size_t ip=0,dp=0; auto geti=[&](){ return ibuf[ip++]; }; auto getd=[&](){ return dbuf[dp++]; };
    m.rank=rank; m.nprocs=nprocs; m.nOwn=(int)geti(); m.nGhost=(int)geti(); long long ngid=geti(),nf=geti(),nnb=geti(); m.edgeCut=(int)geti(); m.nCellsGlobal=geti(); m.nFacesGlobal=geti();
    m.gid.resize(ngid); m.gRank.resize(ngid); m.cx.resize(ngid); m.cy.resize(ngid); m.vol.resize(ngid);
    for(long long i=0;i<ngid;i++) m.gid[i]=(int)geti();
    for(long long i=0;i<ngid;i++) m.gRank[i]=(int)geti();
    for(long long i=0;i<ngid;i++){ m.cx[i]=getd(); m.cy[i]=getd(); m.vol[i]=getd(); }
    m.fL.resize(nf); m.fR.resize(nf); m.fBC.resize(nf); m.fV0.resize(nf); m.fV1.resize(nf); m.fPart.resize(nf); m.fnx.resize(nf); m.fny.resize(nf); m.fa.resize(nf); m.fcx.resize(nf); m.fcy.resize(nf);
    for(long long i=0;i<nf;i++) m.fL[i]=(int)geti();
    for(long long i=0;i<nf;i++) m.fR[i]=(int)geti();
    for(long long i=0;i<nf;i++){ m.fnx[i]=getd(); m.fny[i]=getd(); m.fa[i]=getd(); m.fcx[i]=getd(); m.fcy[i]=getd(); }
    for(long long i=0;i<nf;i++) m.fBC[i]=(BCType)geti();
    for(long long i=0;i<nf;i++) m.fV0[i]=(int)geti();
    for(long long i=0;i<nf;i++) m.fV1[i]=(int)geti();
    for(long long i=0;i<nf;i++) m.fPart[i]=(int)geti();
    m.neighbors.resize(nnb); for(long long i=0;i<nnb;i++) m.neighbors[i]=(int)geti();
    long long nsend=geti(); m.sendCells.resize(nsend); m.recvCells.resize(nsend); m.recvGid.resize(nsend);
    for(long long k=0;k<nsend;k++){ long long s=geti(); m.sendCells[k].resize(s); for(long long i=0;i<s;i++) m.sendCells[k][i]=(int)geti(); }
    for(long long k=0;k<nsend;k++){ long long s=geti(); m.recvCells[k].resize(s); for(long long i=0;i<s;i++) m.recvCells[k][i]=(int)geti(); }
    for(long long k=0;k<nsend;k++){ long long s=geti(); m.recvGid[k].resize(s); for(long long i=0;i<s;i++) m.recvGid[k][i]=(int)geti(); }
  }
}
void write_partition_diag(const std::string &dir, const LocalMesh &m, int rank, int nprocs) {
  int own=m.nOwn, ghost=m.nGhost, nbf=0, nnb=(int)m.neighbors.size();
  for (size_t i=0;i<m.fPart.size();i++) if(m.fPart[i]==2) nbf++;
  int sendTot=0,recvTot=0; for(auto&s:m.sendCells) sendTot+=(int)s.size(); for(auto&s:m.recvCells) recvTot+=(int)s.size();
  const int MXNB=16; int nblist[MXNB]; for(int k=0;k<MXNB;k++) nblist[k]=-1;
  for(size_t k=0;k<m.neighbors.size()&&k<(size_t)MXNB;k++) nblist[k]=m.neighbors[k];
  std::vector<int> ownA(nprocs),ghA(nprocs),bfA(nprocs),nbA(nprocs),sdA(nprocs),rvA(nprocs),nbMat(nprocs*MXNB);
  MPI_Gather(&own,1,MPI_INT,ownA.data(),1,MPI_INT,0,MPI_COMM_WORLD); MPI_Gather(&ghost,1,MPI_INT,ghA.data(),1,MPI_INT,0,MPI_COMM_WORLD);
  MPI_Gather(&nbf,1,MPI_INT,bfA.data(),1,MPI_INT,0,MPI_COMM_WORLD); MPI_Gather(&nnb,1,MPI_INT,nbA.data(),1,MPI_INT,0,MPI_COMM_WORLD);
  MPI_Gather(&sendTot,1,MPI_INT,sdA.data(),1,MPI_INT,0,MPI_COMM_WORLD); MPI_Gather(&recvTot,1,MPI_INT,rvA.data(),1,MPI_INT,0,MPI_COMM_WORLD);
  MPI_Gather(nblist,MXNB,MPI_INT,nbMat.data(),MXNB,MPI_INT,0,MPI_COMM_WORLD);
  if(rank==0){
    std::ofstream f(dir+"/partition_diagnostics.csv");
    f<<"rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
    int mx=0,mn=1<<30,sum=0;
    for(int r=0;r<nprocs;r++){ f<<r<<","<<ownA[r]<<","<<ghA[r]<<","<<bfA[r]<<","<<nbA[r]<<",\"["; for(int k=0;k<nbA[r];k++){if(k)f<<",";f<<nbMat[r*MXNB+k];} f<<"]\","<<sdA[r]<<","<<rvA[r]<<"\n"; mx=std::max(mx,ownA[r]); mn=std::min(mn,ownA[r]); sum+=ownA[r]; }
    double mean=(double)sum/nprocs, lb=(mx>0)?mean/mx:1.0;
    f<<"# edge_cut="<<m.edgeCut<<" max_owned="<<mx<<" min_owned="<<mn<<" mean_owned="<<mean<<" load_balance="<<lb<<"\n";
  }
}
double mpi_sum(double x, MPI_Comm comm){ double s; MPI_Allreduce(&x,&s,1,MPI_DOUBLE,MPI_SUM,comm); return s; }
double mpi_max(double x, MPI_Comm comm){ double s; MPI_Allreduce(&x,&s,1,MPI_DOUBLE,MPI_MAX,comm); return s; }
double mpi_min(double x, MPI_Comm comm){ double s; MPI_Allreduce(&x,&s,1,MPI_DOUBLE,MPI_MIN,comm); return s; }
