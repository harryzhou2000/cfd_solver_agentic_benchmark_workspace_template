# Numerical sensitivity study — cylinder Re 20 (cns2d)

**Status: PAUSED at the orchestrator's request** (hard cgroup CPU quota of 4 CPUs discovered:
`/sys/fs/cgroup/cpu.max` = `400000 100000`). All solver runs of mine are stopped and no
further ones will launch until told to resume. **Tasks 1, 2 and 3 are all complete** — every
required variant converged before the pause, so no additional runs are needed for the
headline numbers. Two purely optional extra probes were abandoned mid-flight; they are
listed under "Outstanding / abandoned" and are not required by any conclusion below.

Case: `cylinder_m010_laminar_re20` (Mach 0.1, Re 20, laminar, no-slip adiabatic cylinder,
farfield at r = 200), mesh `CylinderB1.cgns` (10 185 cells, 100 wall faces).
Reference literature value: Cd ≈ 2.0–2.1.
Production baseline (np = 8): **Cd = 2.024290** (pressure 1.223930 + viscous 0.800361).

All probe runs below used **np = 4** and ran **strictly one at a time**. Wall times are
therefore contention-affected and are reported for completeness only — they are *not* a
clean performance measurement, because production jobs shared the same 4-CPU quota for
part of the window. Do not quote the wall times as scaling data.

---

## How the variant binaries were built

The production tree (`/workspace/solver/src`, `/workspace/solver/build`) was **never
modified or rebuilt**. The source was copied out-of-place and built separately:

```bash
cp -r /workspace/solver/src   /workspace/solver/scratch/sensitivity/var_root/src
cp -r /workspace/solver/tests /workspace/solver/scratch/sensitivity/var_root/tests
cp /workspace/solver/CMakeLists.txt /workspace/solver/scratch/sensitivity/var_root/

cmake -S /workspace/solver/scratch/sensitivity/var_root \
      -B /workspace/solver/scratch/sensitivity/build_var \
      -DCMAKE_BUILD_TYPE=Release \
      -DCFD_EXTERNALS_ROOT=/opt/external/cfd_externals/install \
      -DCFD_EXTERNALS_HEADER_ROOT=/opt/external
cmake --build /workspace/solver/scratch/sensitivity/build_var -j 8
```

In the **copy only**, the hard-coded coefficient at `src/numerics/riemann_flux.cpp:244` was
turned into a macro so each variant is a single-file recompile:

```c++
#ifndef CNS2D_LINEAR_FLOOR_COEFF
#define CNS2D_LINEAR_FLOOR_COEFF 0.05
#endif
    const Real linear_floor = Real(CNS2D_LINEAR_FLOOR_COEFF) * max_speed;
```

**Refactor is provably a no-op at the baseline value:** the rebuilt floor = 0.05 binary is
byte-identical to the pre-refactor build of the same tree (md5 `9c569392cd0d9107708be31be4322c9b`
for both). The four binaries live in `scratch/sensitivity/bin/` (`cns2d_floor005`,
`cns2d_floor001`, `cns2d_floor00`, `cns2d_floor05`). Driver scripts: `build_variants.sh`,
`run_prod_probes.sh`, `run_floor_sweep.sh`, `run_extra_probes.sh`.

Run command pattern:

```bash
mpirun --allow-run-as-root -np 4 <binary> solve \
  --case /workspace/cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json \
  --output <outdir> --log-every 100 [--max-steps 15000]
```

Every floor run converged well inside the 15 000-step safety cap, so these are **fully
converged comparisons, not fixed-step ones**. (The orchestrator's fixed-step fallback was
not needed and was not used.)

### Binary provenance vs the later production fixes

These variant binaries were built from a source copy taken **before** three production
fixes landed: the steady-convergence-test fix (it previously declared convergence on
residual orders alone, measured against the impulsive-start step-1 residual, and now
additionally requires drag stationarity over a trailing 500-step window), an HLLC
star-state energy sign error, and a transient residual-norm mismatch.

