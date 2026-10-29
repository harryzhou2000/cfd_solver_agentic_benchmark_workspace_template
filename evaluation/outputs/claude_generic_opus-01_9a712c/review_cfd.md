# CFD methods and algorithms review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-29T11:22:18.913990+00:00

**Overall score: 4.51**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **cfd.equations** Governing equations and nondimensionalization | 0.06 | 4.5 | report.tex:64–135 and GasModel/Residual describe compressible perfect-gas flow and nondimensional references; report.tex:1232–1237 materially understates excluded normal viscous drag in M2 laminar. |
|   | 2-D compressible Navier-Stokes for a calorically perfect gas written in conservative form; closure p=(gamma-1)rho e; nondimensionalization consistent with case reference quantities; force coefficients defined correctly. | | | |
|   | *Evidence:* Source flux assembly and report equations. | | | |
| 2 | **cfd.mesh** Mesh import and unstructured geometry | 0.10 | 4.5 | CgnsReader.cpp:66–80,161–195,220–273 handles unstructured mixed cells and one-to-one connections; LocalMesh constructs geometry. General CGNS layouts were not independently exercised. |
|   | Reads CGNS unstructured zones and boundary families; computes cell volumes, face areas/normals/centers; handles mixed cells and multi-zone meshes; builds cell-face adjacency for partitioning and reconstruction. | | | |
|   | *Evidence:* Mesh reader code, geometry tests, partition diagnostics. | | | |
| 3 | **cfd.residual** Conservative finite-volume residual | 0.12 | 5 | Residual.cpp:115–229 reconstructs face states, assembles opposite interior flux contributions and includes viscous terms. Kernel ctest passed; this is not a full grid-convergence proof. |
|   | Cell-centered FV residual with correct face orientation, consistent flux signs, boundary-face treatment; residual assembled once per iteration with global MPI reduction. | | | |
|   | *Evidence:* residual assembly code; residuals.csv shows global reduction. | | | |
| 4 | **cfd.flux** Approximate Riemann flux and entropy fix | 0.08 | 4.5 | Flux.cpp:39–75 implements Roe and entropy fix; :113–176 supplies HLLC/Rusanov and shock blending. Submitted cases report HLLC with shock blend; exhaustive shock tests were not run. |
|   | Rusanov/LLF minimum or Roe-type with entropy fix; flux matches the stated method; no loss of conservation at boundaries. | | | |
|   | *Evidence:* Flux implementation; metadata inviscid_flux/entropy_fix; code walkthrough. | | | |
| 5 | **cfd.bc** Boundary conditions | 0.08 | 4.5 | Boundary.cpp contains characteristic farfield and wall states; Residual.cpp:236–273 implements no-slip adiabatic wall flux. Report and kernel tests support boundary treatment, with limited independent coverage. |
|   | Farfield, inviscid slip wall, no-slip adiabatic wall; wall treatment honest (boundary_value vs cell_center in surface output); near-zero wall velocity for no-slip; near-zero normal velocity for slip. | | | |
|   | *Evidence:* BC code; surface.csv checks; metadata wall_boundary_output_semantics. | | | |
| 6 | **cfd.reconstruction** Second-order reconstruction | 0.09 | 4.5 | Gradients.cpp and Residual.cpp:128–149 implement limited linear reconstruction. report.tex:316–329 reports second-order face reconstruction, not a full residual refinement study. |
|   | Least-squares/Green-Gauss gradients; piecewise-linear face states; reconstruction ACTIVE in production runs; first-order fallback triggers documented and disclosed. | | | |
|   | *Evidence:* Gradient/reconstruction code; metadata reconstruction field; report disclosure. | | | |
| 7 | **cfd.limiter** Limiter and positivity control | 0.08 | 4 | Gradients.cpp:114–145 implements Venkat limiter; Driver.cpp:460–480 freezes existing limiter values after a stability test rather than disabling reconstruction. Freezing affects nonlinear evolution and is disclosed. |
|   | Barth-Jespersen or stronger limiter active in production; positivity protection for reconstructed density/pressure and updates. | | | |
|   | *Evidence:* Limiter code; metadata limiter/positivity_preservation; shock-captured fields. | | | |
| 8 | **cfd.viscous** Viscous discretization | 0.10 | 4.5 | Residual.cpp:170–207,253–273 computes corrected velocity/temperature gradients and wall stress; Forces.cpp:32–44 separates tangential traction. The report incorrectly minimizes the excluded normal component for M2 laminar. |
|   | Velocity/temperature gradients on unstructured cells; Newtonian stress tensor and Fourier heat flux; viscosity matches case Reynolds number; wall-normal gradient treatment; pressure vs tangential skin-friction split. | | | |
|   | *Evidence:* Viscous flux code; forces.csv viscous columns; report formulas. | | | |
| 9 | **cfd.implicit** Implicit steady march | 0.10 | 4.5 | ImplicitSolver.cpp:128–152 uses forward/backward sweeps with neighbor exchange, and Driver.cpp controls the local CFL. Original case histories support execution; no new case rerun was performed. |
|   | LU-SGS/matrix-free/block-Jacobi or equivalent with documented Jacobian approximation; CFL ramp/local time stepping honoring case CFL controls; meaningful residual reduction. | | | |
|   | *Evidence:* Implicit solver code; residuals.csv; metadata time_integrator/implicit_solver. | | | |
| 10 | **cfd.transient** Transient BDF2 two-loop method (Re 200) | 0.10 | 4 | Driver.cpp:618–677 uses frozen-history BDF2 and true nonlinear total-residual checks for the fresh run. Restart files omit U(n-1), while a resumed step immediately uses BDF2 with U(n-1)=U(n), reducing restart accuracy. |
|   | True physical-time outer loop with inner iterations at each step (dt=0.01, t_final=300, 5-1000 inner iterations, inner target 1e-3, frozen BDF2 histories) or stricter documented equivalent; real inner-iteration statistics reported. | | | |
|   | *Evidence:* Time loop code; metadata true_bdf2_inner_loop + inner stats; forces.csv unsteady behavior. | | | |
| 11 | **cfd.mpi_partition** MPI partitioning and scalability | 0.09 | 5 | METIS partition graph, neighbor Isend/Irecv and global force reductions appear in Partitioner.cpp:127, HaloExchanger.cpp:23–37 and Forces.cpp:57. Two original rank studies support cross-rank consistency. |
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

- Overall score (0-5): 4.51
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
