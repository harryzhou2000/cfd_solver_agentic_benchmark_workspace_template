#pragma once
#include "types.hpp"
#include <string>
#include <fstream>
#include <mpi.h>

class OutputWriter {
public:
    void init(const CaseConfig& config, const struct LocalMesh& lm, MPI_Comm comm);
    void open(const std::string& output_dir);
    void write_residual(int step, double time, int inner, double cfl, double dt,
                        const Vec4& res_comp, double res_l2, double res_linf);
    void write_force(int step, double time, double cl, double cd, double cmz,
                    double pdrag, double vdrag, double plift, double vlift);
    void write_surface(const Mesh& mesh, const std::vector<Vec4>& U,
                      const std::vector<std::array<Vec4, 2>>& grads,
                      const CaseConfig& config, double mu, int rank, int nranks, MPI_Comm comm);
    void write_field(const Mesh& mesh, const std::vector<Vec4>& U,
                    const CaseConfig& config, int rank, int nranks, MPI_Comm comm);
    void write_metadata(const CaseConfig& config, const struct LocalMesh& lm,
                       const struct SolverStats& stats, const std::string& output_dir);
    void write_run_status(const CaseConfig& config, const struct SolverStats& stats,
                         const std::string& output_dir, int nranks, const std::string& command);
    void write_partition_diagnostics(const struct LocalMesh& lm, int rank, int nranks,
                                    const std::string& output_dir, MPI_Comm comm);
    std::string output_dir() const { return output_dir_; }

private:
    std::string output_dir_;
    std::ofstream res_file_;
    std::ofstream force_file_;
    CaseConfig config_;
    int rank_ = 0;
};
