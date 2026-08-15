#include "output.hpp"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <ctime>

namespace fv {

void CsvWriter::open(const std::string& path, const std::string& header, int rank, bool append) {
    if (rank != 0) return;
    if (append) {
        // continue an existing file without rewriting the header
        std::ifstream probe(path);
        bool hasHeader = false;
        if (probe) {
            std::string first;
            std::getline(probe, first);
            hasHeader = (first == header);
        }
        f_.open(path, std::ios::out | std::ios::app);
        if (!f_) throw std::runtime_error("cannot open " + path);
        if (!hasHeader) f_ << header << "\n";
        f_.precision(12);
        return;
    }
    f_.open(path, std::ios::out | std::ios::trunc);
    if (!f_) throw std::runtime_error("cannot open " + path);
    f_ << header << "\n";
    f_.precision(12);
}

void CsvWriter::row(const std::string& line) {
    if (f_) f_ << line << "\n";
}

void CsvWriter::flush() { if (f_) f_.flush(); }

std::string iso_time_now() {
    std::time_t t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

void write_json_file(const std::string& path, const std::string& jsonText) {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << jsonText << "\n";
}

void truncate_csv_to_step(const std::string& path, long startStep) {
    std::ifstream in(path);
    if (!in) return;
    std::string header, line;
    std::getline(in, header);
    std::vector<std::string> kept;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        long step = std::strtol(line.c_str(), nullptr, 10);
        if (step <= startStep) kept.push_back(line);
    }
    in.close();
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    out << header << "\n";
    for (auto& l : kept) out << l << "\n";
}

// ---------- field VTU ----------

void write_field_vtu(const std::string& path, const LocalMesh& m,
                     std::vector<Vec4>& U, Solver& solver,
                     MPI_Comm comm) {
    int rank, size;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    // local primitive data per owned cell
    int nO = m.nOwned;
    std::vector<double> omega;
    solver.computeCellVorticity(U, omega);
    std::vector<double> prim(nO * 8); // rho,u,v,p,mach,T,vorticity,E
    std::vector<long> gids(nO);
    for (int c = 0; c < nO; ++c) {
        Vec4 Uc = U[c];
        double rho = std::max(Uc[IRHO], 1e-30);
        double u = Uc[IRHOU] / rho, v = Uc[IRHOV] / rho;
        double p = (solver.gamma - 1.0) * (Uc[IRHOE] - 0.5 * rho * (u * u + v * v));
        double a = std::sqrt(solver.gamma * std::max(p, 1e-30) / rho);
        prim[c * 8 + 0] = rho;
        prim[c * 8 + 1] = u;
        prim[c * 8 + 2] = v;
        prim[c * 8 + 3] = p;
        prim[c * 8 + 4] = std::hypot(u, v) / a;
        prim[c * 8 + 5] = p / (rho * solver.Rgas);
        prim[c * 8 + 6] = omega[c];
        prim[c * 8 + 7] = Uc[IRHOE] / rho;
        gids[c] = m.cellGid[c];
    }

    // counts
    std::vector<int> nCellsR(size), nNodesR(size);
    int nNodesL = m.nNodes;
    MPI_Gather(&nO, 1, MPI_INT, nCellsR.data(), 1, MPI_INT, 0, comm);
    MPI_Gather(&nNodesL, 1, MPI_INT, nNodesR.data(), 1, MPI_INT, 0, comm);

    // gather connectivity: each cell: npe, node gids...
    std::vector<long> connL;
    std::vector<int> ctypeL;
    for (int c = 0; c < nO; ++c) {
        int nn = m.cellNodeOff[c + 1] - m.cellNodeOff[c];
        connL.push_back(nn);
        for (int k = 0; k < nn; ++k) connL.push_back(m.nodeGid[m.cellNodes[m.cellNodeOff[c] + k]]);
        ctypeL.push_back(nn == 3 ? 5 : 9); // VTK_TRIANGLE / VTK_QUAD
    }
    int connLSize = (int)connL.size();

    // node coords packed with global id
    std::vector<double> nodeBuf(m.nNodes * 2);
    std::vector<long> nodeGidL = m.nodeGid;
    for (int i = 0; i < m.nNodes; ++i) { nodeBuf[2 * i] = m.nodeX[i]; nodeBuf[2 * i + 1] = m.nodeY[i]; }

    std::vector<int> connSizes(size);
    MPI_Gather(&connLSize, 1, MPI_INT, connSizes.data(), 1, MPI_INT, 0, comm);

    // displacements
    std::vector<int> dCells(size + 1, 0), dNodes(size + 1, 0), dConn(size + 1, 0);
    for (int r = 0; r < size; ++r) {
        dCells[r + 1] = dCells[r] + nCellsR[r];
        dNodes[r + 1] = dNodes[r] + nNodesR[r];
        dConn[r + 1] = dConn[r] + connSizes[r];
    }

    std::vector<long> allGid, allConn, allNodeGid;
    std::vector<int> allType;
    std::vector<double> allPrim, allNodeBuf;
    std::vector<int> c8(size), d8(size), c2n(size), d2n(size);
    for (int r = 0; r < size; ++r) {
        c8[r] = nCellsR[r] * 8; d8[r] = dCells[r] * 8;
        c2n[r] = nNodesR[r] * 2; d2n[r] = dNodes[r] * 2;
    }
    if (rank == 0) {
        allGid.resize(dCells[size]);
        allConn.resize(dConn[size]);
        allType.resize(dCells[size]);
        allPrim.resize((size_t)dCells[size] * 8);
        allNodeGid.resize(dNodes[size]);
        allNodeBuf.resize((size_t)dNodes[size] * 2);
    }
    MPI_Gatherv(gids.data(), nO, MPI_LONG, allGid.data(), nCellsR.data(), dCells.data(), MPI_LONG, 0, comm);
    MPI_Gatherv(connL.data(), connLSize, MPI_LONG, allConn.data(), connSizes.data(), dConn.data(), MPI_LONG, 0, comm);
    MPI_Gatherv(ctypeL.data(), nO, MPI_INT, allType.data(), nCellsR.data(), dCells.data(), MPI_INT, 0, comm);
    MPI_Gatherv(prim.data(), nO * 8, MPI_DOUBLE, allPrim.data(), c8.data(), d8.data(), MPI_DOUBLE, 0, comm);
    MPI_Gatherv(nodeGidL.data(), m.nNodes, MPI_LONG, allNodeGid.data(), nNodesR.data(), dNodes.data(), MPI_LONG, 0, comm);
    MPI_Gatherv(nodeBuf.data(), m.nNodes * 2, MPI_DOUBLE, allNodeBuf.data(), c2n.data(), d2n.data(), MPI_DOUBLE, 0, comm);

    if (rank != 0) return;

    // assemble global node table
    std::unordered_map<long, int> g2l;
    std::vector<double> px, py;
    px.reserve(100000); py.reserve(100000);
    auto nodeIndex = [&](long gid, double x, double y) -> int {
        auto it = g2l.find(gid);
        if (it != g2l.end()) return it->second;
        int idx = (int)px.size();
        g2l[gid] = idx;
        px.push_back(x); py.push_back(y);
        return idx;
    };
    // register nodes (they arrive grouped by rank; global ids are unique)
    for (size_t i = 0; i < allNodeGid.size(); ++i)
        nodeIndex(allNodeGid[i], allNodeBuf[2 * i], allNodeBuf[2 * i + 1]);

    // order cells by global id for deterministic output
    std::vector<int> order(allGid.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return allGid[a] < allGid[b]; });

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    out << "<?xml version=\"1.0\"?>\n";
    out << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
    out << "<UnstructuredGrid>\n";
    out << "<Piece NumberOfPoints=\"" << px.size() << "\" NumberOfCells=\"" << allGid.size() << "\">\n";
    out << "<Points>\n<DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    out.precision(12);
    for (size_t i = 0; i < px.size(); ++i) out << px[i] << " " << py[i] << " 0\n";
    out << "</DataArray>\n</Points>\n<Cells>\n";
    out << "<DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n";
    {
        // walk cells in order, reading their connectivity from allConn
        // conn entries per cell: npe, gid0..gid_n; need offsets per gathered cell
        std::vector<long> connOff(allGid.size() + 1, 0);
        for (size_t i = 0; i < allGid.size(); ++i)
            connOff[i + 1] = connOff[i] + allConn[connOff[i]] + 1;
        for (int oi : order) {
            long off = connOff[oi];
            int npe = (int)allConn[off];
            for (int k = 0; k < npe; ++k) {
                long gid = allConn[off + 1 + k];
                out << g2l[gid] << " ";
            }
            out << "\n";
        }
        out << "</DataArray>\n";
        out << "<DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n";
        long acc = 0;
        for (int oi : order) {
            acc += allConn[connOff[oi]];
            out << acc << "\n";
        }
        out << "</DataArray>\n";
        out << "<DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
        for (int oi : order) out << allType[oi] << "\n";
        out << "</DataArray>\n</Cells>\n";
    }
    static const char* names[8] = {"density", "velocity_u", "velocity_v", "pressure", "mach", "temperature", "vorticity", "total_energy"};
    out << "<CellData>\n";
    for (int v = 0; v < 8; ++v) {
        out << "<DataArray type=\"Float64\" Name=\"" << names[v] << "\" format=\"ascii\">\n";
        for (int oi : order) out << allPrim[(size_t)oi * 8 + v] << "\n";
        out << "</DataArray>\n";
    }
    // rank id per cell
    out << "<DataArray type=\"Int32\" Name=\"rank\" format=\"ascii\">\n";
    {
        std::vector<int> cellRank(allGid.size());
        for (int r = 0; r < size; ++r)
            for (int i = dCells[r]; i < dCells[r + 1]; ++i) cellRank[i] = r;
        for (int oi : order) out << cellRank[oi] << "\n";
    }
    out << "</DataArray>\n";
    out << "<DataArray type=\"Int64\" Name=\"global_cell_id\" format=\"ascii\">\n";
    for (int oi : order) out << allGid[oi] << "\n";
    out << "</DataArray>\n";
    out << "</CellData>\n</Piece>\n</UnstructuredGrid>\n</VTKFile>\n";
    out.close();
}

