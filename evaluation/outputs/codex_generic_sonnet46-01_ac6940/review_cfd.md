# CFD methods and algorithms review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-30T07:31:55.701190+00:00

**Overall score: 3.53**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **cfd.equations** Governing equations and nondimensionalization | 0.06 | 4 | Physics.hpp provides a four-variable 2-D compressible state, ideal-gas flux and constant-viscosity treatment; report.tex:60–123 describes governing equations. |
|   | 2-D compressible Navier-Stokes for a calorically perfect gas written in conservative form; closure p=(gamma-1)rho e; nondimensionalization consistent with case reference quantities; force coefficients defined correctly. | | | |
|   | *Evidence:* Source flux assembly and report equations. | | | |
| 2 | **cfd.mesh** Mesh import and unstructured geometry | 0.10 | 4 | CgnsReader.cpp:40–59,215–239 reads TRI_3/QUAD_4/BAR_2 in all zones of the first CGNS base; unknown/MIXED sections are skipped. MeshBuilder constructs geometry and adjacency. |
|   | Reads CGNS unstructured zones and boundary families; computes cell volumes, face areas/normals/centers; handles mixed cells and multi-zone meshes; builds cell-face adjacency for partitioning and reconstruction. | | | |
|   | *Evidence:* Mesh reader code, geometry tests, partition diagnostics. | | | |
| 3 | **cfd.residual** Conservative finite-volume residual | 0.12 | 4 | Residual.cpp:31–72 assembles interior flux with opposite signs; :110–155 handles boundary and wall viscous contributions. |
|   | Cell-centered FV residual with correct face orientation, consistent flux signs, boundary-face treatment; residual assembled once per iteration with global MPI reduction. | | | |
|   | *Evidence:* residual assembly code; residuals.csv shows global reduction. | | | |
| 4 | **cfd.flux** Approximate Riemann flux and entropy fix | 0.08 | 4 | Physics.hpp:59 implements local Lax–Friedrichs/Rusanov flux; no higher-resolution Riemann solver credit is assumed. |
|   | Rusanov/LLF minimum or Roe-type with entropy fix; flux matches the stated method; no loss of conservation at boundaries. | | | |
|   | *Evidence:* Flux implementation; metadata inviscid_flux/entropy_fix; code walkthrough. | | | |
| 5 | **cfd.bc** Boundary conditions | 0.08 | 4 | Physics.hpp:115–182 implements farfield, slip and no-slip states; Residual.cpp:134–155 applies pressure-only inviscid and viscous no-slip wall fluxes. |
|   | Farfield, inviscid slip wall, no-slip adiabatic wall; wall treatment honest (boundary_value vs cell_center in surface output); near-zero wall velocity for no-slip; near-zero normal velocity for slip. | | | |
|   | *Evidence:* BC code; surface.csv checks; metadata wall_boundary_output_semantics. | | | |
| 6 | **cfd.reconstruction** Second-order reconstruction | 0.09 | 3.5 | Gradient.cpp:15–39 and Reconstruction.cpp:45–56 implement weighted LS face reconstruction. Final SteadySolver.cpp:92–105,139–145 delays M≥0.5 inviscid reconstruction beyond selected run lengths; historical binary is unbound. |
|   | Least-squares/Green-Gauss gradients; piecewise-linear face states; reconstruction ACTIVE in production runs; first-order fallback triggers documented and disclosed. | | | |
|   | *Evidence:* Gradient/reconstruction code; metadata reconstruction field; report disclosure. | | | |
| 7 | **cfd.limiter** Limiter and positivity control | 0.08 | 3.5 | Reconstruction.cpp:6–41 implements Barth limiting and Residual.cpp:40–48 falls back on nonpositive face states. Final-source long first-order startup and unbound historic binary limit production-order evidence. |
|   | Barth-Jespersen or stronger limiter active in production; positivity protection for reconstructed density/pressure and updates. | | | |
|   | *Evidence:* Limiter code; metadata limiter/positivity_preservation; shock-captured fields. | | | |
| 8 | **cfd.viscous** Viscous discretization | 0.10 | 3 | OutputManager.cpp:118–135 uses full viscous traction in force integration; :203–217 projects tangential shear for cf. Report.tex:595–598 acknowledges negative viscous drag. Surface boundary velocities are zero on no-slip at :190–194,224. |
|   | Velocity/temperature gradients on unstructured cells; Newtonian stress tensor and Fourier heat flux; viscosity matches case Reynolds number; wall-normal gradient treatment; pressure vs tangential skin-friction split. | | | |
|   | *Evidence:* Viscous flux code; forces.csv viscous columns; report formulas. | | | |
| 9 | **cfd.implicit** Implicit steady march | 0.10 | 4 | SteadySolver.cpp and ImplicitSolver.cpp implement pseudo-time continuation and LU-SGS; selected M2 laminar diverged before supplied target. |
|   | LU-SGS/matrix-free/block-Jacobi or equivalent with documented Jacobian approximation; CFL ramp/local time stepping honoring case CFL controls; meaningful residual reduction. | | | |
|   | *Evidence:* Implicit solver code; residuals.csv; metadata time_integrator/implicit_solver. | | | |
| 10 | **cfd.transient** Transient BDF2 two-loop method (Re 200) | 0.10 | 2.5 | Final TransientSolver.cpp:182–214 measures the inner ratio after adding pseudo-time regularization; selected Re200 residual history has 943 steps at 2000 iterations, above configured/metadata maximum1000. Historical binary attribution and actual strict full-residual misses remain unknown. |
|   | True physical-time outer loop with inner iterations at each step (dt=0.01, t_final=300, 5-1000 inner iterations, inner target 1e-3, frozen BDF2 histories) or stricter documented equivalent; real inner-iteration statistics reported. | | | |
|   | *Evidence:* Time loop code; metadata true_bdf2_inner_loop + inner stats; forces.csv unsteady behavior. | | | |
| 11 | **cfd.mpi_partition** MPI partitioning and scalability | 0.09 | 2.5 | METIS and neighbor halos exist, but main.cpp:89–109,136–156 retains full mesh on every rank through solver iterations. NACA np8 lift≈0.0303 differs materially from near-zero np1; the cylinder np8 result is a failed 200-step pilot. |
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

- Overall score (0-5): 3.53
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
