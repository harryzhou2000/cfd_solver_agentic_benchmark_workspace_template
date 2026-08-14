#pragma once
#include "types.hpp"
#include <fstream>
#include <string>
#include <ctime>

namespace omo {

struct SolverStats {
    int step = 0;
    Real physical_time = 0.0;
    Real residual_l2 = 0, residual_linf = 0;
    Real cl = 0, cd = 0, cmz = 0;
    Real pressure_drag = 0, viscous_drag = 0;
    Real pressure_lift = 0, viscous_lift = 0;
    Real residual_init = 1.0;
    std::string convergence_status = "running";
};

class FVMSolver {
public:
    FVMSolver(const CaseConfig& cfg, MeshData& mesh);
    void solve(const std::string& output_dir);

private:
    void apply_initial_conditions();

    // Steady solve
    void steady_solve(std::ofstream& res_file, std::ofstream& force_file,
                      const std::string& output_dir);

    // Residual
    void compute_residual();
    void compute_inviscid_flux(const FaceData& face,
                                const CellData& left, const CellData& right,
                                Vector4& flux) const;
    void compute_viscous_flux(const FaceData& face,
                               const CellData& left, const CellData& right,
                               Vector4& flux) const;

    // Boundary
    void apply_boundary_condition_to_face(int face_idx, const FaceData& face, Vector4& flux);
    void farfield_bc(const FaceData& face, CellData& ghost) const;
    void slip_wall_bc(const FaceData& face, CellData& ghost) const;
    void no_slip_adiabatic_wall_bc(const FaceData& face, CellData& ghost) const;

    // Local time step
    void compute_local_time_step(int cell_idx, Real& dt);

    // Forces
    void compute_forces();

    // Output
    void write_field_vtk(const std::string& filename);
    void write_surface(std::ofstream& f);
    void write_metadata(const std::string& output_dir);
    void write_run_status(const std::string& output_dir);
    void write_partition_diagnostics(const std::string& output_dir);

    const CaseConfig& cfg_;
    MeshData& mesh_;
    std::vector<Vector4> R_;
    std::vector<Real> spectral_radius_;
    std::vector<std::string> face_bc_mapping_; // per-face BC type string
    SolverStats stats_;
};

} // namespace omo