// ---------- surface.csv ----------

void write_surface_csv(const std::string& path, const CaseConfig& cfg, const LocalMesh& m,
                       const std::vector<SurfaceRow>& rows, MPI_Comm comm) {
    int rank, size;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);
    int nL = (int)rows.size();
    std::vector<int> cnts(size);
    MPI_Gather(&nL, 1, MPI_INT, cnts.data(), 1, MPI_INT, 0, comm);
    std::vector<int> disp(size + 1, 0);
    for (int r = 0; r < size; ++r) disp[r + 1] = disp[r] + cnts[r];
    std::vector<double> buf(nL * 11);
    for (int i = 0; i < nL; ++i) {
        const SurfaceRow& r = rows[i];
        double* b = buf.data() + i * 11;
        b[0] = r.x; b[1] = r.y; b[2] = r.nx; b[3] = r.ny; b[4] = r.pressure;
        b[5] = r.cp; b[6] = r.cf; b[7] = r.rho; b[8] = r.u; b[9] = r.v; b[10] = r.mach;
    }
    std::vector<int> tagL(nL);
    for (int i = 0; i < nL; ++i) tagL[i] = rows[i].tag;
    std::vector<double> all;
    std::vector<int> allTag;
    if (rank == 0) { all.resize((size_t)disp[size] * 11); allTag.resize(disp[size]); }
    std::vector<int> cnts11(size), disp11(size);
    for (int r = 0; r < size; ++r) { cnts11[r] = cnts[r] * 11; disp11[r] = disp[r] * 11; }
    MPI_Gatherv(buf.data(), nL * 11, MPI_DOUBLE, all.data(), cnts11.data(), disp11.data(), MPI_DOUBLE, 0, comm);
    MPI_Gatherv(tagL.data(), nL, MPI_INT, allTag.data(), cnts.data(), disp.data(), MPI_INT, 0, comm);
    if (rank != 0) return;
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    out.precision(12);
    out << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    for (int i = 0; i < disp[size]; ++i) {
        double* b = all.data() + (size_t)i * 11;
        out << b[0] << "," << b[1] << "," << b[2] << "," << b[3] << "," << b[4] << ","
            << b[5] << "," << b[6] << "," << b[7] << "," << b[8] << "," << b[9] << ","
            << b[10] << "," << m.tagNames[allTag[i]] << "\n";
    }
}