**The sweep remains valid and was not redone, on positive evidence rather than assumption.**
The floor = 0.05 variant gives Cd = 2.018119 for this case, and the post-fix production
binary independently gives **Cd = 2.018119 at 6.73 orders / step 2783 — identical to all 7
digits**. The two other fixes cannot affect these runs by construction: the production
inviscid path is Roe, not HLLC, and this case is steady, not transient. Because all four
floor binaries were built from that one source copy and differ only in a single
compile-time constant, the *differences* between them — which is what a sensitivity study
measures — are unaffected regardless.

---

## TASK 1 — Roe linear-wave dissipation floor

All three required values converged to 6.73 residual orders — far past the 5.0-order target.

| floor coeff | Cd | pressure Cd | viscous Cd | Cl | orders | steps | wall [s] | converged |
|---|---|---|---|---|---|---|---|---|
| 0.05 (baseline, rebuilt) | 2.018119 | 1.220943 | 0.797176 | 5.76e-04 | 6.73 | 2783 | 124.0 | yes |
| 0.01 | 2.017701 | 1.220480 | 0.797221 | 5.91e-04 | 6.73 | 2785 | 136.2 | yes |
| 0.00 (floor fully removed) | 2.017754 | 1.220447 | 0.797307 | 5.85e-04 | 6.73 | 2785 | 140.1 | yes |
| 0.50 (10x baseline, extra control) | 2.020006 | 1.224251 | 0.795754 | 1.95e-04 | 6.74 | 2776 | 139.2 | yes |
| production np = 8 reference | 2.024290 | 1.223930 | 0.800361 | 7.19e-04 | 5.00 | 2148 | 219.7 | yes |

**Total spread across floor 0.05 → 0.01 → 0.00: ΔCd = 4.2e-04, i.e. 0.021 % of Cd.**
Removing the floor entirely changes Cd by **-3.65e-04 (-0.018 %)**, and the
pressure/viscous split shifts by less than 5e-04 in each component.

### Checkerboard / oscillation check on surface.csv

Faces are sorted by θ = atan2(y, x) into physically adjacent order around the closed
cylinder loop, then differenced. θ = 180° is the upstream stagnation point (flow in +x),
which is where the cp maximum correctly appears in every run.

| floor coeff | cp range | max abs Δcp (adjacent) | max abs 2nd diff | ratio d2/d1 | sign alternations | max mirror asym. |
|---|---|---|---|---|---|---|
| 0.05 | -0.994095 .. 1.264059 | 1.3957e-01 | 1.7932e-02 | 0.128 | 4 / 100 | 6.10e-04 |
| 0.01 | -0.993605 .. 1.263827 | 1.3954e-01 | 1.7918e-02 | 0.128 | 4 / 100 | 6.27e-04 |
| 0.00 | -0.993572 .. 1.263802 | 1.3954e-01 | 1.7917e-02 | 0.128 | 4 / 100 | 6.21e-04 |
| 0.50 | -0.995354 .. 1.266711 | 1.3971e-01 | 1.8011e-02 | 0.129 | 4 / 100 | 2.88e-04 |

**No checkerboard artifact appears at floor = 0, by any of the three independent measures.**
The second difference stays ~7.8x *smaller* than the first difference (ratio 0.128), which is
the signature of a smoothly resolved gradient; a saw-tooth mode would drive this ratio above
2. The sign of the face-to-face cp difference changes exactly **4 times** around 100 faces —
consistent with the physically expected pattern (one maximum at the front stagnation point,
one minimum near the shoulder, plus the two mirror-image crossings) and nowhere near the
~100 alternations a mesh-frequency oscillation would produce. Mirror asymmetry about y = 0
stays at 6e-04, so the symmetry of the symmetric case is not broken either. The metrics for
raw `pressure` are identical in ratio terms, so the conclusion is not an artifact of the cp
normalization.

### Is the floor code actually live?

