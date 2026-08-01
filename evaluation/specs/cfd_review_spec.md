# CFD Methods and Algorithms Review Specification

## Purpose

Score the numerical methods and algorithm quality of the submission on the
points defined by the benchmark's evaluation standard (`TASK.md`,
`NUMERICAL_PARAMETERS.md`, `examiner/README_EXAMINER.md`,
`examiner/SCORING_RUBRIC.md`).

## Method

- Point list: `evaluation/config/review_points_cfd.json`.
- Reviewers score each point 0–5 in `review_cfd.md` and mark disqualification
  flags. Reviewing requires reading the source, `metadata.json` fields, and
  the report's method sections; the automated pipeline only prepares the
  scorecard and attaches evidence pointers.

## Points (methods & algorithms)

1. **cfd.equations — Governing equations and nondimensionalization** (0.06):
   conservative 2-D compressible Navier–Stokes for calorically perfect gas;
   correct closure; consistent nondimensionalization and force coefficients.
2. **cfd.mesh — Mesh import and unstructured geometry** (0.10): CGNS
   unstructured zones + boundary families; volumes, face areas/normals/
   centers; mixed cells, multi-zone; cell-face adjacency graph.
3. **cfd.residual — Conservative FV residual** (0.12): cell-centered residual,
   consistent face orientation, boundary-face treatment, globally reduced.
4. **cfd.flux — Approximate Riemann flux / entropy fix** (0.08): Rusanov/LLF
   minimum, Roe + entropy fix acceptable; matches metadata.
5. **cfd.bc — Boundary conditions** (0.08): farfield, slip wall, no-slip
   adiabatic wall; honest wall-output semantics.
6. **cfd.reconstruction — Second-order reconstruction** (0.09): least-squares/
   Green–Gauss gradients, piecewise-linear face states, active in production,
   fallback disclosed.
7. **cfd.limiter — Limiter and positivity control** (0.08): Barth–Jespersen or
   stronger, active in production; positivity protection.
8. **cfd.viscous — Viscous discretization** (0.10): gradients, Newtonian
   stress + Fourier heat flux, Re-matched viscosity, pressure vs skin-friction
   force split.
9. **cfd.implicit — Implicit steady march** (0.10): LU-SGS/matrix-free/
   Jacobi-class inner solver with documented Jacobian approximation; CFL ramp
   per case controls; meaningful residual reduction.
10. **cfd.transient — Transient BDF2 two-loop (Re 200)** (0.10): physical-time
    outer loop with inner iterations (`dt=0.01`, `t_final=300`, 5–1000 inner
    iterations, inner target 1e-3, frozen BDF2 histories) or stricter
    documented equivalent; real inner statistics reported.
11. **cfd.mpi_partition — MPI partitioning and scalability** (0.09): METIS/
    ParMETIS cell-graph partitioning, owned/ghost counts, neighbor ranks,
    send/recv sizes; documented edge cut/load balance; np=8 NACA + cylinder
    with comparable forces.

## Disqualification flags

Explicit time stepping only; claimed reconstruction/limiter disabled in
production; METIS claimed without real partitioning; Re 200 misses inner
target on most steps while presented as periodic; normal traction mislabeled
as skin friction; cell-center velocities presented as no-slip wall rows
without documentation.
