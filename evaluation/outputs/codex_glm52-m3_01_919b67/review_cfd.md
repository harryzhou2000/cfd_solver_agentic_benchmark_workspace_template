# CFD methods and algorithms review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-30T18:39:55.533423+00:00

**Overall score: 3.19**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **cfd.equations** Governing equations and nondimensionalization | 0.06 | 4 | report.tex:16–30 gives compressible perfect-gas equations and constant Reynolds-based viscosity; physics.hpp/solver.cpp contain corresponding flux and conservative state definitions. |
|   | 2-D compressible Navier-Stokes for a calorically perfect gas written in conservative form; closure p=(gamma-1)rho e; nondimensionalization consistent with case reference quantities; force coefficients defined correctly. | | | |
|   | *Evidence:* Source flux assembly and report equations. | | | |
| 2 | **cfd.mesh** Mesh import and unstructured geometry | 0.10 | 4 | mesh.cpp:62–146 reads CGNS multi-zone TRI/QUAD/BAR geometry and builds adjacency; main.cpp:73–143 broadcasts the complete mesh to all ranks. |
|   | Reads CGNS unstructured zones and boundary families; computes cell volumes, face areas/normals/centers; handles mixed cells and multi-zone meshes; builds cell-face adjacency for partitioning and reconstruction. | | | |
|   | *Evidence:* Mesh reader code, geometry tests, partition diagnostics. | | | |
| 3 | **cfd.residual** Conservative finite-volume residual | 0.12 | 4 | solver.cpp:402 onward assembles face contributions and reductions; interior call is Rusanov. No independent conservation or numerical convergence test was run. |
|   | Cell-centered FV residual with correct face orientation, consistent flux signs, boundary-face treatment; residual assembled once per iteration with global MPI reduction. | | | |
|   | *Evidence:* residual assembly code; residuals.csv shows global reduction. | | | |
| 4 | **cfd.flux** Approximate Riemann flux and entropy fix | 0.08 | 3 | solver.cpp:402 onward calls rusanovFlux; report.tex:68–72 correctly states Rusanov, whereas metadata labels Roe/Harten-Yee. Do not infer a report-only DQ6 from metadata alone. |
|   | Rusanov/LLF minimum or Roe-type with entropy fix; flux matches the stated method; no loss of conservation at boundaries. | | | |
|   | *Evidence:* Flux implementation; metadata inviscid_flux/entropy_fix; code walkthrough. | | | |
| 5 | **cfd.bc** Boundary conditions | 0.08 | 4 | report.tex:76–82 and solver.cpp boundary branches cover farfield, slip and no-slip; output.cpp:116–125 records explicitly constructed boundary velocity, not undisclosed adjacent-cell velocity. |
|   | Farfield, inviscid slip wall, no-slip adiabatic wall; wall treatment honest (boundary_value vs cell_center in surface output); near-zero wall velocity for no-slip; near-zero normal velocity for slip. | | | |
|   | *Evidence:* BC code; surface.csv checks; metadata wall_boundary_output_semantics. | | | |
| 6 | **cfd.reconstruction** Second-order reconstruction | 0.09 | 2 | 6b376564 solver.cpp:715–720 traps firstOrder=true throughout every steady solve; :259–264,289–294 then bypasses reconstructed face states. Report.tex:62 incorrectly claims second order after step200. Transient path and unbound historical binary are separate. |
|   | Least-squares/Green-Gauss gradients; piecewise-linear face states; reconstruction ACTIVE in production runs; first-order fallback triggers documented and disclosed. | | | |
|   | *Evidence:* Gradient/reconstruction code; metadata reconstruction field; report disclosure. | | | |
| 7 | **cfd.limiter** Limiter and positivity control | 0.08 | 2.5 | Barth-Jespersen and positivity fallback code exists, but sticky firstOrder at solver.cpp:715–720 bypasses its reconstructed face states throughout steady production. Transient reconstruction remains a separate path. |
|   | Barth-Jespersen or stronger limiter active in production; positivity protection for reconstructed density/pressure and updates. | | | |
|   | *Evidence:* Limiter code; metadata limiter/positivity_preservation; shock-captured fields. | | | |
| 8 | **cfd.viscous** Viscous discretization | 0.10 | 3 | solver.cpp:418,473 uses Newtonian/Fourier flux and :658–673 projects wall shear tangentially. Laminar NACA Cd≈405–532 and Re20 Cd≈2028 demonstrate major physical failure independently of this source feature. |
|   | Velocity/temperature gradients on unstructured cells; Newtonian stress tensor and Fourier heat flux; viscosity matches case Reynolds number; wall-normal gradient treatment; pressure vs tangential skin-friction split. | | | |
|   | *Evidence:* Viscous flux code; forces.csv viscous columns; report formulas. | | | |
| 9 | **cfd.implicit** Implicit steady march | 0.10 | 3 | solver.cpp:685–720 uses the case ramp and cflCap, with an optional max-steps override, then checks residual reduction at :809–815; main.cpp:192–196 reports converged even after exhausting the budget. Do not conflate initialization of currentCFL with the per-step steady ramp. |
|   | LU-SGS/matrix-free/block-Jacobi or equivalent with documented Jacobian approximation; CFL ramp/local time stepping honoring case CFL controls; meaningful residual reduction. | | | |
|   | *Evidence:* Implicit solver code; residuals.csv; metadata time_integrator/implicit_solver. | | | |
| 10 | **cfd.transient** Transient BDF2 two-loop method (Re 200) | 0.10 | 3 | solver.cpp:823–829 caps inner iterations at 10 and :848–855 applies frozen-history BDF2; checkpoint final inner ratio0.156 misses target0.001. Individual per-step ratios are unavailable and the historic binary is not independently hashed. |
|   | True physical-time outer loop with inner iterations at each step (dt=0.01, t_final=300, 5-1000 inner iterations, inner target 1e-3, frozen BDF2 histories) or stricter documented equivalent; real inner-iteration statistics reported. | | | |
|   | *Evidence:* Time loop code; metadata true_bdf2_inner_loop + inner stats; forces.csv unsteady behavior. | | | |
| 11 | **cfd.mpi_partition** MPI partitioning and scalability | 0.09 | 2.5 | 6b376564 main.cpp:73–143 broadcasts the full mesh to all ranks; :164–194 retains it throughout solve and solver.cpp:10–12 stores a reference. This violates TASK.md:46,227 despite actual METIS and neighbor halo communication. Rank comparisons are short and historical binary attribution is not independently hashed. |
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

- Overall score (0-5): 3.19
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
