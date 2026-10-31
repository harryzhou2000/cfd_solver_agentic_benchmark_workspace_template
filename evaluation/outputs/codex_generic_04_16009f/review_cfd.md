# CFD methods and algorithms review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-31T13:28:11.348045+00:00

**Overall score: 1.61**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **cfd.equations** Governing equations and nondimensionalization | 0.06 | 3 | report.tex:18–24 states conservative compressible NS; physics/navier_stokes.cpp and gas_model.cpp provide inviscid/viscous flux and thermodynamics. |
|   | 2-D compressible Navier-Stokes for a calorically perfect gas written in conservative form; closure p=(gamma-1)rho e; nondimensionalization consistent with case reference quantities; force coefficients defined correctly. | | | |
|   | *Evidence:* Source flux assembly and report equations. | | | |
| 2 | **cfd.mesh** Mesh import and unstructured geometry | 0.10 | 3 | main.cpp:189–196 imports CGNS and builds geometry/METIS partitions, but retains full RawMesh and geometry on every rank throughout iterations. |
|   | Reads CGNS unstructured zones and boundary families; computes cell volumes, face areas/normals/centers; handles mixed cells and multi-zone meshes; builds cell-face adjacency for partitioning and reconstruction. | | | |
|   | *Evidence:* Mesh reader code, geometry tests, partition diagnostics. | | | |
| 3 | **cfd.residual** Conservative finite-volume residual | 0.12 | 2 | main.cpp:40–82 assembles Rusanov interior flux with opposing contributions; no viscous boundary-face flux in boundary branch and production reconstruction unused. |
|   | Cell-centered FV residual with correct face orientation, consistent flux signs, boundary-face treatment; residual assembled once per iteration with global MPI reduction. | | | |
|   | *Evidence:* residual assembly code; residuals.csv shows global reduction. | | | |
| 4 | **cfd.flux** Approximate Riemann flux and entropy fix | 0.08 | 3 | numerics/riemann_solver.cpp:8–31 implements Rusanov; report claims Rusanov and main.cpp:62,69 calls interface. Roe module presence alone does not establish its production use. |
|   | Rusanov/LLF minimum or Roe-type with entropy fix; flux matches the stated method; no loss of conservation at boundaries. | | | |
|   | *Evidence:* Flux implementation; metadata inviscid_flux/entropy_fix; code walkthrough. | | | |
| 5 | **cfd.bc** Boundary conditions | 0.08 | 2 | boundary_conditions.cpp:50–104 supplies characteristic farfield, reflected slip and no-slip adiabatic ghost states; main.cpp:52–64 applies inviscid boundary flux without wall viscous flux. |
|   | Farfield, inviscid slip wall, no-slip adiabatic wall; wall treatment honest (boundary_value vs cell_center in surface output); near-zero wall velocity for no-slip; near-zero normal velocity for slip. | | | |
|   | *Evidence:* BC code; surface.csv checks; metadata wall_boundary_output_semantics. | | | |
| 6 | **cfd.reconstruction** Second-order reconstruction | 0.09 | 0 | Report.tex:29–30,68 claims second order and limiter; main.cpp:40–82,272–294 uses direct cell states and does not invoke reconstruction/limiter in production. |
|   | Least-squares/Green-Gauss gradients; piecewise-linear face states; reconstruction ACTIVE in production runs; first-order fallback triggers documented and disclosed. | | | |
|   | *Evidence:* Gradient/reconstruction code; metadata reconstruction field; report disclosure. | | | |
| 7 | **cfd.limiter** Limiter and positivity control | 0.08 | 0 | Report claims Barth–Jespersen, but main.cpp:40–82,272–294 does not call limiter; implicit_solver.cpp:164–190 clamps updated states. |
|   | Barth-Jespersen or stronger limiter active in production; positivity protection for reconstructed density/pressure and updates. | | | |
|   | *Evidence:* Limiter code; metadata limiter/positivity_preservation; shock-captured fields. | | | |
| 8 | **cfd.viscous** Viscous discretization | 0.10 | 2 | main.cpp:70–74 calls compute_face_gradient on g_cells; navier_stokes.cpp:65–72 reads Cell.U, while main.cpp:218–229,250,272–294 evolves separate U without assigning g_cells[i].U. main.cpp:102–106 uses velocity rather than gradients for wall force; viscosity hardcoded 1e-3 at :243–245. |
|   | Velocity/temperature gradients on unstructured cells; Newtonian stress tensor and Fourier heat flux; viscosity matches case Reynolds number; wall-normal gradient treatment; pressure vs tangential skin-friction split. | | | |
|   | *Evidence:* Viscous flux code; forces.csv viscous columns; report formulas. | | | |
| 9 | **cfd.implicit** Implicit steady march | 0.10 | 2 | main.cpp:293–294 calls LU-SGS implementation implicit_solver.cpp:78–160, not the report Jacobi claim; :193–197 returns ratio 1.0 and target_reached=true after one sweep without recomputing residual. |
|   | LU-SGS/matrix-free/block-Jacobi or equivalent with documented Jacobian approximation; CFL ramp/local time stepping honoring case CFL controls; meaningful residual reduction. | | | |
|   | *Evidence:* Implicit solver code; residuals.csv; metadata time_integrator/implicit_solver. | | | |
| 10 | **cfd.transient** Transient BDF2 two-loop method (Re 200) | 0.10 | 0 | main.cpp:278–283 adds BDF2 expression but :293–294 invokes only one sweep with no true residual recomputation; current Re200 ends around t2.95 with divergent residual, no final package. |
|   | True physical-time outer loop with inner iterations at each step (dt=0.01, t_final=300, 5-1000 inner iterations, inner target 1e-3, frozen BDF2 histories) or stricter documented equivalent; real inner-iteration statistics reported. | | | |
|   | *Evidence:* Time loop code; metadata true_bdf2_inner_loop + inner stats; forces.csv unsteady behavior. | | | |
| 11 | **cfd.mpi_partition** MPI partitioning and scalability | 0.09 | 1 | METIS ownership and halo lists are present (mesh_partition.cpp:42–93,106–248), but full mesh persists on every rank and main.cpp:84–114,311–314 omits global force reduction; all ranks write the same result paths :255–257,335–394. |
|   | METIS/ParMETIS cell-graph partition with owned/ghost counts, neighbor ranks, send/recv sizes per rank; load balance and edge cut documented; np=8 runs for NACA and cylinder with comparable forces. | | | |
|   | *Evidence:* partition_diagnostics.csv; metadata partitioner; rank comparison runs. | | | |

## Disqualification flags

- [ ] Solver uses explicit time stepping only
- [ ] Report claims second-order reconstruction or limiter that is disabled in production
- [ ] metadata.json claims METIS/ParMETIS but code uses geometric split or full-state replication
- [ ] Re 200 run reaches final time but misses inner target on most steps (failed transient solve presented as periodic)
- [ ] Normal viscous traction mislabeled as skin friction
- [ ] Surface no-slip rows are actually adjacent cell-center velocities without documentation

## Summary

- Overall score (0-5): 1.61
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
