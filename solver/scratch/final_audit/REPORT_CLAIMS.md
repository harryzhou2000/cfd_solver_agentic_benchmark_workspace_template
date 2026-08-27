# FINAL PRE-SUBMISSION AUDIT — QUANTITATIVE CLAIMS IN report/

Scope: hunt for false or stale quantitative claims. Read-only. Every number
re-derived from `results/` or `src/`, never from the report.

Python: `/workspace/solver/.venv/bin/python`. Scripts in this directory:
`sat_check.py`, `tables.py`, `walls.py`, `slip_scal.py`, `cl_dev.py`,
`cd_hunt.py`, `physics.py`, `orders.py`, `misc.py`, `consts.py`.

## COVERAGE

| Section / file | Claims checked | DISAGREE | UNVERIFIABLE |
|---|---|---|---|
| report.tex (abstract) | 12 | 4 | 0 |
| numbers.tex (hand-maintained) | 16 | 4 | 1 |
| sec_results.tex | 21 | 8 | 0 |
| sec_verification.tex | 14 | 3 | 2 |
| sec_parallel.tex + tab_scaling | 13 | 3 | 0 |
| tab_forces / tab_runstatus / tab_partition | 30 cells + 24 cells + 56 cells | 0 | 0 |
| sec_sensitivity.tex | 4 | 0 | 1 |
| sec_mesh / sec_spatial / sec_bc / sec_implicit / sec_mpi / sec_limitations | 9 | 1 | 0 |
| **TOTAL** | **89 claims + 110 table cells** | **23** | **4** |

---

# CRITICAL FINDINGS

## C1 — CONFIRMED LIVE BUG: `tab:saturation` rows 1–3 are stale mid-run numbers (t≈54.7, not t=300)

**Independently reproduced.** Using the exact algorithm of
`tools/transient_status.py` (zero crossings of `cl`, linearly interpolated;
period = zc[i+2]−zc[i]; spread = max−min; St = 1/period; mean C_D over window),
applied to the FINAL 30001-row record (t = 0.01 … 300.0):

| Window | Report St / T / spread / meanCD | Mine (final record) | max rel err |
|---|---|---|---|
| t≥16.8 | 0.1666 / 6.0023 / 2.2324 / 1.1173 | **0.1811 / 5.5203 / 2.2583 / 1.2292** | 10.0 % |
| t≥30 | 0.1796 / 5.5667 / 0.1947 / 1.1928 | **0.1827 / 5.4730 / 0.2207 / 1.2417** | 13 % |
| t≥40 | 0.1816 / 5.5078 / 0.0360 / 1.2276 | **0.1829 / 5.4681 / 0.0620 / 1.2455** | 72 % (spread) |
| t≥60 | 0.1830 / 5.4652 / 0.0022 / 1.2468 | 0.1829 / 5.4664 / 0.0024 / 1.2466 | 7.2e-2 (AGREE) |

**Truncation scan proves provenance.** Rows 1–3 reproduce to <0.1 % on ALL FOUR
quantities if and only if the record is truncated at **t_end ∈ [54.45, 54.95]**.
At t_end = 54.70 the recomputation returns 0.1666/6.0023/2.2324/1.1171,
0.1796/5.5667/0.1947/1.1927, 0.1816/5.5078/0.0360/1.2275 — i.e. the table's
exact printed digits. The first three rows are a **mid-run snapshot at ≈18 % of
the final record**; only the t≥60 row was refreshed. Verdict: **DISAGREE**.

### Blast radius — sentences that become wrong or unsupported

1. **[sec_results.tex:1197–1199]** the three data rows themselves. DISAGREE.
2. **[sec_results.tex:1197] "Spread/period 37 %"** — on the final record
   2.2583/5.5203 = **40.9 %**, not 37 %. DISAGREE.
3. **[sec_results.tex:1198] "3.5 %"** → 0.2207/5.4730 = **4.03 %**. DISAGREE.
4. **[sec_results.tex:1199] "0.7 %"** → 0.0620/5.4681 = **1.13 %**. DISAGREE
   (61 % relative error; this is the worst cell in the table).
5. **[sec_results.tex:1200] "0.04 %"** → 0.0024/5.4664 = 0.044 %. AGREE.
6. **[sec_results.tex:1205–1206]** "the period spread collapses from $37\,\%$ of
   the period to $\num{0.04}\,\%$" — the headline argument of the subsection.
   The endpoints should be **40.9 % → 0.04 %**. The qualitative claim (spread
   collapses by ~3 orders) SURVIVES and is arguably strengthened, but the
   quoted start value is wrong. DISAGREE on the number, argument intact.