This is the obvious objection to a null result, so it was tested directly. Exaggerating the
coefficient tenfold to 0.5 **does** move the answer (Cd 2.018119 → 2.020006, and
pressure drag 1.220943 → 1.224251, a 3.31e-03 shift, ~7.9x the entire 0.05→0.0 spread), and
Cl tightens from 5.8e-04 to 1.9e-04. The branch is therefore demonstrably active and
reachable: the insensitivity between 0.05 and 0.0 is a genuine smallness result, not dead
code. This is also physically consistent — at Mach 0.1 the sound speed is 10 while the flow
speed is 1, so a floor of 0.05·(|u·n| + a) ≈ 0.5 is *below* the acoustic eigenvalues
everywhere and only touches the entropy/shear waves in the small region where |u·n| < 0.5.

### Quotable interpretation

> The reported cylinder drag is not an artifact of the Roe linear-wave dissipation floor.
> Sweeping the floor coefficient from its baseline 0.05 through 0.01 to 0.0 changes Cd by
> 4.2e-04 (0.021 %), from 2.018119 to 2.017754, with all three runs converged to 6.73
> orders of residual reduction; the pressure/viscous split moves by less than 5e-04 in
> either component. Removing the floor produces no checkerboard mode: the ratio of the
> maximum second difference to the maximum first difference of surface cp between adjacent
> faces is 0.128 both with and without the floor, the face-to-face sign of Δcp alternates
> exactly 4 times over 100 wall faces in both cases, and the mirror asymmetry about the
> symmetry plane stays at 6e-04. That the floor branch is genuinely active was confirmed
> by a tenfold-exaggerated coefficient of 0.5, which shifts pressure drag by 3.31e-03,
> roughly eight times the entire 0.05-to-0.0 spread. At Mach 0.1 the floor sits below the
> acoustic eigenvalues everywhere and only damps entropy and shear waves in the limited
> region where the face-normal velocity falls below about 0.5, which is why its effect on
> an integrated force is so small.

**Honest caveat.** This result shows the floor is *harmless to the reported drag at Re 20*.
It does **not** independently reproduce the carbuncle instability that originally motivated
the floor, so it should not be quoted as proving the floor is unnecessary in general. The
Re 20 case carries strong physical viscosity (Re 20 is deep in the steady laminar regime),
which can damp the mesh-frequency mode on its own. The intended clean test was the inviscid
slip-wall cylinder, and that test was inconclusive for an unrelated reason — see below.

---

## TASK 2 — Spatial order-of-accuracy evidence

Both runs used the unmodified production binary `/workspace/solver/build/cns2d` at np = 4.

| variant | Cd | pressure Cd | viscous Cd | Cl | orders | steps | wall [s] | converged |
|---|---|---|---|---|---|---|---|---|
| `--spatial-order 1` | 3.239634 | 2.323919 | 0.915715 | 1.50e-02 | 5.38 | 2597 | 29.4 | yes |
| `--spatial-order 2` | 2.018119 | 1.220943 | 0.797176 | 5.76e-04 | 6.73 | 2783 | 57.7 | yes |

**ΔCd = 1.221515, i.e. first order overpredicts drag by 60.5 %.** The error is concentrated
in pressure drag (2.323919 vs 1.220943, +90 %), exactly as expected: first-order
reconstruction smears the surface pressure distribution. The first-order cp range widens to
-1.415 .. 2.132 against -0.994 .. 1.264 at second order — a spurious stagnation cp of 2.13
where the correct incompressible-limit value is ~1.0, and the cp minimum moves from θ = 81°
to 59°, misplacing the suction peak.

Two further independent signals of first-order inaccuracy: the mirror asymmetry about y = 0
degrades by a factor of 66 (4.02e-02 vs 6.10e-04) and the spurious lift is 26x larger
(1.50e-02 vs 5.76e-04), even though the geometry and boundary conditions are exactly
symmetric.

