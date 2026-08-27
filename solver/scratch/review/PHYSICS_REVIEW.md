# Adversarial Physics & Results Review

Scope: the 7 completed steady cases in `/workspace/solver/results/` and the report in
`/workspace/solver/report/`. `cylinder_m010_laminar_re200` was still running and is excluded
except where noted. Everything below was recomputed independently from the submitted CSV/VTU
files; scripts are in this directory (`surface_audit.py`, plus inline analyses).

Verdict in one line: **the numerics are sound and the force integration is verifiably correct, but
the convergence claims are systematically overstated.** Five of seven cases were stopped while
their drag was still monotonically drifting, and the mechanism that let this happen is a
drift-cancellation loophole in the stationarity test. One case (M2.0 inviscid) additionally shows
a genuine physics defect — a large up/down asymmetry on a symmetric body at zero incidence — that
the report does not disclose.

---

## 1. Definite errors

### 1.1 [SEVERE] Five of seven "converged" cases were stopped mid-drift; the stationarity test is defeated by drift cancellation

The solver requires *either* a small trailing-window drag span *or* a small drift of the window
mean (`steady_driver.cpp:310` span branch, `:313-323` drift branch). Because either branch alone
suffices, a case that fails the span test can still pass on drift — and the drift of a *sum* can be
small while its *components* move a great deal in opposite directions. That is exactly what
happened.

Recomputed from `forces.csv` (final 500-step window vs the preceding 500, in-loop rows only):

| case | span/tol | drift/tol | 50-step block means over last 500 | true state |
|---|---|---|---|---|
| naca0012_m015_inviscid | 77.5 (FAIL) | 0.68 (pass) | non-monotone | genuine limit cycle — OK |
| naca0012_m080_inviscid | 0.141 (pass) | 2.66 (fail) | **monotone decreasing** | still drifting |
| naca0012_m200_inviscid | 1.32 (FAIL) | 0.096 (pass) | non-monotone | genuine limit cycle — OK |
| naca0012_m015_laminar_re5000 | **0.9990** (pass) | 20.1 (fail) | **monotone decreasing** | still drifting |
| naca0012_m080_laminar_re5000 | 0.530 (pass) | 11.5 (fail) | **monotone increasing** | still drifting |
| naca0012_m200_laminar_re5000 | 8.16 (FAIL) | 0.756 (pass) | **monotone increasing** | **still drifting** |
| cylinder_m010_laminar_re20 | **0.9993** (pass) | 28.4 (fail) | **monotone decreasing** | still drifting |

Five cases have a strictly monotone drag trend across all ten 50-step sub-blocks of the very window
the solver certified as stationary. A monotone trend is the signature of an unfinished transient,
not of a converged fixed point.

**The worst instance is `naca0012_m200_laminar_re5000`.** It is labelled `converged` and the
report's `tab_forces.tex` labels it "limit cycle (8.6% p--p)". It is neither. $C_D$ over the last
500 steps rises monotonically from 0.03921 to 0.04237 — sampled every 25 steps it increases at
every single sample from step 3050 to the end. It passed only through drift cancellation:

```
d(pressure_drag) = +3.170e-03   (+19.3 %)
d(viscous_drag)  = -3.200e-03   (-13.2 %)
d(cd)            = -3.068e-05   <- passes the 4.06e-05 drift tolerance
```

Component motion is **207x larger than the net drag drift**. The two components are individually
moving by 13–19 % per 500 steps while their sum happens to cancel to 0.076 %. Calling this a
converged limit cycle is not defensible; the mean is not constant, and the report's own
`sanity_checks.json:289` records `cd_relative_drift_last_10pct = 0.0686` (6.9 %) for this case
while `:272` reports `convergence_status: converged`.

Same loophole, smaller magnitude, on `naca0012_m080_laminar_re5000` (components move 3.1x more than
their sum: pressure −1.786 %, viscous +7.229 %, net +1.148 %).

### 1.2 [SEVERE] Two cases marked `converged` never reached their residual target

| case | target orders | achieved | status recorded |
|---|---|---|---|
| naca0012_m080_inviscid | 4.00 | **2.89** | `converged` |
| naca0012_m200_inviscid | 3.00 | **2.35** | `converged` |

