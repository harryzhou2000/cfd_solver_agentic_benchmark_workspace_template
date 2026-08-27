# Hand-written number sweep — findings

Scope: report/*.tex prose literals that are a **measurement of a run**. Governing test:
*could re-running the case change this value?* Properties of the method, the mesh, or the
freestream are legitimately hand-written and are not flagged.

All verdicts re-derived independently from results/ and scratch/sensitivity/runs/.
Read-only throughout; scripts live in scratch/hardcoded_sweep/. Frozen snapshots at 15:50
(snapshot/) and 16:01 (snapshot2/); every finding re-checked against the **live** files at 16:11.

Scripts: gt.py, pointwise.py, chk_verif.py, chk_tail.py, chk_m200_ratio.py, chk_sens.py,
chk_sens2.py, chk_osc_prod.py, chk_localise.py, chk_998.py, chk_line226.py, extract_literals.py.

---

## C. Ranked list of what is actually WRONG (worst first)

### 1. sec_results.tex:128 — decrement ratios for naca0012_m200_laminar_re5000. **CONCLUSION INVERTED.**

Printed: ratios 1.11, 1.27, 0.86, 0.85, 1.04, 1.03, 1.00, 0.99 with **mean 1.019**, concluding
"the increments are not decaying at all and the tail sum diverges. There is no asymptote to
extrapolate to, and none is offered... that case is reported as **not converged**."

Correct, from the current forces.csv (500-step trailing window, 10 sub-blocks, duplicate final
row dropped): ratios 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 0.38, **mean 0.9195**. The tail
sum **converges**: remaining movement 1.45e-05 (0.011 %), amplification r/(1-r) = 11.4.

This is the most serious finding in the sweep. It is not a drifted digit — it is the
discriminator the case's convergence verdict rests on, and it has crossed the r<1 threshold.
The report's own criterion, applied to current data, now **classifies this case as converged**,
and run_status.json agrees: convergence_status "converged", 4.78 orders, and a solver note
reading "decrements decaying at a ratio of **0.919** per sub-block, so this is an asymptotic
approach... geometric tail gives an estimated 1.45e-05 of C_D movement remaining (0.011 %)".
The prose argues the opposite of what the solver now records.

Robustness: I swept window in {500, 1000, 2000} x sub-blocks in {10, 9, 5} x {dedup, raw}.
**All 18 combinations give a mean ratio below 1** (0.9162 – 0.9790). The inversion does not
depend on the windowing convention. The decrements are also now *positive* (drag rising
~+3.4e-06 per sub-block, monotone up), whereas the printed ratios mix signs — the printed
sequence cannot be reproduced from this run under any convention I tried.

Knock-on sites carrying the same stale 1.019 and the same inverted verdict:

- sec_results.tex:129-131 — "not decaying at all", "tail sum diverges", "reported as not converged".
- sec_results.tex:133 — "the contrast between mean ratios of 0.733 and **1.019**"; the claimed
  "wide margin" is now 0.7355 vs 0.9195, much narrower, though still on opposite sides of the
  report's own quotability bar.
- sec_results.tex:138 — "right that naca0012_m200_laminar_re5000 was not converged".
- sec_verification.tex:727 — "the M=2.0 laminar airfoil's sit at **1.019**, not decaying at all".
- sec_verification.tex:890 — "0.994 is not converged, **0.919** and 0.733 are". This line already
  treats 0.919 as converged, so the report simultaneously calls 0.919 converged there and 1.019
  not-converged for what is now the same measurement on the submitted run. Internally contradictory.

**Recommended: harvest this quantity.** See section D.

### 2. sec_verification.tex:348 — re-integration table, NACA M=0.15 inviscid row. STALE, argument survives.

Printed: from surface.csv 0.000913, from forces.csv **0.000960**, absolute 4.69e-05, relative 4.88e-02.

Correct: the current forces.csv pressure drag for naca0012_m015_inviscid is **0.000988** (last
row, step 1861). The printed 0.000960 is from a superseded run.

The surrounding argument — that the *absolute* column is the meaningful one, and that this case
has the largest relative and smallest absolute discrepancy — **survives**, since it still has by
far the smallest absolute drag. But the absolute and relative figures in that row are both stale,
and the row's headline claim (every case agrees to better than 1.2e-3 absolute) needs
recomputation to confirm. Cylinder row (1.220902 vs current 1.220905) and M=0.80 row (0.008771
vs current 0.008771) are fine.

### 3. sec_verification.tex:397 and :401 — residual orders for naca0012_m015_inviscid. STALE; argument survives but loses its example.

Printed: "the termination note states 4.94 orders while the machine-readable
residual_reduction_orders field records **4.854**", with arithmetic 1.0390e-3 -> 4.9401 orders
and 1.2665e-3 -> **4.8541**.

Correct: the field now records **5.2606** orders (final residual 4.966995e-04 against initial
90.505671; I reproduced 5.2606 from those two). The current run_status.json note is also
different in kind — it now describes the best-state fallback ("ended at residual 1.6963e-03, a
factor 3.42 above the best value 4.9670e-04") rather than quoting 4.94 orders.

The *point* — that a residual is a property of a state, so two evaluations of one run
legitimately differ — remains valid and worth keeping. But every number in the illustration is
from a superseded run, and the specific 4.94-vs-4.854 pairing no longer exists in the files. The
step-1 value 90.506 still matches.

### 4. sec_results.tex:100-101 — cylinder tail-extrapolation figures. STALE (small), argument survives.

Printed: "the geometric tail gives **9.89e-4** of C_D movement remaining, or **0.049 %**, for an
asymptote near **2.0174**", explicitly attributed to the solver ("the figure quoted here is the
solver's rather than an external reconstruction of it").

Correct, from the current run_status.json note for cylinder_m010_laminar_re20: **1.03e-03**
remaining, **0.051 %**, asymptote **2.017348**, ratio **0.736**. My independent re-derivation
from forces.csv agrees: 1.0310e-03, 0.0511 %, asymptote 2.017348, mean ratio 0.7355.

The "three digits earned, fourth not" conclusion **survives**. But since the sentence claims
these are the solver's own numbers, they should match the solver's note exactly, and they do not.
Same stale pair repeated at sec_verification.tex:988 ("remaining movement is 0.049 %").

Related drifted digits in the same paragraph, conclusion intact:

- mean ratio **0.733** at sec_results.tex:96 and :120 vs re-derived 0.7355 / solver-reported 0.736.
- decrement list at :94 prints -4.42, -3.63, -2.94, -2.22, -1.64, -1.18, -0.82, -0.55, -0.36;
  current data gives -4.42, -3.68, -2.93, -2.22, -1.64, -1.18, -0.82, -0.55, -0.37 (2nd, 3rd, 9th differ).
- ratio list at :97 prints 0.82, 0.81, 0.76, 0.74, 0.72, 0.70, 0.67, 0.65; re-derived 0.83, 0.80,
  0.76, 0.74, 0.72, 0.70, 0.67, 0.67.
- amplification "2.7" at :120 vs re-derived 2.78.

---

## Checked and cleared (recorded so the judgement is auditable)

- **sec_results.tex:226 bare "9 of 404" and "47.1 %", and :220 bare "1.6573".** All verify
  exactly (9 over pitot, 0 over isentropic, isentropic overstates pitot by 47.0647 %, 404 wall
  faces). The 1.6573 is a freestream property, correct by design. But the "9" and "404" here are
  measurements of a run and are the only survivors of that family still hand-typed — the identical
  quantities 20 lines later at :481-482 are now \cnsFacesOverPitotNacaSupInv /
  \cnsNumWallFacesNacaSupInv. Not wrong now; will silently go stale on a re-run. Same for the bare
  395 at :511 (verified 404-9=395).
- **sec_results.tex:512 "more than 99.8 % of the surface".** Verified as the chordwise measure:
  cluster spans 0.1756 % of chord, complement 99.82 %. Correct by design. Worth knowing the
  face-count fraction is 97.77 % and arclength 99.55 %, so pairing "the remaining 395 faces" with
  "99.8 % of the surface" mixes a count with an extent — wording, not a wrong number.
- **Pointwise wall-diagnostic family** — confirmed macro-driven as of 16:01 (24 relevant defs;
  339 total, up from 314). Re-verified the values anyway: max wall Cp 2.143389 / 1.584736, faces
  over pitot 9 / 0, worst pair asymmetry 1.2440 / 0.1554, 202 matched pairs, 404 wall faces,
  +29.33 % / -4.38 % relative to the bound, 8 of 9 on the upper surface, peak excess +0.4861 at
  x=0.000439, nose excess +0.2669, asymmetry ratio 8.01 ("factor of 8"). All match.
- **Slip-wall tangential speeds** sec_verification.tex:270 — 0.9985, 1.0889, 1.0417 verify against
  max wall speed on the three slip cases (0.998529, 1.088867, 1.041702).
- **np=2 vs np=4 partition table** sec_verification.tex:907-908 — 37022 steps and 0.137141 match
  exactly; difference 5.02e-4 and relative 3.65e-3 re-derive as 5.0153e-04 and 3.6570e-03; "35
  times" and "73 times" re-derive as 34.6 and 72.7. Clean.
- **Scaling study** — edge cuts 0/97/282/471 and 0/120/241/439 and all eight wall times match
  results/scaling/. Clean.
- **sec_mpi.tex:92-93** — owned-cell range 1265-1282, mean 1273, neighbour counts 3-7 all match
  the partition diagnostics. Clean.
- Cyl. Re=200 / shedding / Strouhal literals: **EXCLUDED (transient, mid-run)** per brief.

---

## Sensitivity-study tables — full verification (the addition to the brief)

Chain checked at **both** hops: variant run output -> RESULTS.md -> sec_sensitivity.tex. I read
metadata.json, run_status.json, forces.csv and surface.csv from each
scratch/sensitivity/runs/<variant>/ directly rather than trusting RESULTS.md.

**Every number in all three tables is correct at both hops. No transcription error at either link.**

- tab:floorsweep (9 values): floor 0.05 -> 2.018119 / 1.220943 / 0.797176; floor 0.01 -> 2.017701 /
  1.220480 / 0.797221; floor 0.00 -> 2.017754 / 1.220447 / 0.797307. All match runs/floor005,
  runs/floor001, runs/floor00 forces.csv last rows exactly. "All three converge to 6.73 residual
  orders" -> actual 6.7290 / 6.7291 / 6.7289.
- Derived: spread **4.2e-4** -> 4.1800e-04; **0.021 %** -> 0.0207 %.
- Floor-disabled checkerboard measures: ratio **0.128** -> 0.12840; **four sign alternations per
  100 wall faces** -> 4 of 100; mirror asymmetry **6e-4** -> 6.2129e-04.
- tab:stagnation (8 values): 1 of 119 vs 1 of 119 — the JSON reports 1 alternation over n=120
  leading-edge faces, i.e. 119 differences, so "of 119" is the correct reading of a 120-point
  chain; oscillation ratios 0.263 / 0.274 -> 0.262637 / 0.274319; stagnation Cp 1.002635 /
  1.002502 exact; mirror asymmetry 3.42e-3 / 3.28e-3 -> 3.4161e-03 / 3.2801e-03.
- Convergence-favours-disabled trio: **171 versus 168** CFL rejections -> run notes say 171 / 168;
  **50.5 % versus 50.7 %** -> 0.50475 / 0.50700; **max inner iterations 33 versus 19** -> 33.0 / 19.0.
- Order study: **3.239634** vs **2.018119** -> runs/order1 / runs/order2 exactly; **60.5 %** ->
  60.53 %; pressure component "rises by roughly 90 %" -> 90.34 %.
- Venkat study: **2.032244 / 2.018119 / 2.015887** -> runs/venk1 / order2 / venk10 exactly;
  **0.81 %** spread -> 0.8049 % of the K=1 value (0.8105 % of K=5); monotone confirmed.
- Trailing-edge localisation paragraph: whole-surface ratio **1.72** -> 1.7270 / 1.7187 for the two
  variants; production run **1.728** -> I recomputed **1.7262** from the current production
  surface.csv using the study's own signed_arclength_order and open_chain_metrics
  (max|d2|/max|d1|) — a 0.1 % drift, and the only sensitivity-adjacent number that has moved,
  because it is the one figure in that paragraph taken from a *production* run rather than a
  variant. Jump "about 1.46" -> 1.5201 on the current production run (1.4633 on naca_floor005,
  which is what 1.46 matches). Falls to **0.263** excluding the final 1 % of chord -> 0.2613
  production / 0.262637 variant. Blunt TE at **x/c=1.005** -> max wall x = 1.005313.
- Reproduction claim sec_sensitivity.tex:144-149: runs/floor005 gives 2.018119 at 6.7290 orders /
  step 2783. Note the agreement is with the *sweep's* production probe (runs/order2, same binary,
  same 2783 steps), not with the submitted production run in results/cylinder_m010_laminar_re20,
  which converges at step 2450 to C_D=**2.018374**. True as written, but a reader comparing against
  tab:forces (2.018) will not find seven-digit agreement. Worth a clarifying clause.

### The killed run is NOT a source for any reported number — cleared

runs/_stale_naca_floor005_killed_attempt/ has no metadata.json and no run_status.json; its
forces.csv ends at C_D=**0.000878**. That value appears **nowhere** in sec_sensitivity.tex or in
RESULTS.md (grepped both). The stagnation table's floor-0.05 column traces to runs/naca_floor005
(C_D 0.0009356, stagnation Cp 1.002635), the completed 8000-step run. **Clean.**

### Solver-revision mismatch — real, undisclosed in the .tex, worth a sentence

| variant group | git revision | vs production cc42ab4facfd |
|---|---|---|
| floor005, floor001, floor00, floor05, naca_floor005, naca_floor00, inv_floor005, inv_floor00 | 97f37254d00c (06:37) | older |
| order1, order2, venk1, venk10 | 26ef1cb80b86 (21:38, prior day) | older |
| production suite | cc42ab4facfd (13:45) | — |

**All twelve variant runs predate the submitted binary**, and the order/venkat group predates it
by more than the floor group. RESULTS.md:70-84 discloses this honestly and argues the sweep
remains valid on positive evidence: the floor-0.05 variant reproduces the post-fix production C_D
to 7 digits; the HLLC and transient fixes cannot touch a steady Roe case; and differences between
binaries built from one source copy are unaffected. That reasoning is sound.

**But sec_sensitivity.tex does not carry the disclosure.** It says only "built from a separate copy
of the source tree" (line 5) and "an independent CMake configure" (line 144), which reads as
*concurrent* copies of the *same* revision. The section never states that the variants were built
from a source copy taken **before** three production fixes landed. The order1/order2/venk1/venk10
group is the weaker case, because RESULTS.md's positive evidence is specifically about the floor
binaries; those four ran on 26ef1cb80b86 via run_prod_probes.sh using the build/cns2d binary as it
stood at 06:5x, which is not the submitted binary either. Recommend one sentence in
sec_sensitivity.tex stating the revisions and pointing at the RESULTS.md validity argument.

Also sec_sensitivity.tex:6-7: "the baseline variant binary is byte-identical to the unmodified
build of the same tree". RESULTS.md:51 supports this with matching md5
9c569392cd0d9107708be31be4322c9b, but says byte-identical to the **pre-refactor build of the same
tree**, which is 97f37254d00c, not the production tree. The .tex wording "the same tree" is
ambiguous in a way that flatters the claim.

---

## D. Quantities that should become harvested macros

Ranked by risk. Each is a measurement of a run that is currently hand-typed.

1. **Sub-block decrement ratio (mean r) and the derived converged/not-converged classification**
   — naca0012_m200_laminar_re5000 and cylinder_m010_laminar_re20. This is finding #1 and the
   highest-value macro in the report: it is the discriminator the convergence argument turns on,
   the solver already computes it and writes it into the run_status.json note, and it has already
   inverted once. Harvest the ratio, the remaining movement (absolute and %), and the asymptote.
2. **Tail-extrapolation triple — remaining movement, remaining %, asymptote** —
   cylinder_m010_laminar_re20 (finding #4). Already in the solver's termination note verbatim;
   the report retypes it and has drifted.
3. **residual_reduction_orders and the termination-note orders as two distinct values** —
   naca0012_m015_inviscid (finding #3). The argument requires both numbers, so both need macros,
   plus the two residual values behind them.
4. **Surface-vs-forces re-integration quadruple — surface.csv drag, forces.csv drag, absolute and
   relative difference** — cylinder_m010_laminar_re20, naca0012_m015_inviscid,
   naca0012_m080_inviscid (finding #2). The forces.csv column is pure forces.csv data and should
   never be hand-typed.
5. **Faces-over-pitot count and wall-face count at sec_results.tex:226, and the clean-face count
   395 at :511** — the macros already exist (\cnsFacesOverPitotNacaSupInv,
   \cnsNumWallFacesNacaSupInv); these sites simply were not converted. Cheapest fix in the report.
6. **Isentropic-overstates-pitot percentage (47.1 %)** for M=2.0 — derivable from the two existing
   ceiling macros.
7. **Whole-surface oscillation ratio on the production naca0012_m015_inviscid run** (the 1.728 in
   sec_sensitivity.tex) — the one sensitivity-section number that can go stale, because it is
   measured on a production run that refresh.sh regenerates.

Not needed: the variant-run values in the three sensitivity tables. They live outside results/,
refresh.sh never touches them, and they cannot go stale from a production re-run. Their exposure
is transcription, not staleness — and I found zero transcription errors.

---

## E. Coverage

| family | literals inspected | in scope | wrong |
|---|---|---|---|
| sec_results.tex convergence/digits (lines 60-170) | 71 | 24 | 9 |
| sec_results.tex M=2.0 pointwise + localisation (440-530) | 58 | 21 | 0 |
| sec_results.tex other prose | 168 | 39 | 0 |
| sec_verification.tex re-integration + slip wall (255-410) | 74 | 26 | 5 |
| sec_verification.tex tail/conditioning/partition (700-990) | 121 | 47 | 2 |
| sec_verification.tex other prose | 199 | 44 | 0 |
| sec_sensitivity.tex (all three tables + prose) | 37 | 34 | 0 |
| sec_parallel.tex, sec_mpi.tex, scaling | 66 | 29 | 0 |
| sec_bc, sec_spatial, sec_implicit, sec_mesh, sec_intro, sec_limitations, sec_equations | 305 | 18 | 0 |
| auto-generated tab_*.tex (regenerated; not targets) | 234 | — | — |
| **total** | **1099 numeric tokens over 784 prose lines** | **282** | **16 sites, 4 distinct defects** |

Of the 16 wrong sites, **1 inverts a physics/convergence conclusion** (finding #1, with 5 knock-on
sites), 3 are stale illustrations whose surrounding argument survives, and the rest are drifted digits.

The known defect the brief pointed me at — the "9 of 404" bound violation table — is **correct in
the current text** and is now macro-driven. The highest-yield remaining area turned out to be the
convergence-discriminator family, not the pointwise family.

