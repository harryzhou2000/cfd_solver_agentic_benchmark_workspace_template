#pragma once
#include "MeshData.hpp"
#include "Config.hpp"
#include "OutputManager.hpp"
#include <mpi.h>
#include <vector>
#include <string>

struct SteadyResult {
    bool converged = false;
    int final_step = 0;
    double residual_reduction = 0.0;
    double final_cfl = 1.0;
};

SteadyResult runSteady(LocalMesh& lm,
                       std::vector<StateVec>& states,
                       const CaseConfig& cfg,
                       const std::string& output_dir,
                       MPI_Comm comm);