7. **[sec_results.tex:1211–1217]** the "monotone approach from below" paragraph.
   This is the subtle one. The report says an earlier draft inferred monotone
   approach from below and that **"That inference was wrong"** — and it is
   retracted for the right reason (nested windows). But note the retraction is
   argued against the STALE numbers. On the true final record the nested-window
   trend is St 0.1811→0.1827→0.1829→0.1829 and meanCD 1.2292→1.2417→1.2455→1.2466
   — still monotone increasing, so the retraction's logic still applies and the
   conclusion still holds. **The paragraph's conclusion is UNAFFECTED**; only the
   magnitudes it implicitly describes are stale. No correction needed beyond the table.
8. **[sec_results.tex:1180]** "\Cref{tab:saturation} shows how the measurement
   evolves as the window is moved forward through the record" — false as written:
   three of four rows are not from "the record", they are from a truncated
   earlier record. The table caption **[sec_results.tex:1185–1189]** explicitly
   attributes all rows to "`tools/transient_status.py` reading `forces.csv`",
   which is now untrue for rows 1–3. DISAGREE.
9. **Cross-references that inherit the staleness:**
   - **[sec_results.tex:53]** "measurements of \cref{tab:saturation} instead. Its
     row is a single-instant snapshot of…" — points readers at the table as
     evidence.
   - **[sec_verification.tex:1043]** "convergence evidence coming from
     \cref{tab:saturation} instead." — the periodicity/convergence argument for
     the Re200 case is delegated to this table.
   - **[tab_forces.tex:12]** "periodic, see \cref{tab:saturation}" — the
     stationarity cell for Cyl. Re=200 in the headline forces table.
   All three lean on a table that is 3/4 stale. The **conclusions they support
   are still true** (the t≥60 row, which is correct, is sufficient evidence), so
   these are unsupported-as-cited rather than false.

**Net assessment.** The physical conclusion of the subsection survives intact
because it rests on the t≥60 row and on the independent non-overlapping-window
table at [sec_results.tex:1230–1233] (which I verified AGREES — see V1). What
fails is the table itself, four derived percentages, and the caption's claim of
provenance. This is incident #6 of the same class.

---

## C2 — HAND-MAINTAINED `numbers.tex`: three constants match NO run; one is cross-mesh mislabelled

Observed per-run `mesh verification:` values (all 8 production runs, from
`stdout.log`, extracted by `consts.py`):

| Quantity | NACA mesh (6 runs) | Cylinder mesh (2 runs) |
|---|---|---|
| face-closure | 1.025e-16 | 1.020e-16 |
| volume-closure | 1.090e-10 | 4.757e-13 |
| area mismatch | 4.166e-15 | 2.355e-16 |
| linear-gradient (LSQ) | **1.428e-10** | **3.137e-13** |
| conservation defect | 1.225e-18 / 1.364e-17 / 1.759e-17 (inviscid) — **2.960e-03 / 6.025e-02 / 1.491e-01 (viscous)** | **5.731e-04 / 5.731e-05** |

| Macro | numbers.tex value | Actual | Verdict |
|---|---|---|---|
| `\vFaceClosure` | 1.0e-16 | 1.025e-16 (NACA), 1.020e-16 (cyl) | AGREE (rounded, both meshes consistent) |
| `\vVolumeClosure` | 1.1e-10 | 1.090e-10 NACA; cyl is 4.757e-13 | **DISAGREE — NACA-only, presented as global** |
| `\vAreaLineIntegral` | **1.4e-14** | 4.166e-15 (NACA), 2.355e-16 (cyl) | **DISAGREE — matches NEITHER mesh** |
| `\vLsqExactness` | **3.1e-13** | 3.137e-13 is the CYLINDER value; NACA is 1.428e-10 (**340× larger**) | **DISAGREE — cylinder value presented as global** |
| `\vConservation` | **9.2e-17** | appears in NO run; range spans 1.2e-18 … 1.5e-01 | **DISAGREE — matches NEITHER mesh** |
| `\vJacobianFd` 1.7e-8 / `\vJacobianStates` 20,000 | — | `scratch/jac_fd_test.cpp:27` `const int N = 20000`; `scratch/jac.cpp:12` 20000 trials | AGREE on count; tolerance UNVERIFIABLE (see U2) |
| `\vHllcRegression` 1e-11 | — | cited [sec_verification.tex:1115] | UNVERIFIABLE from results/ |
| mesh sizes: `\nacaNodes` 15682, `\nacaCells` 20816, `\nacaTri` 10752, `\nacaQuad` 10064, `\nacaFaces` 36498, `\nacaBoundaryFaces` 484, `\nacaWallEdges` 404, `\nacaFarEdges` 80 | — | `global mesh: 15682 nodes, 20816 cells (10752 tri, 10064 quad), 36498 faces (36014 interior, 484 boundary)`; `'bc-2' (80 faces) 'bc-4' (404 faces)` | **ALL 8 AGREE** |
| `\cylNodes` 9995, `\cylCells` 10185, `\cylFaces` 20180, `\cylBoundaryFaces` 120, `\cylWallEdges` 100, `\cylFarEdges` 20 | — | `global mesh: 9995 nodes …, 10185 cells (500 tri, 9685 quad), 20180 faces (20060 interior, 120 boundary)`; `'WALL' (100 faces) 'FAR' (20 faces)` | **ALL 6 AGREE** |
| `\pLinearFloor` 0.05 | — | `src/numerics/riemann_flux.cpp:244` `const Real linear_floor = 0.05 * max_speed;` | AGREE |
| `\pVenkatK` 5.0 | — | `src/solve/solver_context.cpp:36` `scheme_.venkat_k = 5.0;` | AGREE |