Targets are from the supplied case JSONs (`run_control.residual_reduction_target`). The contract's
`convergence_status` enum is `converged | statistically_periodic | failed` and offers no
"plateaued" value, so the solver writes `converged` (`steady_driver.cpp:383`). The report does
state the shortfall explicitly and honestly (`sec_results.tex:136-151`: "not a run that met the
4.00-order target"), which mitigates this considerably — but the machine-readable
`run_status.json`/`metadata.json` that the validator and a scanning examiner read first say
`converged`, and `tab_runstatus.tex` prints "converged" next to "2.89". Rubric disqualification
trigger 7 is *"`metadata.json` marks incomplete or failed runs as successful final results"* — this
is close enough to that line to be a real exposure.

### 1.3 [SEVERE] The transonic inviscid residual *rose 10.6x*; calling it a "plateau" is wrong

`naca0012_m080_inviscid` did not plateau. From `residuals.csv`:

- minimum residual `4.67653e-04` at step 2546 → **3.92 orders**, essentially the 4.00 target
- final residual `4.95179e-03` at step 3496 → 2.89 orders
- **final/best = 10.59**

The rise is not noise and not limiter chatter. It correlates exactly with the CFL ramp crossing
~50: residual is flat at 4.7e-04 through step 2550 (CFL 50.0), then climbs monotonically to
4.5e-03 by step 2650 (CFL 58.3) as inner iterations jump 15→17, and stays there. The run ended
*above* its own best value having thrown away an order of convergence to CFL-induced degradation.

The report describes this case as a residual that "falls 2.89 orders, then stops improving"
(`sec_results.tex:137-138`) and as a "plateau" caused by limiter switching in small wall cells
(`sec_limitations.tex:19-28`). Both the shape of the history and the attributed cause are wrong:
it reached 3.92 orders and then *degraded* under the CFL ramp. The single best-converged state of
this run was discarded. Nowhere in the report is the residual increase disclosed.

`naca0012_m200_inviscid` shows the same pattern more mildly: best 1.83e-03 at step 4854 vs final
6.44e-03, final/best = 3.51.

### 1.4 [SEVERE] M2.0 inviscid violates up/down symmetry by O(1) on a symmetric body at zero incidence

The exact solution for NACA0012 at AoA 0 is symmetric about $y=0$, and the report correctly says so
(`sec_results.tex:50-56`). The mesh surface is symmetric to machine precision (I verified
$\max|y_{upper}+y_{lower}| = 1.0\times10^{-14}$ and $\max|x_u-x_l| = 1.0\times10^{-14}$ over the 404 wall
faces). But the computed $C_p$ is not:

| case | max abs cp_up − cp_low | as % of mean abs cp |
|---|---|---|
| m015 inviscid | 3.64e-03 | 1.4 % |
| m080 inviscid | 6.11e-02 | 15.7 % |
| **m200 inviscid** | **1.197** | **395 %** |
| m015 laminar | 6.24e-03 | 2.9 % |
| m080 laminar | 1.83e-03 | 0.6 % |
| m200 laminar | 1.23e-02 | 3.2 % |

At the M2.0 leading edge the two sides disagree completely — at $x=0.00178$, $C_p=1.727$ on top and
$0.531$ on the bottom. The difference is a smooth systematic bias (only 8 sign changes over 100
significant stations), not a checkerboard. It is 100x worse than the same geometry run laminar. This
is a real solution defect: the captured bow shock has settled asymmetrically and stayed there.

It also explains the case's $C_L = -3.75\times10^{-3}$, which is 25x larger than the M0.15 inviscid
$C_L$ and the largest lift magnitude of any airfoil case — and $C_{m,z}=8.5\times10^{-4}$. The report
uses $C_L$ and $C_{m,z}$ as "direct measures of the discretization's ability to preserve symmetry"
(`sec_results.tex:52-53`) but never remarks that this case fails that measure badly, and
`sec_results.tex:134` asserts for the transonic case that the two shocks "are mirror images".

### 1.5 [MODERATE] M2.0 inviscid wall pressure exceeds the inviscid maximum by 22 %

For inviscid flow past a blunt body at $M_\infty=2$, the highest attainable wall pressure is the
total pressure behind a normal shock, $p_{02}=1.00722$ ($\gamma=1.4$, $p_\infty=0.178571$), i.e.
$C_{p,\max}=1.6573$. Observed: $p_{\max}=1.22964$, $C_{p,\max}=2.1021$ — **22.1 % above the
theoretical ceiling**, on 9 of 404 faces clustered at the leading edge. That is a genuine
overshoot, physically impossible in a converged inviscid solution, and consistent with 1.4 (the
same leading-edge region is where the asymmetry lives). Not disclosed.

The other cases are clean on this test: M0.15 inviscid $C_{p,\max}=1.00272$ vs limit 1.00564
(under), M0.8 inviscid 1.16854 vs 1.17040 (under). The viscous cases legitimately exceed the
isentropic limit slightly (M0.15 laminar 1.0290 vs 1.0056; cylinder 1.2640 vs 1.0025) — this is
*not* an error, because at Re 20 the viscous normal stress and the finite-cell wall pressure
reconstruction legitimately raise stagnation pressure above the inviscid value; the cylinder's
excess is expected at Re 20 and its two most-upstream faces agree to 5e-6 mirror-symmetrically.

### 1.6 [MINOR] Report equation `eq:stationarity2` states a 600-step window; the code uses 500

`sec_verification.tex:252` writes $\mathrm{span}_{600}(C_D)$ and the surrounding text discusses a
"trailing 600-step window". The production code uses `kForceWindow = 500`
(`steady_driver.cpp:64`), and every note in `run_status.json` says "over the last 500 steps". The
relative/absolute tolerances $(10^{-2},10^{-5})$ and the drift tolerance $10^{-3}$ do match
`kCdRelTol`, `kCdAbsTol`, `kCdDriftRelTol` (`:76,:77,:90`). Only the window length is misstated.

### 1.7 [MINOR] Every CSV has a duplicated final step

All 7 cases write the last step twice in both `forces.csv` and `residuals.csv` with *different*
values, e.g. `naca0012_m200_inviscid` step 4906 appears with $C_D=0.08825552$ then $0.0880165$, and
residual 6.63614e-03 then 6.44232e-03. The second row is the post-loop final state. Harmless for
the contract (the final row does correspond to `surface.csv`/`field_final.vtu`), but it means
`run_status.json` notes quote statistics from a window that a reader recomputing naively from the
file cannot reproduce exactly — and it silently shifts any trailing-window statistic by one sample.
`harvest_numbers.py:356-361` acknowledges this in a comment.

### 1.8 [MODERATE] `surface.csv` and `forces.csv` use *different* wall pressures — the reported split is not reproducible from the submitted surface data on one case

The force integral computes the wall pressure as an **unlimited** linear extrapolation,
`p_wall = p_cell + grad_p . delta` (`forces.cpp:47`), with only a positivity guard. But
`surface.csv` writes the pressure from `reconstructPrimitive(...)` which applies the **limiter
factor** $\phi$ (`surface_output.cpp:49`, `limiter.cpp:132`: `w_cell[v] + phi_cell[v]*d`). Wherever
$\phi<1$ on a wall face the two pressures differ, so the pressure force in `forces.csv` cannot be
recovered by integrating the `pressure`/`cp` columns of `surface.csv`.

In practice this is invisible on six cases (my re-integration matches to $\le1.8\times10^{-3}$
relative) because the limiter is inactive on most wall faces. On `naca0012_m200_laminar_re5000` it
is not invisible:

| case | my pressure drag from surface.csv | forces.csv | rel. diff |
|---|---|---|---|
| m200 laminar | 0.019452214 | 0.018551653 | **+4.85e-02** |
| (all six others) | — | — | $\le$1.8e-03 |

**4.9 % cannot be explained by drift.** $C_{D,p}$ moves 1.95e-05 per step at the end of that run, so
even a full one-step offset between the force row and the surface write accounts for only 1.1e-03
relative. The remaining ~4.7e-02 is the limited-vs-unlimited wall pressure discrepancy, amplified
because M2.0 laminar is the case where the limiter is most active on the wall.

Contract relevance: `OUTPUT_CONTRACT.md:122` requires that "the final force row must correspond to
the final state written to `surface.csv`". On this case it does not, to 4.9 % in the pressure
component. An examiner who re-integrates `surface.csv` as a cross-check — the obvious verification
to attempt — will find a 4.9 % discrepancy and no explanation for it in the report. Either the two
call sites should use the same reconstruction, or the difference must be documented.

---

## 2. Overstatements and unsupported claims

### 2.1 Two tolerances passed by margins of 0.1 % and 0.07 % — cherry-picked

- `naca0012_m015_laminar_re5000`: span 5.568e-04 against tolerance 5.574e-04 → **span/tol = 0.9990**
- `cylinder_m010_laminar_re20`: span 2.0269e-02 against tolerance 2.0389e-02 → **span/tol = 0.9993**

Both cases cleared the bar by roughly one part in a thousand, and both are simultaneously failing
the drift test by 20x and 28x respectively while trending monotonically. The run_status notes
present these as "$C_D$ = ... stationary to 5.568e-04 (tolerance 5.574e-04)" — technically true,
but a tolerance met to four digits on a quantity that is monotonically decreasing is a coincidence
of stopping time, not evidence of stationarity. Had the run continued 50 more steps the span would
have failed.

### 2.2 The cylinder Cd is quoted to 4 digits but is still drifting in the 3rd

Submitted $C_D=2.018399$. The trend: 50-step block means over the final 500 steps run
2.036325 → 2.018530, monotone decreasing, and my geometric extrapolation of the tail gives an
asymptote near 2.014. The report's *own* sensitivity study
(`sec_sensitivity.tex` `tab:floorsweep`) reports $C_D=2.018119$ for the same case at 6.73 residual
orders — a longer run, landing *below* the submitted value, which corroborates that the submitted
2.018399 is a snapshot on a descending curve rather than a converged number. The literature range
2.0–2.1 is wide enough that this does not change the conclusion, but four-digit precision is not
earned. Similarly the M0.15 laminar $C_D=0.055181$ extrapolates to ≈0.05486 (−0.58 %) and M0.8
laminar $C_D=0.081377$ extrapolates to ≈0.08170 (+0.40 %), with its viscous component alone
extrapolating +25 %.

### 2.3 The Blasius comparison is arithmetically right but the compared quantity is still moving

The arithmetic checks out exactly: $2\times1.328/\sqrt{5000} = 0.037561512$, report says
`0.037562` — correct. It *is* compared against the viscous component alone ($C_{D,v}=0.036667$),
and `sec_results.tex:344-347` states this explicitly and emphatically, which is exactly right.
Agreement is 2.38 % of the Blasius value; the report says "2.4 %" — correct.

The problem is not the comparison, it is the operand. $C_{D,v}$ for this case was falling
monotonically when the run stopped: block means over the final 1500 steps go 0.04502 → 0.04108 →
0.03888 → 0.03772 → 0.03707 → 0.03676, and the tail extrapolates to ≈0.03630 (−0.99 %). So the
headline "agree to 2.4 %" is a function of *when the run was stopped*: continue it and agreement
degrades to ≈3.4 %. The report calls this "the most stringent validation available anywhere in this
benchmark" (`sec_results.tex:313`) and "an absolute comparison against analytic theory"; that
framing is too strong for a number that moves by 1 % if you iterate further. The caveats about
thickness and pressure gradient are appropriately given, but the convergence caveat is absent.

### 2.4 The limit-cycle table is from a different run than the one submitted

`tab:limitcycle` (`sec_verification.tex`) presents window-independence statistics "over the last
40000-step history" — window 500/2000/5000/10000 all giving span ≈1.31e-3 and mean constant to
4.4e-5. That is a persuasive demonstration and I believe it. But the **submitted** run is 4906
steps, and its window statistics are *not* window-independent:

| window | mean | span |
|---|---|---|
| 500 | 0.08798337 | 1.1707e-03 |
| 1000 | 0.08798757 | 1.1707e-03 |
| 2000 | 0.08827612 | 4.6198e-03 |
| 4000 | 0.08915265 | 8.2809e-03 |

The span grows 7x from window 500 to 4000 in the submitted data. The report's evidence for "this is
a limit cycle, not a transient" therefore comes from a 40000-step run that is not in `results/`,
applied to justify a 4906-step run that is. The conclusion is probably correct, but as submitted the
claim is not verifiable from the submitted artifacts — and the caption's phrase "over the last
40000-step history" does not flag that this is a different, longer, non-submitted run.

### 2.5 The dissipation-floor conclusion is sound but rests on non-submitted runs

The conclusion — floor coefficient 0.05 → 0.01 → 0.00 changes $C_D$ by 4.2e-04 (0.021 %), no
checkerboard by three measures — follows from its data, and the scope limit
(`sec_sensitivity.tex:94-101`: not tested at M0.8/M2.0 where carbuncles actually appear) is an
honest and correctly-placed caveat. Two observations: (a) none of the three sweep runs is in
`results/`, so the table cannot be checked against submitted artifacts; (b) the 0.021 % spread is
quoted as "below the case's own convergence tolerance", which is true but weak given §2.2 shows
that tolerance was met by 0.07 % on a drifting signal. The conclusion "no result in this report
depends on it" is supportable.

### 2.6 "Force coefficients agree to within 5e-4 relative across np=1,2,4,8" — true, but at step 1500 of a *not_converged* run

I verified the numbers: worst deviation 4.69e-04 (cylinder np=8), airfoil 1.05e-04. All 8 scaling
runs terminate at exactly step 1500 with `convergence_status: not_converged` and ~3.0 residual
orders, and their $C_D$ (2.126 cylinder, 0.1188 airfoil) differs substantially from the production
converged values (2.018, 0.0552). The report's caption does say "an identical fixed budget of 1500
pseudo-time steps, so the comparison is like-for-like" — which is honest and is the methodologically
correct way to isolate rank effects. But the conclusion in `sec_limitations.tex:130` /
`sec_parallel.tex:30-32` is phrased as an unqualified statement about "force coefficients", and a
hostile reader will note that rank-independence was never demonstrated *at the converged state* of
any case. The wall-time contention caveat (`sec_parallel.tex:20-23`) is exemplary and correctly
disclaims the np=8 airfoil timing anomaly (70.38 s > np=4's 27.64 s).

### 2.7 "an unconverged run cannot be submitted as a final result"

`sec_verification.tex` claims that if the step limit is hit with forces still moving, the run is
recorded `not_converged` with `completed = false`. The code does implement this
(`steady_driver.cpp:440,454,492`). But it only fires on *step exhaustion*. Five submitted runs
terminated early through the span-or-drift test while their forces were still moving (§1.1), so the
stated guarantee does not deliver what the sentence implies to a reader.

---

## 3. What is correct and well-supported

These held up under deliberate attack and should be credited:

- **Force integration is verifiably correct.** I re-integrated $C_D$ and $C_L$ from `surface.csv`
  independently, reconstructing boundary face lengths from edge midpoints and normals via a
  polygon-closure least-squares solve (residual 2.5e-13 to 2.8e-12; for the cylinder my recovered
  edge lengths match the exact circular chord to 3.0e-12, perimeter 3.141076 vs $\pi$ for a
  100-gon, enclosed area 0.784881). Agreement with `forces.csv`:
  - cylinder Re20: pressure drag mine 1.220905 vs solver 1.220925 (**1.7e-05** rel), viscous
    0.797469789 vs 0.797469789 (**2.7e-14**)
  - M0.15 laminar: pressure 0.018514048 vs 0.018513750 (**1.6e-05**), viscous **7.3e-14**
  - M2.0 inviscid: pressure **4.1e-05**; M0.8 inviscid **3.9e-04**
  The viscous component matches to machine precision, confirming `cf` is a true tangential
  projection consistent with the force integral. Pressure agreement is limited only by my
  midpoint-rule quadrature vs the solver's face-centroid value. The residual ~5e-2 discrepancy on
  M2.0 laminar is a real limiter-reconstruction mismatch, treated separately in §1.8.
- **Sign conventions and the pressure/viscous split are right.** `nx,ny` points *into* the body on
  100 % of faces in all 7 cases (verified via $\mathbf{n}\cdot\mathbf{r}<0$ about each body centre);
  `forces.cpp:28-32` documents this and the Cauchy traction $t = p\mathbf{n} - \tau\cdot\mathbf{n}$ is
  applied correctly, giving positive pressure drag on the bluff body. $C_D = C_{D,p}+C_{D,v}$ closes
  to <1e-12 in all cases. Inviscid cases have `viscous_drag` *exactly* 0.0.
- **Cylinder Re 20 is physically excellent.** $C_D=2.018$ against a literature range of 2.0–2.1,
  with a pressure/friction split of 1.221/0.797 (ratio 1.53) that matches the accepted ≈1.22/0.78.
  Getting the *split* right is much harder than getting the total right.
- **Drag varies with Mach in the correct direction.** Inviscid: 0.00101 (M0.15, exact answer 0) →
  0.00873 (M0.8, wave drag from the shock-terminated supersonic pocket) → 0.0880 (M2.0, bow-shock
  wave drag). The M0.15 inviscid value being ~1e-3 rather than 0 is the correct d'Alembert
  discretization-error interpretation the report gives it.
- **No-slip and slip wall reporting is exactly per contract.** On all four no-slip cases
  `u=v=mach=0.0` identically (max 0.000e+00). On all three slip-wall cases $|\mathbf{V}\cdot\mathbf{n}|
  \le 2.4\times10^{-13}$ with tangential speed retained (up to 1.18). Cell-centre values are in a
  separate documented `surface_cell_center.csv`, avoiding disqualification trigger 11.
- **All fields finite, positive, and plausible.** Every `field_final.vtu`: single `<Piece>`, cell
  counts matching `metadata.json` (20816 airfoil / 10185 cylinder), zero non-finite values, strictly
  positive density and pressure, all 8 required cell arrays present, `velocity` correctly 3-component.
  The `mach` column reproduces $|\mathbf{V}|/a$ to 1.6e-12. Peak Mach 1.258 (M0.8, correct supersonic
  pocket) and 6.04 (M2.0 — high, but it is in the trailing-edge expansion fan off a blunt base,
  where $p/p_\infty=0.002$; the wall entropy is nowhere negative, min +6.5e-03).
- **The M0.15 and M2.0 inviscid limit-cycle classifications are genuinely correct.** Both fail the
  span test and pass on drift with non-monotone block means — real bounded oscillation. These two are
  the cases where the "limit cycle" label is earned, and the solver correctly distinguishes them in
  its notes text rather than calling them fixed points.
- **Blasius arithmetic, and the discipline of comparing against the viscous component only.** See §2.3.
- **First vs second order (60.5 % drag inflation) is a legitimate demonstration** that the
  reconstruction does real work, and the report correctly refuses to call it an order-of-accuracy
  measurement (`sec_limitations.tex:57-69`).
- **Numbers in prose do trace to the data.** I spot-checked ~20 harvested macros against
  `run_status.json`/`forces.csv`; `residual_reduction_orders` recomputed as
  $\log_{10}(R_1/R_{final})$ matches all 7 recorded values to 4+ decimals. The 53 `\pending`
  placeholders in `numbers_auto.tex` are all Re200/shedding quantities (22 actually referenced in
  the body) — appropriate for a run still in flight, not a defect.

---

## 4. Disclosure gaps

Ranked by what a hostile examiner would seize on:

1. **The residual increase on `naca0012_m080_inviscid` (10.6x above its own minimum) is nowhere
   disclosed**, and is mis-described as a plateau with a mis-attributed cause (limiter chatter
   rather than CFL-ramp degradation). Same issue unmentioned for M2.0 inviscid (3.5x).
2. **The M2.0 inviscid symmetry failure is not disclosed** — and the report affirmatively claims
   mirror-image shocks for the transonic case and presents $C_L$/$C_{m,z}$ as symmetry metrics
   without noting that this case fails them.
3. **The $C_{p,\max}$ overshoot above the normal-shock total pressure at M2.0 is not disclosed.**
4. **The drift-cancellation loophole is not disclosed.** The report devotes a long, self-critical
   section to how a span-only test can be fooled by a limit cycle, and adds the drift test as the
   fix — but never observes that the drift test can itself be fooled by opposing component motion.
   Given how thoroughly the report reasons about this exact class of error, its absence is
   conspicuous. The fix is cheap and worth stating: require *both* branches, or apply the drift test
   to $C_{D,p}$ and $C_{D,v}$ separately.
5. **The monotone trends in the certifying windows are not disclosed** for the 5 affected cases,
   even though `sanity_checks.json` already computes `cd_relative_drift_last_10pct` and records
   6.9 % for M2.0 laminar. The submission has the evidence and does not surface it.
6. **`tab_forces.tex` labels three still-drifting cases "fixed point"** (M0.15 laminar, M0.8
   laminar, cylinder Re20) and one "limit cycle" (M2.0 laminar) that is monotone. The labels come
   from parsing the solver's note text (`harvest_numbers.py:363-377`), so they inherit the loophole
   rather than providing an independent check — despite the docstring at `:187-196` claiming the
   classifier exists so a table "would not contradict the status recorded in run_status.json".
7. Minor: the 600-vs-500 window mismatch (§1.6); the duplicated final CSV rows (§1.7); the fact
   that the sensitivity and limit-cycle tables draw on runs absent from `results/`.

On the positive side, the report *does* disclose, unprompted and creditably: the failed
free-stream-preservation check (2.1e-11 vs 1e-12 requested, `uniform_flow_check_passes: false` in
`sanity_checks.json`), LU-SGS degradation at high CFL and the explicit refusal to inflate the
diagonal to fake the inner ratio, boundary-layer under-resolution at Re 5000, the absence of an
observed-order measurement, unused located dependencies (ParMETIS/Eigen/fmt), CPU-quota contention
invalidating all timings in `tab:scaling`, and a latent HLLC sign error in an unused path. That is
a substantially more candid limitations section than most submissions produce, and it is the main
reason the convergence problems read as a systematic blind spot rather than concealment.

---

## 5. Rubric disqualification-trigger check

| # | Trigger | Assessment |
|---|---|---|
| 1 | External solver called internally | **Clear.** No evidence; own CGNS reader, own flux/LU-SGS. |
| 2 | Force/residual files without solving | **Clear.** Histories show physical transients; I re-derived forces from surface data and they match. |
| 3 | Hard-coded mesh filenames | **Clear.** Mesh path comes from case JSON; two different meshes/topologies handled generically. |
| 4 | Rank count changes steady results by O(1) | **Clear.** Max 4.7e-04 relative over np=1→8. |
| 5 | Explicit time stepping only | **Clear.** Implicit LU-SGS with local time stepping, inner-iteration logs present. |
| 6 | Report claims algorithms absent from source | **Clear** on the items I checked (Venkatakrishnan limiter, Harten–Hyman fix, METIS k-way, BDF2 dual time all present). One numeric mismatch (600 vs 500 window). |
| 7 | **metadata marks incomplete runs as successful** | **AT RISK.** Two cases record `converged` below their residual target; five were stopped mid-drift. Mitigated by explicit prose disclosure of the order shortfall, but not of the drift. |
| 8 | Figures don't correspond to outputs | Not assessed (figure/data correspondence was outside my assignment); manifest exists. |
| 9 | METIS reported but geometric split used | **Clear.** `partitioner: metis_kway`, edge cuts scale sensibly (0/97/282/471), `full_state_replication: false`. |
| 10 | Claimed limiter disabled in production | **Clear.** `limiter: venkatakrishnan` in every metadata; first-order comparison shows a 60 % difference, so it is active. |
| 11 | Wall rows are really cell-centre values | **Clear.** Boundary values exactly zero; cell-centre values in a separate documented file. |
| 12 | Misnamed figures | Not assessed. |
| 13 | Core copied from an existing codebase | Not assessed (separate audit). |

Trigger 7 is the live exposure. Nothing else I examined comes close.

---

## 6. Recommended remediation, in priority order

1. **Re-run the 5 drifting cases to genuine stationarity** — require span *and* drift, and apply
   both to $C_{D,p}$ and $C_{D,v}$ separately so cancellation cannot pass. M2.0 laminar is the
   urgent one; it is presently ~5 % from its own asymptote and is labelled a converged limit cycle.
2. **Fix or disclose the M0.8 inviscid residual rise.** Capping the CFL near 50 for this case
   recovers 3.92 orders — nearly the 4.00 target — which would remove the "plateau" narrative
   entirely. Failing that, the report must state that the residual rose 10.6x from its minimum.
3. **Investigate the M2.0 inviscid asymmetry.** An O(1) $C_p$ difference between upper and lower
   surfaces on a symmetric body at zero incidence, together with a 22 % $C_{p,\max}$ overshoot, is
   the single most attackable physics result in the submission. If it cannot be fixed, disclose it
   prominently and stop using that case's $C_L$ as a symmetry-preservation metric.
4. **Add the drift-cancellation loophole to the limitations section** and correct the 600→500 window.
5. **Add a convergence caveat to the Blasius comparison**, noting $C_{D,v}$ was still falling and
   the asymptotic agreement is nearer 3.4 % than 2.4 %.
6. **Reconcile the two wall-pressure reconstructions** (§1.8) so `surface.csv` re-integrates to
   `forces.csv`, or document the difference. Right now the most natural independent cross-check an
   examiner can run fails by 4.9 % on one case.
7. Quote the cylinder and laminar drags to the precision actually earned (3 digits, not 4–6).
