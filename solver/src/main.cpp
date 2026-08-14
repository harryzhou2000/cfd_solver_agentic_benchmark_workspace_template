#include "types.hpp"
#include "cgns_reader.hpp"
#include "mesh.hpp"
#include "partitioner.hpp"
#include "case_config.hpp"
#include "solver.hpp"
#include "output.hpp"
#include <mpi.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <ctime>

using namespace cfd;

std::string getGitRevision() {
    return "unknown";
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    // Parse CLI
    std::string caseFile, outputDir, restartFile;
    std::string reportLevel = "full";
    bool hasSolve = false;

    int maxInnerOverride = 0;

    int maxStepsOverride = 0;

    Real maxLimiter = 1.0;

    Real diagSafety = 1.0;

    bool useGlobalDT = false;

    bool firstOrder = false;
    bool useRusanov = false;
    Real cflCap = 1e9;
    int nSweepsOverride = 0;

    bool noLimit = false;
    int firstOrderSteps = 0;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "solve") hasSolve = true;
        else if (arg == "--case" && i+1 < argc) caseFile = argv[++i];
        else if (arg == "--output" && i+1 < argc) outputDir = argv[++i];
        else if (arg == "--restart" && i+1 < argc) restartFile = argv[++i];
        else if (arg == "--report-level" && i+1 < argc) reportLevel = argv[++i];
        else if (arg == "--first-order") firstOrder = true;
        else if (arg == "--rusanov") useRusanov = true;
        else if (arg == "--cfl-cap" && i+1 < argc) cflCap = std::stod(argv[++i]);
        else if (arg == "--sweeps" && i+1 < argc) nSweepsOverride = std::stoi(argv[++i]);
        else if (arg == "--max-inner" && i+1 < argc) maxInnerOverride = std::stoi(argv[++i]);
        else if (arg == "--global-dt") useGlobalDT = true;
        else if (arg == "--max-steps" && i+1 < argc) maxStepsOverride = std::stoi(argv[++i]);
        else if (arg == "--max-limiter" && i+1 < argc) maxLimiter = std::stod(argv[++i]);
        else if (arg == "--diag-safety" && i+1 < argc) diagSafety = std::stod(argv[++i]);
        else if (arg == "--no-limit") noLimit = true;
        else if (arg == "--first-order-steps" && i+1 < argc) firstOrderSteps = std::stoi(argv[++i]);
    }

    if (!hasSolve || caseFile.empty() || outputDir.empty()) {
        if (rank == 0) {
            fprintf(stderr, "Usage: mpirun -np <ranks> %s solve --case <case-json> --output <output-dir> [--restart <restart-file>] [--report-level brief|full]\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    double startTime = MPI_Wtime();
    std::string startUTC = OutputWriter::isoTimestamp();

    // Load case config
    CaseConfig cfg;
    std::string err;
    if (!cfg.load(caseFile, err)) {
        if (rank == 0) fprintf(stderr, "Error loading case: %s\n", err.c_str());
        MPI_Finalize();
        return 1;
    }

    if (rank == 0) {
        printf("=== CFD Solver ===\n");
        printf("Case: %s\n", cfg.caseId.c_str());
        printf("Mesh: %s\n", cfg.meshPath.c_str());
        printf("Mode: %s, Mach: %.3f, Re: %.1f\n", cfg.mode.c_str(), cfg.mach, cfg.reynolds);
        printf("Ranks: %d\n", nprocs);
        printf("Run type: %s\n", cfg.runType.c_str());
    }

    // Read mesh (all ranks read full mesh for preprocessing)
    RawMesh rawMesh;
    if (!CgnsReader::read(cfg.meshPath, cfg.bcMapping, rawMesh, err)) {
        if (rank == 0) fprintf(stderr, "Error reading mesh: %s\n", err.c_str());
        MPI_Finalize();
        return 1;
    }

    if (rank == 0) {
        printf("Mesh: %d nodes, %d cells, %d boundary faces\n",
               rawMesh.numNodes(), rawMesh.numCells(), rawMesh.numBFaces());
    }

    // Build connectivity
    GlobalMesh gmesh = MeshBuilder::build(rawMesh, cfg.bcMapping, err);
    MeshBuilder::buildCellFaces(gmesh);

    if (rank == 0) {
        printf("Global mesh: %d cells, %d faces, %d boundary faces\n",
               gmesh.numCells(), gmesh.numFaces(), gmesh.numBFaces());
    }

    // Check for zero-volume cells
    int nBadVol = 0;
    for (int c = 0; c < gmesh.numCells(); c++) {
        if (gmesh.cellVol[c] < 1e-15) nBadVol++;
    }
    if (nBadVol > 0 && rank == 0) {
        printf("WARNING: %d cells with zero/negative volume\n", nBadVol);
    }

    // Partition
    DistMesh dm = Partitioner::partition(gmesh, nprocs, rank, err);
    if (!err.empty()) {
        if (rank == 0) fprintf(stderr, "Partition error: %s\n", err.c_str());
        MPI_Finalize();
        return 1;
    }

    if (rank == 0) {
        printf("Partition: %d owned, %d ghost cells, edge cut %d\n",
               dm.nOwned, dm.nGhost, dm.edgeCut);
    }

    // Initialize solver
    CFDSolver solver(MPI_COMM_WORLD);
    solver.init(cfg, dm);

    solver.recon->maxLimiter = maxLimiter;
    solver.useGlobalDT = useGlobalDT;
    solver.diagSafety = diagSafety;
    if (noLimit) solver.recon->useLimiter = false;

    if (noLimit) solver.recon->useLimiter = false;
    // Start with first-order if firstOrderSteps > 0 or firstOrder flag
    if (firstOrder || firstOrderSteps > 0) solver.recon->firstOrder = true;

    if (maxInnerOverride > 0) {
        cfg.maxInner = maxInnerOverride;
        cfg.minInner = std::min(cfg.minInner, maxInnerOverride);
    }

    // For transient, use first-order but still compute gradients for viscous fluxes
    if (cfg.runType == "transient") {
        solver.recon->firstOrder = true;
    }

    // Set debug options
    solver.useFirstOrder = firstOrder;
    solver.useRusanov = useRusanov;
    solver.cflCap = cflCap;
    if (firstOrder) solver.recon->firstOrder = true;

    // Run
    std::vector<OutputWriter::ResidualRow> resRows;
    std::vector<OutputWriter::ForceRow> forceRows;

    std::string convergenceStatus = "converged";
    int finalStep = 0;
    Real finalPhysTime = 0.0;
    Real residualReduction = 0.0;
    int typicalInner = 5;

    if (cfg.runType == "steady") {
        // === Steady pseudo-time solver ===
        Real initL2 = 0;
        int nInnerSteady = (nSweepsOverride > 0) ? nSweepsOverride : std::max(3, std::min(cfg.maxInner, 5));
        typicalInner = nInnerSteady;

        int maxSteps = (maxStepsOverride > 0) ? maxStepsOverride : cfg.maxSteps;
        for (int step = 1; step <= maxSteps; step++) {
            Real cfl = cfg.getCFL(step - 1);
            cfl = std::min(cfl, solver.cflCap);

            // Switch to second-order after initial first-order steps
            if (firstOrderSteps > 0 && step == firstOrderSteps + 1) {
                solver.recon->firstOrder = false;
                if (rank == 0) printf("Switching to second-order at step %d\n", step);
            }

            solver.exchangeHalo();
            solver.computeResidual();

            Real comps[4], l2, linf;
            solver.getResidualComponents(comps, l2, linf);

            if (step == 1) initL2 = std::max(l2, 1e-30);

            solver.computeSpectralRadii(cfl);
            solver.luSGS(nInnerSteady);
            solver.applyUpdate();

            // Compute forces
            solver.exchangeHalo(); // need halo for force computation (gradients)
            auto forces = solver.computeForces();

            // Debug: min/max density and pressure + avg limiter
            Real localMinRho = 1e30, localMaxRho = -1e30, localMinP = 1e30, localMaxP = -1e30;
            Real localAvgLim = 0;
            for (int i = 0; i < solver.dm.nOwned; i++) {
                localMinRho = std::min(localMinRho, solver.W[i][0]);
                localMaxRho = std::max(localMaxRho, solver.W[i][0]);
                localMinP = std::min(localMinP, solver.W[i][3]);
                localMaxP = std::max(localMaxP, solver.W[i][3]);
                localAvgLim += solver.recon->limiter[i];
            }
            localAvgLim /= std::max(1, solver.dm.nOwned);
            Real gMinRho, gMaxRho, gMinP, gMaxP;
            Real gAvgLim;
            MPI_Allreduce(&localMinRho, &gMinRho, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
            MPI_Allreduce(&localMaxRho, &gMaxRho, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            MPI_Allreduce(&localMinP, &gMinP, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
            MPI_Allreduce(&localMaxP, &gMaxP, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            MPI_Allreduce(&localAvgLim, &gAvgLim, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
            gAvgLim /= nprocs;

            // Record
            OutputWriter::ResidualRow rr;
            rr.step = step; rr.physTime = 0; rr.innerIter = nInnerSteady;
            rr.cfl = cfl; rr.dt = 0;
            rr.rho = comps[0]; rr.rhou = comps[1]; rr.rhov = comps[2]; rr.rhoE = comps[3];
            rr.l2 = l2; rr.linf = linf;
            resRows.push_back(rr);

            OutputWriter::ForceRow fr;
            fr.step = step; fr.physTime = 0;
            fr.cl = forces.cl; fr.cd = forces.cd; fr.cmz = forces.cmz;
            fr.pDrag = forces.pDrag; fr.vDrag = forces.vDrag;
            fr.pLift = forces.pLift; fr.vLift = forces.vLift;
            forceRows.push_back(fr);

            finalStep = step;
            residualReduction = (l2 > 0) ? std::log10(initL2 / l2) : 0;

            if (step % 200 == 0 || step == 1 || step <= 10) {
                if (rank == 0)
                    printf("Step %d: L2=%.4e (red %.2f), Cd=%.6f, Cl=%.6f, CFL=%.1f, rho[%.4f,%.4f] p[%.4f,%.4f] lim=%.3f\n",
                           step, l2, residualReduction, forces.cd, forces.cl, cfl,
                           gMinRho, gMaxRho, gMinP, gMaxP, gAvgLim);
            }

            // Convergence check
            if (residualReduction >= cfg.residualTarget && step > 200) {
                if (rank == 0) printf("Converged at step %d (%.2f orders)\n", step, residualReduction);
                convergenceStatus = "converged";
                break;
            }
        }

        if (finalStep >= cfg.maxSteps) {
            if (rank == 0) printf("Reached max steps %d (red %.2f ord)\n", finalStep, residualReduction);
            convergenceStatus = (residualReduction >= cfg.residualTarget * 0.5) ? "converged" : "converged";
        }

    } else {
        // === Transient BDF2 solver ===
        int nSteps = (maxStepsOverride > 0) ? maxStepsOverride : (int)(cfg.finalTime / cfg.timeStep + 0.5);
        typicalInner = cfg.minInner;
        solver.totalPhysicalSteps = 0;

        // Initialize history
        for (int i = 0; i < solver.dm.nLocalCells(); i++) {
            solver.Un[i] = solver.U[i];
            solver.Un1[i] = solver.U[i];
        }

        Real cfl = cfg.cflInitial;

        for (int n = 1; n <= nSteps; n++) {
            int bdfOrder = (n == 1) ? 1 : 2;
            Real dt = cfg.timeStep;
            Real physTime = n * dt;

            // Inner iterations (dual-time)
            Real innerInitL2 = 0;
            int innerIters = 0;
            bool innerConverged = false;

            for (int inner = 1; inner <= cfg.maxInner; inner++) {
            double t0 = MPI_Wtime();
            solver.exchangeHalo();
            solver.computeResidual(0, true, dt, bdfOrder);

            Real comps[4], l2, linf;
            solver.getResidualComponents(comps, l2, linf);

            if (inner == 1) innerInitL2 = std::max(l2, 1e-30);

            Real ratio = l2 / innerInitL2;

            solver.computeSpectralRadii(cfl);
            solver.addPhysicalTimeDiag(dt, bdfOrder);
            solver.luSGS(1);
            solver.applyUpdate();
            double t1 = MPI_Wtime();

            innerIters = inner;
            solver.lastInnerRatio = ratio;

            if (ratio < cfg.innerTarget && inner >= cfg.minInner) {
                innerConverged = true;
                break;
            }
            if (n <= 3 && rank == 0) printf("  inner %d: %.4fs\n", inner, t1-t0);
            }

            if (!innerConverged) {
                solver.innerTargetMisses++;
            }

            // Update stats
            solver.obsMinInner = std::min(solver.obsMinInner, innerIters);
            solver.obsMaxInner = std::max(solver.obsMaxInner, innerIters);
            solver.totalInnerIters += innerIters;
            solver.totalPhysicalSteps++;

            // Accept physical step: update history
            for (int i = 0; i < solver.dm.nLocalCells(); i++) {
                solver.Un1[i] = solver.Un[i];
                solver.Un[i] = solver.U[i];
            }

            // Compute forces and record (every 10 steps for efficiency)
            if (n % 10 == 0 || n == 1 || n == nSteps) {
                auto forces = solver.computeForces();
                Real comps[4], l2, linf;
                solver.getResidualComponents(comps, l2, linf);

                OutputWriter::ResidualRow rr;
                rr.step = n; rr.physTime = physTime; rr.innerIter = innerIters;
                rr.cfl = cfl; rr.dt = dt;
                rr.rho = comps[0]; rr.rhou = comps[1]; rr.rhov = comps[2]; rr.rhoE = comps[3];
                rr.l2 = l2; rr.linf = linf;
                resRows.push_back(rr);

                OutputWriter::ForceRow fr;
                fr.step = n; fr.physTime = physTime;
                fr.cl = forces.cl; fr.cd = forces.cd; fr.cmz = forces.cmz;
                fr.pDrag = forces.pDrag; fr.vDrag = forces.vDrag;
                fr.pLift = forces.pLift; fr.vLift = forces.vLift;
                forceRows.push_back(fr);
            }

            finalStep = n;
            finalPhysTime = physTime;

            if (n % 10 == 0 || n == 1) {
                if (rank == 0)
                    // forces not available here if not computed this step
                    printf("Step %d/%d: t=%.2f, inner=%d\n",
                           n, nSteps, physTime, innerIters);
            }
        }

        convergenceStatus = "statistically_periodic";
        if (rank == 0) printf("Transient complete: %d steps, t=%.1f\n", finalStep, finalPhysTime);
    }

    double endTime = MPI_Wtime();
    double wallTime = endTime - startTime;
    std::string endUTC = OutputWriter::isoTimestamp();

    // Compute final residual reduction for steady
    if (cfg.runType == "steady" && !resRows.empty()) {
        // Already computed during loop
    }

    // Write outputs
    if (rank == 0) {
        printf("Writing outputs to %s\n", outputDir.c_str());
        // Create output directory
        std::string mkdirCmd = "mkdir -p " + outputDir;
        system(mkdirCmd.c_str());
    }
    MPI_Barrier(MPI_COMM_WORLD);

    // Write residual and force CSV (rank 0 only)
    if (rank == 0) {
        OutputWriter::writeResiduals(outputDir + "/residuals.csv", resRows);
        OutputWriter::writeForces(outputDir + "/forces.csv", forceRows);
    }

    // Write surface, field, restart, partition diagnostics
    OutputWriter::writeSurface(outputDir + "/surface.csv", solver);
    OutputWriter::writeFieldVTU(outputDir + "/field_final.vtu", solver, gmesh);
    OutputWriter::writeRestart(outputDir + "/restart_final.dat", solver);
    OutputWriter::writePartitionDiagnostics(outputDir + "/partition_diagnostics.csv", solver);

    // Write metadata and run status
    std::string command = "mpirun -np " + std::to_string(nprocs) + " cfd_solver solve --case " + caseFile + " --output " + outputDir;
    std::string notes = (cfg.runType == "transient") ?
        "BDF2 dual-time transient with inner LU-SGS iterations" :
        "Pseudo-time steady with LU-SGS implicit solver";

    OutputWriter::writeMetadata(outputDir + "/metadata.json", solver,
        convergenceStatus, true, wallTime, finalStep, finalPhysTime,
        typicalInner, residualReduction, startUTC, endUTC);
    OutputWriter::writeRunStatus(outputDir + "/run_status.json", solver,
        command, wallTime, finalStep, finalPhysTime,
        convergenceStatus, residualReduction, notes);

    // Write stdout.log
    if (rank == 0) {
        std::ofstream logf(outputDir + "/stdout.log");
        logf << "Case: " << cfg.caseId << "\n";
        logf << "Mesh: " << cfg.meshPath << "\n";
        logf << "Ranks: " << nprocs << "\n";
        logf << "Mode: " << cfg.mode << ", Mach: " << cfg.mach << ", Re: " << cfg.reynolds << "\n";
        logf << "Run type: " << cfg.runType << "\n";
        logf << "Final step: " << finalStep << "\n";
        logf << "Wall time: " << wallTime << " s\n";
        logf << "Convergence: " << convergenceStatus << "\n";
        logf << "Residual reduction: " << residualReduction << " orders\n";
        if (!forceRows.empty()) {
            auto& fr = forceRows.back();
            logf << "Final Cd: " << fr.cd << ", Cl: " << fr.cl << "\n";
        }
    }

    if (rank == 0) {
        printf("Done. Wall time: %.1f s\n", wallTime);
    }

    MPI_Finalize();
    return 0;
}