Mesh sizes and scheme parameters are clean — 14 of 14 verified. The verification
constants are the problem: **4 of 8 wrong**, and the two worst
(`\vAreaLineIntegral`, `\vConservation`) correspond to no measurement in the
submitted results at all.

### C2a — The conservation-defect claim is the most serious

**[report.tex:34]** (abstract, unqualified):
> "a discrete conservation defect of $\vConservation$" → **9.2e-17**

**[sec_spatial.tex:31]** (also unqualified):
> "This is verified to $\vConservation$ in \cref{sec:verification}."

**[sec_limitations.tex:168]** (also unqualified):
> "$\vConservation$, and an analytic Jacobian confirmed against finite differences."

The actual submitted conservation defects span **1.225e-18 to 1.491e-01** — the
three viscous NACA cases are 2.960e-03, 6.025e-02, 1.491e-01, i.e. **fourteen to
fifteen orders of magnitude larger** than the quoted figure.

I investigated whether the report's scoping rescues this. `sec_verification`
does scope it, twice:
- **[sec_verification.tex:153–154]** table footnote: "$^\dagger$Measured in the
  first-order configuration; see the discussion below for why this scope matters."
- **[sec_verification.tex:208–210]**: "\emph{discrete conservation is verified to
  $\vConservation$ in the first-order configuration}. It is not a demonstration
  that the second-order scheme is conservative to that tolerance."

And I confirmed in `src/` that the check genuinely does force first order:
`src/mesh/mesh_verification.cpp:135–137` sets `probe_scheme.second_order = false`
before the probe, with a comment explaining that comparing a reconstructed
interior against a first-order boundary term "measures the reconstruction, not
the conservation". So the *first-order scoping is real and honestly explained*.

**But the scoping does not explain the viscous values, and the report never
addresses them.** The defect is computed by the same code path in every run
(`mesh_verification.cpp:158–201`, normalised by total boundary flux magnitude at
line 200–201), yet the six inviscid/cylinder runs give 1e-18…1e-17 while the
three viscous NACA runs give 1e-3…1.5e-1. The probe forces first-order
*reconstruction* but does not disable the *viscous* flux, so on viscous cases the
interior residual includes a viscous contribution that the independently
recomputed boundary term (`riemannFlux` only, line 175–177) omits entirely —
the comparison is inconsistent exactly as the report warns about for
reconstruction, but for viscous terms. That is a real, undisclosed limitation of
the check.

Verdict: **DISAGREE.** Three problems, in descending severity:
1. The value 9.2e-17 **matches no run** — provenance unknown, unreproducible.
2. The abstract and two other sites quote it **unqualified**, with no
   first-order caveat, so a reader of the abstract alone is misled.
3. Neither the scoped nor the unscoped statement discloses that the submitted
   viscous cases report defects up to 1.5e-1. The honest statement is that
   conservation is verified at 1e-17 **on inviscid cases in the first-order
   configuration**, and that the viscous-case diagnostic is not a valid
   conservation measurement.

### C2b — LSQ "exactness": cylinder number presented as the global figure

**[report.tex:33–34]** "exactness of the least-squares gradient on a linear field
$\vLsqExactness$" → 3.1e-13. **[sec_verification.tex:143–144]** table row "LSQ
exactness | gradient of a linear field reproduced exactly | $\vLsqExactness$ |
$O(\varepsilon)$". **[sec_limitations.tex:95]** "linear-field gradients
($\vLsqExactness$)".

3.137e-13 is the **cylinder** value. On the NACA mesh — which carries six of the
eight cases and every airfoil result in the report — it is **1.428e-10**, i.e.
**340× larger and not $O(\varepsilon)$ in any reasonable reading** (machine
epsilon is 2.2e-16; 1.428e-10 is ~6e5 ε). I confirmed from
`mesh_verification.cpp:228–246` that the metric is a *relative* error
(`norm(g - exact_gradient)/norm(exact_gradient)`, max-reduced), so the two
meshes are directly comparable and the NACA value is genuinely worse.
Verdict: **DISAGREE** — quoting the better of two meshes as the single global
figure, in the abstract, under the word "exactness".

---

