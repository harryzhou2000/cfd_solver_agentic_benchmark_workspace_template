# CFD methods and algorithms review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-29T10:02:03.507548+00:00

**Overall score: 4.6**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **cfd.equations** Governing equations and nondimensionalization | 0.06 | 4.5 | report.tex governing equations and GasModel/ResidualAssembler implement four-variable perfect-gas compressible flow; normal viscous contribution claim in report is quantitatively incorrect. |
|   | 2-D compressible Navier-Stokes for a calorically perfect gas written in conservative form; closure p=(gamma-1)rho e; nondimensionalization consistent with case reference quantities; force coefficients defined correctly. | | | |
|   | *Evidence:* Source flux assembly and report equations. | | | |
| 2 | **cfd.mesh** Mesh import and unstructured geometry | 0.10 | 4.5 | CgnsReader.cpp:54–94,127–138,217–238 and :319–333 handles TRI/QUAD, MIXED and one-to-one zone connectivity; geometry/adjacency source and tests exist. Not every possible CGNS layout was independently exercised. |
|   | Reads CGNS unstructured zones and boundary families; computes cell volumes, face areas/normals/centers; handles mixed cells and multi-zone meshes; builds cell-face adjacency for partitioning and reconstruction. | | | |
|   | *Evidence:* Mesh reader code, geometry tests, partition diagnostics. | | | |
| 3 | **cfd.residual** Conservative finite-volume residual | 0.12 | 5 | ResidualAssembler.cpp:120–194 reconstructs states, computes flux and applies equal/opposite face contributions. Unit tests pass and reported manufactured verification covers smooth fields. |
|   | Cell-centered FV residual with correct face orientation, consistent flux signs, boundary-face treatment; residual assembled once per iteration with global MPI reduction. | | | |
|   | *Evidence:* residual assembly code; residuals.csv shows global reduction. | | | |
| 4 | **cfd.flux** Approximate Riemann flux and entropy fix | 0.08 | 5 | RiemannSolvers.cpp:31–120 implements Roe and entropy fix with alternative Rusanov; ResidualAssembler invokes riemannFlux and positivity fallback. Kernel unit tests pass. |
|   | Rusanov/LLF minimum or Roe-type with entropy fix; flux matches the stated method; no loss of conservation at boundaries. | | | |
|   | *Evidence:* Flux implementation; metadata inviscid_flux/entropy_fix; code walkthrough. | | | |
| 5 | **cfd.bc** Boundary conditions | 0.08 | 4.5 | BoundaryConditions.cpp and ResidualAssembler.cpp implement characteristic farfield, slip and no-slip wall states; original surface data show wall values. Independent general boundary tests not repeated. |
|   | Farfield, inviscid slip wall, no-slip adiabatic wall; wall treatment honest (boundary_value vs cell_center in surface output); near-zero wall velocity for no-slip; near-zero normal velocity for slip. | | | |
|   | *Evidence:* BC code; surface.csv checks; metadata wall_boundary_output_semantics. | | | |
| 6 | **cfd.reconstruction** Second-order reconstruction | 0.09 | 4 | WLS gradient and piecewise-linear reconstruction are present; committed verification_table.tex shows reconstruction order near 1.95 on triangles but the residual operator only 0.96, a material spatial-order limitation disclosed in report.tex:419–429. |
|   | Least-squares/Green-Gauss gradients; piecewise-linear face states; reconstruction ACTIVE in production runs; first-order fallback triggers documented and disclosed. | | | |
|   | *Evidence:* Gradient/reconstruction code; metadata reconstruction field; report disclosure. | | | |
| 7 | **cfd.limiter** Limiter and positivity control | 0.08 | 4.5 | Limiters.cpp implements Venkatakrishnan and Barth–Jespersen; SolverDriver.cpp:514–521 freezes values rather than disabling their action, and metadata records limiter diagnostics. Full sensitivity beyond submitted comparisons was not rerun. |
|   | Barth-Jespersen or stronger limiter active in production; positivity protection for reconstructed density/pressure and updates. | | | |
|   | *Evidence:* Limiter code; metadata limiter/positivity_preservation; shock-captured fields. | | | |
| 8 | **cfd.viscous** Viscous discretization | 0.10 | 4 | ViscousFlux.h and ResidualAssembler.cpp:232–251 compute full and tangential traction separately; metadata records both, but forces.csv viscous_drag includes full traction and report.tex:220–221 incorrectly claims normal contribution <1% for all cases (M2 laminar is 26.6% of viscous drag). |
|   | Velocity/temperature gradients on unstructured cells; Newtonian stress tensor and Fourier heat flux; viscosity matches case Reynolds number; wall-normal gradient treatment; pressure vs tangential skin-friction split. | | | |
|   | *Evidence:* Viscous flux code; forces.csv viscous columns; report formulas. | | | |
| 9 | **cfd.implicit** Implicit steady march | 0.10 | 5 | ImplicitSolver.cpp and SolverDriver.cpp implement implicit marching, CFL ramp and local pseudotime. Submitted steady results and rank runs are readable; kernel test passed. |
|   | LU-SGS/matrix-free/block-Jacobi or equivalent with documented Jacobian approximation; CFL ramp/local time stepping honoring case CFL controls; meaningful residual reduction. | | | |
|   | *Evidence:* Implicit solver code; residuals.csv; metadata time_integrator/implicit_solver. | | | |
| 10 | **cfd.transient** Transient BDF2 two-loop method (Re 200) | 0.10 | 4.5 | SolverDriver.cpp:639–707 uses BDF1 startup, full BDF2 transient residual and frozen histories inside inner iterations; Re200 metadata records dt=0.01, t=300, target 1e-3 with zero misses. Report acknowledges underpredicted shedding and wake diffusion; no Re200 rerun. |
|   | True physical-time outer loop with inner iterations at each step (dt=0.01, t_final=300, 5-1000 inner iterations, inner target 1e-3, frozen BDF2 histories) or stricter documented equivalent; real inner-iteration statistics reported. | | | |
|   | *Evidence:* Time loop code; metadata true_bdf2_inner_loop + inner stats; forces.csv unsteady behavior. | | | |
| 11 | **cfd.mpi_partition** MPI partitioning and scalability | 0.09 | 5 | METIS source and neighbor halos are present; submitted np1/2/4/8 NACA/cylinder complete histories agree in drag within 9e-8 and 3.2e-7 respectively, with reported timing. |
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

- Overall score (0-5): 4.6
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