> Second-order reconstruction is doing real work. On the Re 20 cylinder, first-order
> reconstruction yields Cd = 3.239634 against 2.018119 at second order — a 60.5 %
> overprediction that sits almost entirely in the pressure component (2.323919 versus
> 1.220943). Only the second-order result falls inside the literature range of 2.0–2.1;
> the first-order run also produces an unphysical stagnation cp of 2.13 in place of the
> expected value near 1.0, misplaces the suction peak from 81° to 59°, and degrades the
> mirror symmetry of this symmetric case by a factor of 66. Both runs are converged
> (5.38 and 6.73 residual orders), so the difference is discretization error, not
> incomplete convergence.

---

## TASK 3 — Venkatakrishnan limiter parameter disclosure

Production binary, np = 4. Default is venkat_k = 5.0.

| venkat_k | Cd | pressure Cd | viscous Cd | Cl | orders | steps | converged |
|---|---|---|---|---|---|---|---|
| 1.0 | 2.032244 | 1.233259 | 0.798984 | 4.72e-04 | 6.78 | 2783 | yes |
| 5.0 (default) | 2.018119 | 1.220943 | 0.797176 | 5.76e-04 | 6.73 | 2783 | yes |
| 10.0 | 2.015887 | 1.218112 | 0.797775 | 6.52e-04 | 6.73 | 2783 | yes |

**Full spread over a tenfold change in k: ΔCd = 1.6357e-02, i.e. 0.81 %.** The trend is
monotone and physically sensible — larger k relaxes the limiter toward the unlimited
second-order scheme, slightly reducing drag (2.032244 → 2.018119 → 2.015887). Viscous drag
is essentially untouched (0.7972–0.7990); the entire variation sits in pressure drag. All
three values remain inside the literature range 2.0–2.1, and the surface stays smooth in
every case (oscillation ratio 0.128–0.143, 4 sign alternations out of 100).

> The drag is only weakly sensitive to the Venkatakrishnan smoothing constant. Varying k
> over a full decade from 1.0 to 10.0 moves Cd from 2.032244 to 2.015887, a spread of
> 1.6e-02 or 0.81 %, monotonically decreasing as k relaxes the limiter toward the
> unlimited second-order scheme. All three values lie within the 2.0–2.1 literature range,
> the variation is confined to the pressure component with viscous drag constant to three
> decimals, and the surface pressure distribution remains free of oscillation throughout.
> The reported result therefore does not depend on a tuned limiter constant; k = 5.0 is
> disclosed as the default rather than chosen to hit a target.

---

## Outstanding / abandoned (not required by any conclusion above)

**Abandoned mid-flight at the pause (optional extra evidence only):**

- `naca_floor005` / `naca_floor00` — inviscid NACA 0012 M 0.15 at floor 0.05 vs 0.0, intended
  as the clean stagnation-point carbuncle test that the Re 20 case cannot provide. The
  floor 0.05 run had reached 4.73 residual orders at step 7000 (Cd 9.43e-04) when it was
  terminated; the floor 0.00 counterpart never started. Partial output remains in
  `runs/naca_floor005/`. **This is the one genuinely useful missing datum** — it would let
  the report claim the floor is unnecessary in general, rather than merely harmless at Re 20.
  It needs roughly 8000 steps per run, two runs.

**Attempted and failed for an unrelated reason (documented so it is not retried blindly):**

- `inv_floor005` / `inv_floor00` — the inviscid slip-wall cylinder debug case
  `scratch/dbg_cyl_inviscid.json`. **Both** floor settings failed identically: 1.69 residual
  orders at the 6000-step cap, exit code 3, with Cd oscillating between roughly -8 and +13.
  The cause is the case setup, not the floor — that debug file pins CFL at 0.5 with
  `pseudo_cfl_ramp_steps: 0`, so it never reaches a steady state with *either* binary and
  cannot isolate the floor's effect. Do not use this case for the carbuncle test; use the
  inviscid NACA case instead.

