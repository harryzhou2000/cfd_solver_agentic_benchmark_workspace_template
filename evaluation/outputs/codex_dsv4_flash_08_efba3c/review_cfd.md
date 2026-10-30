# CFD methods and algorithms review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-30T19:13:19.170506+00:00

**Overall score: 3.7**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **cfd.equations** Governing equations and nondimensionalization | 0.06 | 4 | numerics.cpp:132–163 implements conservative Euler flux, :478–610 assembles face and viscous terms; perfect-gas 2-D Vec4 state and report equations. Actual physical accuracy is limited by case results. |
|   | 2-D compressible Navier-Stokes for a calorically perfect gas written in conservative form; closure p=(gamma-1)rho e; nondimensionalization consistent with case reference quantities; force coefficients defined correctly. | | | |
|   | *Evidence:* Source flux assembly and report equations. | | | |
| 2 | **cfd.mesh** Mesh import and unstructured geometry | 0.10 | 4 | mesh.cpp:86–110 reads CGNS bases/zones, constructs connectivity, geometry and tagged faces; :690–773 partitions. Explicit two-dimensional restriction, no independent mesh geometry oracle. |
|   | Reads CGNS unstructured zones and boundary families; computes cell volumes, face areas/normals/centers; handles mixed cells and multi-zone meshes; builds cell-face adjacency for partitioning and reconstruction. | | | |
|   | *Evidence:* Mesh reader code, geometry tests, partition diagnostics. | | | |
| 3 | **cfd.residual** Conservative finite-volume residual | 0.12 | 4 | numerics.cpp:478–610 computes boundary/interior fluxes and owned-cell residual; driver.cpp:86–112 globally reduces residual norms. Transient and converged physical correctness evaluated separately. |
|   | Cell-centered FV residual with correct face orientation, consistent flux signs, boundary-face treatment; residual assembled once per iteration with global MPI reduction. | | | |
|   | *Evidence:* residual assembly code; residuals.csv shows global reduction. | | | |
| 4 | **cfd.flux** Approximate Riemann flux and entropy fix | 0.08 | 4 | numerics.cpp:132–163 Rusanov, :165–245 Roe with NaN fallback; :576–580 selects flux at interior faces. Submitted M2 cases explicitly use first order. |
|   | Rusanov/LLF minimum or Roe-type with entropy fix; flux matches the stated method; no loss of conservation at boundaries. | | | |
|   | *Evidence:* Flux implementation; metadata inviscid_flux/entropy_fix; code walkthrough. | | | |
| 5 | **cfd.bc** Boundary conditions | 0.08 | 4 | numerics.cpp:490–545 sets pressure-only wall flux and farfield ghost flux with viscous mirror gradients; no-slip surface routine reports zero wall velocity. Separate wall shear limitations remain. |
|   | Farfield, inviscid slip wall, no-slip adiabatic wall; wall treatment honest (boundary_value vs cell_center in surface output); near-zero wall velocity for no-slip; near-zero normal velocity for slip. | | | |
|   | *Evidence:* BC code; surface.csv checks; metadata wall_boundary_output_semantics. | | | |
| 6 | **cfd.reconstruction** Second-order reconstruction | 0.09 | 4 | numerics.cpp:290–369 computes least-squares coefficients and gradients; :71 limited face reconstruction, default active. M2 first-order exception is explicitly reported; no demonstrated order-convergence study. |
|   | Least-squares/Green-Gauss gradients; piecewise-linear face states; reconstruction ACTIVE in production runs; first-order fallback triggers documented and disclosed. | | | |
|   | *Evidence:* Gradient/reconstruction code; metadata reconstruction field; report disclosure. | | | |
| 7 | **cfd.limiter** Limiter and positivity control | 0.08 | 4 | numerics.cpp:372–430 implements Barth–Jespersen limiter and :71 density/pressure fallback; CFD_FIRST_ORDER and limiter disabling options exist, with M2 first order explicitly disclosed. |
|   | Barth-Jespersen or stronger limiter active in production; positivity protection for reconstructed density/pressure and updates. | | | |
|   | *Evidence:* Limiter code; metadata limiter/positivity_preservation; shock-captured fields. | | | |
| 8 | **cfd.viscous** Viscous discretization | 0.10 | 3 | numerics.cpp:1130–1173 integrates full viscous traction as the viscous force split, not tangential skin friction; surface skin friction at :713 is tangential. Stress/heat and no-slip paths exist. |
|   | Velocity/temperature gradients on unstructured cells; Newtonian stress tensor and Fourier heat flux; viscosity matches case Reynolds number; wall-normal gradient treatment; pressure vs tangential skin-friction split. | | | |
|   | *Evidence:* Viscous flux code; forces.csv viscous columns; report formulas. | | | |
| 9 | **cfd.implicit** Implicit steady march | 0.10 | 4 | driver.cpp:470–528 updates reconstruction and executes implicit sweeps; numerics.cpp:806 implements LU-SGS. Nominal residual-based stop can mark strongly evolving forces converged (M2 laminar). |
|   | LU-SGS/matrix-free/block-Jacobi or equivalent with documented Jacobian approximation; CFL ramp/local time stepping honoring case CFL controls; meaningful residual reduction. | | | |
|   | *Evidence:* Implicit solver code; residuals.csv; metadata time_integrator/implicit_solver. | | | |
| 10 | **cfd.transient** Transient BDF2 two-loop method (Re 200) | 0.10 | 2 | driver.cpp:138–164 assembles BDF2/BDF1 full transient residual and :803–861 inner iterations; NaN ratio can evade miss count. Re200 fails by t=68.9; restart CLI is ignored. |
|   | True physical-time outer loop with inner iterations at each step (dt=0.01, t_final=300, 5-1000 inner iterations, inner target 1e-3, frozen BDF2 histories) or stricter documented equivalent; real inner-iteration statistics reported. | | | |
|   | *Evidence:* Time loop code; metadata true_bdf2_inner_loop + inner stats; forces.csv unsteady behavior. | | | |
| 11 | **cfd.mpi_partition** MPI partitioning and scalability | 0.09 | 4 | mesh.cpp:718–731 calls METIS and :771–773 clears global mesh; numerics.cpp:432–478 exchanges local ghost fields. No submitted independent np1/np8 result comparison. |
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

- Overall score (0-5): 3.7
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