## C3 — ABSTRACT: force-agreement claim rests on an undisclosed metric choice, and the runs are non-production

**[report.tex:38–39]**
> "force coefficients agree to within $\num{5e-4}$ relative across $np=1,2,4,8$"

What I measured from `results/scaling/` (`cl_dev.py`, `slip_scal.py`):

On the **written/reported final row** of each run, relative to np=1:

| Case | worst rel C_D | worst rel C_L | worst rel C_mz |
|---|---|---|---|
| cylinder_re20 | 2.36e-4 (np=4) | **5.33e-2 (np=8)** | **3.95e-1 (np=8)** |
| naca_m015_lam | 1.05e-4 (np=4) | **8.09e-2 (np=8)** | **1.13e-1 (np=8)** |

On the **step-1500 row** (which is what `tab_scaling` actually reports):
cylinder worst rel C_D **4.16e-4** (np=8), NACA worst **1.05e-4** (np=4).

So the claim is **true only for $C_D$**. For $C_L$ the worst relative
deviation is **8.1e-2 — 162× the quoted bound** — and for $C_{m,z}$ it reaches
**0.40**. The abstract says "force coefficients" (plural, unrestricted), which
on its face covers $C_L$ and $C_{m,z}$.

In fairness: $C_L$ here is ~1.1e-3 to 1.6e-3, i.e. near zero for a symmetric
body at zero incidence, so a *relative* $C_L$ error is a near-meaningless
metric — the absolute $C_L$ spread is only ~9e-5. That is a legitimate defence
of the physics but **not of the sentence**, which promises a relative bound on
"force coefficients" without restricting to $C_D$ or disclosing that $C_L$ is
excluded because relative error on a near-zero quantity is uninformative.
`sec_parallel` never states the metric either. Verdict: **DISAGREE** —
overbroad as written; needs "$C_D$" substituted for "force coefficients", or an
explicit absolute-tolerance statement for the near-zero coefficients.

### C3a — sec_parallel quotes two cylinder $C_D$ values that exist in no file

**[sec_parallel.tex:35–39]**
> "the worst case being $\num{4.7e-4}$ for the cylinder at $np=8$. Per case: the
> airfoil agrees to five significant figures with a maximum deviation of
> $\num{1.0e-4}$ … and the cylinder agrees to four significant figures, from
> $2.12601$ at $np=1$ to $2.12701$ at $np=8$, with the edge cut rising through
> $0, 97, 282, 471$."

Step-1500 cylinder $C_D$ (`cd_hunt.py`): np=1 **2.12627534**, np=2 2.12634697,
np=4 2.12652151, np=8 **2.12716067**.

- "2.12601 at np=1" → actual **2.12628**. DISAGREE (off by 2.7e-4 absolute; the
  nearest row anywhere in the np=1 file to 2.12601 is step 1500 itself at
  2.12628, so this is not a different-step artifact — it is simply wrong).
- "2.12701 at np=8" → actual **2.12716**. DISAGREE (the value 2.12710 occurs at
  step 1258, so this looks like a stale earlier-step reading).
- "worst case 4.7e-4 for the cylinder at np=8" → recomputed **4.16e-4**.
  DISAGREE. (The report's own quoted pair, |2.12701−2.12601|/2.12601 = 4.70e-4,
  reproduces 4.7e-4 exactly — confirming the 4.7e-4 is derived from the two
  wrong values, not measured.)
- "airfoil … maximum deviation 1.0e-4" → 1.05e-4. AGREE.
- edge cut "0, 97, 282, 471" → confirmed exactly from
  `partition_diagnostics.json`. AGREE.

Note the direction of the error is *conservative* (real worst case 4.16e-4 is
better than the claimed 4.7e-4, and both are inside the 5e-4 bound), so the
conclusion survives — but three printed numbers are wrong and internally
self-consistent only with each other, which is the signature of a stale edit.

### C3b — Truncated non-converged scaling runs: adequately disclosed

All eight scaling runs report `convergence_status: "not_converged"` and stop at
step 1472/1473 (cylinder) or 1500 (NACA), with last-row $C_D$ = 2.1364–2.1369
versus the production value **2.0184**. Checked disclosure:

- **[sec_parallel.tex:11–13]** caption: "Every run uses an identical fixed budget
  of 1500 pseudo-time steps, so the comparison is like-for-like". AGREE — states
  the fixed budget.
- **[sec_parallel.tex:74–75]** "At a fixed budget of 1500 steps the iterates have
  therefore not travelled identical trajectories". AGREE.
- **[sec_parallel.tex:210–212]** "under this quota $np=8$ is a correctness and
  consistency demonstration rather than a performance one." AGREE.
- `tab_scaling` column header is "$C_D$ at step 1500" — explicitly labels the
  step. AGREE.