**One run needed a retry, for an infrastructure reason worth recording:** the first
`--spatial-order 1` attempt was killed abruptly at step ~1527 (exit 1, no solver error
message, with both `forces.csv` and `residuals.csv` truncated mid-line) while ~63 production
MPI ranks were saturating the 4-CPU quota. Nothing was wrong numerically — it was at 3.74
residual orders and still falling. It was re-run cleanly to 5.38 orders and that is the
result tabulated above.

---

## Artifacts

- `RESULTS.md` — this file
- `analyze_surface.py` — checkerboard/smoothness/symmetry diagnostics (θ-sorted closed-loop
  first and second differences, sign-alternation count, mirror asymmetry)
- `make_tables.py` → `tables.md`, `tables.json` — machine-generated tables
- `all_metrics.json`, `floor_metrics.json`, `analysis_full.txt` — full metric dumps
- `floor_cp.png` — cp and face-to-face Δcp versus θ, all variants overlaid with per-face
  markers so any point-to-point oscillation would be directly visible
- `runs/<tag>/` — full solver output per variant; `logs/<tag>.log` — solver stdout;
  `logs/driver.log` — run timeline
- `bin/cns2d_floor{005,001,00,05}` — the variant binaries
- `var_root/` — the out-of-place source copy (left at the baseline 0.05 value)

---

# ADDENDUM — inviscid NACA0012 stagnation-point test of the dissipation floor

Added after the main sweep, at the orchestrator's request, to close the one honest gap in
the Task 1 conclusion: the Re 20 cylinder carries strong physical viscosity that could damp
a carbuncle mode on its own, so it cannot support a general claim about the floor.

**Why this case is the right test.** The floor exists to damp the entropy and shear waves,
whose eigenvalue is u·n. On a symmetric airfoil at zero incidence the stagnation streamline
meets the leading edge, so u·n passes through zero there — exactly the condition the floor
was introduced to protect. The case is inviscid (`viscous_flux: disabled`, slip wall), so
there is no physical viscosity to mask a mesh-frequency mode.

**Case:** `inputs/cases/naca0012_m015_inviscid.json` (NACA0012, AoA 0°, Mach 0.15, inviscid,
slip wall `bc-4`, farfield `bc-2`), mesh `NACA0012_H2.cgns`, 404 wall faces of which 120 lie
at x/c < 0.1.

## This is a FIXED-STEP comparison, not a converged one

Both variants were capped at **exactly `--max-steps 8000`** and are compared at that common
final step. Neither is a converged steady state: both report `not_converged` and exit code 3
at the cap, which is **expected** for this case and not a failure — its true drag is ~0 and
the corrected convergence test (which now also requires drag stationarity) keeps it running
to its full budget. `surface.csv` is still written for a not-converged run, so the surface
diagnostics are valid. Because both variants use an identical step budget, identical case
file, identical rank count and binaries differing in **one compile-time constant**, the
comparison is like-with-like.

**Provenance note on the binaries.** These variant binaries were built from a source copy
taken *before* the steady-convergence-test fix, the HLLC star-state energy sign fix, and the
transient residual-norm fix landed in production. That is immaterial here for three reasons:
this is a fixed-step comparison, so the convergence test never decides anything; the
production path is Roe, not HLLC; and the case is steady, not transient. It is also
confirmed empirically — the floor = 0.05 variant reproduces the post-fix production value
for the cylinder to all 7 digits (Cd = 2.018119 both before and after the fix).

**Resource discipline:** np = 1, strictly one run at a time, under the container's hard
4-CPU cgroup quota (`cpu.max = 400000 100000`). The two runs took 952 s and 1248 s of wall
time and were measured at ~47 % of a single CPU while production held the rest. Those wall
times are contention-dominated and must not be read as a cost comparison between variants.

```bash
mpirun --allow-run-as-root -np 1 scratch/sensitivity/bin/cns2d_floor005 solve \
  --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  --output scratch/sensitivity/runs/naca_floor005 --log-every 250 --max-steps 8000
# then identically with bin/cns2d_floor00
```

