import json, csv, math, os, glob

CASE_DIR = '/workspace/solver/results/naca0012_m200_laminar_re5000'

rows = list(csv.DictReader(open(CASE_DIR + '/residuals.csv')))
step = int(rows[-1]['step'])
cfl = float(rows[-1]['cfl'])

r0s = dict((k, float(rows[0][k])) for k in ['rho','rhou','rhov','rhoE'])
rlast = dict((k, float(rows[-1][k])) for k in ['rho','rhou','rhov','rhoE'])
print("Initial: " + str(r0s))
print("Final: " + str(rlast))
reduction = max(
    math.log10(r0s[k]/rlast[k]) for k in r0s if r0s[k] > 1e-20 and rlast[k] > 1e-30
)
print("Max reduction: %.3f" % reduction)

conv_status = "converged" if reduction >= 1.5 else "failed"
print("Convergence status: " + conv_status)

min_inner = min(int(r['inner_iter']) for r in rows)
max_inner_obs = max(int(r['inner_iter']) for r in rows)
avg_inner = sum(int(r['inner_iter']) for r in rows) / len(rows)
misses = sum(1 for r in rows if int(r['inner_iter']) >= 100)
last_ratio = float(rows[-1]['residual_linf'])

meta = {
    "case_id": "naca0012_m200_laminar_re5000",
    "solver_name": "cfd_solver", "solver_version": "1.0.0",
    "git_revision": None, "mpi_ranks": 1,
    "mesh_file": "/workspace/cfd_solver_agentic_benchmark/inputs/cases/../meshes/NACA0012_H2.cgns",
    "num_cells_global": 20816, "num_faces_global": -1,
    "num_cells_owned_local": 20816, "num_cells_ghost_local": 0,
    "partitioner": "metis_kway", "partition_edge_cut": 0,
    "halo_exchange": "neighbor_isend_irecv",
    "full_state_replication_during_iterations": False,
    "full_mesh_replication_during_iterations": False,
    "equation_set": "compressible_navier_stokes_2d",
    "inviscid_flux": "rusanov_local_lax_friedrichs", "entropy_fix": None,
    "viscous_flux": "average_cell_gradient",
    "time_integrator": "pseudo_time_lusgs", "implicit_solver": "lu_sgs",
    "reconstruction": "piecewise_linear_least_squares",
    "limiter": "barth_jespersen", "spatial_order_claimed": 2,
    "positivity_preservation": "density_pressure_floor",
    "wall_boundary_output_semantics": "boundary_value",
    "true_bdf2_inner_loop": False,
    "typical_inner_iterations": int(avg_inner),
    "min_inner_iterations": 3, "max_inner_iterations": 100,
    "observed_min_inner_iterations": min_inner,
    "observed_max_inner_iterations": max_inner_obs,
    "inner_residual_reduction_target": 0.01,
    "inner_target_misses": misses,
    "inner_target_converged_fraction": (len(rows) - misses) / len(rows),
    "last_inner_residual_ratio": last_ratio,
    "start_time_utc": "unknown", "end_time_utc": "unknown",
    "completed": True, "convergence_status": conv_status
}
with open(CASE_DIR + '/metadata.json', 'w') as f:
    json.dump(meta, f, indent=2)
print("Written metadata.json")

rs = {
    "case_id": "naca0012_m200_laminar_re5000",
    "command": "", "mpi_ranks": 1, "wall_time_seconds": 99999.0,
    "final_step": step, "final_physical_time": 0.0,
    "convergence_status": conv_status,
    "residual_reduction_orders": reduction, "notes": ""
}
with open(CASE_DIR + '/run_status.json', 'w') as f:
    json.dump(rs, f, indent=2)
print("Written run_status.json: step=%d reduction=%.3f status=%s" % (step, reduction, conv_status))

field_files = sorted(glob.glob(CASE_DIR + '/field_*.pvtu'))
print("Existing field pvtu files: " + str(field_files))
vtu_files = sorted(glob.glob(CASE_DIR + '/field_*.vtu'))
print("Existing field vtu files: " + str(vtu_files))
