# Code Audit: fv2d (src/) — benchmark submission verification

Audit date: 2026-08-27. Scope: /workspace/solver/src/*.cpp,*.hpp (~3.5k lines).
Read-only audit; no files modified, no CFD runs executed.
Verdicts: PASS / FAIL / CONCERN, each with file:line evidence.

## 1. Conservative FV residual assembly — PASS

Genuine per-face conservative assembly, not a stub. Face loop in
src/solver.cpp:209-293; flux accumulated with opposite signs to the two
adjacent cells:

- solver.cpp:289-292 `if (own0) R[f.c0][v] += F[v] * f.area; if (own1) R[f.c1][v] -= F[v] * f.area;`
- Residual includes inviscid flux (solver.cpp:226-233), viscous subtraction
  (solver.cpp:281-282), and optional physical-time term (solver.cpp:294-299).
- Cell volumes/centroids/face areas/normals computed from geometry
  (local_mesh.cpp:6-46).

## 2. Approximate Riemann fluxes: Roe + Harten fix AND Rusanov/LLF — PASS

Both implemented and selectable.

- Rusanov/LLF: flux.cpp:16-26 (`rusanovFlux`, `0.5*(FL+FR) - 0.5*smax*(UR-UL)`,
  `smax = max(|unL|+aL, |unR|+aR) * scale`).
- Roe: flux.cpp:34-82 (Roe averages, 4 wave strengths, eigenvalues
  `lam1 = un - a ... lam4 = un + a` at flux.cpp:62).
- Harten entropy fix: flux.cpp:28-32 (`hartenFix`: `(lam^2+delta^2)/(2*delta)`
  for `|lam| < delta`), applied at flux.cpp:63-67 with `delta = 0.1*a`.
- Selection: `useRoe_` branch in residual (solver.cpp:229-233); CLI
  `--flux roe|rusanov` (main.cpp:74-78). Default: Roe for steady, Rusanov for
  transient (main.cpp:72-73).

## 3. Second-order WLSQ reconstruction, used in production — PASS (caveat)

- Weighted (1/d) least-squares stencil over face neighbors + mirrored boundary
  ghost points, with per-cell stored inverse 2x2 moment matrix:
  local_mesh.cpp:64-114 (`e.w = 1/d` at :102; `lsqInv00/01/11` at :110-112).
- Gradient evaluation: grad.cpp:5-35 over [rho,u,v,p,T].
- USE in residual: solver.cpp:173-180 calls computeGrads/computeLimiters;
  face states reconstructed with limited gradients at solver.cpp:194-207
  (`wr.rho = w.rho + L.rho*(gr.rho.x*dx + ...)`).
- Production gating: steady runs start first-order and switch to 2nd order
  once residual drops `switchOrders_`=1.5 orders and step>=300
  (solver_run.cpp:90-93, defaults solver.hpp:56-57); transient always enables
  2nd order (solver_run.cpp:256). Caveat: if a steady run never achieves the
  1.5-order drop it stays first-order; env FV2D_SWITCH_ORDERS=1e30 disables
  2nd order. Defaults enable it — not hidden-off-by-default.

## 4. Limiters: Barth-Jespersen AND Venkatakrishnan, applied, with floors — PASS

- Both in grad.cpp:37-105: Venkatakrishnan smooth form at grad.cpp:71-79
  (`sig = (d1^2 + 2 d1 d2 + eps2)/(d1^2 + d1 d2 + 2 d2^2 + eps2)`,
  `eps2 = (K*h)^3` at :65); Barth-Jespersen min-mod room/delta at :80-88.
  Selected by `venkatK > 0` (grad.hpp:133-135), set from env FV2D_VENKAT
  (solver.cpp:38-39); default venkatK_=-1 -> Barth-Jespersen.
- Positivity floors on rho and p inside the limiter (grad.cpp:90-97) AND in
  the face reconstruction (solver.cpp:204-205) AND in the update backtracking
  (solver.cpp:414-425).
- Applied in production: limiter is recomputed every residual evaluation
  (solver.cpp:178) and used in every face reconstruction. The steady-run
  limiter *freeze* is DISABLED by default (solver.cpp:42:
  `freezeOrders_ = 1e30`), so the limiter stays live for the whole run unless
  an env var is set; if frozen, it unfreezes on residual rise
  (solver_run.cpp:103-106). Transient freezes the limiter only *within* the
  inner dual-time iterations and recomputes it fresh each physical step
  (solver_run.cpp:293-302) — standard practice. No disabled-limiter trigger.

## 5. Viscous flux: Newtonian + Fourier, corrected face gradient, Re-consistent mu — PASS

- Newtonian stress with Stokes hypothesis + Fourier heat flux:
  flux.cpp:140-151 (`txx = 2*mu*gux - (2/3)*mu*div`, `txy = mu*(guy+gvx)`,
  energy row `(u*txx + v*txy + k*gTx)*nx + ...`).
- Primitive gradients of u,v,T from the same LSQ operator; face gradient is
  the average of cell gradients corrected along the cell-connecting direction
  (solver.cpp:261-270: `resid = (phiR-phiL) - gb.d; g = gb + resid*d/|d|^2`),
  including ghost-state correction at boundaries (solver.cpp:252-260).
- mu from Reynolds: case_config.hpp:214-217
  `mu = fs_rho * fs_vel * ref_reynolds_length / reynolds` (rho*U*L/Re);
  k = mu*cp/Pr (case_config.hpp:218). Re required > 0 for laminar cases
  (case_config.cpp:72).

## 6. Implicit LU-SGS: two-sweep, analytic split Jacobians, block-Jacobi — PASS

- Real forward + backward sweep pair: solver.cpp:333-392
  (`// forward sweep` :359-373, `syncDU`, `// backward sweep` :376-391
  with `s = diag*dUstar` then upper-neighbor correction — the classic
  (D+L) dU* = rhs, (D+U) dU = D dU* factorization, not a single Jacobi update).
- Off-diagonals are analytic 4x4 split-flux Jacobians:
  jacobian.cpp:144-172 (exact Euler normal-flux Jacobian dF_n/dU) and
  jacobian.cpp:174-183 (`A+- = 0.5*(A +/- lambda*I)` matvec), applied via
  `offMatvec` solver.cpp:343-357 with viscous scalar correction
  `-0.5*lamV*I` (:355-356).
- Diagonal: scalar Yoon-Jameson full spectral-radius sum (solver.cpp:324-330)
  — "scalar LU-SGS" as disclosed in metadata (output.cpp:399).
- Block-Jacobi across ranks: ghost dU values lagged within a sweep pair
  (solver.cpp:341, :366, :384; halo refresh between pairs via syncDU).

## 7. Steady pseudo-time: local dt with conv+visc radii, CFL ramp — PASS

- Local time step from summed convective + viscous face spectral radii:
  solver.cpp:302-315 (`rf = faceLam_[fi] + faceLamV_[fi]`, `dtau = cfl*vol/spec`).
- Convective radius |un|+a per face (solver.cpp:235-238); viscous radius
  `max(4/3, gamma/Pr) * (mu/rho) * A/d` (solver.cpp:192, :285).
- CFL ramp cfl0 -> cfl1 over ramp steps: solver.cpp:518-522, applied
  solver_run.cpp:59-60; optional residual-driven adaptive CFL (opt-in env,
  solver_run.cpp:81-87).

## 8. Transient BDF2 dual-time, true two-level loop — PASS

- Outer physical loop solver_run.cpp:269; inner nonlinear loop :296-319 with
  `max_inner_iterations`, early exit only after `min_inner_iterations` and
  inner residual ratio <= target (:317-318).
- BDF2 coefficients c0=1.5, c1=-2.0, c2=+0.5 with BDF1 startup
  (solver_run.cpp:270-273).
- Inner residual INCLUDES the physical-time term: every inner iteration calls
  `computeResidual(R_, true, c0, c1, c2, dt)` (solver_run.cpp:312; time term
  added solver.cpp:294-299 from `bdfC0*U + bdfC1*Un + bdfC2*Unm1`).
- Histories frozen during inner solve and updated only after inner
  convergence: `Unm1_ = Un_; Un_ = U_;` at solver_run.cpp:329-330, outside the
  k-loop. Matches `bdf2_history_update = "after_inner_convergence"`
  (case_config.hpp:166).
- Dual-time diagonal accounts for the physical term: solver_run.cpp:287
  (`spec + vol*c0/dt`) and buildDiag bdfC0 term (solver.cpp:321).

## 9. MPI: METIS k-way, owned+ghost, neighbor halo, no full-state replication — PASS

- METIS k-way graph partitioning on the cell-adjacency graph built from
  interior faces (CSR xadj/adjncy, partition.cpp:72-85), `METIS_PartGraphKway`
  at partition.cpp:95-96. Genuinely graph-based; no coordinate/geometric
  bisection anywhere. (Edge cut recorded: output.cpp:388.)
- Rank-local owned+ghost cells: partition.cpp:99-116; local indexing owned
  first then ghosts (:134-140). LocalMesh holds only local cells/nodes/faces
  (local_mesh.hpp:155-194).
- NEIGHBOR-scoped halo via nonblocking Isend/Irecv over per-neighbor
  send/recv index lists: solver.cpp:74-99 (MPI_Irecv :81, MPI_Isend :89,
  Waitall :92). Used for U, gradients+limiters, and dU (syncU/syncRecon/
  syncDU). No MPI_Allgather/Allgatherv anywhere in the codebase (grep:
  zero hits); the only collectives on state are output-time MPI_Gather/
  Gatherv to rank 0 (output.cpp:31-48) and restart-read Bcast
  (output.cpp:516-521) — none inside the iteration loop.
- Global reductions for residuals (MPI_Allreduce SUM/MAX,
  solver.cpp:443-446) and forces (solver.cpp:505), GMRES dots (gmres.cpp:115).
- CRITICAL check: no full-mesh or full-state replication during iterations.
  Only setup-time rank-0 global mesh read + serial partition with per-rank
  serialized distribution (partition.cpp:63-261) — a scalability note, not a
  correctness/disqualification issue. Metadata honestly reports
  `full_state_replication_during_iterations: false` (output.cpp:390-391).

## 10. Boundary conditions + surface output semantics — PASS

- Farfield: characteristic/Riemann-invariant state with subsonic in/outflow
  and supersonic limits (flux.cpp:84-116), used via ghostFn
  (solver.cpp:64).
- Inviscid slip wall: mirrored-velocity ghost (flux.cpp:118-124) and
  pressure-only wall flux (flux.cpp:133-138; solver.cpp:227-228).
- No-slip adiabatic wall: negated-velocity ghost (flux.cpp:126-131), zero
  face velocity in the viscous flux (solver.cpp:275-276).
- Surface output reports BOUNDARY values, not adjacent cell-center velocity:
  output.cpp:64-66 sets `uB = 0.0; vB = 0.0` for no-slip walls (exact wall
  value); slip walls report the tangential projection `uB = w.u - un*nx`
  (output.cpp:68-70), i.e. the boundary value with zero normal component.
  Wall pressure is the adjacent-cell pressure — the same pressure used in the
  wall flux, standard for these BCs. No cell-center-as-wall-velocity trigger.

## 11. Forces: pressure vs viscous separation — PASS

- Pressure force integrated separately (solver.cpp:467-470:
  `dpx = (pw - p_inf)*nx*area`); viscous force uses only the TANGENTIAL
  traction (normal component explicitly removed: solver.cpp:493-495
  `stx = sx - sn*nx`) so pressure and shear are not double-counted.
- Moment about the configured moment center from both contributions
  (solver.cpp:471-473, :499). Global MPI_Allreduce (solver.cpp:505);
  coefficients normalized by q_inf*A_ref and reported separately as
  pressure_drag/viscous_drag/pressure_lift/viscous_lift (solver.cpp:507-514,
  forces.csv header output.cpp:419).

## 12. Originality / hidden shortcuts — PASS (minor notes)

- No case-specific branches: grep for "cylinder|naca|hardcod|hack|fake"
  across src/ returns zero hits. BC selection is purely via the case-file
  family->BC map; flux selection via CLI/case type; all strategy switches
  (2nd-order switch, limiter freeze, CFL adapt) are generic,
  residual-triggered, and env-tunable — none keyed on case identity.
- No hard-coded force/residual values; all outputs derive from the assembled
  residual/flux integration shown above.
- No external solver invoked: only libraries are CGNS (mesh I/O), METIS
  (partitioning), MPI, nlohmann/json (config parsing) — all infrastructure.
- Notes (not disqualifiers):
  - gmres.cpp (Newton-Krylov Frechet GMRES) is compiled but NEVER called
    outside itself (grep: no call sites); production inner solve is the
    relinearized LU-SGS sub-stepping in solver_run.cpp:63-75 / :296-319.
    Harmless dead experimental path; could be removed to avoid confusion.
  - `bndFirstOrder_` (FV2D_BND1) is set but never used (solver.hpp:47,
    solver.cpp:40-41) — dead debug knob. Boundary faces already use the
    cell-center interior state + ghost (solver.cpp:220-224), i.e. first-order
    boundary fluxes by construction; interior faces are fully 2nd-order.
  - Steady runs may continue past the case max_steps up to 2x
    (solver_run.cpp:183-188) — stricter than the case cap, logged, and
    disclosed; flag in the writeup if the rubric requires strict caps.
  - FV2D_ONESTEP diagnostic short-circuits a run after one sweep
    (solver_run.cpp:13-34) — debug-only env; ensure it is unset in
    production submissions.

## Disqualification-trigger assessment

| Trigger | Risk | Evidence |
|---|---|---|
| existing-solver-called | NONE | Only CGNS/METIS/MPI/json deps; all numerics hand-written in src/ |
| fake outputs | NONE | All CSV/VTK/JSON values traced to assembled residual/flux integration |
| hardcoded mesh filenames | NONE | Mesh path from case JSON (case_config.cpp:58-64) |
| rank-count-dependent steady results | LOW | Block-Jacobi (lagged ghost dU) makes intermediate iterates partition-dependent — standard and disclosed (output.cpp:399); the converged steady state (R=0) is partition-independent up to tolerance; residuals and forces are global reductions. Not a hidden dependence |
| explicit-only | NONE | Implicit LU-SGS with analytic Jacobians is the production update (items 6-8) |
| claims-not-in-source | NONE | Every metadata claim (output.cpp:374-425) verified against source above |
| metis-but-actually-geometric | NONE | METIS_PartGraphKway on face-adjacency graph (partition.cpp:72-97) |
| disabled-limiter | NONE | Limiter live by default every residual evaluation; freeze off by default (solver.cpp:42) |
| cell-center-as-wall-velocity | NONE | Surface u=v=0 at no-slip walls (output.cpp:64-66) |

Overall: no disqualification trigger found. Two cosmetic cleanups worth doing
before submission (dead GMRES path, dead FV2D_BND1 knob) and transparency
notes to state explicitly in the writeup (steady 2x max_steps extension;
first-order startup phase before the 2nd-order switch).
