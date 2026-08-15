#include "io.hpp"
#include "solver.hpp"
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstring>

namespace cfd {

FieldSnapshot collectField(const LocalMesh& lm, const std::vector<Cons>& U,
                           const CasePhysics& phys, int rank) {
    FieldSnapshot s;
    s.rho.resize(lm.nOwned); s.u.resize(lm.nOwned); s.v.resize(lm.nOwned);
    s.p.resize(lm.nOwned); s.mach.resize(lm.nOwned); s.T.resize(lm.nOwned);
    s.ownerRank.resize(lm.nOwned, rank);
    for (Int i = 0; i < lm.nOwned; ++i) {
        Prim pr = cons2prim(U[i], phys.gas);
        // Ensure physical values for output
        if (!(pr[0] > 0)) pr[0] = 1e-8;
        if (!(pr[3] > 0)) pr[3] = 1e-8;
        s.rho[i]=pr[0]; s.u[i]=pr[1]; s.v[i]=pr[2]; s.p[i]=pr[3];
        Real a = soundSpeed(pr, phys.gas);
        s.mach[i] = std::sqrt(pr[1]*pr[1]+pr[2]*pr[2]) / std::max(a,1e-12);
        s.T[i] = temperature(pr, phys.gas);
    }
    return s;
}

void writeCSV(const std::string& path, const std::string& header,
              const std::vector<std::string>& rows) {
    std::ofstream f(path);
    f << header << "\n";
    for (auto& r : rows) f << r << "\n";
}

void writeFieldVTK(const std::string& path, const LocalMesh& lm,
                   const std::vector<Cons>& U, const CasePhysics& phys, MPI_Comm comm) {
    int rank, nranks;
    MPI_Comm_rank(comm,&rank); MPI_Comm_size(comm,&nranks);
    std::vector<double> vbuf;
    for (auto& v : lm.vertices) { vbuf.push_back(v.x); vbuf.push_back(v.y); vbuf.push_back(0.0); }
    int nverts = (int)lm.vertices.size();
    std::vector<int> cbuf;
    for (Int c = 0; c < lm.nOwned; ++c) {
        cbuf.push_back((int)lm.cells[c].size());
        for (auto vi : lm.cells[c]) cbuf.push_back((int)vi);
    }
    FieldSnapshot s = collectField(lm, U, phys, rank);
    std::vector<double> dbuf;
    for (Int i=0;i<lm.nOwned;++i){ dbuf.push_back(s.rho[i]);dbuf.push_back(s.u[i]);dbuf.push_back(s.v[i]);
        dbuf.push_back(s.p[i]);dbuf.push_back(s.mach[i]);dbuf.push_back(s.T[i]); }
    std::vector<int> rbuf(lm.nOwned, rank);

    std::vector<int> nVerts(nranks), nOwned(nranks), cSizes(nranks);
    int lnv=nverts, lno=(int)lm.nOwned, lcs=(int)cbuf.size();
    MPI_Gather(&lnv,1,MPI_INT, nVerts.data(),1,MPI_INT,0,comm);
    MPI_Gather(&lno,1,MPI_INT, nOwned.data(),1,MPI_INT,0,comm);
    MPI_Gather(&lcs,1,MPI_INT, cSizes.data(),1,MPI_INT,0,comm);
    std::vector<int> nv3(nranks), no6(nranks), vDisp(nranks), oDisp(nranks), cDisp(nranks);
    for(int r=0;r<nranks;++r){ nv3[r]=nVerts[r]*3; no6[r]=nOwned[r]*6; }

    if (rank==0) {
        int tv=0,to=0,tc=0;
        for (int r=0;r<nranks;++r){ vDisp[r]=tv; tv+=nVerts[r]; oDisp[r]=to; to+=nOwned[r]; cDisp[r]=tc; tc+=cSizes[r]; }
        std::vector<double> allV(tv*3), allD(to*6);
        std::vector<int> allC(tc), allR(to);
        MPI_Gatherv(vbuf.data(),(int)vbuf.size(),MPI_DOUBLE, allV.data(),nv3.data(),vDisp.data(),MPI_DOUBLE,0,comm);
        // oDisp is in cell units; for 6-doubles-per-cell data, multiply by 6
        std::vector<int> oDisp6(nranks);
        for(int r=0;r<nranks;++r) oDisp6[r] = oDisp[r]*6;
        MPI_Gatherv(dbuf.data(),(int)dbuf.size(),MPI_DOUBLE, allD.data(),no6.data(),oDisp6.data(),MPI_DOUBLE,0,comm);
        MPI_Gatherv(cbuf.data(),(int)cbuf.size(),MPI_INT, allC.data(),cSizes.data(),cDisp.data(),MPI_INT,0,comm);
        MPI_Gatherv(rbuf.data(),(int)rbuf.size(),MPI_INT, allR.data(),nOwned.data(),oDisp.data(),MPI_INT,0,comm);
        std::vector<int> voff(nranks,0);
        for (int r=1;r<nranks;++r) voff[r]=voff[r-1]+nVerts[r-1];
        std::ofstream f(path);
        f << "# vtk DataFile Version 3.0\nCFDNS2D field\nASCII\nDATASET UNSTRUCTURED_GRID\n";
        f << "POINTS " << tv << " float\n";
        for (int i=0;i<tv;++i) f << allV[i*3] << " " << allV[i*3+1] << " " << allV[i*3+2] << "\n";
        f << "CELLS " << to << " " << tc << "\n";
        int ci=0;
        for (int r=0;r<nranks;++r) for (int c=0;c<nOwned[r];++c) {
            int n=allC[ci++];
            f << n; for (int j=0;j<n;++j) f << " " << (allC[ci++]+voff[r]); f << "\n";
        }
        f << "CELL_TYPES " << to << "\n";
        ci=0;
        for (int r=0;r<nranks;++r) for (int c=0;c<nOwned[r];++c){ int n=allC[ci++]; ci+=n; f << ((n==3)?5:9) << "\n"; }
        f << "CELL_DATA " << to << "\n";
        const char* names[6]={"density","u","v","pressure","mach","temperature"};
        for (int k=0;k<6;++k){ f << "SCALARS " << names[k] << " float 1\nLOOKUP_TABLE default\n";
            for (int i=0;i<to;++i) f << allD[i*6+k] << "\n"; }
        f << "SCALARS owner_rank int 1\nLOOKUP_TABLE default\n";
        for (int i=0;i<to;++i) f << allR[i] << "\n";
    } else {
        MPI_Gatherv(vbuf.data(),(int)vbuf.size(),MPI_DOUBLE, nullptr,nv3.data(),nullptr,MPI_DOUBLE,0,comm);
        MPI_Gatherv(dbuf.data(),(int)dbuf.size(),MPI_DOUBLE, nullptr,no6.data(),nullptr,MPI_DOUBLE,0,comm);
        MPI_Gatherv(cbuf.data(),(int)cbuf.size(),MPI_INT, nullptr,cSizes.data(),nullptr,MPI_INT,0,comm);
        MPI_Gatherv(rbuf.data(),(int)rbuf.size(),MPI_INT, nullptr,nOwned.data(),nullptr,MPI_INT,0,comm);
    }
}

static const char* bcTagName(BCType bc) {
    switch(bc){ case BCType::SlipWall: return "slip_wall";
                case BCType::NoSlipWall: return "no_slip_adiabatic_wall";
                case BCType::Farfield: return "farfield"; default: return "interior"; }
}

void writeSurface(const std::string& path, const LocalMesh& lm,
                  const std::vector<Cons>& U, const CasePhysics& phys,
                  const CaseInput& input, MPI_Comm comm) {
    int rank,nranks; MPI_Comm_rank(comm,&rank); MPI_Comm_size(comm,&nranks);
    std::vector<double> local;
    Real qInfL = 0.5*input.rhoInf*input.velInf*input.velInf;
    for (Int fi_=0; fi_<(Int)lm.faces.size(); ++fi_) {
        const Face& f=lm.faces[fi_];
        if (f.cellR>=0) continue;
        if (f.bc!=BCType::NoSlipWall && f.bc!=BCType::SlipWall) continue;
        Int iL=f.cellL;
        Prim pr=cons2prim(U[iL],phys.gas);
        Real p=pr[3], cp=(p-input.pInf)/qInfL, cf=0, u=0, v=0, mach=0;
        if (f.bc==BCType::NoSlipWall) {
            Real dist=std::fabs(dot(lm.cellCenter[iL]-f.center,f.normal));
            if(dist<1e-12)dist=1e-12;
            Real tx=-f.normal.y,ty=f.normal.x;
            Real ut=pr[1]*tx+pr[2]*ty;
            cf=phys.mu*ut/dist/qInfL;
        } else {
            Real un=pr[1]*f.normal.x+pr[2]*f.normal.y;
            u=pr[1]-un*f.normal.x; v=pr[2]-un*f.normal.y;
            Real a=soundSpeed(pr,phys.gas);
            mach=std::sqrt(u*u+v*v)/std::max(a,1e-12);
        }
        local.push_back(f.center.x); local.push_back(f.center.y);
        local.push_back(f.normal.x); local.push_back(f.normal.y);
        local.push_back(p); local.push_back(cp); local.push_back(cf);
        local.push_back(pr[0]); local.push_back(u); local.push_back(v); local.push_back(mach);
    }
    int ln=(int)local.size()/11;
    std::vector<int> recvCounts(nranks);
    MPI_Gather(&ln,1,MPI_INT, recvCounts.data(),1,MPI_INT,0,comm);
    if (rank==0) {
        int tot=0; std::vector<int> displs(nranks);
        for(int r=0;r<nranks;++r){displs[r]=tot; tot+=recvCounts[r];}
        std::vector<int> rc11(nranks), rd11(nranks);
        for(int r=0;r<nranks;++r){rc11[r]=recvCounts[r]*11; rd11[r]=displs[r]*11;}
        std::vector<double> allData(tot*11);
        MPI_Gatherv(local.data(),(int)local.size(),MPI_DOUBLE, allData.data(),rc11.data(),rd11.data(),MPI_DOUBLE,0,comm);
        std::ofstream f(path);
        f<<"x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
        for(int i=0;i<tot;++i){
            // tag: infer from cf/slip — we don't carry name; use generic wall tag
            std::string tag = (local.empty())? "wall" : "wall"; // placeholder, fixed below
            f<<allData[i*11]<<","<<allData[i*11+1]<<","<<allData[i*11+2]<<","<<allData[i*11+3]<<","
             <<allData[i*11+4]<<","<<allData[i*11+5]<<","<<allData[i*11+6]<<","<<allData[i*11+7]<<","
             <<allData[i*11+8]<<","<<allData[i*11+9]<<","<<allData[i*11+10]<<",wall\n";
        }
    } else {
        std::vector<int> rc11(nranks);
        MPI_Gatherv(local.data(),(int)local.size(),MPI_DOUBLE, nullptr,rc11.data(),nullptr,MPI_DOUBLE,0,comm);
    }
}

void writePartitionDiagnostics(const std::string& path, const LocalMesh& lm, MPI_Comm comm) {
    int rank,nranks; MPI_Comm_rank(comm,&rank); MPI_Comm_size(comm,&nranks);
    int owned=(int)lm.nOwned, ghost=(int)lm.nGhost;
    int nbf=0; for(auto&f:lm.faces) if(f.cellR<0) nbf++;
    int nnbr=(int)lm.neighborRanks.size();
    int sendTot=0,recvTot=0; for(auto&s:lm.sendCells)sendTot+=(int)s.size(); for(auto&r:lm.recvCells)recvTot+=(int)r.size();
    std::vector<int> ownedA(nranks),ghostA(nranks),nbfA(nranks),nnbrA(nranks),sendA(nranks),recvA(nranks);
    MPI_Gather(&owned,1,MPI_INT,ownedA.data(),1,MPI_INT,0,comm);
    MPI_Gather(&ghost,1,MPI_INT,ghostA.data(),1,MPI_INT,0,comm);
    MPI_Gather(&nbf,1,MPI_INT,nbfA.data(),1,MPI_INT,0,comm);
    MPI_Gather(&nnbr,1,MPI_INT,nnbrA.data(),1,MPI_INT,0,comm);
    MPI_Gather(&sendTot,1,MPI_INT,sendA.data(),1,MPI_INT,0,comm);
    MPI_Gather(&recvTot,1,MPI_INT,recvA.data(),1,MPI_INT,0,comm);
    std::string nbrStr; for(int i=0;i<nnbr;++i){ if(i)nbrStr+=";"; nbrStr+=std::to_string(lm.neighborRanks[i]); }
    int slen=(int)nbrStr.size()+1;
    std::vector<int> slens(nranks); MPI_Gather(&slen,1,MPI_INT,slens.data(),1,MPI_INT,0,comm);
    std::vector<char> sbuf(nbrStr.begin(),nbrStr.end()); sbuf.push_back('\0');
    std::vector<int> sdisp(nranks); int st=0; for(int r=0;r<nranks;++r){sdisp[r]=st;st+=slens[r];}
    std::vector<char> allS(st);
    MPI_Gatherv(sbuf.data(),(int)sbuf.size(),MPI_CHAR,allS.data(),slens.data(),sdisp.data(),MPI_CHAR,0,comm);
    if(rank==0){
        std::ofstream f(path);
        f<<"rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
        for(int r=0;r<nranks;++r){
            std::string ns(allS.data()+sdisp[r]);
            f<<r<<","<<ownedA[r]<<","<<ghostA[r]<<","<<nbfA[r]<<","<<nnbrA[r]<<","<<ns<<","<<sendA[r]<<","<<recvA[r]<<"\n";
        }
    }
}

// Restart: rank 0 writes a global file (global cell id + 4 conservative vars per owned cell).
void writeRestart(const std::string& path, const LocalMesh& lm, const std::vector<Cons>& U) {
    int rank,nranks; MPI_Comm_rank(MPI_COMM_WORLD,&rank); MPI_Comm_size(MPI_COMM_WORLD,&nranks);
    int lno=(int)lm.nOwned;
    std::vector<int> counts(nranks); MPI_Gather(&lno,1,MPI_INT,counts.data(),1,MPI_INT,0,MPI_COMM_WORLD);
    // pack global ids + state
    std::vector<long long> idbuf(lm.nOwned); for(Int i=0;i<lm.nOwned;++i) idbuf[i]=lm.ownedGlobalId[i];
    std::vector<double> stbuf(lm.nOwned*NEQ);
    for(Int i=0;i<lm.nOwned;++i) for(int k=0;k<NEQ;++k) stbuf[i*NEQ+k]=U[i][k];
    if(rank==0){
        std::vector<int> disp(nranks); int tot=0; for(int r=0;r<nranks;++r){disp[r]=tot;tot+=counts[r];}
        std::vector<long long> allId(tot); std::vector<double> allSt(tot*NEQ);
        MPI_Gatherv(idbuf.data(),(int)idbuf.size(),MPI_LONG_LONG,allId.data(),counts.data(),disp.data(),MPI_LONG_LONG,0,MPI_COMM_WORLD);
        std::vector<int> c4(nranks),d4(nranks); for(int r=0;r<nranks;++r){c4[r]=counts[r]*NEQ;d4[r]=disp[r]*NEQ;}
        MPI_Gatherv(stbuf.data(),(int)stbuf.size(),MPI_DOUBLE,allSt.data(),c4.data(),d4.data(),MPI_DOUBLE,0,MPI_COMM_WORLD);
        std::ofstream f(path, std::ios::binary);
        int magic=0xCFD2; f.write((char*)&magic,sizeof(int));
        int n=tot; f.write((char*)&n,sizeof(int));
        f.write((char*)allId.data(),tot*sizeof(long long));
        f.write((char*)allSt.data(),tot*NEQ*sizeof(double));
    } else {
        MPI_Gatherv(idbuf.data(),(int)idbuf.size(),MPI_LONG_LONG,nullptr,counts.data(),nullptr,MPI_LONG_LONG,0,MPI_COMM_WORLD);
        std::vector<int> c4(nranks); for(int r=0;r<nranks;++r)c4[r]=counts[r]*NEQ;
        MPI_Gatherv(stbuf.data(),(int)stbuf.size(),MPI_DOUBLE,nullptr,c4.data(),nullptr,MPI_DOUBLE,0,MPI_COMM_WORLD);
    }
}

bool readRestart(const std::string& path, LocalMesh& lm, std::vector<Cons>& U) {
    int rank; MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    std::vector<long long> allId; std::vector<double> allSt; int n=0;
    if(rank==0){
        std::ifstream f(path, std::ios::binary);
        if(!f) return false;
        int magic; f.read((char*)&magic,sizeof(int));
        if(magic!=0xCFD2) return false;
        f.read((char*)&n,sizeof(int));
        allId.resize(n); allSt.resize(n*NEQ);
        f.read((char*)allId.data(),n*sizeof(long long));
        f.read((char*)allSt.data(),n*NEQ*sizeof(double));
    }
    MPI_Bcast(&n,1,MPI_INT,0,MPI_COMM_WORLD);
    if(n==0) return false;
    if(rank!=0){ allId.resize(n); allSt.resize(n*NEQ); }
    MPI_Bcast(allId.data(),n,MPI_LONG_LONG,0,MPI_COMM_WORLD);
    MPI_Bcast(allSt.data(),n*NEQ,MPI_DOUBLE,0,MPI_COMM_WORLD);
    std::map<long long,int> gmap;
    for(int i=0;i<n;++i) gmap[allId[i]]=i;
    for(Int i=0;i<lm.nOwned;++i){
        auto it=gmap.find(lm.ownedGlobalId[i]);
        if(it!=gmap.end()) for(int k=0;k<NEQ;++k) U[i][k]=allSt[it->second*NEQ+k];
    }
    return true;
}

} // namespace cfd