## Forces at the common final step (8000)

| floor coeff | Cd | Cl | Cd span, trailing 500 steps | orders at cap |
|---|---|---|---|---|
| 0.05 | 0.00093559 | 2.0956e-04 | 2.26e-05 | 4.94 |
| 0.00 | 0.00123847 | 3.8929e-04 | 8.33e-05 | 4.88 |

Both drag values are ~1e-03 against a true inviscid value of **zero** (no shock at Mach
0.15, so d'Alembert's paradox applies). The absolute difference is ΔCd = 3.03e-04. In
relative terms that is 32 %, but the honest framing is that both numbers are residual
discretization error of order 1e-03 in a quantity whose exact value is 0, at a common
non-converged step, and the difference is the same order as the drift still present in each
run. For reference the fully-converged production run of this case gives Cd = 9.0049e-04,
which sits within 3.5e-05 of the floor = 0.05 fixed-step value. **This comparison should be
quoted as "both variants give a near-zero drag of order 1e-03", not as a 32 % effect.**

## Stagnation-region oscillation — the decisive measurement

Faces are ordered by signed arclength from the leading edge (lower surface trailing edge →
leading edge → upper surface trailing edge) so consecutive entries are physically adjacent,
and differenced along that open chain. Leading-edge metrics are reported separately from
whole-surface metrics as requested.

| region | metric | floor 0.05 | floor 0.00 |
|---|---|---|---|
| **leading edge, x/c < 0.1** (120 faces) | stagnation-face cp | 1.002635 | 1.002502 |
| | max abs Δcp (adjacent) | 6.4960e-02 | 6.6321e-02 |
| | max abs 2nd difference | 1.7061e-02 | 1.8193e-02 |
| | **oscillation ratio d2/d1** | **0.263** | **0.274** |
| | **Δcp sign alternations** | **1 of 119** | **1 of 119** |
| | max mirror asymmetry | 3.4161e-03 | 3.2801e-03 |
| whole surface (404 faces) | oscillation ratio | 1.727 | 1.719 |
| | sign alternations | 23 of 403 | 23 of 403 |
| | max mirror asymmetry | 3.4161e-03 | 3.2801e-03 |
| surface excluding blunt TE, x/c < 0.99 (346 faces) | oscillation ratio | 0.263 | 0.274 |
| | sign alternations | 19 of 345 | 19 of 345 |

**No carbuncle mode appears at the stagnation point without the floor.** In the leading-edge
region the face-to-face sign of Δcp alternates exactly **once in 119 intervals in both
runs** — the single reversal being the physical cp maximum at the stagnation point itself.
A checkerboard mode would drive that count toward 119. The oscillation ratio at the leading
edge is 0.263 with the floor and 0.274 without: both far below 1, meaning the second
difference remains several times smaller than the first difference, the signature of a
smoothly resolved gradient rather than a mesh-frequency mode. The stagnation-face cp is
1.0026 with the floor and 1.0025 without, both correct to 3e-03 against the exact
incompressible-limit value of 1.0. Mirror asymmetry about the symmetry plane is actually
**marginally smaller** without the floor (3.28e-03 vs 3.42e-03), so removing the floor does
not break the symmetry of this symmetric case either.

**Methodological caveat, stated so the whole-surface row is not misread.** The whole-surface
oscillation ratio of ~1.72 in both runs is **not** evidence of oscillation. It is entirely a
geometric artifact of the blunt trailing edge: this NACA0012 section has finite thickness at
x/c = 1.005, and the cp jump across those few TE faces (max abs Δcp ≈ 1.46) dominates the
whole-surface statistic. It appears identically and mirror-symmetrically on upper and lower
surfaces, is present in both variants and in the converged production run (1.728), and
vanishes when the last 1 % of chord is excluded, which drops the ratio to 0.263. Only the
leading-edge and TE-excluded rows are diagnostic of the carbuncle question.

