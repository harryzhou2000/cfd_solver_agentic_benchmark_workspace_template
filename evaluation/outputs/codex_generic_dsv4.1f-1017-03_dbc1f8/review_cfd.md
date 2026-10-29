# CFD methods and algorithms review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-29T09:30:59.047049+00:00

**Overall score: 3.68**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **cfd.equations** Governing equations and nondimensionalization | 0.06 | 4 | report.tex:44–79 describes nondimensional compressible Navier–Stokes; solver/src/common.h:24–27 and :53–69 define four conserved variables and perfect-gas conversion. Full independent derivation verification is not established. |
|   | 2-D compressible Navier-Stokes for a calorically perfect gas written in conservative form; closure p=(gamma-1)rho e; nondimensionalization consistent with case reference quantities; force coefficients defined correctly. | | | |
|   | *Evidence:* Source flux assembly and report equations. | | | |
| 2 | **cfd.mesh** Mesh import and unstructured geometry | 0.10 | 3.5 | cgns_reader.cpp:34–58 loops unstructured zones and maps boundary edges; :115–121 accepts TRI_3, QUAD_4, BAR_2 section types but rejects CGNS MIXED element sections. global_mesh.cpp:99–190 constructs edge adjacency and polygon geometry; supplied packages exercise the meshes but general multi-zone/mixed layouts were not independently executed. |
|   | Reads CGNS unstructured zones and boundary families; computes cell volumes, face areas/normals/centers; handles mixed cells and multi-zone meshes; builds cell-face adjacency for partitioning and reconstruction. | | | |
|   | *Evidence:* Mesh reader code, geometry tests, partition diagnostics. | | | |
| 3 | **cfd.residual** Conservative finite-volume residual | 0.12 | 4 | solver.cpp:781–820 assembles interior numerical and viscous face fluxes; :870–874 adds equal-and-opposite interior contributions to owned cells. Supplied field and residual histories are available, but no independent convergence-order test was run. |
|   | Cell-centered FV residual with correct face orientation, consistent flux signs, boundary-face treatment; residual assembled once per iteration with global MPI reduction. | | | |
|   | *Evidence:* residual assembly code; residuals.csv shows global reduction. | | | |
| 4 | **cfd.flux** Approximate Riemann flux and entropy fix | 0.08 | 4 | solver.cpp contains HLLC, Roe and Rusanov implementations, selected at :797–799; metadata records HLLC for seven cases and Rusanov for the disclosed M0.15 first-order fallback. A complete independent flux verification was not run. |
|   | Rusanov/LLF minimum or Roe-type with entropy fix; flux matches the stated method; no loss of conservation at boundaries. | | | |
|   | *Evidence:* Flux implementation; metadata inviscid_flux/entropy_fix; code walkthrough. | | | |
| 5 | **cfd.bc** Boundary conditions | 0.08 | 4 | solver.cpp:823–867 handles farfield, slip and no-slip adiabatic wall fluxes; reported wall-state and surface files provide submitted checks. Independent analytical wall and farfield tests were not repeated. |
|   | Farfield, inviscid slip wall, no-slip adiabatic wall; wall treatment honest (boundary_value vs cell_center in surface output); near-zero wall velocity for no-slip; near-zero normal velocity for slip. | | | |
|   | *Evidence:* BC code; surface.csv checks; metadata wall_boundary_output_semantics. | | | |
| 6 | **cfd.reconstruction** Second-order reconstruction | 0.09 | 3 | solver/src/solver.cpp:419–478 assembles weighted least-squares gradients and :371–410 reconstructs face states. Report.tex:554–567 discloses production slope factor 0.5 and first-order fallback for M0.15 inviscid, materially limiting accuracy. This is implementation evidence rather than a measured convergence-order claim. |
|   | Least-squares/Green-Gauss gradients; piecewise-linear face states; reconstruction ACTIVE in production runs; first-order fallback triggers documented and disclosed. | | | |
|   | *Evidence:* Gradient/reconstruction code; metadata reconstruction field; report disclosure. | | | |
| 7 | **cfd.limiter** Limiter and positivity control | 0.08 | 3 | solver/src/solver.cpp:508–545 applies neighbor bounds, positivity floor and slope factors. Report.tex:563–567 discloses the first-order M0.15 inviscid fallback and 0.5 production slope elsewhere; no claim of uniform full-order behavior is inferred. |
|   | Barth-Jespersen or stronger limiter active in production; positivity protection for reconstructed density/pressure and updates. | | | |
|   | *Evidence:* Limiter code; metadata limiter/positivity_preservation; shock-captured fields. | | | |
| 8 | **cfd.viscous** Viscous discretization | 0.10 | 3 | solver.cpp:800–820 assembles Newtonian stress and Fourier heat flux; :586–608 computes full stress-vector wall traction and :1082–1110 directly adds it to viscous forces without tangential projection. Report.tex:166–167,292,312 incorrectly claims tangential-only traction. Surface Cf uses a separate projection. The magnitude of the normal contribution has not been independently quantified. |
|   | Velocity/temperature gradients on unstructured cells; Newtonian stress tensor and Fourier heat flux; viscosity matches case Reynolds number; wall-normal gradient treatment; pressure vs tangential skin-friction split. | | | |
|   | *Evidence:* Viscous flux code; forces.csv viscous columns; report formulas. | | | |
| 9 | **cfd.implicit** Implicit steady march | 0.10 | 4 | solver.cpp:979–1024 implements implicit diagonal and LU-SGS machinery, with production steady point-implicit path described at report.tex:174–184 and transient inner solve. CFL caps differ from supplied high maxima, disclosed in report; independent stability or order tests were not run. |
|   | LU-SGS/matrix-free/block-Jacobi or equivalent with documented Jacobian approximation; CFL ramp/local time stepping honoring case CFL controls; meaningful residual reduction. | | | |
|   | *Evidence:* Implicit solver code; residuals.csv; metadata time_integrator/implicit_solver. | | | |
| 10 | **cfd.transient** Transient BDF2 two-loop method (Re 200) | 0.10 | 4 | solver.cpp:879–894 forms the BDF2 transient residual; :1359–1418 tests its reduction in the inner loop and shifts physical histories afterward. Submitted Re200 metadata records dt=0.01, t=300, min/max inner 5/300 and all steps meeting target, with force history covering t=0.01–300. BDF2 is used at the first step with identical historical states (:169), an imperfect startup. Unforced result quality is a separate case judgment; no unsteady rerun. |
|   | True physical-time outer loop with inner iterations at each step (dt=0.01, t_final=300, 5-1000 inner iterations, inner target 1e-3, frozen BDF2 histories) or stricter documented equivalent; real inner-iteration statistics reported. | | | |
|   | *Evidence:* Time loop code; metadata true_bdf2_inner_loop + inner stats; forces.csv unsteady behavior. | | | |
| 11 | **cfd.mpi_partition** MPI partitioning and scalability | 0.09 | 4 | global_mesh.cpp:296 calls METIS; solver.cpp:231–249 and :329–349 exchange state and gradients with neighbors, :913–915 and :1110 reduce residuals/forces. Submitted np=1/2/4/8 short continuation histories agree within 1e-9 in Cd for a cylinder and a NACA case; report.tex:248–266 documents the shared-restart test and 4-CPU quota. It is not an independent full convergence comparison. |
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

- Overall score (0-5): 3.68
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
