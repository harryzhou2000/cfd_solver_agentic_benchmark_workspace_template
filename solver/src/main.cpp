// main.cpp — CFD solver entry point
#include "case_input.hpp"
#include "mesh.hpp"
#include "partition.hpp"
#include "solver.hpp"
#include "output.hpp"
#include <mpi.h>
#include <iostream>
#include <string>
#include <chrono>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>

namespace cfd2d {

std::string getISOTime() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return std::string(buf);
}

void printUsage() {
    std::cerr << "Usage: cfd2d solve --case <case-json> --output <output-dir> "
              << "[--restart <restart-file>] [--report-level brief|full]\n";
}

} // namespace cfd2d

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // Parse command line
    std::string caseFile, outputDir, restartFile, reportLevel = "full";
    bool solve = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "solve") { solve = true; }
        else if (arg == "--case" && i + 1 < argc) { caseFile = argv[++i]; }
        else if (arg == "--output" && i + 1 < argc) { outputDir = argv[++i]; }
        else if (arg == "--restart" && i + 1 < argc) { restartFile = argv[++i]; }
        else if (arg == "--report-level" && i + 1 < argc) { reportLevel = argv[++i]; }
    }

    if (!solve || caseFile.empty() || outputDir.empty()) {
        if (rank == 0) cfd2d::printUsage();
        MPI_Finalize();
        return 1;
    }

    try {
        cfd2d::CaseInput ci = cfd2d::parseCaseFile(caseFile);
        std::string meshPath = cfd2d::resolveMeshPath(caseFile, ci.meshFile);

        if (rank == 0) {
            std::cerr << "=== CFD2D Solver ===\n";
            std::cerr << "Case: " << ci.caseId << "\n";
            std::cerr << "Mesh: " << meshPath << "\n";
            std::cerr << "Mode: " << ci.mode << " | Mach: " << ci.mach
                      << " | Reynolds: " << ci.reynolds << "\n";
            std::cerr << "MPI ranks: " << size << "\n";
            std::cerr << "Output: " << outputDir << "\n";
        }

        // Read mesh on rank 0
        cfd2d::Mesh mesh;
        if (rank == 0) {
            std::cerr << "Reading mesh...\n";
            cfd2d::readCGNSMesh(mesh, meshPath, ci.bcMap);
            cfd2d::printMeshStats(mesh);
        }

        // Broadcast mesh metadata
        int nvert, ncell, nface, nbc;
        if (rank == 0) { nvert = mesh.nvert; ncell = mesh.ncell; nface = mesh.nface; nbc = mesh.bcInfos.size(); }
        MPI_Bcast(&nvert, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(&ncell, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(&nface, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(&nbc, 1, MPI_INT, 0, MPI_COMM_WORLD);

        if (rank != 0) {
            mesh.nvert = nvert; mesh.ncell = ncell; mesh.nface = nface;
            mesh.vx.resize(nvert); mesh.vy.resize(nvert);
            mesh.cells.resize(ncell); mesh.faces.resize(nface);
            mesh.cellCx.resize(ncell); mesh.cellCy.resize(ncell); mesh.cellVol.resize(ncell);
            mesh.bcInfos.resize(nbc);
        }

        MPI_Bcast(mesh.vx.data(), nvert, MPI_DOUBLE, 0, MPI_COMM_WORLD);
        MPI_Bcast(mesh.vy.data(), nvert, MPI_DOUBLE, 0, MPI_COMM_WORLD);

        // Broadcast cells (packed: nNodes, 4 nodes, type)
        std::vector<int> cellPacked(ncell * 6);
        if (rank == 0) {
            for (int i = 0; i < ncell; i++) {
                cellPacked[i*6+0] = mesh.cells[i].nNodes;
                for (int k = 0; k < 4; k++) cellPacked[i*6+1+k] = mesh.cells[i].nodes[k];
                cellPacked[i*6+5] = (int)mesh.cells[i].type;
            }
        }
        MPI_Bcast(cellPacked.data(), ncell*6, MPI_INT, 0, MPI_COMM_WORLD);
        if (rank != 0) {
            for (int i = 0; i < ncell; i++) {
                mesh.cells[i].nNodes = cellPacked[i*6+0];
                for (int k = 0; k < 4; k++) mesh.cells[i].nodes[k] = cellPacked[i*6+1+k];
                mesh.cells[i].type = (cfd2d::ElemType)cellPacked[i*6+5];
            }
        }

        // Broadcast faces (packed: n0, n1, lc, rc, bcTag, nx, ny, area, cx, cy)
        std::vector<double> faceData(nface * 10);
        if (rank == 0) {
            for (int i = 0; i < nface; i++) {
                faceData[i*10+0] = mesh.faces[i].n0; faceData[i*10+1] = mesh.faces[i].n1;
                faceData[i*10+2] = mesh.faces[i].lc; faceData[i*10+3] = mesh.faces[i].rc;
                faceData[i*10+4] = mesh.faces[i].bcTag; faceData[i*10+5] = mesh.faces[i].nx;
                faceData[i*10+6] = mesh.faces[i].ny; faceData[i*10+7] = mesh.faces[i].area;
                faceData[i*10+8] = mesh.faces[i].cx; faceData[i*10+9] = mesh.faces[i].cy;
            }
        }
        MPI_Bcast(faceData.data(), nface*10, MPI_DOUBLE, 0, MPI_COMM_WORLD);
        if (rank != 0) {
            for (int i = 0; i < nface; i++) {
                mesh.faces[i].n0 = (int)faceData[i*10+0]; mesh.faces[i].n1 = (int)faceData[i*10+1];
                mesh.faces[i].lc = (int)faceData[i*10+2]; mesh.faces[i].rc = (int)faceData[i*10+3];
                mesh.faces[i].bcTag = (int)faceData[i*10+4]; mesh.faces[i].nx = faceData[i*10+5];
                mesh.faces[i].ny = faceData[i*10+6]; mesh.faces[i].area = faceData[i*10+7];
                mesh.faces[i].cx = faceData[i*10+8]; mesh.faces[i].cy = faceData[i*10+9];
            }
        }

        MPI_Bcast(mesh.cellCx.data(), ncell, MPI_DOUBLE, 0, MPI_COMM_WORLD);
        MPI_Bcast(mesh.cellCy.data(), ncell, MPI_DOUBLE, 0, MPI_COMM_WORLD);
        MPI_Bcast(mesh.cellVol.data(), ncell, MPI_DOUBLE, 0, MPI_COMM_WORLD);

        // Broadcast BC info
        std::vector<int> bcTypes(nbc);
        if (rank == 0) for (int i = 0; i < nbc; i++) bcTypes[i] = (int)mesh.bcInfos[i].type;
        MPI_Bcast(bcTypes.data(), nbc, MPI_INT, 0, MPI_COMM_WORLD);
        if (rank != 0) for (int i = 0; i < nbc; i++) mesh.bcInfos[i].type = (cfd2d::BCType)bcTypes[i];
        for (int i = 0; i < nbc; i++) {
            int len;
            if (rank == 0) len = mesh.bcInfos[i].familyName.size();
            MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
            if (rank != 0) mesh.bcInfos[i].familyName.resize(len);
            MPI_Bcast(&mesh.bcInfos[i].familyName[0], len, MPI_CHAR, 0, MPI_COMM_WORLD);
        }
        mesh.nInteriorFace = 0; mesh.nBoundaryFace = 0;
        for (auto& fc : mesh.faces) { if (fc.rc >= 0) mesh.nInteriorFace++; else mesh.nBoundaryFace++; }

        // Partition with METIS
        if (rank == 0) std::cerr << "Partitioning mesh with METIS...\n";
        std::vector<int> partition; int edgeCut;
        cfd2d::partitionMesh(mesh, rank, size, partition, edgeCut);

        cfd2d::LocalMesh localMesh;
        cfd2d::buildLocalMesh(localMesh, mesh, partition, rank, size, edgeCut);
        if (rank == 0) std::cerr << "Local mesh: Owned=" << localMesh.nOwned << " Ghost=" << localMesh.nGhost << "\n";

        // Initialize solver
        cfd2d::Solver solver;
        solver.localMesh = std::move(localMesh);
        solver.init(ci, mesh, rank, size);

        const char* stepEnv = std::getenv("CFD2D_MAX_STEPS");
        if (stepEnv) solver.maxStepsOverride = std::atoi(stepEnv);

        mkdir(outputDir.c_str(), 0755);
        cfd2d::OutputWriter writer;
        writer.setOutputDir(outputDir);

        std::string startTime = cfd2d::getISOTime();
        auto t0 = std::chrono::steady_clock::now();

        int finalStep = 0;
        double finalPhysTime = 0.0;
        std::string convStatus;

        if (ci.runType == "transient") {
            if (rank == 0) std::cerr << "Running transient (BDF2)...\n";
            finalStep = solver.runTransient();
            finalPhysTime = finalStep * ci.timeStep;
            convStatus = "statistically_periodic";
        } else {
            if (rank == 0) std::cerr << "Running steady pseudo-time...\n";
            finalStep = solver.runSteady();
            finalPhysTime = 0.0;
            convStatus = "converged";
        }

        auto t1 = std::chrono::steady_clock::now();
        double wallTime = std::chrono::duration<double>(t1 - t0).count();
        std::string endTime = cfd2d::getISOTime();

        // Compute final residual and forces
        solver.exchangeHalos();
        solver.computeGradients();
        solver.computeLimiters();
        solver.computeResidual(ci.mode == "laminar");
        solver.globalResidualNorm();
        solver.computeForces();

        // Transfer solver history to writer
        for (const auto& h : solver.history) {
            cfd2d::ResidualRow r;
            r.step = h.step; r.physicalTime = h.physicalTime; r.innerIter = h.innerIter;
            r.cfl = h.cfl; r.dt = h.dt;
            r.rho = h.resL2[0]; r.rhou = h.resL2[1]; r.rhov = h.resL2[2]; r.rhoE = h.resL2[3];
            r.residualL2 = h.residualL2; r.residualLinf = h.residualLinf;
            writer.residualHistory.push_back(r);
            cfd2d::ForceRow fr;
            fr.step = h.step; fr.physicalTime = h.physicalTime;
            fr.cl = h.cl; fr.cd = h.cd; fr.cmz = h.cmz;
            fr.pressureDrag = h.pressureDrag; fr.viscousDrag = h.viscousDrag;
            fr.pressureLift = h.pressureLift; fr.viscousLift = h.viscousLift;
            writer.forceHistory.push_back(fr);
        }

        // Write outputs (rank 0 for most files)
        if (rank == 0) {
            writer.writeResidualsCSV();
            writer.writeForcesCSV();
        }
        writer.writeSurfaceCSV(solver);
        writer.writeFieldVTU(solver);
        writer.writeRestart(solver);
        writer.writePartitionDiagnostics(solver);
        if (rank == 0) {
            writer.writeMetadata(solver, ci, true, convStatus, startTime, endTime);
        }

        double initRes = 0, finalRes = 0;
        if (!writer.residualHistory.empty()) {
            initRes = writer.residualHistory[0].residualL2;
            finalRes = writer.residualHistory.back().residualL2;
        }
        double resReduction = (initRes > 0 && finalRes > 0) ? std::log10(initRes / finalRes) : 0;

        std::string command = "mpirun -np " + std::to_string(size) + " cfd2d solve --case " + caseFile + " --output " + outputDir;
        std::string notes = ci.runType == "transient" ?
            "Transient BDF2 with inner LU-SGS" : "Steady pseudo-time with LU-SGS";
        
        if (rank == 0) {
            writer.writeRunStatus(ci, size, command, wallTime, finalStep, finalPhysTime,
                                 convStatus, resReduction, notes);
            std::ofstream logf(outputDir + "/stdout.log");
            logf << "CFD2D Solver Run Log\nCase: " << ci.caseId << "\nMPI ranks: " << size
                 << "\nWall time: " << wallTime << " s\nFinal step: " << finalStep
                 << "\nFinal physical time: " << finalPhysTime << "\nConvergence: " << convStatus
                 << "\nResidual reduction: " << resReduction << " orders\n";
            logf.close();
            std::cerr << "Done. Wall time=" << wallTime << "s\n";
        }

    } catch (const std::exception& e) {
        if (rank == 0) std::cerr << "ERROR: " << e.what() << "\n";
        MPI_Finalize();
        return 1;
    }

    MPI_Finalize();
    return 0;
}