// ---------- partition diagnostics ----------

void write_partition_diagnostics(const std::string& path, const LocalMesh& m, MPI_Comm comm) {
    int rank, size;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);
    // per-rank records assembled by rank 0
    struct Rec {
        int rank, owned, ghost, bdry, nnb;
        std::string nbs;
        long send, recv;
    };
    int nnb = (int)m.sendRanks.size();
    std::string nbs;
    for (int i = 0; i < nnb; ++i) {
        if (i) nbs += ";";
        nbs += std::to_string(m.sendRanks[i]);
    }
    int nbsLen = (int)nbs.size();
    std::vector<int> lens(size);
    MPI_Gather(&nbsLen, 1, MPI_INT, lens.data(), 1, MPI_INT, 0, comm);
    std::vector<int> disp(size + 1, 0);
    for (int r = 0; r < size; ++r) disp[r + 1] = disp[r] + lens[r];
    std::vector<char> allNbs(rank == 0 ? disp[size] : 1);
    std::vector<int> dispv(size), cntsv(size);
    for (int r = 0; r < size; ++r) { dispv[r] = disp[r]; cntsv[r] = lens[r]; }
    MPI_Gatherv(nbs.data(), nbsLen, MPI_CHAR, allNbs.data(), cntsv.data(), dispv.data(), MPI_CHAR, 0, comm);

    int ints[5] = {rank, m.nOwned, m.nGhost, (int)m.bfaces.size(), nnb};
    long longs[2] = {(long)m.sendCells.size(), (long)m.recvCells.size()};
    std::vector<int> allInts(rank == 0 ? size * 5 : 0);
    std::vector<long> allLongs(rank == 0 ? size * 2 : 0);
    MPI_Gather(ints, 5, MPI_INT, allInts.data(), 5, MPI_INT, 0, comm);
    MPI_Gather(longs, 2, MPI_LONG, allLongs.data(), 2, MPI_LONG, 0, comm);
    if (rank != 0) return;

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    out << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
    for (int r = 0; r < size; ++r) {
        std::string nb(allNbs.data() + disp[r], lens[r]);
        out << allInts[r * 5 + 0] << "," << allInts[r * 5 + 1] << "," << allInts[r * 5 + 2] << ","
            << allInts[r * 5 + 3] << "," << allInts[r * 5 + 4] << ",\"" << nb << "\","
            << allLongs[r * 2 + 0] << "," << allLongs[r * 2 + 1] << "\n";
    }
}

