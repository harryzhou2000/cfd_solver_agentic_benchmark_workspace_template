# CFD methods and algorithms review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-30T10:24:24.526193+00:00

**Overall score: 3.08**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **cfd.equations** Governing equations and nondimensionalization | 0.06 | 4 | Compressible NS source and nondimensional report present; independent force histories show material numerical failures. |
|   | 2-D compressible Navier-Stokes for a calorically perfect gas written in conservative form; closure p=(gamma-1)rho e; nondimensionalization consistent with case reference quantities; force coefficients defined correctly. | | | |
|   | *Evidence:* Source flux assembly and report equations. | | | |
| 2 | **cfd.mesh** Mesh import and unstructured geometry | 0.10 | 4 | CGNS reader and global/local graph structures support required inputs; complete mesh remains replicated on all ranks during iteration. |
|   | Reads CGNS unstructured zones and boundary families; computes cell volumes, face areas/normals/centers; handles mixed cells and multi-zone meshes; builds cell-face adjacency for partitioning and reconstruction. | | | |
|   | *Evidence:* Mesh reader code, geometry tests, partition diagnostics. | | | |
| 3 | **cfd.residual** Conservative finite-volume residual | 0.12 | 3.5 | solver.cpp:133–210 reconstructs face states and assembles inviscid/viscous residual; numerical accuracy remains weak in delivered results. |
|   | Cell-centered FV residual with correct face orientation, consistent flux signs, boundary-face treatment; residual assembled once per iteration with global MPI reduction. | | | |
|   | *Evidence:* residual assembly code; residuals.csv shows global reduction. | | | |
| 4 | **cfd.flux** Approximate Riemann flux and entropy fix | 0.08 | 3.5 | solver.cpp:179 uses Rusanov; no stronger flux/entropy implementation or independent order verification established. |
|   | Rusanov/LLF minimum or Roe-type with entropy fix; flux matches the stated method; no loss of conservation at boundaries. | | | |
|   | *Evidence:* Flux implementation; metadata inviscid_flux/entropy_fix; code walkthrough. | | | |
| 5 | **cfd.bc** Boundary conditions | 0.08 | 4 | Farfield, slip and no-slip source paths exist; output.cpp:102–105 explicitly writes zero no-slip boundary velocity. |
|   | Farfield, inviscid slip wall, no-slip adiabatic wall; wall treatment honest (boundary_value vs cell_center in surface output); near-zero wall velocity for no-slip; near-zero normal velocity for slip. | | | |
|   | *Evidence:* BC code; surface.csv checks; metadata wall_boundary_output_semantics. | | | |
| 6 | **cfd.reconstruction** Second-order reconstruction | 0.09 | 3.5 | solver.cpp:140–151 uses Green–Gauss/Barth unless CFD2D_FIRST_ORDER is set. Selected earlier session includes first-order experiments; production environment and exact binary remain unbound. |
|   | Least-squares/Green-Gauss gradients; piecewise-linear face states; reconstruction ACTIVE in production runs; first-order fallback triggers documented and disclosed. | | | |
|   | *Evidence:* Gradient/reconstruction code; metadata reconstruction field; report disclosure. | | | |
| 7 | **cfd.limiter** Limiter and positivity control | 0.08 | 3.5 | Barth limiter and positivity fallback are implemented; full historical production environment and order verification remain unbound. |
|   | Barth-Jespersen or stronger limiter active in production; positivity protection for reconstructed density/pressure and updates. | | | |
|   | *Evidence:* Limiter code; metadata limiter/positivity_preservation; shock-captured fields. | | | |
| 8 | **cfd.viscous** Viscous discretization | 0.10 | 1.5 | solver.cpp:356–368 integrates full traction, not tangential skin friction; output.cpp:107–109 hardcodes surface cf=0. Recovered viscous/cylinder surfaces confirm zero cf, despite nonzero integrated viscous drag. |
|   | Velocity/temperature gradients on unstructured cells; Newtonian stress tensor and Fourier heat flux; viscosity matches case Reynolds number; wall-normal gradient treatment; pressure vs tangential skin-friction split. | | | |
|   | *Evidence:* Viscous flux code; forces.csv viscous columns; report formulas. | | | |
| 9 | **cfd.implicit** Implicit steady march | 0.10 | 3 | LU-SGS source exists but capped CFL and evolving forces limit demonstrated performance; Re200 uses relaxed acceptance. |
|   | LU-SGS/matrix-free/block-Jacobi or equivalent with documented Jacobian approximation; CFL ramp/local time stepping honoring case CFL controls; meaningful residual reduction. | | | |
|   | *Evidence:* Implicit solver code; residuals.csv; metadata time_integrator/implicit_solver. | | | |
| 10 | **cfd.transient** Transient BDF2 two-loop method (Re 200) | 0.10 | 1.5 | Selected final response identifies eight completed cases and canonical Re200 dt0.005/t300; workspace metadata SHA256 0d71cdc2ff36da0714c032a813c3ef2edfba7b470aa2009b7fea7b6c2a17e94a reports completed/statistically_periodic, target0.001, zero misses/fraction1, last ratio0.998594. Submitted 1e29ab3c main.cpp:272–275 relaxes acceptance to max(target,0.1), :307–314 also accepts any reduction at the cap, :317–322 counts that as target success and :348–349 declares periodic. Workspace forces SHA256 399a6ab0c64cfff3da086b9fbe767305828dfce5280cc6e20b090009df8d2436 over t180–300 show Cd21.01216..21.23840 and Cl3.74469..3.82989, with no substantiated periodic shedding. The final ratio directly misses target; relaxed success count does not establish actual strict miss proportion, and exact historic binary is unbound. |
|   | True physical-time outer loop with inner iterations at each step (dt=0.01, t_final=300, 5-1000 inner iterations, inner target 1e-3, frozen BDF2 histories) or stricter documented equivalent; real inner-iteration statistics reported. | | | |
|   | *Evidence:* Time loop code; metadata true_bdf2_inner_loop + inner stats; forces.csv unsteady behavior. | | | |
| 11 | **cfd.mpi_partition** MPI partitioning and scalability | 0.09 | 2.5 | Immutable 1e29ab3c main.cpp:83–86 reads complete CGNS mesh on every rank; :92–100 builds/partitions a GlobalMesh on every rank. Both rawMesh and gm remain in scope throughout steady loop :150–160 and transient loop; no release occurs. TASK.md:46,227 prohibit full mesh replication during iterations, separately from numbered DQ9 even when METIS is real. |
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

- Overall score (0-5): 3.08
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