Verdict: **AGREE / adequately disclosed.** The runs are labelled as a
fixed-budget consistency study at a named step, not as production results, and
the production $C_D$ = 2.018 appears in `tab_forces`. The word
"not_converged" is never used, and the 2.136-vs-2.018 gap is never spelled out,
so a careless reader could still conflate them — but the framing is honest. Minor
recommendation only, not a defect.

---

# OTHER DISAGREEMENTS

## D1 — "all three no-slip cases" — there are FIVE

**[sec_verification.tex:265–266]**
> "on all three no-slip cases the maximum wall speed is \textbf{exactly}
> $0.000\mathrm{e}{+}00$"

From `metadata.json` `boundary_families`, the cases with
`no_slip_adiabatic_wall` are **five**: naca0012_m015_laminar_re5000,
naca0012_m080_laminar_re5000, naca0012_m200_laminar_re5000,
cylinder_m010_laminar_re20, cylinder_m010_laminar_re200. I verified
max|mach| = 0.000000e+00 on the wall in `surface.csv` for **all five**.
Verdict: **DISAGREE on the count (3 vs 5)**; the underlying property holds on all
five, so the fix strengthens the claim. Likely a stale count from when only the
three NACA laminar cases were being discussed.

## D2 — Re200 residual orders: 2.25 vs 2.07, undisclosed at the point of use

`tab_runstatus` [tab_runstatus.tex:26] reports **2.25** orders for Cyl. Re=200,
matching `run_status.json` (`residual_reduction_orders: 2.2471`).

But recomputing from `residuals.csv`: `residual_l2` init 1.798097 → last
1.526702e-02 = **2.0711 orders**. The 2.247 figure is not the `residual_l2`
ratio. `residual_linf` gives 3.0335. So the reported 2.25 is a
**different norm** (spatial-only, per the established finding) from the
`residual_l2` column a reader would naturally check.

**[sec_verification.tex:1124]** does explain the distinction:
"\cref{eq:bdf2}, which includes the physical-time term, by the \emph{spatial-only}"
— so the mechanism is documented somewhere. But I found **no occurrence of
2.07** anywhere in `report/`, and no note at `tab_runstatus` warning that its
"Orders" cell for this one case uses a different norm than the other seven.
Verdict: **DISAGREE / incompletely disclosed.** The table cell matches its
source, but the table silently mixes two norms across rows.

## D3 — `tab:rankchoice` timings are unverifiable and inconsistent with the measured study

**[sec_parallel.tex:168–171]** table: np=1 50.7 s, np=2 18.3 s (speedup 2.77),
np=4 9.8 s (5.17), np=8 12.2 s (4.16), for "30 physical steps of the $\Reinf=200$
transient".

Arithmetic checks out internally (50.7/18.3 = 2.770, 50.7/9.8 = 5.173,
50.7/12.2 = 4.156 — all AGREE). But there is **no artifact in `results/`** for a
30-step Re200 rank sweep; the only Re200 run is the 30000-step production case.
Verdict: **UNVERIFIABLE** (see U1) — and note the superlinear 2.77 and 5.17 are
much stronger than the idle-machine study's 1.98/3.89 (cylinder) and 2.14/4.43
(NACA) that I did verify, so the two tables tell noticeably different
quantitative stories about the same claimed cache effect.

## D4 — np=8 regression: disclosed, with a plausible and partly corroborated explanation

Recomputed from `results/scaling/` (wall time from `metadata.json`):

| Case | np=1 | np=2 | np=4 | np=8 |
|---|---|---|---|---|
| cylinder_re20 | 24.32 s (1.00) | 12.27 s (**1.98**) | 6.258 s (**3.89**) | 8.883 s (**2.74**, eff 34 %) |
| naca_m015_lam | 46.41 s (1.00) | 21.69 s (**2.14**) | 10.48 s (**4.43**) | 13.76 s (**3.37**, eff 42 %) |

np=8 is slower than np=4 in both cases — **confirmed**. Disclosure:
- **[sec_parallel.tex:192–195]** "The \textbf{regression at $np=8$} is
  oversubscription of the quota: eight processes share four CPUs, so each is
  descheduled roughly half the time while still paying the full halo-exchange and
  synchronisation cost. It is not evidence of a communication bottleneck in the
  solver".
- **[sec_parallel.tex:201–205]** "monotone speedup through $np=4$ followed by
  regression at $np=8$, with speedups at the optimum of $3.9\times$ for the
  cylinder at $\Reinf=20$ and $4.4\times$ for the $\Reinf=5000$ airfoil."
  → my 3.89 and 4.43. **AGREE.**
- **[sec_parallel.tex:122]** cross-reference to the regression.

Verdict: **AGREE — required disclosure present, explanation plausible.** The
4-CPU quota is independently corroborated by
`sec_parallel.tex:179` and the oversubscription story is consistent with the
~34–42 % efficiency I measured. One gap: the NACA np=4 speedup of **4.43 is
superlinear** and the report presents it in the same breath as the cylinder's
sub-linear 3.89 without flagging it; the cache explanation is given only for
`tab:rankchoice`, not here.

