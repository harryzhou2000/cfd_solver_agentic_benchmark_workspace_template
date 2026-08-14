#include "mesh.hpp"
#include <cgnslib.h>
#include <metis.h>
#include <algorithm>
#include <set>
#include <map>
#include <cmath>
#include <stdexcept>

namespace cfd2d {

struct VertexMerger {
  std::map<std::pair<int,int>, std::vector<int>> grid;
  double cellSize, ox, oy;
  std::vector<double>& vx; std::vector<double>& vy;
  VertexMerger(std::vector<double>& x, std::vector<double>& y, double cs, double x0, double y0)
    : cellSize(cs), ox(x0), oy(y0), vx(x), vy(y) {}
  int findOrAdd(double x, double y, double tol) {
    int ix = (int)std::floor((x - ox) / cellSize);
    int iy = (int)std::floor((y - oy) / cellSize);
    for (int dx = -1; dx <= 1; dx++)
      for (int dy = -1; dy <= 1; dy++) {
        auto it = grid.find({ix+dx, iy+dy});
        if (it == grid.end()) continue;
        for (int idx : it->second)
          if (std::abs(vx[idx]-x) < tol && std::abs(vy[idx]-y) < tol) return idx;
      }
    int idx = vx.size();
    vx.push_back(x); vy.push_back(y);
    grid[{ix, iy}].push_back(idx);
    return idx;
  }
};

void GlobalMesh::readCGNS(const std::string& file,
                          const std::vector<std::pair<std::string,BCType>>& bcMap) {
  int fn;
  if (cg_open(file.c_str(), CG_MODE_READ, &fn))
    throw std::runtime_error("Failed to open CGNS: " + file);
  std::vector<std::vector<double>> zoneX, zoneY;
  std::vector<std::vector<int>> zoneVMap;
  double xmin=1e30,ymin=1e30,xmax=-1e30,ymax=-1e30;
  int nbases; cg_nbases(fn, &nbases);
  for (int b=1; b<=nbases; b++) {
    int nzones; cg_nzones(fn,b,&nzones);
    for (int z=1; z<=nzones; z++) {
      char zn[33]; cgsize_t sizes[9];
      cg_zone_read(fn,b,z,zn,sizes);
      int nv=sizes[0];
      std::vector<double> xs(nv),ys(nv);
      cgsize_t s=1,e=nv;
      cg_coord_read(fn,b,z,"CoordinateX",RealDouble,&s,&e,xs.data());
      cg_coord_read(fn,b,z,"CoordinateY",RealDouble,&s,&e,ys.data());
      for(int i=0;i<nv;i++){xmin=std::min(xmin,xs[i]);ymin=std::min(ymin,ys[i]);xmax=std::max(xmax,xs[i]);ymax=std::max(ymax,ys[i]);}
      zoneX.push_back(std::move(xs)); zoneY.push_back(std::move(ys));
    }
  }
  double bbox=std::max({xmax-xmin,ymax-ymin,1.0});
  double tol=bbox*1e-10;
  double cs=bbox/std::sqrt(std::max(1.0,(double)(vx.size()+100)));
  VertexMerger mg(vx,vy,cs,xmin,ymin);
  for(size_t zi=0;zi<zoneX.size();zi++){
    std::vector<int> vm(zoneX[zi].size());
    for(size_t i=0;i<zoneX[zi].size();i++) vm[i]=mg.findOrAdd(zoneX[zi][i],zoneY[zi][i],tol);
    zoneVMap.push_back(std::move(vm));
  }
  nVert=vx.size();
  std::map<std::pair<int,int>,BCType> bndPairMap;
  cellOff.push_back(0);
  int zi=0;
  for(int b=1;b<=nbases;b++){
    int nzones; cg_nzones(fn,b,&nzones);
    for(int z=1;z<=nzones;z++,zi++){
      char zn[33]; cgsize_t sizes[9]; cg_zone_read(fn,b,z,zn,sizes);
      int nsec; cg_nsections(fn,b,z,&nsec);
      for(int s=1;s<=nsec;s++){
        char sn[33]; CGNS_ENUMT(ElementType_t) et; cgsize_t start,end; int nb,parentf;
        cg_section_read(fn,b,z,s,sn,&et,&start,&end,&nb,&parentf);
        int npe=0;
        switch((int)et){case 5:npe=3;break;case 7:npe=4;break;case 3:npe=2;break;default:continue;}
        int nelem=end-start+1;
        std::vector<cgsize_t> conn((size_t)npe*nelem);
        cg_elements_read(fn,b,z,s,conn.data(),nullptr);
        if(et==5||et==7){
          for(int e=0;e<nelem;e++){
            for(int k=0;k<npe;k++) cellVerts.push_back(zoneVMap[zi][conn[(size_t)e*npe+k]-1]);
            cellOff.push_back(cellVerts.size()); cellNVert.push_back(npe);
          }
        } else {
          BCType bct=BCType::Interior; bool found=false;
          for(auto&[fam,bc]:bcMap) if(sn==fam){bct=bc;found=true;break;}
          if(!found) continue;
          for(int e=0;e<nelem;e++){
            int v0=zoneVMap[zi][conn[(size_t)e*2]-1], v1=zoneVMap[zi][conn[(size_t)e*2+1]-1];
            bndPairMap[{std::min(v0,v1),std::max(v0,v1)}]=bct;
          }
        }
      }
    }
  }
  cg_close(fn);
  nCells=cellNVert.size();
  if(nCells==0) throw std::runtime_error("No cells in mesh: "+file);
  // Ensure CCW vertex ordering for correct face normals
  for(int c=0;c<nCells;c++){
    int s=cellOff[c],nv=cellNVert[c];
    double area=0;
    for(int k=0;k<nv;k++){
      int k1=(k+1)%nv;
      area += vx[cellVerts[s+k]]*vy[cellVerts[s+k1]] - vx[cellVerts[s+k1]]*vy[cellVerts[s+k]];
    }
    if(area < 0){ // CW, reverse to CCW
      for(int k=0;k<nv/2;k++) std::swap(cellVerts[s+k], cellVerts[s+nv-1-k]);
    }
  }
  buildFaces();
  for(auto&f:faces){
    if(f.cr==-1){auto it=bndPairMap.find({std::min(f.v0,f.v1),std::max(f.v0,f.v1)});
      f.bc=(it!=bndPairMap.end())?it->second:BCType::Farfield;}
  }
  computeGeometry();
}

void GlobalMesh::buildFaces(){
  if((int)cellOff.size()!=nCells+1){cellOff.clear();cellOff.push_back(0);int a=0;for(int nv:cellNVert){a+=nv;cellOff.push_back(a);}}
  std::unordered_map<uint64_t,int> edgeMap; edgeMap.reserve(nCells*4);
  faces.clear();
  for(int c=0;c<nCells;c++){
    int s=cellOff[c],nv=cellNVert[c];
    for(int k=0;k<nv;k++){
      int v0=cellVerts[s+k], v1=cellVerts[s+(k+1)%nv];
      uint64_t key=(uint64_t)std::min(v0,v1)*(nVert+1)+std::max(v0,v1);
      auto it=edgeMap.find(key);
      if(it==edgeMap.end()){int fi=faces.size();faces.push_back({v0,v1,c,-1,BCType::Interior});edgeMap[key]=fi;}
      else{faces[it->second].cr=c;}
    }
  }
}

void GlobalMesh::computeGeometry(){
  cx.resize(nCells);cy.resize(nCells);cvol.resize(nCells);
  for(int c=0;c<nCells;c++){
    int s=cellOff[c],nv=cellNVert[c];
    double ax=0,ay=0,area=0; double x0=vx[cellVerts[s]],y0=vy[cellVerts[s]];
    for(int k=1;k<nv-1;k++){
      double x1=vx[cellVerts[s+k]],y1=vy[cellVerts[s+k]],x2=vx[cellVerts[s+k+1]],y2=vy[cellVerts[s+k+1]];
      double ta=0.5*((x1-x0)*(y2-y0)-(x2-x0)*(y1-y0));
      area+=ta; ax+=ta*(x0+x1+x2)/3.0; ay+=ta*(y0+y1+y2)/3.0;
    }
    cvol[c]=std::abs(area);
    if(std::abs(area)>1e-30){cx[c]=ax/area;cy[c]=ay/area;}
    else{cx[c]=0;cy[c]=0;for(int k=0;k<nv;k++){cx[c]+=vx[cellVerts[s+k]];cy[c]+=vy[cellVerts[s+k]];}cx[c]/=nv;cy[c]/=nv;}
  }
  int nf=faces.size();
  fcx.resize(nf);fcy.resize(nf);fnx.resize(nf);fny.resize(nf);flen.resize(nf);
  for(int f=0;f<nf;f++){
    int v0=faces[f].v0,v1=faces[f].v1;
    double dx=vx[v1]-vx[v0],dy=vy[v1]-vy[v0]; double L=std::sqrt(dx*dx+dy*dy);
    flen[f]=L; fcx[f]=0.5*(vx[v0]+vx[v1]); fcy[f]=0.5*(vy[v0]+vy[v1]);
    fnx[f]=dy/L; fny[f]=-dx/L;
  }
}

void partitionAndBuildLocal(const GlobalMesh& gm,int rank,int nprocs,LocalMesh& lm,MPI_Comm comm){
  std::vector<idx_t> xadj(gm.nCells+1,0),adjncy;
  for(const auto&f:gm.faces) if(f.cr>=0){xadj[f.cl+1]++;xadj[f.cr+1]++;}
  for(int c=0;c<gm.nCells;c++) xadj[c+1]+=xadj[c];
  adjncy.resize(xadj[gm.nCells]);
  std::vector<idx_t> fill(gm.nCells,0);
  for(const auto&f:gm.faces) if(f.cr>=0){adjncy[xadj[f.cl]+fill[f.cl]++]=f.cr;adjncy[xadj[f.cr]+fill[f.cr]++]=f.cl;}
  std::vector<idx_t> part(gm.nCells);
  idx_t objval=0;
  if(nprocs==1){
    for(int c=0;c<gm.nCells;c++) part[c]=0;
    lm.edgeCut=0;
  } else {
    idx_t nvtxs=gm.nCells,ncon=1,nparts=nprocs;
    idx_t options[METIS_NOPTIONS]; METIS_SetDefaultOptions(options); options[METIS_OPTION_NUMBERING]=0;
    if(METIS_PartGraphKway(&nvtxs,&ncon,xadj.data(),adjncy.data(),nullptr,nullptr,nullptr,&nparts,nullptr,nullptr,options,&objval,part.data())!=METIS_OK)
      throw std::runtime_error("METIS failed");
    lm.edgeCut=objval;
  } lm.nCellsGlobal=gm.nCells; lm.nFacesGlobal=gm.faces.size();
  std::vector<int> g2l(gm.nCells,-1); std::vector<int> owned;
  for(int c=0;c<gm.nCells;c++) if(part[c]==rank){g2l[c]=owned.size();owned.push_back(c);}
  lm.nOwned=owned.size();
  std::vector<int> ghost;
  for(const auto&f:gm.faces){
    if(f.cr<0) continue;
    if(part[f.cl]==rank&&part[f.cr]!=rank&&g2l[f.cr]==-1){g2l[f.cr]=lm.nOwned+ghost.size();ghost.push_back(f.cr);}
    if(part[f.cr]==rank&&part[f.cl]!=rank&&g2l[f.cl]==-1){g2l[f.cl]=lm.nOwned+ghost.size();ghost.push_back(f.cl);}
  }
  lm.nGhost=ghost.size(); lm.nCells=lm.nOwned+lm.nGhost;
  lm.globalCellId.resize(lm.nCells);
  for(int i=0;i<lm.nOwned;i++) lm.globalCellId[i]=owned[i];
  for(int i=0;i<lm.nGhost;i++) lm.globalCellId[lm.nOwned+i]=ghost[i];
  std::vector<int> v2l(gm.nVert,-1);
  auto addV=[&](int gv)->int{if(v2l[gv]==-1){v2l[gv]=lm.vx.size();lm.vx.push_back(gm.vx[gv]);lm.vy.push_back(gm.vy[gv]);}return v2l[gv];};
  lm.cellOff.clear(); lm.cellOff.push_back(0); lm.cellNVert.resize(lm.nCells);
  for(int lc=0;lc<lm.nCells;lc++){
    int gc=lm.globalCellId[lc],s=gm.cellOff[gc],nv=gm.cellNVert[gc];
    lm.cellNVert[lc]=nv;
    for(int k=0;k<nv;k++) lm.cellVerts.push_back(addV(gm.cellVerts[s+k]));
    lm.cellOff.push_back(lm.cellVerts.size());
  }
  lm.faces.clear();
  for(const auto&gf:gm.faces){
    if(gf.cr < 0) {
      // Boundary face: include if left cell is owned
      if(part[gf.cl]!=rank) continue;
      Face lf; lf.v0=addV(gf.v0); lf.v1=addV(gf.v1);
      lf.cl=g2l[gf.cl]; lf.cr=-1; lf.bc=gf.bc;
      lm.faces.push_back(lf);
    } else if(part[gf.cl]==rank) {
      // Interior face: left cell owned, right may be ghost
      Face lf; lf.v0=addV(gf.v0); lf.v1=addV(gf.v1);
      lf.cl=g2l[gf.cl]; lf.cr=g2l[gf.cr]; lf.bc=gf.bc;
      lm.faces.push_back(lf);
    } else if(part[gf.cr]==rank) {
      // Interior face: right cell owned, left is ghost -> swap to make owned the left cell
      Face lf; lf.v0=addV(gf.v1); lf.v1=addV(gf.v0);  // swap vertices to flip normal
      lf.cl=g2l[gf.cr]; lf.cr=g2l[gf.cl]; lf.bc=gf.bc;
      lm.faces.push_back(lf);
    }
  }
  int nc=lm.nCells;
  lm.cx.resize(nc);lm.cy.resize(nc);lm.cvol.resize(nc);
  for(int lc=0;lc<nc;lc++){int gc=lm.globalCellId[lc];lm.cx[lc]=gm.cx[gc];lm.cy[lc]=gm.cy[gc];lm.cvol[lc]=gm.cvol[gc];}
  int nf=lm.faces.size();
  lm.fcx.resize(nf);lm.fcy.resize(nf);lm.fnx.resize(nf);lm.fny.resize(nf);lm.flen.resize(nf);
  for(int f=0;f<nf;f++){int v0=lm.faces[f].v0,v1=lm.faces[f].v1;double dx=lm.vx[v1]-lm.vx[v0],dy=lm.vy[v1]-lm.vy[v0];double L=std::sqrt(dx*dx+dy*dy);lm.flen[f]=L;lm.fcx[f]=0.5*(lm.vx[v0]+lm.vx[v1]);lm.fcy[f]=0.5*(lm.vy[v0]+lm.vy[v1]);lm.fnx[f]=dy/L;lm.fny[f]=-dx/L;}
  lm.cellFaceOff.assign(nc+1,0);
  for(int f=0;f<nf;f++){lm.cellFaceOff[lm.faces[f].cl+1]++;if(lm.faces[f].cr>=0)lm.cellFaceOff[lm.faces[f].cr+1]++;}
  for(int c=0;c<nc;c++)lm.cellFaceOff[c+1]+=lm.cellFaceOff[c];
  lm.cellFaces.resize(lm.cellFaceOff[nc]); std::vector<int> ff(nc,0);
  for(int f=0;f<nf;f++){int cl=lm.faces[f].cl;lm.cellFaces[lm.cellFaceOff[cl]+ff[cl]++]=f;if(lm.faces[f].cr>=0){int cr=lm.faces[f].cr;lm.cellFaces[lm.cellFaceOff[cr]+ff[cr]++]=f;}}
  std::map<int,std::set<int>> recvByRank,sendByRank;
  for(const auto&gf:gm.faces){
    if(gf.cr<0) continue;
    if(part[gf.cl]==rank&&part[gf.cr]!=rank){recvByRank[part[gf.cr]].insert(gf.cr);sendByRank[part[gf.cr]].insert(gf.cl);}
    if(part[gf.cr]==rank&&part[gf.cl]!=rank){recvByRank[part[gf.cl]].insert(gf.cl);sendByRank[part[gf.cl]].insert(gf.cr);}
  }
  lm.neighborRanks.clear();
  for(auto&[nr,cells]:recvByRank){
    lm.neighborRanks.push_back(nr);
    std::vector<int> rl; for(int gc:cells) rl.push_back(g2l[gc]); lm.recvLocal.push_back(rl);
    std::vector<int> sl; for(int gc:sendByRank[nr]) sl.push_back(g2l[gc]); lm.sendLocal.push_back(sl);
  }
  std::vector<int> counts(nprocs,0); for(int c=0;c<gm.nCells;c++) counts[part[c]]++;
  int maxC=*std::max_element(counts.begin(),counts.end());
  double avgC=(double)gm.nCells/nprocs; lm.loadBalance=avgC>0?avgC/maxC:1.0;
}

void LocalMesh::exchangeGhost(Cons* U,MPI_Comm comm,int rank) const {
  int nn=neighborRanks.size(); if(nn==0) return;
  std::vector<std::vector<double>> sb(nn),rb(nn);
  std::vector<MPI_Request> reqs;
  for(int i=0;i<nn;i++){
    sb[i].resize(sendLocal[i].size()*NEQ);
    for(size_t j=0;j<sendLocal[i].size();j++) for(int e=0;e<NEQ;e++) sb[i][j*NEQ+e]=U[sendLocal[i][j]][e];
    rb[i].resize(recvLocal[i].size()*NEQ);
    MPI_Request r; MPI_Irecv(rb[i].data(),rb[i].size()*sizeof(double),MPI_BYTE,neighborRanks[i],200,comm,&r); reqs.push_back(r);
  }
  for(int i=0;i<nn;i++){MPI_Request r;MPI_Isend(sb[i].data(),sb[i].size()*sizeof(double),MPI_BYTE,neighborRanks[i],200,comm,&r);reqs.push_back(r);}
  MPI_Waitall(reqs.size(),reqs.data(),MPI_STATUSES_IGNORE);
  for(int i=0;i<nn;i++) for(size_t j=0;j<recvLocal[i].size();j++){int lc=recvLocal[i][j];for(int e=0;e<NEQ;e++)U[lc][e]=rb[i][j*NEQ+e];}
}

} // namespace cfd2d