// ---------- restart ----------

void write_restart(const std::string& path, const LocalMesh& m,
                   const std::vector<Vec4>& U, const std::vector<Vec4>& Un,
                   const std::vector<Vec4>& Unm1, long step, double time, MPI_Comm comm) {
    int rank, size;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);
    int nO = m.nOwned;
    std::vector<int> cnts(size), disp(size + 1, 0);
    MPI_Gather(&nO, 1, MPI_INT, cnts.data(), 1, MPI_INT, 0, comm);
    for (int r = 0; r < size; ++r) disp[r + 1] = disp[r] + cnts[r];
    std::vector<long> gid(nO);
    for (int c = 0; c < nO; ++c) gid[c] = m.cellGid[c];
    std::vector<long> allGid(rank == 0 ? m.nCellsGlobal : 0);
    MPI_Gatherv(gid.data(), nO, MPI_LONG, allGid.data(), cnts.data(), disp.data(), MPI_LONG, 0, comm);
    auto gatherState = [&](const std::vector<Vec4>& S, std::vector<double>& all) {
        std::vector<double> loc(nO * NVAR);
        for (int c = 0; c < nO; ++c)
            for (int k = 0; k < NVAR; ++k) loc[c * NVAR + k] = S[c][k];
        if (rank == 0) all.resize((size_t)m.nCellsGlobal * NVAR);
        std::vector<int> c4(size), d4(size);
        for (int r = 0; r < size; ++r) { c4[r] = cnts[r] * NVAR; d4[r] = disp[r] * NVAR; }
        MPI_Gatherv(loc.data(), nO * NVAR, MPI_DOUBLE, all.data(), c4.data(), d4.data(), MPI_DOUBLE, 0, comm);
    };
    std::vector<double> allU, allUn, allUnm1;
    gatherState(U, allU);
    bool hasHist = !Un.empty();
    int histFlag = hasHist ? 1 : 0;
    MPI_Bcast(&histFlag, 1, MPI_INT, 0, comm);
    if (hasHist) { gatherState(Un, allUn); gatherState(Unm1, allUnm1); }
    if (rank != 0) return;

    // reorder into global-cell order
    std::vector<double> Uo(m.nCellsGlobal * NVAR), Uno, Unm1o;
    if (hasHist) { Uno.resize(m.nCellsGlobal * NVAR); Unm1o.resize(m.nCellsGlobal * NVAR); }
    for (long i = 0; i < m.nCellsGlobal; ++i) {
        long g = allGid[i];
        for (int k = 0; k < NVAR; ++k) {
            Uo[g * NVAR + k] = allU[i * NVAR + k];
            if (hasHist) {
                Uno[g * NVAR + k] = allUn[i * NVAR + k];
                Unm1o[g * NVAR + k] = allUnm1[i * NVAR + k];
            }
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write("FVR1", 4);
    out.write(reinterpret_cast<const char*>(&step), 8);
    out.write(reinterpret_cast<const char*>(&time), 8);
    long nc = m.nCellsGlobal;
    out.write(reinterpret_cast<const char*>(&nc), 8);
    out.write(reinterpret_cast<const char*>(&histFlag), 4);
    out.write(reinterpret_cast<const char*>(Uo.data()), Uo.size() * 8);
    if (hasHist) {
        out.write(reinterpret_cast<const char*>(Uno.data()), Uno.size() * 8);
        out.write(reinterpret_cast<const char*>(Unm1o.data()), Unm1o.size() * 8);
    }
}

bool read_restart(const std::string& path, const LocalMesh& m,
                  std::vector<Vec4>& U, std::vector<Vec4>& Un, std::vector<Vec4>& Unm1,
                  long& step, double& time, MPI_Comm comm) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char magic[4];
    in.read(magic, 4);
    if (std::memcmp(magic, "FVR1", 4) != 0) return false;
    long nc;
    int histFlag;
    in.read(reinterpret_cast<char*>(&step), 8);
    in.read(reinterpret_cast<char*>(&time), 8);
    in.read(reinterpret_cast<char*>(&nc), 8);
    in.read(reinterpret_cast<char*>(&histFlag), 4);
    if (nc != m.nCellsGlobal) return false;
    std::vector<double> all(nc * NVAR);
    in.read(reinterpret_cast<char*>(all.data()), nc * NVAR * 8);
    std::vector<double> allUn, allUnm1;
    if (histFlag) {
        allUn.resize(nc * NVAR); allUnm1.resize(nc * NVAR);
        in.read(reinterpret_cast<char*>(allUn.data()), nc * NVAR * 8);
        in.read(reinterpret_cast<char*>(allUnm1.data()), nc * NVAR * 8);
    }
    in.close();
    for (int c = 0; c < m.nOwned; ++c) {
        long g = m.cellGid[c];
        for (int k = 0; k < NVAR; ++k) U[c][k] = all[g * NVAR + k];
    }
    if (histFlag && !Un.empty()) {
        for (int c = 0; c < m.nOwned; ++c) {
            long g = m.cellGid[c];
            for (int k = 0; k < NVAR; ++k) {
                Un[c][k] = allUn[g * NVAR + k];
                Unm1[c][k] = allUnm1[g * NVAR + k];
            }
        }
    }
    // fill ghosts via a following exchange by the caller
    MPI_Barrier(comm);
    return true;
}

} // namespace fv