## D5 — "130 to 139 checks": half-verifiable

**[sec_verification.tex:179]** "taking it from $130$ to $139$ checks, all passing".
`./build/cns2d_tests` → `cns2d unit tests: 139 checks, 0 failure(s)`.
**139 and "all passing" AGREE.** The prior count of 130 is historical and cannot
be verified from the present tree (the pre-regression-test binary no longer
exists). Partially UNVERIFIABLE, no evidence of error. Also: 139 − 130 = 9 new
checks, consistent with the described "two facts pinned separately".

---

# VERIFIED-CORRECT SAMPLE (selected; all AGREE)

**V1 — non-overlapping-window table [sec_results.tex:1230–1233].** Independently
recomputed on the final record: St constant 0.1829–0.1830, meanCD 1.2462–1.2470,
amplitude ±0.5307. Report gives 0.1830/1.2466, 0.1829/1.2470, 0.1829/1.2466,
0.1829/1.2462 and "amplitude … flat at $\pm0.530$", "$\overline{C_D}$ constant
to $\num{8e-4}$" (my spread 1.2470−1.2462 = 8e-4). **All AGREE.** This table is
clean — which is precisely why the stale `tab:saturation` is survivable.

**V2 — `tab_forces` (30 cells).** Every $C_D, C_L, C_{m,z}, C_{D,p}, C_{D,v}$
matches the **last row** of the corresponding `forces.csv` to the printed
precision. Spot values: m015_inv $C_D$ 0.0009884 → 9.883561552587e-04 ✓;
cyl_re20 2.018 → 2.018374488056 ✓ with $C_{D,p}$ 1.221 → 1.220904699297 ✓ and
$C_{D,v}$ 0.7975 → 7.974697887585e-01 ✓ (and 1.2209+0.7975 = 2.0184 ✓);
cyl_re200 1.269/−0.47/0.00107/1.03/0.239 → 1.268950/−0.470222/1.0716e-3/1.030/0.23895 ✓.
**0 mismatches.** Correctly uses the restored-best-state last row throughout.

**V3 — `tab_runstatus` (24 cells).** Ranks all 4 (`mpi_ranks: 4` in every
`run_status.json`) ✓. Steps 1861/2546/4854/3839/4639/37022/2450/30000 ✓ =
`final_step`. Orders 5.26/3.92/2.9/5.47/4/4.78/5.86/2.25 ✓ = `run_status`
values 5.2606/3.9158/2.8964/5.4674/4.0006/4.7831/5.8604/2.2471. Wall times
20.23/55.24/43.23/37.84/40.88/529.3/13.72/1.08e4 ✓. Statuses ✓ (7 converged,
1 statistically_periodic). **0 mismatches** (norm caveat at D2).

**V4 — `tab_partition` (56 cells).** Byte-for-byte identical to
`results/scaling/cylinder_m010_laminar_re20_np8/partition_diagnostics.csv`:
owned 1266/1265/1281/1276/1282/1277/1273/1265, ghost
103/106/107/129/101/105/119/118, boundary faces 20/38/33/29/0/0/0/0, neighbours
7/4/3/5/3/4/4/6, send 101/104/109/127/101/107/120/119, recv = ghost column.
**0 mismatches.** Owned sums to 10185 = `\cylCells` ✓. Note recv ≡ ghost
exactly, as it must.

**V5 — residual orders, all 8 cases [tab_runstatus, sec_results].** Recomputed
log10(init/last) from `residuals.csv` using the LAST row (restored best state),
confirming the non-monotonicity gotcha: m015_inv 5.2606 (max-step row would give
4.7272), m080_inv 3.9158 (max-step 2.8868), m200_inv 2.8964 (max-step 2.3380),
m015_lam 5.4674, m080_lam 4.0006, m200_lam 4.7831, cyl_re20 5.8604,
cyl_re200 2.0711/2.2471. **The report consistently uses the restored-state
values.** AGREE.

**V6 — shortfall disclosure, as the abstract promises.** **[report.tex:52–54]**
"$3.92$ of $4.00$ orders for the transonic inviscid case and $2.90$ of $3.00$
for the supersonic inviscid case --- and this is stated wherever those cases are
quoted". Verified: [sec_results.tex:424, 439, 440–442, 460–466]. The specific
supporting numbers all check out:
- "residual reaches $\num{4.681e-4}$ at step $2547$" → `residuals.csv` step
  2547 = 4.682450e-04 ✓ (4.681e-4 vs 4.682e-4 — rounds to 4.682, a 1-in-4682
  discrepancy in the last digit; trivial but technically off)
