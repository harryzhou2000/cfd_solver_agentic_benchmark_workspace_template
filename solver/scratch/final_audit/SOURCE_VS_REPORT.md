# SOURCE_VS_REPORT.md — Algorithm-vs-Source & Originality Audit

Adversarial read-only audit of /workspace/solver/. Every verdict below was re-derived from
source; report text was treated as a claim to be tested, not as evidence.

**Coverage: 14 of 14 assigned claims checked (A.1–A.8, B, C#1, C#2, C#3, C#5, C#6).**

## Headline

The solver is **substantively honest**. Roe+entropy fix, Venkatakrishnan limiter, second-order
reconstruction, LU-SGS with real off-diagonal coupling, BDF2 frozen history, METIS k-way, and
neighbour-scoped halo exchange are all genuinely implemented and match the report's formulas
term by term. No disqualification trigger fires.

**One real defect found (MINOR–MODERATE, disclosure-grade, not a trigger):** the transient
`residuals.csv` `residual_l2` column is computed **after** the BDF2 history shift, making the
physical-time term structurally non-zero. The reported value is exactly **1.5×** the true
spatial residual (measured: ratio 1.499893). See A.5(f).

---

## A. ALGORITHM-VS-SOURCE VERIFICATION

### A.1 Inviscid Riemann solver — **MATCH**

Report claim: sec_spatial.tex:140-142 "Three inviscid numerical fluxes are implemented — Roe
with an entropy fix, HLLC, and Rusanov/local Lax--Friedrichs — selectable per run. Production
runs use **Roe**."

Source: production flux is **hard-coded**, not case-file-driven —
solver_context.cpp:30 `scheme_.inviscid_flux = RiemannFluxType::kRoeEntropyFix;`. Overridable
only via an explicit CLI verification flag (solver_context.cpp:45-48), and any override is
`logWarn`-ed. All 8 `results/*/metadata.json` report
`inviscid_flux=roe_approximate_riemann_with_hllc_positivity_fallback` — and that string is
**derived from the live scheme object**, not a literal (main.cpp:46-47 via `riemannFluxName()`).
Metadata cannot claim a flux the run did not use.

Term-by-term verification of riemann_flux.cpp:165-330 against eq:roe / eq:harten / eq:floor:

| Report | Source | OK |
|---|---|---|
| `rho_hat = sqrt(rhoL*rhoR)` | :187 | yes |
| Roe-avg `phi_hat` sqrt-rho weights | :186-190 | yes |
| `a2_hat=(g-1)(H_hat-q2/2)` | :192 | yes |
| `lambda = un-a, un, un+a` | :205 | yes |
| `alpha_{1,3}=(dp ∓ rho_hat*a_hat*dun)/(2 a2_hat)` | :269-270 | yes |
| `alpha_2 = drho - dp/a2_hat` | :271 | yes |
| shear term ∝ `rho_hat*dut` | :305-308 | yes |
| Harten `(l^2+d^2)/(2d)` for `|l|<d` | :233-238 | yes (algebraically identical: `0.5*(l*l/d + d)`) |
| Harten–Hyman spread `delta_1` | :248-249, :256-257 | yes |
| floor `0.05*max(|unL|+aL,|unR|+aR)` | :241-244 | yes |
| floor applied to **all three** waves | :250, :253, :258 | yes |
| final `0.5(fL+fR) - 0.5*diss` | :326-328 | yes |

Entropy fix is **active and unconditional** — not gated behind a flag. `entropyFixName()`
returns `harten_hyman_entropy_fix` (:25) matching metadata for all 8 cases.

**Notable credit:** sec_spatial.tex:236-271 explicitly discloses that the linear-wave floor is
**added artificial dissipation and not part of classical Roe**, and then reports that the
sensitivity sweep **did not support the design choice** (floor changes C_D by 0.021%; disabling
it produces no checkerboard). Volunteering a negative result about one's own tuning constant is
the opposite of overclaiming.

HLLC positivity fallback: real, reachable, and correctly wired (riemann_flux.cpp:193-200 sets
`fallback=true` when `a2_hat<=0`; :341-347 dispatches to HLLC). Report states at
sec_spatial.tex:222-225 that the path is **not taken** in production — confirmed:
`positivity_fallback_events == 0` in all 8 metadata files. The report flags this as a
limitation on what production runs can verify (cref{sec:hllcbug}) rather than as a strength.

### A.2 Limiter — **MATCH, and ACTIVE in production**

Report claim: sec_spatial.tex:106-107 "Both a Barth--Jespersen and a Venkatakrishnan limiter are
implemented; the production runs use Venkatakrishnan"; eq:limiter gives
`Phi^V = (y^2+2y+eps^2/D^2)/(y^2+y+2+eps^2/D^2)`, threshold `eps^2=(K h)^3`, `h=sqrt(V)`.

Source limiter.cpp:116-120:
```
y = bound / d;
factor = (y*y + 2y + eps2/max(d2,kTiny)) / (y*y + y + 2 + eps2/max(d2,kTiny));
```
Exact match to eq:limiter. Threshold limiter.cpp:86-88 `eps2 = pow(max(venkat_k,0)*h, 3.0)` with
`h = sqrt(cells[c].volume)` — matches `(K h)^3`, `h=sqrt(V_i)`. Barth–Jespersen `min(1, y)`
at :111 matches eq:limiter. Cell limiter = min over faces, clipped to [0,1] (:122).

**Rubric trigger 10 (claimed limiter disabled for production): CLEAR.** Traced the full config
path. There is **no case-JSON key that can disable the limiter** — I grepped
case_input.cpp/.h for limiter keys and found none. The limiter is set unconditionally in
solver_context.cpp:35-36 (`kVenkatakrishnan`, `venkat_k = 5.0`). `kNone` exists
(limiter.h:23, marked "verification only") but is reachable **only** via the `--limiter` CLI
override, which `logWarn`s (solver_context.cpp:49-52). Call site is unconditional for
second-order runs: residual.cpp:88-91. All 8 metadata report `limiter=venkatakrishnan`, derived
from the live object via `limiterName()` (main.cpp:56). K=5.0 is disclosed in the report
(sec_spatial.tex:231-232, `\pVenkatK`).

### A.3 Second-order reconstruction — **MATCH; fallback disclosure HONEST**

Report claim: sec_spatial.tex:98-101 "Second-order reconstruction is **active in every
production run reported here**; the only first-order fallback is the positivity mechanism
below, whose activation count is recorded per run and reported in `metadata.json`."

This is a strong, falsifiable claim. I enumerated **every** first-order fallback trigger in the
source:

1. `scheme_.second_order == false` — residual.cpp:30-31, :53-54. Set from
   `input_.numerics_required.spatial_order >= 2` (solver_context.cpp:22) or the
   `--spatial-order` CLI override (:40-44, logged). All 8 cases report
   `spatial_order_claimed: 2` and `reconstruction=piecewise_linear_least_squares_primitive_gradients`.
2. Positivity clipping in `reconstructPrimitive` — limiter.cpp:140-153. Progressive halving
   (8 attempts) then full drop to cell average; returns `false`, which increments
   `diag.positivity_fallbacks` (residual.cpp:34-36, :60-62).
3. Wall-pressure guard in force integration — forces.cpp:59 `if (!(p_wall>0.0)) p_wall = wc[kPrimP];`
4. Surface-output guard — surface_output.cpp:48-50 falls back to cell value if reconstruction clipped.

Triggers 1 is config-only and logged; 2 is counted and reported; 3–4 are narrow output-path
guards. **Empirical check: `positivity_fallback_events == 0` for all 8 production cases**, so
the report's claim that second order was active throughout is not just disclosed but
*verified by the counter it points to*. The diagnostic plumbing exists and is real
(residual.h ResidualDiagnostics → outcome → metadata).

Gradient claims (eq:lsq, eq:normaleq) also verified: inverse-distance weights `w=1/|dx|`
(distributed_mesh.cpp:671), 2x2 normal matrix assembled and inverted **once at preprocessing**
into per-neighbour weight vectors (:676-707), giving the single-sum hot loop the report
describes (gradients.cpp:47-51). Rank-deficiency guard `|det| < 1e-12*tr^2` with diagonal
shift matches sec_spatial.tex:81-83 exactly (distributed_mesh.cpp:685-692). Boundary stencil
entries carry the **physical** boundary state, not the mirrored ghost — gradients.cpp:37-44,
matching the report's emphasis at sec_spatial.tex:85-89.

### A.4 LU-SGS inner solve — **MATCH. Genuinely not point-Jacobi.**

This was the highest-risk claim (TASK.md: "A single diagonal local update per pseudo step is not
sufficient"). **It passes.**

Source lusgs.cpp:177-211 is a true symmetric Gauss–Seidel:
- **Forward sweep** ascending `c = 0 .. num_owned-1` (:178), writing `dU.set(c, next)`
  **in place** (:192) so later cells in the same sweep read already-updated lower neighbours.
- **Backward sweep** descending `c = num_owned-1 .. 0` (:196), same in-place update (:210).
- Off-diagonal contributions are gathered over **all interior faces** of the cell (:181-188,
  :199-206) via `offDiagonalAction`.

The decisive structural evidence that this is *not* Jacobi-dressed-up: the code contains a
**separate, genuinely different** `kJacobi` branch (:155-174) which stages results into
`work_` and only copies back after the full loop (:173). Two distinct update semantics coexist;
the LU-SGS path deliberately omits the staging buffer. A fake LU-SGS would not need `work_`.

Jacobian approximation, stated exactly:
- **Diagonal (scalar, 1 double/cell):** `D_i = diag_scale + 0.5*sum(|un|+a)*ds + sum(mu/rho)*ds^2/V`
  — lusgs.cpp:85-89. Matches eq:diag including the 1/2 upwind split factor.
- **Off-diagonal:** `O_ij dU_j = 0.5*ds*(A_j(n) dU_j - lambda_j dU_j)` — lusgs.cpp:127-129.
  Matches eq:offdiag. `lambda_j = |un|+a + 2mu/(rho*|dx|)` (:111, :122) matches eq:offdiag
  including the viscous term being viscous-runs-only (`if (flow_.viscous)`, :115).
- `A_j(n) dU_j` is the **exact analytic** Euler flux Jacobian-vector product, matrix-free —
  lusgs.cpp:27-67. I checked all four components against eq:jacvec: `d(rho un)` (:56),
  momentum rows with `dp*n` (:60-61), energy `dun*(rhoE+p)+un*(d(rhoE)+dp)` (:63). Exact match.
  `dp` expression (:55) matches the report's stated form.

The report is **precise and non-overclaiming** about the exact/approximate boundary:
sec_implicit.tex:91-102 explicitly reconciles "exact analytic Jacobian-vector product" with the
metadata label `lu_sgs_simplified_jacobian`, stating the diagonal is scalar not 4x4 and the
off-diagonal uses scalar dissipation rather than the true upwind linearisation. That is the
correct characterisation of what I found in the code.

Cross-rank behaviour honestly described: halo refresh once per sweep (:153), ghosts frozen
between refreshes ⇒ block-Jacobi across ranks, LU-SGS within rank — stated at
sec_implicit.tex:104-108 and lusgs.h:16-20, including the admission that iteration counts depend
mildly on partition.

Inner convergence measured by the **true linear residual** of the system actually being solved,
using the same matrix-free operator, globally reduced (`MPI_Allreduce`, lusgs.cpp:263) —
matches eq:linres. Increment deliberately **not** reset between blocks
(`reset_increment=performed==0`, steady_driver.cpp:243) matching sec_implicit.tex:121-123.

**Credit:** sec_implicit.tex:125-131 records a *negative* result — inflating the diagonal by
beta=2 made the reported ratio look like 2.2e-16 while the true ratio was 0.82, and the shortcut
was rejected. Disclosing a rejected shortcut that would have flattered the numbers is strong
evidence of good faith.

### A.5 BDF2 frozen-history claim — **(a)-(e) MATCH; (f) DEFECT FOUND**

Source: transient_driver.cpp.

**(a) True two-level loop — MATCH.** Outer physical loop `for (step=1; step<=num_physical_steps; ++step)`
at :71; inner nonlinear loop `for (inner=1; inner<=rc.max_inner_iterations; ++inner)` at :90.

**(b) U^n, U^{n-1} frozen during ALL inner iterations — MATCH.** `U_n` and `U_nm1` are separate
`StateField` objects (:28-29). Inside the inner loop they are **read only** — :102-104
(`U_n.cell(c)`, `U_nm1.cell(c)`). The only writes are at :170-171, outside the inner loop.
Verified by grep: no `U_n.set` or `U_nm1.set` anywhere within :90-162. Only the `U` iterate is
mutated (:155-160).

**(c) History shifted only after inner solve accepted — MATCH.** :167-172, after the inner loop
closes at :162 and after the `diverged` break check at :164. Order is correct:
`U_nm1.set(c, U_n.get(c))` then `U_n.set(c, U.get(c))`.

**(d) Inner residual target on the FULL transient residual — MATCH.** :99-110 builds
`rt[k] = rs[k] - volume*(a0*u + a1*un + a2*unm1)/dt`, i.e. eq:bdf2 including the physical-time
term. The convergence test at :133 uses `inner_ratio` derived from
`computeNorms(total_residual)` (:112, :128) — **not** the spatial residual. Diagonal carries
both pseudo- and physical-time terms (:148-149, `volume/dt_local + volume*a0/dt`), matching
eq:bdf2diag, so each inner iteration is Newton-like on the time-accurate system as claimed.

**(e) BDF2 startup — MATCH and disclosed.** :76-80 uses BDF1 `a0=1, a1=-1, a2=0` on step 1.
Report states this at sec_implicit.tex:173-174 ("The first physical step uses the self-starting
BDF1 coefficients"). Both history levels initialised to the start state (:50-54).

**(f) DEFECT — reported transient residual is built from post-shift history (measured 1.5x the spatial residual).**

At :185-196, after the history shift at :169-172, the code recomputes `total_residual` using
`a0*u + a1*un + a2*unm1`. The bug is that the shift has already happened: `U_n` now holds
`U` and `U_nm1` now holds the *old* `U_n`. So with `un == u` identically, the time term
collapses to
```
(a0*u + a1*u + a2*u_n_old)/dt = (a0+a1)*u/dt + a2*u_n_old/dt
                              = 0.5*(u_n_old - u)/dt        [a0=1.5, a1=-2, a2=0.5]
```
i.e. a **one-step backward difference scaled by V/(2 dt)** — a quantity evaluated at
inconsistent time levels, and structurally non-zero for an unsteady flow. The correctly-formed
pre-shift residual (:99-110) is *not* what gets written.

Because `V/dt` is large here (dt=0.01 on a fine near-wall mesh), this stale time term dominates
the row, which is why the written column tracks neither the spatial residual nor the inner
residual.

Measured consequence on the submitted data:
```
metadata final SPATIAL residual_l2   : 0.010178736069257852
residuals.csv last row residual_l2   : 0.015267017750610
ratio (csv / spatial)                : 1.499893272281609
```
The ratio is **1.4999**. I report that as a measured fact, not a derived identity: it numerically
coincides with `a0=1.5`, but I did not prove the equality follows by construction, and the
near-exactness is likely an artefact of the settled periodic state on this case. Confirmed across
the tail: last-500 reported values sit in [1.5206e-2, 1.5308e-2], tightly clustered — a flat
offset band, not a converging sequence. The robust, case-independent statement is that the row is
built from post-shift history and is therefore wrong by a stale `V/(2 dt)`-scaled time term.

What this means concretely: the `residual_l2` column of the transient `residuals.csv` is
**neither** the converged inner residual (which reached 9.988e-4, per
`last_inner_residual_ratio`) **nor** the spatial residual (1.0179e-2). It is a third quantity
contaminated by a time term evaluated across inconsistent time levels.

Severity assessment: **MINOR–MODERATE.** Reasons it is not a disqualification:
- The **solver mathematics is unaffected.** The inner loop's own convergence test (:133) uses
  the correctly-formed pre-shift residual. The BDF2 time integration itself is correct.
- `metadata.json` `final_residual_l2` and `residual_reduction_orders` are computed from the
  clean spatial residual (:252-269) and are correct.
- The report **does not lean on this column** to argue convergence. It explicitly argues the
  opposite at sec_verification.tex:1119-1137 and transient_driver.cpp:255-265: for a vortex
  street the spatial residual "is not expected to decrease at all" and the real evidence is
  force-signal periodicity + inner-solve statistics.
- OUTPUT_CONTRACT.md:110 requires only that residuals be finite and use a consistent global MPI
  reduction. Both hold.

Why it still deserves flagging: sec_results.tex:1381-1382 labels the plotted quantity "the
**total transient residual** per physical step from `residuals.csv`". Post-shift, the
coefficients no longer correspond to eq:bdf2 evaluated at a consistent time level, so the label
is imprecise. A one-line fix (compute the row's residual before the shift, or reuse the inner
loop's final `norms`) would resolve it. Recommend disclosing in limitations if not fixed.

### A.6 METIS call — **MATCH. No silent geometric fallback.**

partitioner.cpp:82-83 calls `METIS_PartGraphKway(&nvtxs, &ncon, xadj, adjncy, ...)` on the
**cell dual graph**. Graph construction verified non-degenerate (:13-40): boundary faces skipped
(`if (face.right_cell < 0) continue`, :18, :32), each interior face contributes **both**
directed entries `l->r` and `r->l` (:35-38) — a correct symmetric CSR adjacency. Degree-count
then prefix-sum then cursor-fill, exactly as sec_mpi.tex:9-10 describes.

Options match sec_mpi.tex:12-16 precisely: `ncon=1` (:69), `NUMBERING=0` (:76), `SEED=12345`
(:79), `CONTIG=1` (:80).

**Rubric trigger 9 — CLEAR. There is no geometric fallback at all.** The only retry is
METIS-to-METIS with `CONTIG=0` (:87-92), and it is `logWarn`-ed. A genuine METIS failure
**throws** (:93-95) rather than degrading silently. I specifically looked for RCB/stripe/
space-filling-curve/modulo fallbacks and found none.

One honest nuance: `num_parts <= 1` returns `partitioner = "metis_kway_single_part"` (:48-51),
a *distinct* string — so a serial run cannot masquerade as a real k-way partition. Good.
All 8 production cases report `partitioner=metis_kway` with `mpi_ranks=4` and non-trivial
`partition_edge_cut` (e.g. 241, 282), consistent with a real cut.

METIS is genuinely linked, not stubbed: `#include <metis.h>` (:6), CMakeLists.txt:97 locates
the real library, :105-106 hard-errors if `metis.h` is absent, :181 links it. No in-tree
`METIS_PartGraphKway` definition exists (grep confirms the only reference is the call site).

### A.7 Halo exchange & no-replication — **MATCH. No state/mesh collective in the iteration loop.**

Halo exchange (halo_exchange.cpp:23-78) is textbook neighbour-scoped: `MPI_Irecv` posted first
for all neighbours (:41-44), then pack + `MPI_Isend` (:46-60), single `MPI_Waitall` (:63),
then unpack into ghost slots (:67-75). Indexing uses the precomputed `plan.send_cells` /
`plan.recv_cells` with a shared ordering, so no index metadata on the wire — matching
sec_mpi.tex:41-46. Matches metadata `halo_exchange=neighbor_isend_irecv`.

**Full-tree collective sweep.** I grepped the entire `solver/src` for every MPI collective and
classified each by phase via call-chain tracing:

| Call | file:line | Phase | Payload |
|---|---|---|---|
| `MPI_Bcast` x3 | distributed_mesh.cpp:169,185,187 | **SETUP** (`DistributedMesh::build`) | global scalars, BC-name buffer |
| `MPI_Scatter`/`Scatterv` | distributed_mesh.cpp:251,257 | **SETUP** | per-rank cell/face packets |
| `MPI_Allgather`/`Allgatherv` | distributed_mesh.cpp:375,384 | **SETUP** (ghost-ownership resolution) | ghost request **IDs** |
| `MPI_Alltoall` | distributed_mesh.cpp:440 | **SETUP** | reply sizes |
| `MPI_Isend`/`Irecv`/`Waitall` | distributed_mesh.cpp:451-468 | **SETUP** | reply positions |
| `MPI_Gather`/`Gatherv` | restart_io.cpp:51,76,78 | **OUTPUT** | restart state |
| `MPI_Bcast` | restart_io.cpp:153 | **SETUP** (restart read) | initial state |
| `MPI_Gather`/`Gatherv` | vtu_writer.cpp:93,104 | **OUTPUT** | VTU bytes |
| `MPI_Gather` | output_writer.cpp:188 | **OUTPUT** | partition diagnostics |
| `MPI_Gather`/`Gatherv` | surface_output.cpp:131,142 | **OUTPUT** | surface rows |
| `MPI_Allreduce` | solver_context.cpp:112,114 | ITERATION | **scalars** (residual norms) |
| `MPI_Allreduce` | lusgs.cpp:263 | ITERATION | **scalars** (2 doubles) |
| `MPI_Allreduce` | forces.cpp:116 | ITERATION | **scalars** (6 doubles) |
| `MPI_Allreduce` | main.cpp:208 | shutdown | 1 int status |
| `MPI_Allreduce` x5 | mesh_verification.cpp:191-257 | **SETUP** (pre-solve checks) | scalars |

**Verdict: no `Allgather`/`Allgatherv`/`Bcast`/`Gather` of state or mesh runs during solver
iterations.** The only iteration-time collectives are scalar `Allreduce` calls, which are
physically required for global norms. The setup-phase `Allgatherv` at :384 carries ghost cell
**identifiers**, not state, and is inside `DistributedMesh::build` — called exactly once from
`SolverContext`'s constructor (solver_context.cpp:17). This matches sec_mpi.tex:41-46's claim
that the collective "appears only in preprocessing".

**No-replication verified structurally, not just asserted.** The `GlobalMesh` is constructed
**inside a `if (rank == 0)` block scope** (distributed_mesh.cpp:74-162) and is destroyed when
that scope exits, before any solver field is allocated — the code comment at :58-61 states this
and the brace structure confirms it. Non-root ranks never allocate it. Node coordinates travel
*with* each cell packet (`CellPacket.x[4]/.y[4]`, :20-27) precisely so "no rank needs the
global node array" (:19). Rank-0 packet buffers are explicitly `clear()`+`shrink_to_fit()`ed
after scatter (:262-267, and per-rank sources at :244-245). Solver state is sized
`mesh_->numLocal()` = owned+ghost (solver_context.cpp:63, residual.cpp:14-19), **never**
`num_cells_global`. Metadata's `full_mesh_replication_during_iterations=false` and
`full_state_replication_during_iterations=false` are **truthful**.

TASK.md Mandatory MPI Partitioning 1-3: (1) real graph partitioner — yes, A.6; (2) owned+ghost
only — yes, above; (3) neighbour-scoped halo — yes.

### A.8 Four supplementary checks — **all MATCH**

**(a) Global reductions for residual/force norms.** Residual norms: `MPI_Allreduce` over
component sums + volume, plus `MPI_MAX` for Linf (solver_context.cpp:112-114). Ghost cells
**excluded** — loop bound is `num_owned` (:95), so no double counting. Matches eq:norms
including the volume-weighted RMS-rate form `sqrt(sum (R/V)^2 V / sum V)` (:120-124). Forces:
`MPI_Allreduce` of 6 accumulators (forces.cpp:116), with wall faces counted **once globally**
via the owned-once rule `if (f.left >= mesh.numOwned()) continue` (:27). Correct.

**(b) Skin friction from tangential shear.** `wallShearTraction` (viscous_flux.cpp:29-41)
builds the full traction then **explicitly removes the normal component**:
`normal_part = dot(traction,n); return traction - normal_part*n` (:39-40). Used by both
forces.cpp:95 and surface_output.cpp:113. This satisfies OUTPUT_CONTRACT.md:122 ("Skin-friction
quantities should be based on tangential wall shear, not on the full viscous normal traction").

Stress tensor verified term by term (viscous_flux.cpp:7-12): `div = du/dx + dv/dy`;
`tau_xx = 2 mu du/dx - (2/3) mu div`; `tau_yy = 2 mu dv/dy - (2/3) mu div`;
`tau_xy = mu (du/dy + dv/dx)`. Correct 2-D compressible Newtonian stress under the Stokes
hypothesis (zero bulk viscosity). Heat flux `q = -k grad T` with energy flux carrying
`-q.n` plus stress work (:16, :25) — correct.

**(c) No-slip wall surface output reports boundary values.** surface_output.cpp:55-56 applies
`boundaryFaceState` and reports `w_boundary`; for no-slip it writes **identically zero**
velocity and Mach (:70-74). Coordinates are the **face centroid** (:60-61), not the cell centre.
Cell-centre values are carried in *separate* `cell_*` fields (:84-92) and written to a
*separate* `surface_cell_center.csv` — i.e. the transparency file is additive, not a substitute.
Metadata `wall_boundary_output_semantics="boundary_value"` is **truthful**.

**(d) Farfield BC.** Genuine characteristic treatment via locally-1D Riemann invariants
(boundary_conditions.cpp:78-146), **not** naive Dirichlet: supersonic outflow returns interior
(:103-105), supersonic inflow returns freestream (:107-109), subsonic combines outgoing
`R+ = un_i + 2a_i/(g-1)` with incoming `R- = un_o - 2a_o/(g-1)` (:112-113), then
`un_b=(R++R-)/2`, `a_b=(g-1)(R+-R-)/4` (:114-115), with entropy `p/rho^g` taken from the
upwind side (:124-134) and tangential velocity carried from upwind (:136-140). Matches
sec_bc.tex. Degenerate-invariant guard falls back to freestream (:116-120) rather than producing
a negative sound speed. Slip wall reflects normal momentum `u - 2(u.n)n` (:10-27); the separate
`slipWallFaceState` zeroes normal velocity and corrects total energy for the reduced kinetic
energy (:29-49) — a distinction the report makes and the code honours.

---

## B. ORIGINALITY — **CLEAR**

### What I examined

All 61 files under `solver/src` (~7.0 kLOC of `.cpp`/`.h`, excluding the 514-line
`tests/unit_tests.cpp`). Read in full: riemann_flux.cpp (352), lusgs.cpp (267),
transient_driver.cpp (315), residual.cpp (292), limiter.cpp (156), partitioner.cpp (105),
halo_exchange.cpp (80), boundary_conditions.cpp (174), forces.cpp (137), viscous_flux.cpp (44),
gradients.cpp (62), solver_context.cpp (169), surface_output.cpp (170, partial),
distributed_mesh.cpp (737, key sections: build/scatter/gradient-stencil), steady_driver.cpp
(675, partial), main.cpp (211, partial). Plus targeted greps across the whole tree.

### Tell-tale signs of copying — none found

| Signal | Result |
|---|---|
| Licence/copyright headers | **NONE** (grepped copyright, licen[cs]e, GPL/LGPL/Apache, SPDX, "All rights reserved") |
| Doxygen fragments | **NONE** (`@brief`, `\brief`, `@param`, `@author`, `@file`, `@ingroup`) |
| SU2 idioms | **NONE** (`CConfig`, `CGeometry`, `CSolver`, `CNumerics`, `CEuler`, `SU2_`, `nDim`, `nVar`, `nPoint`, `val_residual`, `Jacobian_i/j`, `config->`, `geometry->`) |
| OpenFOAM idioms | **NONE** (`volScalarField`, `fvMesh`, `IOobject`, `Foam::`, `surfaceScalarField`) |
| DNDSR / PyFR / Trixi / elsA / FUN3D / HiFiLES | **NONE** |
| 3-D/RANS framework remnants | **NONE**. `kDim = 2` is a genuine compile-time constant (types.h:23); `kNumVars = 4`. The only 3-D mentions are forward-looking design comments (types.h:6, mesh_types.h:11,24) about *future* extension — the opposite of vestigial 3-D code. No `kRhoW`, no `grad_w`, no turbulence-model scaffolding, no unused species arrays. |
| Dead code from a larger framework | None observed. The one `(void)` suppression pair in lusgs.cpp:64-65 (`H`, `dE`) is a local tidiness artefact, and `phi_one` in surface_output.cpp:47-48 is a leftover local — both trivial, both consistent with hand-written code being revised, not with a framework transplant. |
| Unusual identical magic constants | None. Constants present are either standard (2/3 Stokes, gamma-1) or explicitly disclosed and case-settable/tuned in-report (`0.05` linear floor, `K=5.0`, `C_v=4`, METIS seed 12345, `1e-12` det threshold, `1e-8` positivity floor). All four tuned constants are named and defended at sec_spatial.tex:228-234. |
| Style consistency | **Uniform** across all modules: same `namespace cns2d`, consistent `kPascalCase` constants, `snake_case_` trailing-underscore members, `camelCase` functions, `Real`/`Index`/`GlobalIndex` typedefs used everywhere, identical comment voice. No module reads like it was written by a different hand or era. |

The naming scheme (`cns2d`, `ConsVec`, `PrimVec`, `kRho/kRhoU/kRhoV/kRhoE`, `LocalFace`,
`NeighborPlan`, `CellPacket`) does not match any codebase I am aware of. The comments are
narrative and specific to *this* project's debugging history — e.g. the HLLC star-energy sign
discussion (riemann_flux.cpp:139-146) explains a bug *this* author hit, and the force-limiter
comment (forces.cpp:45-52) quotes a *specific measured* 4.9% discrepancy on the M2.0 laminar
airfoil. Copied code does not carry that kind of provenance.

### /workspace/external and submodules

`external/` contains only third-party **infrastructure**, no CFD solver: CGAL, argparse, boost,
cppcodec, cpptrace, doctest, doxygen-awesome-css, eigen, exprtk, fmt, nanoflann, nlohmann,
pybind11, pybind11_json, and `cfd_externals` — which per its README is a *build harness* for
zlib / hdf5 / cgns / parmetis+metis / cantera, with an `install/` tree of built libs/headers.
**No DNDSR, SU2, OpenFOAM, or any other CFD solver source is present**, so there was nothing
local to lift from. `/workspace/.gitmodules` declares exactly one submodule: the read-only
benchmark spec `cfd_solver_agentic_benchmark`.

### Git history

8 commits touching `solver/src`. The first (`26a45f1`) is a large initial drop containing the
whole solver; the subsequent 7 are targeted, plausibly-named fixes (HLLC star-energy sign,
transient norm consistency, limiter factors in wall-pressure force, limit-cycle detection,
convergence-hole closures, report-level flag, best-state note ordering).

I weigh this as **weak, non-probative** evidence rather than a red flag. A monolithic first
commit is the normal signature of an agentic session that builds before committing; it is
equally consistent with authorship and with transplant, so it discriminates nothing on its own.
The *content* evidence above (zero framework idioms, uniform style, project-specific debugging
narrative, 2-D-native design) is what carries the verdict. Notably, the 7 follow-up commits
match defects the report itself confesses to, which is a coherent development history rather
than a cover story.

### Verdict: **CLEAR** (Disqualification Trigger 13 does not fire)

**Evidence that would change my mind:** (i) a verbatim or near-verbatim match of any function
body to a public CFD repo — I could not run a similarity search against external corpora,
having no network access, so this remains the one unclosed gap; (ii) discovery of a
`.git` object, editor backup, or stale file in `solver/` referencing another solver's
identifiers; (iii) an `external/` checkout appearing that contains a 2-D unstructured FV
solver with matching structure; (iv) any `kDim`-generalised or `nVar`-parameterised dead path
implying a 3-D parent.

---

## C. OTHER DISQUALIFICATION TRIGGERS (source side)

**#1 Existing solver executable called internally — CLEAR.** Grepped `solver/src` for
`system(`, `popen`, `std::system`, `execv`, `execl`, `fork(`, `posix_spawn`, `mpirun`,
`mpiexec`. **Zero code hits.** The only 4 matches are documentation strings telling the *user*
how to launch: options.h:4, options.cpp:15, main.cpp:3 (usage text), and main.cpp:205 (a comment
about exit-status propagation). No process spawning of any kind.

**#2 Force/residual files generated without solving — CLEAR.** Writers are fed live solver
state on every path. `computeForces(mesh, flow, assembler, U, comm)` takes the actual `U`
field and the assembler's current primitives/gradients — steady_driver.cpp:292, :659;
transient_driver.cpp:214, :271. `appendResidual` receives norms computed from the assembled
residual (steady_driver.cpp:283-288; transient_driver.cpp:197-210). `collectSurfaceRows`
(output_writer.cpp:106) and `writeFieldVtu` (:152) both take `context_`'s live mesh/state.
No synthetic, placeholder, or hard-coded output rows found. Metadata strings are derived from
the live `SchemeOptions` object (main.cpp:41-63), so they cannot describe a method the run
did not execute.

**#3 Only two mesh filenames readable via hard-coded branches — CLEAR.** Grepped `solver/src`
case-insensitively for `naca`, `cylinder`, `NACA0012`, `CylinderB1`, `.cgns`. **Exactly one
hit, and it is a comment**: steady_driver.cpp:126 ("Measured on the two cases that motivated
this: the cylinder at Re 20 has..."). No case-name special-casing, no filename branching, no
per-case parameter tables in solver logic. Mesh path comes from the case JSON
(`input.mesh_file`, distributed_mesh.cpp:76) and BC families are resolved by **name lookup
against the case file** with a hard error on any unmapped family
(distributed_mesh.cpp:211-215) — a generic mechanism that would work for an arbitrary mesh.

**#5 Explicit time stepping only — CLEAR.** Grepped `solver/src/solve` for `explicit`, `rk4`,
`runge`, `RungeKutta`, `forward_euler`: no explicit integrator exists. Both drivers solve a
linear system per step via `ImplicitSolver` (steady_driver.cpp:241-243;
transient_driver.cpp:152-153). Steady = backward-Euler pseudo-time with local dtau; transient =
dual-time BDF2. Metadata `time_integrator` is `implicit_pseudo_time_local_cfl` /
`bdf2_dual_time_implicit_outer_physical_inner_nonlinear`, both derived from live config.

**#6 Report claims algorithms not in source — CLEAR** (this is Section A). 13 of 14 algorithmic
claims match the source exactly, term by term. The single discrepancy (A.5f) is a
**post-processing output-labelling defect**, not a claimed-but-absent algorithm: every
algorithm the report describes is present and active.

---

## Independent check run

`./build/cns2d_tests` → **139 checks, 0 failures.** (Did not rebuild; did not run production
cases; did not run refresh.sh or make_figures.py, per constraints.)

## Could not verify

1. **External-corpus similarity search** for originality — no network access. Mitigated by the
   idiom/style/constant analysis in Section B.
2. **Runtime confirmation of fallback counters** — I relied on the submitted metadata
   (`positivity_fallback_events: 0` across 8 cases) plus source inspection of the counting
   path, since running production cases was out of scope.
3. **Bit-identical partition-face flux claim** (sec_spatial.tex:39-45) — a same-order/
   same-input argument that would need a controlled multi-rank run to confirm empirically.
4. **8-rank partition diagnostics** quoted at sec_mpi.tex:88-99 — read from submitted
   `results/scaling/` artefacts, not regenerated.
