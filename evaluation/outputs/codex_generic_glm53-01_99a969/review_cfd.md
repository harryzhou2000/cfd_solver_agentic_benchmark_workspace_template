# CFD methods and algorithms review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-30T19:32:24.491743+00:00

**Overall score: 3.42**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **cfd.equations** Governing equations and nondimensionalization | 0.06 | 4 | physics.hpp implements calorically perfect gas conservative variables, Roe/Rusanov and Newtonian-Fourier flux. Thermal gradient indexing defect recorded separately. |
|   | 2-D compressible Navier-Stokes for a calorically perfect gas written in conservative form; closure p=(gamma-1)rho e; nondimensionalization consistent with case reference quantities; force coefficients defined correctly. | | | |
|   | *Evidence:* Source flux assembly and report equations. | | | |
| 2 | **cfd.mesh** Mesh import and unstructured geometry | 0.10 | 4 | main.cpp:81–84,151–168 imports CGNS and partitions on root, scatters local mesh and frees global before run; selected metadata gives 8-rank ownership/ghosts. |
|   | Reads CGNS unstructured zones and boundary families; computes cell volumes, face areas/normals/centers; handles mixed cells and multi-zone meshes; builds cell-face adjacency for partitioning and reconstruction. | | | |
|   | *Evidence:* Mesh reader code, geometry tests, partition diagnostics. | | | |
| 3 | **cfd.residual** Conservative finite-volume residual | 0.12 | 4 | bf6e816 fv_solver.cpp:365–395 reconstructs and assembles Riemann face flux, :397–447 adds directional-corrected viscous flux; temperature gradient uses wrong primitive slot. |
|   | Cell-centered FV residual with correct face orientation, consistent flux signs, boundary-face treatment; residual assembled once per iteration with global MPI reduction. | | | |
|   | *Evidence:* residual assembly code; residuals.csv shows global reduction. | | | |
| 4 | **cfd.flux** Approximate Riemann flux and entropy fix | 0.08 | 4 | physics.hpp:58–133 implements Rusanov and Roe Harten entropy fix; manifest uses Roe for M080 inviscid and Re200, Rusanov for other production cases. |
|   | Rusanov/LLF minimum or Roe-type with entropy fix; flux matches the stated method; no loss of conservation at boundaries. | | | |
|   | *Evidence:* Flux implementation; metadata inviscid_flux/entropy_fix; code walkthrough. | | | |
| 5 | **cfd.bc** Boundary conditions | 0.08 | 4 | physics.hpp:153 onwards implements farfield, slip and no-slip adiabatic ghosts; fv_solver.cpp:694–725 outputs no-slip zero velocity and projected tangential friction. |
|   | Farfield, inviscid slip wall, no-slip adiabatic wall; wall treatment honest (boundary_value vs cell_center in surface output); near-zero wall velocity for no-slip; near-zero normal velocity for slip. | | | |
|   | *Evidence:* BC code; surface.csv checks; metadata wall_boundary_output_semantics. | | | |
| 6 | **cfd.reconstruction** Second-order reconstruction | 0.09 | 4 | bf6e816 fv_solver.cpp:229–283 computes and exchanges Green–Gauss primitive gradients and :343–389 reconstructs faces with positivity fallback. Frozen per-inner coefficients and steady limiter freeze are disclosed. |
|   | Least-squares/Green-Gauss gradients; piecewise-linear face states; reconstruction ACTIVE in production runs; first-order fallback triggers documented and disclosed. | | | |
|   | *Evidence:* Gradient/reconstruction code; metadata reconstruction field; report disclosure. | | | |
| 7 | **cfd.limiter** Limiter and positivity control | 0.08 | 4 | bf6e816 fv_solver.cpp:283 onward computes single Barth–Jespersen coefficient; :1049–1085 can freeze it on steady chatter, disclosed in report.tex:180–187,770–775. |
|   | Barth-Jespersen or stronger limiter active in production; positivity protection for reconstructed density/pressure and updates. | | | |
|   | *Evidence:* Limiter code; metadata limiter/positivity_preservation; shock-captured fields. | | | |
| 8 | **cfd.viscous** Viscous discretization | 0.10 | 3 | Production bf6e816 and final b962bf5 fv_solver.cpp:229–255,421–437 read pressure gradient for temperature slot via grad_off(1+q); correction replaces along-connecting-direction component but generally retains wrong transverse component. :449–462 and :718–725 project tangential shear; no-slip surface velocity is zero. |
|   | Velocity/temperature gradients on unstructured cells; Newtonian stress tensor and Fourier heat flux; viscosity matches case Reynolds number; wall-normal gradient treatment; pressure vs tangential skin-friction split. | | | |
|   | *Evidence:* Viscous flux code; forces.csv viscous columns; report formulas. | | | |
| 9 | **cfd.implicit** Implicit steady march | 0.10 | 2 | Production bf6e816 fv_solver.cpp:1049–1094 uses SGS with lagged gradients/limiter; selected steady metadata records every inner target missed, but separate outer target and force plateau govern convergence. |
|   | LU-SGS/matrix-free/block-Jacobi or equivalent with documented Jacobian approximation; CFL ramp/local time stepping honoring case CFL controls; meaningful residual reduction. | | | |
|   | *Evidence:* Implicit solver code; residuals.csv; metadata time_integrator/implicit_solver. | | | |
| 10 | **cfd.transient** Transient BDF2 two-loop method (Re 200) | 0.10 | 3 | bf6e816 fv_solver.cpp:893–926 refreshes gradients/limiter once per physical step and tests frozen-coefficient residual; zero reported misses do not prove fully refreshed nonlinear target. St≈0.11281 for t>=105 but ≈0.075 peak for t>=100. |
|   | True physical-time outer loop with inner iterations at each step (dt=0.01, t_final=300, 5-1000 inner iterations, inner target 1e-3, frozen BDF2 histories) or stricter documented equivalent; real inner-iteration statistics reported. | | | |
|   | *Evidence:* Time loop code; metadata true_bdf2_inner_loop + inner stats; forces.csv unsteady behavior. | | | |
| 11 | **cfd.mpi_partition** MPI partitioning and scalability | 0.09 | 2 | Same revision bf6e816 and matching step-0 Re20 force rows, but settled np4/8 Cl differs by 0.40238 and Cd by 0.07007; Selected root session confirms matching np4 controls and fresh start; DQ4 applies. Numerical rank sensitivity material. |
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

- Overall score (0-5): 3.42
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
