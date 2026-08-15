#include "cfd/Output.hpp"

#include <mpi.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  cfd::CaseConfig config;
  config.case_id = "cylinder_re200 \"quoted\"";
  config.mesh.file = "mesh.cgns";
  config.gas.gamma = 1.4;
  config.gas.gas_constant = 287.0;
  config.physics.mode = "laminar";
  config.physics.reynolds = 200.0;
  config.freestream.rho = 1.0;
  config.freestream.velocity_magnitude = 1.0;
  config.reference.reynolds_length = 1.0;
  config.numerics_required.spatial_order = 2;
  config.numerics_required.inviscid_flux = "rusanov";
  config.numerics_required.viscous_flux = "green_gauss";
  config.numerics_required.main_time_method = "bdf2";
  config.numerics_required.implicit_solver = "lu_sgs";
  config.run_control.type = "transient";
  config.run_control.time_integrator = "bdf2_or_trapezoidal";
  config.run_control.time_step = 0.01;
  config.run_control.final_time = 300.0;
  config.run_control.inner_residual_norm = "total_spatial_plus_physical_time";
  config.run_control.bdf2_history_update = "after_inner_convergence";
  config.run_control.rusanov_dissipation_scale = 1.0;
  config.run_control.min_inner_iterations = 5;
  config.run_control.max_inner_iterations = 1000;
  config.run_control.inner_residual_reduction_target = 1.0e-3;

  cfd::LocalMesh mesh;
  mesh.rank = rank;
  mesh.ranks = 1;
  mesh.global_cells = 1;
  mesh.global_faces = 3;
  mesh.owned_count = 1;
  mesh.cells.push_back({0, 0, {0.5, 0.25}, 0.5, {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}}});
  mesh.faces.push_back({0, -1, {0.0, 0.0}, {1.0, 0.0}, {0.5, 0.0}, {0.0, -1.0}, 1.0, "wall"});

  const auto directory = std::filesystem::current_path() / "cfd_output_writer_test";
  if (rank == 0) std::filesystem::remove_all(directory);
  MPI_Barrier(MPI_COMM_WORLD);
  cfd::OutputWriter writer(directory, config, mesh, MPI_COMM_WORLD);
  writer.initialize();
  writer.write_partition_diagnostics();
  writer.write_residual({1, 0.01, 5, 1.0, 0.01, {1.0, 2.0, 3.0, 4.0}, 5.0, 4.0});
  writer.write_force({1, 0.01, 0.1, 0.2, 0.0, 0.2, 0.0, 0.1, 0.0});
  writer.write_surface({{{0.5, 0.0, 0.0, -1.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, "wall,tag"}}});
  const cfd::Conserved state{1.0, 2.0, 0.0, 4.5};
  writer.write_final_field({state});
  writer.write_restart({state});
  cfd::RunSummary summary;
  summary.command = "solve --case \"x\"";
  summary.wall_time_seconds = 1.0;
  summary.final_step = 1;
  summary.final_physical_time = 0.01;
  summary.convergence_status = "statistically_periodic";
  summary.completed = true;
  summary.notes = "line\nbreak";
  summary.true_bdf2_inner_loop = true;
  summary.inner_solve = {5, 6, 5.5, 0, 1.0, 1.0e-4};
  writer.write_metadata_and_status(summary);
  MPI_Barrier(MPI_COMM_WORLD);
  if (rank == 0) {
    for (const char* name : {"partition_diagnostics.csv", "residuals.csv", "forces.csv", "surface.csv",
                             "field_final_rank0000.vtu", "field_final.pvtu", "restart_final.rank0000.bin",
                             "metadata.json", "run_status.json"}) {
      assert(std::filesystem::exists(directory / name));
    }
    std::ifstream metadata(directory / "metadata.json");
    const std::string text((std::istreambuf_iterator<char>(metadata)), std::istreambuf_iterator<char>());
    assert(text.find("cylinder_re200 \\\"quoted\\\"") != std::string::npos);
    assert(text.find("rusanov_local_lax_friedrichs") != std::string::npos);
    assert(text.find("bdf2_with_backward_euler_startup") != std::string::npos);
    assert(text.find("distributed_additive_schwarz_lu_sgs_scalar_spectral_jacobian") !=
           std::string::npos);
    assert(text.find("newtonian_stress_fourier_heat_flux_constant_viscosity") !=
           std::string::npos);
    assert(text.find("constant_from_freestream_reynolds_number") != std::string::npos);
    assert(text.find("total_spatial_plus_physical_time") != std::string::npos);
    assert(text.find("after_inner_convergence") != std::string::npos);
    std::ifstream residuals(directory / "residuals.csv");
    std::string header;
    std::getline(residuals, header);
    assert(header == "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf");
    std::ifstream forces(directory / "forces.csv");
    std::getline(forces, header);
    assert(header == "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift");
    std::ifstream surface(directory / "surface.csv");
    std::getline(surface, header);
    assert(header == "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag");
    std::ifstream field(directory / "field_final_rank0000.vtu");
    const std::string field_text((std::istreambuf_iterator<char>(field)), std::istreambuf_iterator<char>());
    assert(field_text.find("Name=\"density\"") != std::string::npos);
    assert(field_text.find("Name=\"owner\"") != std::string::npos);
    assert(std::filesystem::file_size(directory / "restart_final.rank0000.bin") > 32);
    std::filesystem::remove_all(directory);
  }
  MPI_Finalize();
}