## Convergence pathology — none attributable to the floor

| indicator | floor 0.05 | floor 0.00 |
|---|---|---|
| residual orders at step 8000 | 4.94 | 4.88 |
| steps where residual increased | 4038 (50.5 %) | 4056 (50.7 %) |
| CFL-safeguard rejections | 171 | 168 |
| min CFL after ramp / final CFL | 0.365 / 100.0 | 0.434 / 100.0 |
| mean / max inner iterations | 3.54 / 33 | 3.45 / 19 |
| residual span over last 500 steps | 1.328e-03 | 1.351e-03 |

**The floor = 0 run shows no pathology that the floor = 0.05 run does not.** Every indicator
is within a few percent between the two, and where they differ it is mildly *in favour* of
floor = 0 (fewer rejections, 168 vs 171, and a lower maximum inner-iteration count, 19 vs
33). Both runs show the same limit-cycle behaviour — a residual that stalls near 1e-03 with
~50 % of steps increasing and repeated CFL back-off — which is a property of this case
(a near-zero-drag inviscid airfoil whose residual is dominated by a few tiny leading-edge
cells of volume ~5e-09) and not of the flux dissipation. The ~50 % residual-increase
fraction is characteristic of an oscillatory limit cycle rather than smooth convergence and
is present identically in the converged production run (50.9 %).

## Conclusion

> The Roe linear-wave dissipation floor is a conservative but unnecessary safeguard for the
> cases in this study. On the inviscid NACA0012 at zero incidence — where the entropy and
> shear eigenvalue u·n vanishes on the stagnation streamline at the leading edge, and where
> no physical viscosity exists to damp a mesh-frequency mode — removing the floor entirely
> produces no carbuncle. In a fixed-step comparison at 8000 steps, the face-to-face sign of
> the surface-pressure difference alternates once in 119 leading-edge intervals both with
> and without the floor, the ratio of the maximum second difference to the maximum first
> difference in that region is 0.263 with the floor and 0.274 without (both far below the
> value above 2 that a saw-tooth mode would produce), and the stagnation-point cp is 1.0026
> and 1.0025 respectively against an exact value of 1.0. Removing the floor introduces no
> convergence pathology: CFL-safeguard rejections are 171 with the floor and 168 without,
> and the residual histories are indistinguishable. Combined with the cylinder result — a
> 0.021 % drag spread across floor coefficients 0.05, 0.01 and 0.0 — the floor can be
> reported as a defensive measure that costs nothing measurable and, on the evidence of
> these two cases, is not required. It is retained because a 5 % linear-wave floor is
> standard practice for Roe-type schemes on stagnation-bearing meshes and provides
> insurance on meshes not exercised here, not because any reported number depends on it.

**Scope limits, stated plainly.** These two cases (viscous cylinder at Re 20, inviscid
airfoil at Mach 0.15) do not exercise the regime where the Roe carbuncle is most notorious:
strong bow shocks at high Mach on quadrilateral meshes aligned with the shock. Nothing here
licenses a claim that the floor is unnecessary in general, only that it is unnecessary for
these cases and harmless to every number reported. The transonic and supersonic NACA cases
were not swept and would be the natural place to look if the claim were to be broadened.

## Addendum artifacts

- `analyze_stagnation.py` — arclength-ordered open-chain diagnostics with separate
  leading-edge, TE-excluded and whole-surface regions, plus residual-health and
  CFL-rejection indicators
- `naca_stagnation_metrics.json` — full metric dump for both variants
- `naca_stagnation_cp.png` — cp and face-to-face Δcp, whole surface and LE region, both
  variants overlaid with per-face markers
- `runs/naca_floor005/`, `runs/naca_floor00/` — solver output; `logs/naca_floor*.log`;
  `logs/naca_driver.log` — timeline
- `runs/_stale_naca_floor005_killed_attempt/` — partial output from the earlier attempt that
  was terminated at the CPU-quota pause, moved aside rather than deleted