- "degrades by a factor of $10.7$" → 4.9993e-3/4.6765e-4 = **10.69** ✓
- "Over the $950$ steps after the best state at step $2546$" → max step 3496 −
  2546 = **950** ✓
- "averages $\num{4.51e-3}$ … reaching $\num{5.00e-3}$" → tail mean **4.5086e-3**,
  max **4.9993e-3** ✓
- "never returning below $\num{4.68e-4}$" → tail min 4.6825e-4 > best 4.67653e-4 ✓
- m200: "best $\num{1.8346e-3}$ at step $4854$, which is $2.8964$ … plateau
  averaging $\num{3.84e-3}$ and peaking at $\num{6.64e-3}$" → 1.834624e-3 ✓,
  2.8964 ✓, tail mean **3.8427e-3** ✓, max **6.6361e-3** ✓
- "within $0.09$ and $0.10$ orders of their targets" → 4.00−3.9158 = 0.0842,
  3.00−2.8964 = 0.1036 ✓
**AGREE throughout — this section is exemplary.**

**V7 — pitot / isentropic $C_p$ arithmetic [sec_results.tex:317–338].**
Isentropic $C_{p0}$ at M=2, γ=1.4 = **2.4373**; Rayleigh pitot
$p_{02}/p_1$ = 5.6404 → $C_p$ = **1.65730**, matching the report's "1.6573"
exactly ✓. "$\num{9}$ … exceed [the pitot] bound while \textbf{zero} exceed the
isentropic one": from `surface.csv`, count(cp > 1.6573) = **9**, count(cp >
2.4373) = **0**, max cp = 2.1434 ✓. Both counts and the reasoning AGREE. The
report correctly identifies that the isentropic reference is invalid behind a bow
shock — physics applied to the right case.

**V8 — Blasius [sec_results.tex:767–770].** $C_{D,f} = 2\times1.328/\sqrt{5000}$
= **0.0375615**. Measured $C_{D,v}$ (m015 lam) = 0.0366668, ratio **0.976**.
Formula and factor-of-2 for both sides of a unit chord are correct; the ~2.4 %
shortfall is consistent with the report's discussion. AGREE.

**V9 — Re200 physics vs literature [sec_results.tex:1240–1247].** "converged at
$St=0.183$ against a literature range of $0.19$--$0.20$ and
$\overline{C_D}=1.247$ against $1.3$--$1.4$", "shortfall of about $4\,\%$ in
$St$ and $5\,\%$ in mean drag". My independent values St 0.1829, meanCD 1.2466.
(0.19−0.1829)/0.19 = 3.7 % ≈ 4 % ✓; (1.3−1.2466)/1.3 = 4.1 %, vs midpoint 1.35 →
7.7 % — "about 5 %" is a fair reading against the low end. Literature ranges match
`transient_status.py:73` ("St ~ 0.19-0.20, mean C_D ~ 1.3-1.4"). AGREE, and the
same-sign 2D/dissipation explanation is sound.

**V10 — Re20 $C_D$ ≈ 2.0 [sec_results.tex:108–111].** "asymptote of $2.0177$
bracketed between $2.0176$ and $2.0178$", "$C_D=\num{2.018119}$" at 6.73
orders. Submitted $C_D$ = 2.018374, consistent with a ~2.018 asymptote. AGREE.

**V11 — Re200 run characterisation [sec_results.tex:1107–1119].** "one row per
physical step", "$\Delta t=0.01$", "step numbers increment by exactly one with
\textbf{zero} exceptions". `forces.csv` has 30001 rows for 30000 steps
(header + 30000, t = 0.01…300.0, uniform) ✓. `run_status` inner min/mean/max
**49/132.8/144, 0 misses** ✓ (`metadata.json`: min 49… mean 132.818, target met
100.00 %). AGREE.

**V12 — slip-wall surface measurements [sec_verification.tex:268–270].**
"maximum $|\mathbf{V}\!\cdot\!\mathbf{n}|$ is \num{9.7e-14}, \num{1.7e-13} and
\num{1.4e-13} while the maximum tangential speed is $0.9985$, $1.0889$ and
$1.0417$". Recomputed from `surface.csv` with unit-normalised normals:
**9.695e-14 / 1.699e-13 / 1.433e-13** and **0.998529 / 1.088867 / 1.041702**.
All six AGREE to the printed precision. Correctly restricted to "the three slip
cases" (the three inviscid NACA runs) — contrast D1.

**V13 — scheme parameters vs `src/`.** `\pLinearFloor` 0.05 =
`riemann_flux.cpp:244`; `\pVenkatK` 5.0 = `solver_context.cpp:36`. Limiter
recorded as `venkatakrishnan` and inviscid flux as
`roe_approximate_riemann_with_hllc_positivity_fallback` in all 8
`metadata.json`. `positivity_fallback_events: 0` in every case — consistent with
the abstract calling HLLC a *fallback* that was not needed. CFL ramp reaches its
cap in the two shortfall cases (the whole mechanism of V6). AGREE.

