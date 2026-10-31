# CFD methods and algorithms review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-31T13:28:12.661090+00:00

**Overall score: 4.53**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **cfd.equations** Governing equations and nondimensionalization | 0.06 | 5 | core/types.hpp and physics/perfect_gas.cpp implement 2D conservative perfect-gas equations; report.tex describes nondimensional equations. |
|   | 2-D compressible Navier-Stokes for a calorically perfect gas written in conservative form; closure p=(gamma-1)rho e; nondimensionalization consistent with case reference quantities; force coefficients defined correctly. | | | |
|   | *Evidence:* Source flux assembly and report equations. | | | |
| 2 | **cfd.mesh** Mesh import and unstructured geometry | 0.10 | 4.5 | cgns_reader.cpp supports TRI/QUAD/MIXED and multiple zones with interface welding; production Re200 log reports both zones and 10185 merged cells. No 3D support. |
|   | Reads CGNS unstructured zones and boundary families; computes cell volumes, face areas/normals/centers; handles mixed cells and multi-zone meshes; builds cell-face adjacency for partitioning and reconstruction. | | | |
|   | *Evidence:* Mesh reader code, geometry tests, partition diagnostics. | | | |
| 3 | **cfd.residual** Conservative finite-volume residual | 0.12 | 4.5 | numerics/residual.cpp constructs interior and boundary FV fluxes, with positivity fallback. Eight bounded verification checks passed including first- and second-order conservation; these do not prove full-run convergence. |
|   | Cell-centered FV residual with correct face orientation, consistent flux signs, boundary-face treatment; residual assembled once per iteration with global MPI reduction. | | | |
|   | *Evidence:* residual assembly code; residuals.csv shows global reduction. | | | |
| 4 | **cfd.flux** Approximate Riemann flux and entropy fix | 0.08 | 4.5 | numerics/riemann.cpp:94–166 implements Roe/Harten-Yee with Rusanov fallback; HLLC and Rusanov alternatives exist. Re200 exact old binary remains unbound. |
|   | Rusanov/LLF minimum or Roe-type with entropy fix; flux matches the stated method; no loss of conservation at boundaries. | | | |
|   | *Evidence:* Flux implementation; metadata inviscid_flux/entropy_fix; code walkthrough. | | | |
| 5 | **cfd.bc** Boundary conditions | 0.08 | 4.5 | numerics/boundary_conditions.cpp and residual.cpp:264–311 implement farfield, slip and adiabatic no-slip wall paths; wall output semantics use boundary values. |
|   | Farfield, inviscid slip wall, no-slip adiabatic wall; wall treatment honest (boundary_value vs cell_center in surface output); near-zero wall velocity for no-slip; near-zero normal velocity for slip. | | | |
|   | *Evidence:* BC code; surface.csv checks; metadata wall_boundary_output_semantics. | | | |
| 6 | **cfd.reconstruction** Second-order reconstruction | 0.09 | 4.5 | residual.cpp:115–190 reconstructs limited face states; seven steady metadata entries revision b350651 report active second order with documented fallback. Re200 full-run fallback count was not measured on older binary. |
|   | Least-squares/Green-Gauss gradients; piecewise-linear face states; reconstruction ACTIVE in production runs; first-order fallback triggers documented and disclosed. | | | |
|   | *Evidence:* Gradient/reconstruction code; metadata reconstruction field; report disclosure. | | | |
| 7 | **cfd.limiter** Limiter and positivity control | 0.08 | 4.5 | Venkat and positivity guard exist in source. Steady M2 selected metadata reports 595 fallback events on 324 steps; contestant docs/binary_provenance.md:117–120 says 326, a stale/inconsistent count. Re200 production fallback counts were not measured; a separate one-rank first-100-step probe cannot establish the remaining trajectory. |
|   | Barth-Jespersen or stronger limiter active in production; positivity protection for reconstructed density/pressure and updates. | | | |
|   | *Evidence:* Limiter code; metadata limiter/positivity_preservation; shock-captured fields. | | | |
| 8 | **cfd.viscous** Viscous discretization | 0.10 | 4.5 | numerics/viscous.cpp implements stress/heat transport and solver/forces.cpp:122–131 projects wall traction tangentially; reported pressure/viscous split is not a full normal traction mislabel. |
|   | Velocity/temperature gradients on unstructured cells; Newtonian stress tensor and Fourier heat flux; viscosity matches case Reynolds number; wall-normal gradient treatment; pressure vs tangential skin-friction split. | | | |
|   | *Evidence:* Viscous flux code; forces.csv viscous columns; report formulas. | | | |
| 9 | **cfd.implicit** Implicit steady march | 0.10 | 4.5 | solver/implicit_lusgs.cpp contains implicit spectral-radius LU-SGS sweeps; steady stopping checks residual and Cd window. Selected M015 laminar lift drift qualifies the Cd-only check. |
|   | LU-SGS/matrix-free/block-Jacobi or equivalent with documented Jacobian approximation; CFL ramp/local time stepping honoring case CFL controls; meaningful residual reduction. | | | |
|   | *Evidence:* Implicit solver code; residuals.csv; metadata time_integrator/implicit_solver. | | | |
| 10 | **cfd.transient** Transient BDF2 two-loop method (Re 200) | 0.10 | 4.5 | Final transient source assembles total BDF residual and measures inner ratio (transient_driver.cpp:185–209,246–264,288–294); selected Re200 metadata claims 0/30000 misses, log provides sampled ratios and t300. Exact older production binary is not independently bound; no unsteady rerun. |
|   | True physical-time outer loop with inner iterations at each step (dt=0.01, t_final=300, 5-1000 inner iterations, inner target 1e-3, frozen BDF2 histories) or stricter documented equivalent; real inner-iteration statistics reported. | | | |
|   | *Evidence:* Time loop code; metadata true_bdf2_inner_loop + inner stats; forces.csv unsteady behavior. | | | |
| 11 | **cfd.mpi_partition** MPI partitioning and scalability | 0.09 | 4.5 | main.cpp:285–325 root preprocesses/partitions and destroys global mesh; halo_exchange.cpp:59–70 performs neighbor nonblocking exchanges. Submitted np1/np8 comparisons lack order-one absolute force mismatch. |
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

- Overall score (0-5): 4.53
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
