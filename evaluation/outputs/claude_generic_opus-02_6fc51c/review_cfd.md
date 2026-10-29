# CFD methods and algorithms review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-29T10:56:39.161554+00:00

**Overall score: 3.41**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **cfd.equations** Governing equations and nondimensionalization | 0.06 | 4 | report.tex governing equations and solver.cpp residual/flux source implement compressible conservation; spatial scheme and output limitations are separately noted. |
|   | 2-D compressible Navier-Stokes for a calorically perfect gas written in conservative form; closure p=(gamma-1)rho e; nondimensionalization consistent with case reference quantities; force coefficients defined correctly. | | | |
|   | *Evidence:* Source flux assembly and report equations. | | | |
| 2 | **cfd.mesh** Mesh import and unstructured geometry | 0.10 | 3.5 | mesh.cpp:48 and :270–282 read zones and TRI/QUAD sections, builds geometry and graph; general MIXED elements and multi-zone cases were not independently validated. |
|   | Reads CGNS unstructured zones and boundary families; computes cell volumes, face areas/normals/centers; handles mixed cells and multi-zone meshes; builds cell-face adjacency for partitioning and reconstruction. | | | |
|   | *Evidence:* Mesh reader code, geometry tests, partition diagnostics. | | | |
| 3 | **cfd.residual** Conservative finite-volume residual | 0.12 | 4 | solver.cpp:455 onward assembles conservative face residuals; global norm reductions :616–620. All steady histories reach specified reductions, but formal order is not demonstrated. |
|   | Cell-centered FV residual with correct face orientation, consistent flux signs, boundary-face treatment; residual assembled once per iteration with global MPI reduction. | | | |
|   | *Evidence:* residual assembly code; residuals.csv shows global reduction. | | | |
| 4 | **cfd.flux** Approximate Riemann flux and entropy fix | 0.08 | 3.5 | solver.cpp:254 onward implements Rusanov; no Roe or higher-resolution flux is required, but large numerical drag indicates substantial limitations in supplied steady results. |
|   | Rusanov/LLF minimum or Roe-type with entropy fix; flux matches the stated method; no loss of conservation at boundaries. | | | |
|   | *Evidence:* Flux implementation; metadata inviscid_flux/entropy_fix; code walkthrough. | | | |
| 5 | **cfd.bc** Boundary conditions | 0.08 | 4 | solver.cpp boundary_ghost_state and io.cpp:72–99 implement boundary states; no-slip output explicitly gives wall values. General boundary variants were not independently tested. |
|   | Farfield, inviscid slip wall, no-slip adiabatic wall; wall treatment honest (boundary_value vs cell_center in surface output); near-zero wall velocity for no-slip; near-zero normal velocity for slip. | | | |
|   | *Evidence:* BC code; surface.csv checks; metadata wall_boundary_output_semantics. | | | |
| 6 | **cfd.reconstruction** Second-order reconstruction | 0.09 | 2 | solver.cpp:768–775 zeros gradients and limiters on every steady iteration; second-order reconstruction/limiter is used only in transient. report.tex:91–93 and :561–564 disclose this; steady metadata nevertheless claims spatial_order_claimed=2. |
|   | Least-squares/Green-Gauss gradients; piecewise-linear face states; reconstruction ACTIVE in production runs; first-order fallback triggers documented and disclosed. | | | |
|   | *Evidence:* Gradient/reconstruction code; metadata reconstruction field; report disclosure. | | | |
| 7 | **cfd.limiter** Limiter and positivity control | 0.08 | 2.5 | solver.cpp:230–295 implements Barth–Jespersen and reconstruction; it is explicitly disabled in all steady runs and computed once per transient physical step. Report discloses this. |
|   | Barth-Jespersen or stronger limiter active in production; positivity protection for reconstructed density/pressure and updates. | | | |
|   | *Evidence:* Limiter code; metadata limiter/positivity_preservation; shock-captured fields. | | | |
| 8 | **cfd.viscous** Viscous discretization | 0.10 | 2.5 | solver.cpp:399–452 computes viscous gradients and stress, but :645–689 integrates full stress vector without tangential projection; report calls this friction drag. Large reported laminar and Re20 drag limit credibility. |
|   | Velocity/temperature gradients on unstructured cells; Newtonian stress tensor and Fourier heat flux; viscosity matches case Reynolds number; wall-normal gradient treatment; pressure vs tangential skin-friction split. | | | |
|   | *Evidence:* Viscous flux code; forces.csv viscous columns; report formulas. | | | |
| 9 | **cfd.implicit** Implicit steady march | 0.10 | 4 | solver.cpp:500–607 implements scalar spectral-radius LU-SGS with local pseudo time; steady runs reach reported residual targets. Full discretization order and restart are separate limitations. |
|   | LU-SGS/matrix-free/block-Jacobi or equivalent with documented Jacobian approximation; CFL ramp/local time stepping honoring case CFL controls; meaningful residual reduction. | | | |
|   | *Evidence:* Implicit solver code; residuals.csv; metadata time_integrator/implicit_solver. | | | |
| 10 | **cfd.transient** Transient BDF2 two-loop method (Re 200) | 0.10 | 3.5 | solver.cpp:840–898 implements BDF1/BDF2 full transient residual and inner iteration, with gradients frozen per physical step; metadata gives dt=0.01, t=300 and zero target misses. Residual CSV dt field remains zero and no unsteady rerun was performed. |
|   | True physical-time outer loop with inner iterations at each step (dt=0.01, t_final=300, 5-1000 inner iterations, inner target 1e-3, frozen BDF2 histories) or stricter documented equivalent; real inner-iteration statistics reported. | | | |
|   | *Evidence:* Time loop code; metadata true_bdf2_inner_loop + inner stats; forces.csv unsteady behavior. | | | |
| 11 | **cfd.mpi_partition** MPI partitioning and scalability | 0.09 | 4 | METIS and neighbor exchanges as above; np2/np8 original histories match closely for NACA and cylinder. Global mesh temporarily exists on all ranks during preprocessing, then local mesh is returned; no np1 full run verified. |
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

- Overall score (0-5): 3.41
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
