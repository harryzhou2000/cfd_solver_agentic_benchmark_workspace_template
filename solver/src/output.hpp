// output.hpp — Output writers: CSV, VTU, metadata, run_status
#pragma once
#include "solver.hpp"
#include <string>
#include <vector>
#include <fstream>

namespace cfd2d {

struct ResidualRow {
    int step;
    double physicalTime;
    int innerIter;
    double cfl;
    double dt;
    double rho, rhou, rhov, rhoE;
    double residualL2, residualLinf;
};

struct ForceRow {
    int step;
    double physicalTime;
    double cl, cd, cmz;
    double pressureDrag, viscousDrag, pressureLift, viscousLift;
};

struct OutputWriter {
    std::vector<ResidualRow> residualHistory;
    std::vector<ForceRow> forceHistory;

    std::string outputDir;

    void setOutputDir(const std::string& dir) { outputDir = dir; }

    void addResidualRow(const Solver& s, int step, double physTime, int innerIter,
                        double cfl, double dt);
    void addForceRow(const Solver& s, int step, double physTime);

    void writeResidualsCSV() const;
    void writeForcesCSV() const;
    void writeSurfaceCSV(const Solver& s) const;
    void writeFieldVTU(const Solver& s) const;
    void writeRestart(const Solver& s) const;
    void writeMetadata(const Solver& s, const CaseInput& ci, bool completed,
                       const std::string& convStatus, const std::string& startTime,
                       const std::string& endTime) const;
    void writeRunStatus(const CaseInput& ci, int mpiRanks, const std::string& command,
                        double wallTime, int finalStep, double finalPhysTime,
                        const std::string& convStatus, double resReduction,
                        const std::string& notes) const;
    void writePartitionDiagnostics(const Solver& s) const;
};

} // namespace cfd2d