**V14 — no-rank-holds-full-state [report.tex:27–29].** Every
`metadata.json` records `full_mesh_replication_during_iterations: false` and
`full_state_replication_during_iterations: false`, and `halo_exchange:
neighbor_isend_irecv`. AGREE (solver's own self-report; source-level audit of the
IO gather path was out of my scope — see U4).

**V15 — uniform-flow shortfall honestly disclosed.** [sec_verification.tex:227–245]
states the check reaches $O(10^{-11})$ against a $10^{-12}$ threshold, is
"reported as a warning in every run log", quotes \num{2.123e-11}, and explains the
ill-conditioning cause; [sec_limitations.tex:8] repeats it. Confirmed: every one
of the 8 `stdout.log` files carries the
`[cns2d][warn] mesh verification: uniform-flow residual …` line, and the cylinder
value is exactly **2.123e-11** ✓. The verdict is `CHECK` not `PASS` in all 8
logs, as claimed. **AGREE — a genuine defect disclosed rather than buried.**

**V16 — `tab:stagnation` [sec_sensitivity.tex:81–84]** internally consistent;
"1 of 119" sign alternations and the floor-on/floor-off comparison are stated as
a controlled experiment. Values live in `scratch/` variant runs, not `results/`,
so only self-consistency was checkable. Sections `sec_mesh`, `sec_equations`,
`sec_implicit`, `sec_mpi` — no quantitative disagreement found in the claims
sampled.

---

# UNVERIFIABLE

- **U1 — `tab:rankchoice` (sec_parallel.tex:168–171).** 30-step Re200 rank sweep;
  no corresponding artifact under `results/`. Internally consistent arithmetic
  but externally unconfirmable, and quantitatively at odds with the verified
  idle-machine study (see D3).
- **U2 — `\vJacobianFd` 1.7e-8.** The 20,000-state count is confirmed in
  `scratch/jac_fd_test.cpp:27` and `scratch/jac.cpp:12`, but the resulting
  tolerance is not recorded in any file under `results/`; re-deriving it would
  require running the probe, which is outside a read-only audit.
- **U3 — `\vHllcRegression` 1e-11** [sec_verification.tex:1115]. Pinned by a unit
  test inside the 139-check binary, which reports only pass/fail, not the margin.
- **U4 — the "130" in "130 to 139 checks"** (D5): historical, pre-dates the
  current binary.
- Figure-count claims: no "71 figures" string exists in `report/*.tex`; nothing
  to check. (`figure_manifest.csv` exists but no prose count references it in the
  files I sampled.)

---

# BOTTOM LINE — must-fix before submission

1. **`tab:saturation` rows 1–3 + the four Spread/period percentages + the
   caption's provenance claim + the "37 %" in the argument sentence**
   (sec_results.tex:1185–1206). Stale mid-run snapshot at t≈54.7. Regenerate from
   the t=300 record: 0.1811/5.5203/2.2583/40.9 %/1.2292, 0.1827/5.4730/0.2207/4.03 %/1.2417,
   0.1829/5.4681/0.0620/1.13 %/1.2455, t≥60 row unchanged. Conclusions survive.
2. **`\vConservation` 9.2e-17** — matches no run; quoted unqualified in the
   abstract while submitted viscous cases show 1e-3…1.5e-1. Re-scope to
   inviscid + first-order, and disclose the viscous-case values.
3. **`\vLsqExactness` 3.1e-13** — cylinder value sold as global under the word
   "exactness"; NACA is 1.428e-10 (340×), which is not $O(\varepsilon)$.
4. **`\vAreaLineIntegral` 1.4e-14** — matches neither mesh (4.166e-15 / 2.355e-16).
5. **`\vVolumeClosure` 1.1e-10** — NACA-only, presented as global (cyl 4.757e-13).
6. **Abstract "force coefficients agree to within 5e-4 relative"** — true for
   $C_D$ only; $C_L$ reaches 8.1e-2 relative, $C_{m,z}$ 0.40.
7. **sec_parallel.tex:38–39** — "2.12601" → 2.12628, "2.12701" → 2.12716, and
   the derived "4.7e-4" → 4.16e-4.
8. **sec_verification.tex:265** — "all three no-slip cases" → **five**.
9. **`tab_runstatus` "Orders" column** silently mixes norms for Cyl. Re=200
   (2.25 spatial-only vs 2.07 from the `residual_l2` column). Add a footnote.

The three headline tables (`tab_forces`, `tab_runstatus`, `tab_partition`) and
the residual-shortfall narrative are clean — 110 table cells checked, 0
mismatches. The failures are concentrated in **hand-maintained `numbers.tex`
constants** and in **one stale table**, exactly the class of error that has
already bitten this report five times.
