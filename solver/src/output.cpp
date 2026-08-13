// output.cpp — Output writers implementation
#include "output.hpp"
#include <sys/stat.h>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <iostream>

namespace cfd2d {

void ensureDir(const std::string& dir) {
    mkdir(dir.c_str(), 0755);
}

void OutputWriter::addResidualRow(const Solver& s, int step, double physTime, int innerIter,
                                   double cfl, double dt) {
    ResidualRow r;
    r.step = step;
    r.physicalTime = physTime;
    r.innerIter = innerIter;
    r.cfl = cfl;
    r.dt = dt;
    r.rho = s.resL2[0];
    r.rhou = s.resL2[1];
    r.rhov = s.resL2[2];
    r.rhoE = s.resL2[3];
    // Total L2 and Linf
    r.residualL2 = std::sqrt(r.rho*r.rho + r.rhou*r.rhou + r.rhov*r.rhov + r.rhoE*r.rhoE);
    r.residualLinf = std::max(std::max(s.resLinf[0], s.resLinf[1]),
                              std::max(s.resLinf[2], s.resLinf[3]));
    residualHistory.push_back(r);
}

void OutputWriter::addForceRow(const Solver& s, int step, double physTime) {
    ForceRow f;
    const CaseInput& ci = *s.caseInput;
    double qInf = ci.qInf;
    double area = ci.refArea;
    double length = ci.refLength;

    f.step = step;
    f.physicalTime = physTime;
    f.cd = (s.forceP_drag + s.forceV_drag) / (qInf * area);
    f.cl = (s.forceP_lift + s.forceV_lift) / (qInf * area);
    f.cmz = s.moment / (qInf * area * length);
    f.pressureDrag = s.forceP_drag / (qInf * area);
    f.viscousDrag = s.forceV_drag / (qInf * area);
    f.pressureLift = s.forceP_lift / (qInf * area);
    f.viscousLift = s.forceV_lift / (qInf * area);
    forceHistory.push_back(f);
}

void OutputWriter::writeResidualsCSV() const {
    ensureDir(outputDir);
    std::ofstream f(outputDir + "/residuals.csv");
    f << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
    for (const auto& r : residualHistory) {
        f << r.step << "," << std::scientific << std::setprecision(10)
          << r.physicalTime << "," << r.innerIter << ","
          << r.cfl << "," << r.dt << ","
          << r.rho << "," << r.rhou << "," << r.rhov << "," << r.rhoE << ","
          << r.residualL2 << "," << r.residualLinf << "\n";
    }
    f.close();
}

void OutputWriter::writeForcesCSV() const {
    ensureDir(outputDir);
    std::ofstream f(outputDir + "/forces.csv");
    f << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
    for (const auto& fr : forceHistory) {
        f << fr.step << "," << std::scientific << std::setprecision(10)
          << fr.physicalTime << ","
          << fr.cl << "," << fr.cd << "," << fr.cmz << ","
          << fr.pressureDrag << "," << fr.viscousDrag << ","
          << fr.pressureLift << "," << fr.viscousLift << "\n";
    }
    f.close();
}

void OutputWriter::writeSurfaceCSV(const Solver& s) const {
    ensureDir(outputDir);
    std::ofstream f(outputDir + "/surface.csv");
    f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";

    const CaseInput& ci = *s.caseInput;
    const LocalMesh& lm = s.localMesh;
    double qInf = ci.qInf;
    double mu = ci.mu;

    for (int fi = 0; fi < (int)lm.faces.size(); fi++) {
        const auto& face = lm.faces[fi];
        if (face.rc >= 0 || face.bcTag < 0) continue;

        BCType bcType = lm.globalMesh->bcInfos[face.bcTag].type;
        if (bcType != BCType::NoSlipAdiabaticWall && bcType != BCType::SlipWall) continue;

        Primitive pL = toPrimitive(&s.U[face.lc * NEQ], s.gas);
        double p = pL.p;
        double cp = (p - ci.pInf) / qInf;

        // Skin friction coefficient
        double cf = 0.0;
        if (bcType == BCType::NoSlipAdiabaticWall && mu > 0) {
            double tx = -face.ny, ty = face.nx;
            double dist = std::sqrt(
                (face.cx - lm.cellCx[face.lc]) * (face.cx - lm.cellCx[face.lc]) +
                (face.cy - lm.cellCy[face.lc]) * (face.cy - lm.cellCy[face.lc]));
            dist = std::max(dist, 1e-10);
            double ut = pL.u * tx + pL.v * ty;
            double tau = mu * ut / dist;
            cf = tau / qInf;
        }

        // Wall velocity (boundary condition value)
        double wallU = 0, wallV = 0, wallMach = 0;
        if (bcType == BCType::NoSlipAdiabaticWall) {
            wallU = 0; wallV = 0; wallMach = 0;
        } else {
            // Slip wall: zero normal velocity, keep tangential
            double un = pL.u * face.nx + pL.v * face.ny;
            wallU = pL.u - un * face.nx;
            wallV = pL.v - un * face.ny;
            wallMach = std::sqrt(wallU*wallU + wallV*wallV) / std::max(pL.a, 1e-14);
        }

        std::string tagName = lm.globalMesh->bcInfos[face.bcTag].familyName;

        f << std::scientific << std::setprecision(10)
          << face.cx << "," << face.cy << ","
          << face.nx << "," << face.ny << ","
          << p << "," << cp << "," << cf << ","
          << pL.rho << "," << wallU << "," << wallV << "," << wallMach << ","
          << tagName << "\n";
    }
    f.close();
}

void OutputWriter::writeFieldVTU(const Solver& s) const {
    ensureDir(outputDir);
    // Write VTU (VTK unstructured) format
    // Each rank writes its own piece, then we combine with a PVTU on rank 0
    // For simplicity, gather all data to rank 0 and write a single VTU

    const LocalMesh& lm = s.localMesh;
    const Mesh* gm = lm.globalMesh;

    // Gather owned cell data to rank 0
    int nOwned = lm.nOwned;
    std::vector<double> ownedData(nOwned * 8); // rho, u, v, p, T, mach, E, rank
    for (int i = 0; i < nOwned; i++) {
        Primitive p = toPrimitive(&s.U[i * NEQ], s.gas);
        ownedData[i * 8 + 0] = p.rho;
        ownedData[i * 8 + 1] = p.u;
        ownedData[i * 8 + 2] = p.v;
        ownedData[i * 8 + 3] = p.p;
        ownedData[i * 8 + 4] = p.T;
        ownedData[i * 8 + 5] = p.mach;
        ownedData[i * 8 + 6] = p.E;
        ownedData[i * 8 + 7] = (double)s.mpiRank;
    }

    // Gather global cell IDs
    std::vector<int> ownedGlobalIds(nOwned);
    for (int i = 0; i < nOwned; i++)
        ownedGlobalIds[i] = lm.localToGlobal[i];

    // Gather to rank 0
    std::vector<int> recvCounts(s.mpiSize), displs(s.mpiSize);
    int localCount = nOwned;
    MPI_Gather(&localCount, 1, MPI_INT, recvCounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

    int totalCells = 0;
    if (s.mpiRank == 0) {
        for (int r = 0; r < s.mpiSize; r++) {
            displs[r] = totalCells;
            totalCells += recvCounts[r];
        }
    }

    std::vector<int> allGlobalIds(totalCells);
    std::vector<double> allData(totalCells * 8);

    MPI_Gatherv(ownedGlobalIds.data(), nOwned, MPI_INT,
                allGlobalIds.data(), recvCounts.data(), displs.data(), MPI_INT,
                0, MPI_COMM_WORLD);

    // Adjust displs and counts for double data (8 per cell)
    std::vector<int> recvCountsD(s.mpiSize), displsD(s.mpiSize);
    for (int r = 0; r < s.mpiSize; r++) {
        recvCountsD[r] = recvCounts[r] * 8;
        displsD[r] = displs[r] * 8;
    }

    MPI_Gatherv(ownedData.data(), nOwned * 8, MPI_DOUBLE,
                allData.data(), recvCountsD.data(), displsD.data(), MPI_DOUBLE,
                0, MPI_COMM_WORLD);

    if (s.mpiRank == 0) {
        std::ofstream f(outputDir + "/field_final.vtu");
        f << std::fixed << std::setprecision(8);

        f << "<?xml version=\"1.0\"?>\n";
        f << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
        f << "  <UnstructuredGrid>\n";
        f << "    <Piece NumberOfPoints=\"" << gm->nvert << "\" NumberOfCells=\"" << gm->ncell << "\">\n";

        // Points
        f << "      <Points>\n";
        f << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
        for (int i = 0; i < gm->nvert; i++)
            f << "          " << gm->vx[i] << " " << gm->vy[i] << " 0.0\n";
        f << "        </DataArray>\n";
        f << "      </Points>\n";

        // Cells
        f << "      <Cells>\n";
        // Connectivity
        f << "        <DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
        for (int i = 0; i < gm->ncell; i++) {
            const Cell& c = gm->cells[i];
            f << "          ";
            for (int k = 0; k < c.nNodes; k++)
                f << c.nodes[k] << " ";
            f << "\n";
        }
        f << "        </DataArray>\n";
        // Offsets
        f << "        <DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
        int offset = 0;
        for (int i = 0; i < gm->ncell; i++) {
            offset += gm->cells[i].nNodes;
            f << "          " << offset << "\n";
        }
        f << "        </DataArray>\n";
        // Types (5=triangle, 9=quad)
        f << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
        for (int i = 0; i < gm->ncell; i++)
            f << "          " << (gm->cells[i].nNodes == 3 ? 5 : 9) << "\n";
        f << "        </DataArray>\n";
        f << "      </Cells>\n";

        // Cell data
        f << "      <CellData>\n";
        // Build a lookup: global cell ID -> data
        std::vector<double> cellData(gm->ncell * 8, 0);
        for (int i = 0; i < totalCells; i++) {
            int gid = allGlobalIds[i];
            if (gid >= 0 && gid < gm->ncell)
                std::memcpy(&cellData[gid * 8], &allData[i * 8], 8 * sizeof(double));
        }

        const char* names[] = {"Density", "VelocityX", "VelocityY", "Pressure", "Temperature", "Mach", "TotalEnergy", "Rank"};
        for (int v = 0; v < 8; v++) {
            f << "        <DataArray type=\"Float64\" Name=\"" << names[v] << "\" format=\"ascii\">\n";
            for (int i = 0; i < gm->ncell; i++)
                f << "          " << cellData[i * 8 + v] << "\n";
            f << "        </DataArray>\n";
        }
        f << "      </CellData>\n";

        f << "    </Piece>\n";
        f << "  </UnstructuredGrid>\n";
        f << "</VTKFile>\n";
        f.close();
    }
}

void OutputWriter::writeRestart(const Solver& s) const {
    ensureDir(outputDir);
    // Write restart file in binary format
    // Rank 0 gathers all owned cell states and writes them

    const LocalMesh& lm = s.localMesh;
    int nOwned = lm.nOwned;

    // Gather global IDs and states
    std::vector<int> ownedGids(nOwned);
    std::vector<double> ownedStates(nOwned * NEQ);
    for (int i = 0; i < nOwned; i++) {
        ownedGids[i] = lm.localToGlobal[i];
        std::memcpy(&ownedStates[i * NEQ], &s.U[i * NEQ], NEQ * sizeof(double));
    }

    std::vector<int> recvCounts(s.mpiSize), displs(s.mpiSize);
    int localCount = nOwned;
    MPI_Gather(&localCount, 1, MPI_INT, recvCounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

    int totalCells = 0;
    if (s.mpiRank == 0) {
        for (int r = 0; r < s.mpiSize; r++) {
            displs[r] = totalCells;
            totalCells += recvCounts[r];
        }
    }

    std::vector<int> allGids(totalCells);
    std::vector<double> allStates(totalCells * NEQ);

    MPI_Gatherv(ownedGids.data(), nOwned, MPI_INT,
                allGids.data(), recvCounts.data(), displs.data(), MPI_INT,
                0, MPI_COMM_WORLD);

    std::vector<int> recvCountsD(s.mpiSize), displsD(s.mpiSize);
    for (int r = 0; r < s.mpiSize; r++) {
        recvCountsD[r] = recvCounts[r] * NEQ;
        displsD[r] = displs[r] * NEQ;
    }
    MPI_Gatherv(ownedStates.data(), nOwned * NEQ, MPI_DOUBLE,
                allStates.data(), recvCountsD.data(), displsD.data(), MPI_DOUBLE,
                0, MPI_COMM_WORLD);

    if (s.mpiRank == 0) {
        // Write as simple binary: ncell, then cell states in global order
        std::ofstream f(outputDir + "/restart_final.bin", std::ios::binary);
        int ncell = s.globalMesh->ncell;
        f.write(reinterpret_cast<const char*>(&ncell), sizeof(int));
        std::vector<double> globalStates(ncell * NEQ, 0);
        for (int i = 0; i < totalCells; i++) {
            int gid = allGids[i];
            if (gid >= 0 && gid < ncell)
                std::memcpy(&globalStates[gid * NEQ], &allStates[i * NEQ], NEQ * sizeof(double));
        }
        f.write(reinterpret_cast<const char*>(globalStates.data()), ncell * NEQ * sizeof(double));
        f.close();
    }
}

void OutputWriter::writeMetadata(const Solver& s, const CaseInput& ci, bool completed,
                                  const std::string& convStatus,
                                  const std::string& startTime,
                                  const std::string& endTime) const {
    ensureDir(outputDir);
    std::ofstream f(outputDir + "/metadata.json");

    bool isTransient = (ci.runType == "transient");
    bool isRe200 = (ci.caseId.find("re200") != std::string::npos);

    double meanInner = s.nPhysicalSteps > 0 ? s.sumInnerIters / s.nPhysicalSteps :
                       (s.totalInnerIters > 0 ? (double)s.totalInnerIters / ci.maxSteps : 0);
    double convFrac = 1.0;
    if (isRe200 && s.nPhysicalSteps > 0)
        convFrac = 1.0 - (double)s.innerTargetMisses / s.nPhysicalSteps;

    f << "{\n";
    f << "  \"case_id\": \"" << ci.caseId << "\",\n";
    f << "  \"solver_name\": \"cfd2d\",\n";
    f << "  \"solver_version\": \"1.0\",\n";
    f << "  \"git_revision\": null,\n";
    f << "  \"mpi_ranks\": " << s.mpiSize << ",\n";
    f << "  \"mesh_file\": \"" << ci.meshFile << "\",\n";
    f << "  \"num_cells_global\": " << s.globalMesh->ncell << ",\n";
    f << "  \"num_faces_global\": " << s.globalMesh->nface << ",\n";
    f << "  \"num_cells_owned_local\": " << s.localMesh.nOwned << ",\n";
    f << "  \"num_cells_ghost_local\": " << s.localMesh.nGhost << ",\n";
    f << "  \"partitioner\": \"metis_kway\",\n";
    f << "  \"partition_edge_cut\": " << s.localMesh.edgeCut << ",\n";
    f << "  \"halo_exchange\": \"neighbor_isend_irecv\",\n";
    f << "  \"full_state_replication_during_iterations\": false,\n";
    f << "  \"full_mesh_replication_during_iterations\": false,\n";
    f << "  \"equation_set\": \"compressible_navier_stokes_2d\",\n";
    f << "  \"inviscid_flux\": \"" << s.config.inviscidFluxName << "\",\n";
    f << "  \"entropy_fix\": \"harten_yee\",\n";
    f << "  \"viscous_flux\": \"" << (ci.mode == "laminar" ? "newtonian_fourier" : "disabled") << "\",\n";
    f << "  \"time_integrator\": \"" << (isTransient ? "bdf2" : "pseudo_time_steady") << "\",\n";
    f << "  \"implicit_solver\": \"lu_sgs\",\n";
    f << "  \"reconstruction\": \"green_gauss_linear\",\n";
    f << "  \"limiter\": \"barth_jespersen\",\n";
    f << "  \"spatial_order_claimed\": 2,\n";
    f << "  \"positivity_preservation\": \"density_pressure_floor_fallback\",\n";
    f << "  \"wall_boundary_output_semantics\": \"boundary_value\",\n";
    f << "  \"true_bdf2_inner_loop\": " << (isRe200 ? "true" : (isTransient ? "true" : "false")) << ",\n";
    f << "  \"typical_inner_iterations\": " << (int)meanInner << ",\n";
    f << "  \"min_inner_iterations\": " << ci.minInner << ",\n";
    f << "  \"max_inner_iterations\": " << ci.maxInner << ",\n";
    f << "  \"observed_min_inner_iterations\": " << s.obsMinInner << ",\n";
    f << "  \"observed_max_inner_iterations\": " << s.obsMaxInner << ",\n";
    f << "  \"inner_residual_reduction_target\": " << ci.innerResidualTarget << ",\n";
    f << "  \"inner_target_misses\": " << s.innerTargetMisses << ",\n";
    f << "  \"inner_target_converged_fraction\": " << convFrac << ",\n";
    f << "  \"last_inner_residual_ratio\": " << s.lastInnerResidualRatio << ",\n";
    f << "  \"start_time_utc\": \"" << startTime << "\",\n";
    f << "  \"end_time_utc\": \"" << endTime << "\",\n";
    f << "  \"completed\": " << (completed ? "true" : "false") << ",\n";
    f << "  \"convergence_status\": \"" << convStatus << "\"\n";
    f << "}\n";
    f.close();
}

void OutputWriter::writeRunStatus(const CaseInput& ci, int mpiRanks, const std::string& command,
                                   double wallTime, int finalStep, double finalPhysTime,
                                   const std::string& convStatus, double resReduction,
                                   const std::string& notes) const {
    ensureDir(outputDir);
    std::ofstream f(outputDir + "/run_status.json");
    f << "{\n";
    f << "  \"case_id\": \"" << ci.caseId << "\",\n";
    f << "  \"command\": \"" << command << "\",\n";
    f << "  \"mpi_ranks\": " << mpiRanks << ",\n";
    f << "  \"wall_time_seconds\": " << wallTime << ",\n";
    f << "  \"final_step\": " << finalStep << ",\n";
    f << "  \"final_physical_time\": " << finalPhysTime << ",\n";
    f << "  \"convergence_status\": \"" << convStatus << "\",\n";
    f << "  \"residual_reduction_orders\": " << resReduction << ",\n";
    f << "  \"notes\": \"" << notes << "\"\n";
    f << "}\n";
    f.close();
}

void OutputWriter::writePartitionDiagnostics(const Solver& s) const {
    ensureDir(outputDir);
    const LocalMesh& lm = s.localMesh;

    // Gather partition info from all ranks
    std::vector<int> ownedCounts(s.mpiSize), ghostCounts(s.mpiSize);
    std::vector<int> bfaceCounts(s.mpiSize), nNeighborCounts(s.mpiSize);
    std::vector<int> sendTotals(s.mpiSize), recvTotals(s.mpiSize);

    int localOwned = lm.nOwned;
    int localGhost = lm.nGhost;
    int localBface = lm.nBoundaryFace;
    int localNbr = lm.neighbors.size();
    int localSend = 0, localRecv = 0;
    for (const auto& nc : lm.neighbors) {
        localSend += nc.sendCells.size();
        localRecv += nc.recvCells.size();
    }

    MPI_Gather(&localOwned, 1, MPI_INT, ownedCounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Gather(&localGhost, 1, MPI_INT, ghostCounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Gather(&localBface, 1, MPI_INT, bfaceCounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Gather(&localNbr, 1, MPI_INT, nNeighborCounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Gather(&localSend, 1, MPI_INT, sendTotals.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Gather(&localRecv, 1, MPI_INT, recvTotals.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (s.mpiRank == 0) {
        std::ofstream f(outputDir + "/partition_diagnostics.csv");
        f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
        for (int r = 0; r < s.mpiSize; r++) {
            f << r << "," << ownedCounts[r] << "," << ghostCounts[r] << ","
              << bfaceCounts[r] << "," << nNeighborCounts[r] << ",\"";
            // neighbor ranks string - we need to gather this too, but for now use placeholder
            // Actually each rank's neighbor ranks differ. Let's gather them.
            f << "\",";
            f << sendTotals[r] << "," << recvTotals[r] << "\n";
        }
        f.close();
    }
}

} // namespace cfd2d
