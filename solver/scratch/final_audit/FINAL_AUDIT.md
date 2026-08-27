# FINAL PRE-SUBMISSION AUDIT — cns2d CFD solver benchmark

Auditor: /root/final_audit (lead) with subagents source_vs_report, report_claims.
Date: 2026-08-27. Deliverable: /workspace/solver/. Spec: /workspace/cfd_solver_agentic_benchmark/ (read-only).
Method: READ-ONLY. Every number below was re-derived by the auditor from results/ or src/,
not taken from the report. Scripts in scratch/final_audit/py/.

---

## 0. HEADLINE

The submission is **substantively complete**: all 8 required cases are real solver runs
with the supplied production parameters, the algorithm in the report matches the source,
originality is CLEAR, and no disqualification trigger fires.

Two defects were found that the prior audits did not catch:

- **F1 (MUST FIX — stale numbers, 5th incident).** Three of the four rows of the shedding
  saturation table in sec_results.tex reproduce only from a **mid-run snapshot at t≈54.8**,
  not the final t=300 record. The surrounding prose argument is built on those stale rows.
- **F2 (SHOULD FIX — unmet contract item).** OUTPUT_CONTRACT "Required visualizations"
  item 4 (cylinder wall pressure/friction plots for **cylinder cases**) is not met for
  cylinder Re200: the figure exists on disk but is never displayed in the report.

Neither is a disqualification trigger. F1 is the more serious because it is exactly the
failure mode this report has already had four times, and it sits in the case that carries
the most analysis weight.

---

## 1. VERDICT ON COMPLETENESS

### 1.1 Official validator — PASS

    cd /workspace/solver && .venv/bin/python \
      /workspace/cfd_solver_agentic_benchmark/examiner/validate_outputs.py \
      results/<all 8 cases> --report report

Result: "OK" on all 8 case directories and "OK report". Exit 0. No warnings.
Unit tests: ./build/cns2d_tests -> **139 checks, 0 failures**.

### 1.2 Output contract, structural — MET (520 checks, 0 failures)

Programmatic sweep (py/c1.py) over all 8 cases:

- All 9 required files present in every case dir (incl. restart_final.*, partition_diagnostics.*).
- All 4 CSV headers match the contract **character-for-character** (residuals, forces,
  surface, partition_diagnostics).
- All **40** required metadata.json fields present in all 8 cases, correct types.
- All 9 required run_status.json fields present in all 8 cases.
- metadata/run_status cross-agree on mpi_ranks and convergence_status.
- run_status.final_step equals the last forces.csv row step in all 8 cases.

### 1.3 The contract's hardest item — final force row == final written state — MET

Contract: "The final force row must correspond to the final state written to surface.csv
and field_final.*". This is where an early-stopped steady run would betray itself.

Mechanism found in src/solve/steady_driver.cpp:615-660: when the march ends materially
above its own best residual, the best state is restored, then the residual **and forces are
recomputed from that restored state** and appended. Hence forces.csv/residuals.csv are
deliberately **non-monotonic in step**: max(step) is where the march stopped, the LAST row
is the state actually written. Verified: exactly 2 rows share the final step in each of the
8 files, and no other non-monotonicity exists.

Independent numerical confirmation (py/g2.py): I re-integrated cl and cd directly from each
submitted surface.csv (ordering the wall loop geometrically, ds from half-distance to each
neighbour, pressure force from (p-p_inf)n ds, skin friction from cf*q*t ds) and compared
with the LAST forces.csv row:

| case | cd from surface.csv | cd last force row | rel err |
|---|---|---|---|
| naca0012_m015_inviscid | 0.00098 | 0.00099 | 0.88 % |
| naca0012_m080_inviscid | 0.00876 | 0.00877 | 0.17 % |
| naca0012_m200_inviscid | 0.08750 | 0.08749 | 0.01 % |
| naca0012_m015_laminar_re5000 | 0.05517 | 0.05518 | 0.02 % |
| naca0012_m080_laminar_re5000 | 0.08147 | 0.08147 | 0.00 % |
| naca0012_m200_laminar_re5000 | 0.13716 | 0.13714 | 0.01 % |
| cylinder_m010_laminar_re20 | 2.01738 | 2.01837 | 0.05 % |
| cylinder_m010_laminar_re200 | 1.26832 | 1.26895 | 0.05 % |

cl agrees to 2.8e-8..2.3e-4 absolute. Residual discrepancy is fully explained by my crude
edge-length reconstruction. **No mixing of a final field with an earlier force row.**

### 1.4 Production parameters — MET, no silent loosening (py/p1.py)

For all 8 cases the case-JSON run_control values equal what the solver logged and recorded:
max_steps, cfl_initial->cfl_max, ramp steps, inner-iteration bounds, inner target.
Re200 specifically: dt=0.01, final_time=300 (30000 physical steps), inner 5..1000,
target 1e-3 **on the total transient residual**, pseudo-CFL fixed 1->1, true_bdf2_inner_loop=true,
observed inner 49..144 (typical 133), 0 target misses, converged fraction 1.0,
last_inner_residual_ratio 9.988e-4. This satisfies NUMERICAL_PARAMETERS.md exactly and is
the opposite of the "reaches final time but misses the inner target on most steps" failure
that README_EXAMINER check 13 warns about.

### 1.5 Residual targets — 6 of 8 MET, 2 SHORT but honestly disclosed

Recomputed (py/a2.py) as log10(initial / written-state residual):

| case | achieved | spec target | verdict |
|---|---|---|---|
| naca0012_m015_inviscid | 5.26 | 4.0 | MET |
| naca0012_m080_inviscid | **3.92** | 4.0 | SHORT |
| naca0012_m200_inviscid | **2.90** | 3.0 | SHORT |
| naca0012_m015_laminar_re5000 | 5.47 | 4.0 | MET |
| naca0012_m080_laminar_re5000 | 4.00 | 4.0 | MET |
| naca0012_m200_laminar_re5000 | 4.78 | 3.0 | MET |
| cylinder_m010_laminar_re20 | 5.86 | 5.0 | MET |
| cylinder_m010_laminar_re200 | 2.07 (spatial norm) | n/a | n/a |

The two shortfalls are **disclosed in the abstract itself** (report.tex:52-57) with the exact
figures 3.92/4.00 and 2.90/3.00, attributed to the residual rising once the prescribed CFL
ramp reaches its cap. NUMERICAL_PARAMETERS.md permits a documented plateau; the solver
records the achieved reduction rather than the requested one. Judged **acceptable and honest**,
not a hidden failure. Note run_status.residual_reduction_orders for Re200 is 2.247 vs my
2.07 from residuals.csv — different norms (total transient vs spatial-only), explained in
src/solve/transient_driver.cpp:60-66.

### 1.6 Physics sanity gate — MET, independently re-derived (py/s1.py)

| case | min p | min rho | max wall speed | max normal vel | max tangential | cp span | max abs cf |
|---|---|---|---|---|---|---|---|
| m015_inviscid | 31.19 | 0.982 | 9.99e-1 | **9.7e-14** | 0.999 | 2.108 | 0 |
| m080_inviscid | 0.652 | 0.678 | 1.09 | **1.7e-13** | 1.089 | 2.097 | 0 |
| m200_inviscid | 4e-4 | 4.2e-3 | 1.04 | **1.4e-13** | 1.042 | 2.500 | 0 |
| m015_lam | 31.55 | 0.991 | **0** | 0 | 0 | 1.414 | 1.48e-1 |
| m080_lam | 0.930 | 0.790 | **0** | 0 | 0 | 1.497 | 1.28e-1 |
| m200_lam | 0.128 | 0.433 | **0** | 0 | 0 | 1.687 | 9.26e-2 |
| cyl_re20 | 70.93 | 0.992 | **0** | 0 | 0 | 2.259 | 7.86e-1 |
| cyl_re200 | 70.66 | 0.988 | **0** | 0 | 0 | 2.589 | 3.26e-1 |

Positivity holds everywhere. No-slip walls report **exactly zero** velocity (boundary values,
not cell-centre — a separate surface_cell_center.csv is provided, satisfying rubric trigger 11).
Slip walls have round-off normal velocity with fully preserved tangential velocity. Inviscid
viscous force columns are identically 0. cf nonzero on 404/404 and 100/100 wall rows for every
viscous case. cp varies substantially on every body. **All 9 gate items verified true.**

### 1.7 Re200 statistical periodicity — MET, genuinely periodic (py/a4.py, a5.py)

On the final record, post-transient window:

- Strouhal **St = 0.1829** (zero-crossing), 0.1830 (DFT peak) — agrees with numbers_auto (0.1829).
- Period T = 5.4664, **period spread only 0.0023** over 43 cycles => a true limit cycle.
- mean CD = 1.2466, CL amplitude +/-0.5307, CL mean ~0.
- Non-overlapping late windows [60,100],[100,140],[140,180],[180,220]: St = 0.1830/0.1829/
  0.1829/0.1829 and CD = 1.2466/1.2469/1.2466/1.2460. **My values match the report's
  non-overlapping table (sec_results.tex:1230-1233) to the last digit.**

The report honestly quotes St=0.183 against literature 0.19-0.20 and CD=1.247 against 1.3-1.4
as **standing discrepancies** of a 2-D laminar computation, with a physically correct causal
explanation, and explicitly retracts an earlier wrong inference. That is exemplary.

### 1.8 Figures and manifest — MET except F2 (py/f1.py..f4.py)

- 71 PNGs on disk; **71** mapped in figure_manifest.csv + the documented sidecar
  figure_manifest_extra.csv. Zero on disk unmapped, zero mapped-but-absent.
- 48 distinct figures displayed via the guarded \cnsfig macro; **all 48 resolve** — zero
  placeholder boxes, confirmed by 0 hits for "figure not yet generated" in report.log.
- All manifest source_file entries resolve on disk (0 unresolvable).
- Required per-case visualizations: residual, force, Mach and pressure fields present and
  displayed for **all 8** cases. Re200 wake: vorticity, velocity, spectrum all displayed.

**F2:** cylinder Re200 has no wall cp (or cf) plot displayed. cylinder_m010_laminar_re200_cp.png
and _cf.png exist and are in the manifest but no \cnsfig displays them; Re20 does display its
cp. OUTPUT_CONTRACT requires "Cylinder wall pressure/friction or pressure coefficient plots
for cylinder **cases**" (plural). Fix is one \cnsfig line.

Manifest note (not a defect): 143 rows for 71 figures because 18 figures carry up to 5 rows
each, one per source run (production + np1/2/4/8). Provenance is over-specified rather than
wrong, but a reader diffing "143 rows vs 71 figures" will pause; the scaling rows point at
truncated non-converged runs that did not generate the displayed figure.

---

## 2. F1 — THE STALE SATURATION TABLE (primary finding)

sec_results.tex:1195-1203, table tab:saturation, "Shedding statistics as the analysis window
is advanced through the record, from tools/transient_status.py reading forces.csv".

I reimplemented **the exact algorithm of tools/transient_status.py** (true sign-change zero
crossings, period = zc[i+2]-zc[i], mean CD over the window) and searched for the record-end
time that reproduces each published row (py/a7.py, a8.py, a9.py):

| window | published St / T / spread / meanCD | reproduces at record-end | value on FINAL t=300 record |
|---|---|---|---|
| t>=16.8 | 0.1666 / 6.0023 / 2.2324 / 1.1173 | **t≈54.75** (err 8e-5) | 0.1811 / 5.5203 / 2.2583 / 1.2292 |
| t>=30 | 0.1796 / 5.5667 / 0.1947 / 1.1928 | **t≈54.75** (err 5e-5) | 0.1827 / 5.4730 / 0.2207 / 1.2417 |
| t>=40 | 0.1816 / 5.5078 / 0.0360 / 1.2276 | **t≈54.90** (err 9e-4) | 0.1829 / 5.4681 / 0.0620 / 1.2455 |
| t>=60 | 0.1830 / 5.4652 / 0.0022 / 1.2468 | final record (OK) | 0.1829 / 5.4664 / 0.0024 / 1.2466 |

Three of four rows are **mid-run values from a t≈54.8 snapshot**, i.e. from a run that had
completed ~18 % of its physical time. The match is exact to 4-5 decimal places at that
truncation and wrong by up to 4 % in mean CD on the final data, so this is not a rounding or
algorithm-variant artefact — those rows were harvested before the final transient finished and
never refreshed. The table caption attributes them to the submitted forces.csv, which is false
for 3 of 4 rows.

Blast radius — the prose immediately after the table is the report's **central argument that
the flow reached a genuine limit cycle**:

- sec_results.tex:1206-1209 "the period spread collapses from 37 % of the period to 0.04 %".
  On the final record the t>=16.8 spread/period is 2.2583/5.5203 = **41 %**, not 37 %, and the
  t>=40 spread is 0.0620 not 0.0360 (72 % larger). The "Spread/period" column (37/3.5/0.7/0.04 %)
  is stale in its first three rows.
- The nested-window trend the report then correctly identifies as a windowing artefact is
  itself computed from stale numbers.

Mitigating: the **conclusion survives**. The t>=60 row is correct, and the independent
non-overlapping table at :1230-1233 (which I reproduced exactly) already carries the
periodicity argument on final data. So this is a false-evidence problem, not a false-conclusion
problem. But an examiner who recomputes the table — which the report invites by naming the
tool — finds three wrong rows in the flagship case.

Recommended fix: rerun tools/transient_status.py at --start 16.8/30/40/60 on the submitted
forces.csv, replace the four rows and the spread/period percentages, and adjust the "37 %"
sentence to the recomputed value.

---

## 3. ALGORITHM VS SOURCE — MATCH (delegated, 14/14 claims)

Full evidence in SOURCE_VS_REPORT.md. Summary of the six items I was asked to verify:

1. **Riemann solver** — Roe with Harten-Hyman entropy fix, all 12 formula terms verified
   against src/numerics/riemann_flux.cpp; HLLC and Rusanov also implemented, HLLC as
   positivity fallback. Matches metadata inviscid_flux/entropy_fix. MATCH.
2. **Limiter** — Venkatakrishnan, K=5.0, **active in production** (config traced from case
   JSON through case_input.cpp to the call site). Rubric trigger 10 CLEAR.
3. **Reconstruction** — piecewise-linear least-squares active; every first-order fallback
   trigger located and honestly disclosed. MATCH.
4. **LU-SGS** — genuine forward (ascending, in-place) + backward (descending) sweeps with
   off-diagonal contributions over interior faces (lusgs.cpp:178-210), matrix-free exact
   analytic A*dU product. A **separate** kJacobi branch exists with different update
   semantics, which is strong evidence the SGS structure is real and not point-Jacobi
   relabelled. Satisfies TASK.md "a single diagonal local update is not sufficient". MATCH.
5. **BDF2 frozen history** — true two-level loop, U^n/U^{n-1} frozen through all inner
   iterations, history updated only after the step is accepted, inner target measured on the
   full transient residual. MATCH, with one defect (see F3 below).
6. **METIS** — METIS_PartGraphKway on the cell dual graph, no geometric fallback path that
   could silently activate; metadata partitioner=metis_kway is truthful. Trigger 9 CLEAR.
7. **Halo exchange** — neighbour-scoped Isend/Irecv. Every MPI collective in the tree was
   classified by call-chain phase: all Bcast/Scatter/Allgather/Gather of state or mesh are
   one-time setup or output; the **only** iteration-time collectives are scalar Allreduce for
   norms/forces. GlobalMesh is scoped inside a rank-0 block and destroyed before solver
   fields allocate, so both full_*_replication_during_iterations=false flags are truthful.

**F3 (MINOR, disclosure-grade).** transient_driver.cpp:185-196 writes the residuals.csv row
*after* the history shift at :169-172, so the logged transient residual is formed across
inconsistent time levels (measured ~1.4999x the true spatial residual on the settled case).
The **solver mathematics is unaffected** — the inner loop's own convergence test uses the
correctly-formed pre-shift residual, and metadata final_residual_l2 is clean. But
sec_results.tex:1381 labels that plot "the total transient residual", which is now imprecise.
One-line code fix or one sentence in limitations.

---

## 4. DISQUALIFICATION TRIGGERS — ALL 13 CLEAR

| # | Trigger | Verdict | Evidence |
|---|---|---|---|
| 1 | Existing solver executable called internally | CLEAR | Zero system/popen/exec/fork in code; 4 matches are usage text/comment |
| 2 | Force/residual files generated without solving | CLEAR | Writers fed live U on every path; 8/8 final force rows reproduce from surface.csv (1.3) |
| 3 | Only these 2 mesh filenames via hard-coded branches | CLEAR | Exactly one naca/cylinder hit tree-wide, in a comment; BC families read from mesh/case mapping |
| 4 | MPI rank count changes steady results order-one | CLEAR | Worst CD spread np=1..8: 2.36e-4 (cyl), 1.05e-4 (NACA) |
| 5 | Explicit time stepping only | CLEAR | No explicit integrator exists; implicit pseudo-time + dual-time BDF2 |
| 6 | Report claims algorithms not in source | CLEAR | 14/14 algorithm claims MATCH (section 3) |
| 7 | metadata marks incomplete/failed runs successful | CLEAR | 8/8 completed=true are real completed runs; solver has a live not_converged path that fires on the truncated scaling runs |
| 8 | Figures do not correspond to submitted outputs | CLEAR | 71/71 manifest-mapped, all source_file resolve, all 48 displayed figures resolve |
| 9 | metadata reports METIS but code uses geometric/replication | CLEAR | METIS_PartGraphKway verified; no full replication during iterations |
| 10 | Claims 2nd-order/limiter but disabled in production | CLEAR | Venkatakrishnan + LSQ reconstruction active in production |
| 11 | No-slip rows are actually cell-centre values | CLEAR | Wall u,v,mach exactly 0; separate surface_cell_center.csv provided |
| 12 | Misnamed figures (mach plots pressure) | CLEAR | Manifest variable column consistent; spot-checks match filenames |
| 13 | Solver core copied from open-source CFD code | CLEAR | See below |

**Trigger 13 — originality, CLEAR.** ~7.0 kLOC across 61 files examined. Zero licence headers,
zero doxygen fragments, zero idioms characteristic of SU2/OpenFOAM/DNDSR/PyFR/Trixi/FUN3D.
No 3-D or RANS remnants (kDim=2 is genuine; 3-D references are forward-looking comments).
Uniform style across all modules. /workspace/external contains **only infrastructure** — CGNS,
HDF5, METIS, Eigen, Boost, CGAL, fmt, nlohmann_json etc. — and **no CFD solver source that
could have been lifted**. Comments carry project-specific provenance (the HLLC star-energy
sign fix, a measured 4.9 % force discrepancy on M2.0).

What would change this verdict: a similarity hit against an external corpus. Neither I nor the
subagent has network access, so no external corpus search was performed — this is the one
unclosed gap in the originality assessment, and it is stated rather than papered over. The
monolithic first commit is **non-probative** either way (normal for an agentic session).

---

## 5. REQUIREMENTS NOT MET

1. **F2 — OUTPUT_CONTRACT required visualization 4**, cylinder wall pressure/friction plot for
   cylinder Re200: figure exists, never displayed. One-line fix.

Everything else in TASK.md "Submission Contents", "Completion And Quality Gate",
OUTPUT_CONTRACT.md and report_requirements.tex that I checked is MET.

---

## 6. THE ONE THING MOST LIKELY TO LOSE POINTS

**F1, the stale saturation table.** Not because the conclusion is wrong — it isn't; the
periodicity result is solid and independently reproduced — but because the report names the
tool and the source file, an examiner recomputing three rows finds them wrong by up to 4 %,
and this is the **fifth** instance of a superseded number surviving into prose. The pattern
is what damages credibility on a report whose main claim to trust is its own honesty.

**Readiness: ready after F1 and F2 are fixed.** Both are small, local, and require no
re-running of the solver. Nothing found in the source, the results, the contract compliance
or the originality assessment blocks submission.


---

## 7. ADDITIONAL CONFIRMED FALSEHOODS (delegated claim sweep, re-verified by lead)

Coverage: **89 prose claims + 110 table cells** checked across all sections (REPORT_CLAIMS.md).
**23 DISAGREE, 4 UNVERIFIABLE.** The findings below were independently re-verified by the lead.

### F4 (MUST FIX) - hand-maintained verification constants: 4 of 8 wrong, 3 match no submitted run

numbers.tex is hand-maintained (unlike numbers_auto.tex) and feeds the abstract and
tab:verification. Actual values from the 8 stdout.log 'mesh verification:' lines:

| macro | report | NACA mesh actual | cylinder mesh actual | verdict |
|---|---|---|---|---|
| vFaceClosure | 1.0e-16 | 1.025e-16 | 1.020e-16 | OK (rounded) |
| vVolumeClosure | 1.1e-10 | 1.090e-10 | 4.757e-13 | NACA only, presented as global |
| vAreaLineIntegral | 1.4e-14 | 4.166e-15 | 2.355e-16 | matches NEITHER run |
| vLsqExactness | 3.1e-13 | 1.428e-10 | 3.137e-13 | cylinder value sold as global; NACA 340x larger |
| vConservation | 9.2e-17 | 1.2e-18 .. 1.5e-01 | 5.7e-05 .. 5.7e-04 | matches NO submitted run |

Two aggravating details:

- vLsqExactness is quoted in the ABSTRACT under the word 'exactness'. On the NACA mesh, which
  carries 6 of the 8 cases, the measured linear-gradient error is 1.428e-10 - not O(eps) by any
  reading, while tab:verification lists the expected column as O(eps).
- vConservation 9.2e-17 is quoted UNQUALIFIED in the abstract. The viscous cases measure
  2.960e-03 (m015_lam), 6.025e-02 (m080_lam), 1.491e-01 (m200_lam), 5.731e-04 (cyl_re20),
  5.731e-05 (cyl_re200) - up to 15 orders larger. sec_verification scopes the number to 'the
  first-order configuration' and I confirmed the probe sets second_order=false at
  src/mesh/mesh_verification.cpp:135. But that scoping does NOT cover the viscous gap, and I
  found the mechanism: the probe disables reconstruction but NOT viscous flux, while the
  independently recomputed boundary term at mesh_verification.cpp:166-181 calls only the
  inviscid Riemann flux. The interior residual therefore carries a viscous contribution the
  boundary term omits - structurally the same inconsistency the report warns about for
  reconstruction, but undisclosed.

### F5 (SHOULD FIX) - sec_parallel:38-39, stale cylinder scaling digits

Prose claims the cylinder goes 'from 2.12601 at np=1 to 2.12701 at np=8', and (:35) 'the worst
case being 4.7e-4'. Verified (py/v1.py): those digits are the step-1500 max-step rows
(2.12627534 / 2.12716067), not the reported last rows (2.13640805 / 2.13666609). The derived
4.7e-4 reproduces from the wrong pair; correct worst deviation is 4.16e-4, or 2.36e-4 on the
reported last rows. The auto-generated tab_scaling.tex is CORRECT - hand-written prose
contradicts its own adjacent table.

### F6 (SHOULD FIX) - abstract force-agreement claim is true only for C_D

report.tex:39 and sec_limitations.tex:169 claim force coefficients agree to within 5e-4
relative across np=1,2,4,8. Verified worst relative deviations:

| coefficient | cylinder Re20 | NACA m015_lam |
|---|---|---|
| C_D | 2.36e-4 | 1.05e-4 |
| C_L | 5.33e-2 | 8.09e-2 |
| C_mz | 3.95e-1 | 1.13e-1 |

C_L and C_mz are near zero by symmetry so relative error is a poor metric - a fair defence of
the physics, but not of the sentence, which bounds 'force coefficients' without restriction.

### F7 (TRIVIAL) - sec_verification:265 undercount

'on all three no-slip cases the maximum wall speed is exactly 0.000e+00' - there are FIVE
no-slip cases (3 NACA laminar + 2 cylinder). The property holds on all five (section 1.6), so
the claim is true but the count is wrong.

### Clean areas (verified, no defects)

- tab_forces, tab_runstatus, tab_partition: 110 cells, ZERO mismatches, correctly using the
  restored-best-state last row.
- m080_inviscid residual-shortfall narrative: factor 10.69, 950 steps, tail mean 4.5086e-3,
  and the 0.09/0.10 order gaps all check out - exemplary disclosure.
- Pitot Cp 1.65730, the '9 of 404 faces' count, Blasius, 139 unit checks: verified.
- np=8 regression properly disclosed as quota oversubscription; recomputed optima 3.89x (cyl)
  and 4.43x (NACA) match the report.
- All 14 mesh sizes and named scheme parameters in numbers.tex verified clean.
- The 'monotone approach from below' retraction paragraph REMAINS VALID on the true record:
  the nested-window trend is still monotone, so its logic and conclusion survive F1.

---

## 8. REVISED DEFECT SUMMARY

| id | severity | defect | fix cost |
|---|---|---|---|
| F1 | MUST FIX | tab:saturation rows 1-3 and all 4 spread/period percentages come from a t~54.8 mid-run snapshot | rerun transient_status.py, retype 4 rows |
| F4 | MUST FIX | 4 of 8 hand-maintained verification constants wrong; abstract 'exactness' 3.1e-13 vs NACA 1.428e-10; unqualified conservation 9.2e-17 vs viscous 1e-3..1.5e-1 | requote per-mesh, scope the conservation claim |
| F5 | SHOULD FIX | stale cylinder scaling digits and derived 4.7e-4 in prose | retype 3 numbers |
| F6 | SHOULD FIX | 'force coefficients agree to 5e-4' true only for C_D | qualify the sentence |
| F2 | SHOULD FIX | Re200 cylinder wall cp/cf figure never displayed (contract visualization item 4) | one cnsfig line |
| F3 | MINOR | residuals.csv transient row written post-history-shift; plot label imprecise | one line, or disclose |
| F7 | TRIVIAL | 'three no-slip cases' should be five | one word |

None is a disqualification trigger. All are local text/number fixes; NO solver re-run is needed.

## 9. FINAL VERDICT

**Substantively complete, but NOT ready to sign off as-is.** The engineering is sound and the
claims about the ALGORITHM are accurate: 14 of 14 verified against source, originality CLEAR,
all 13 triggers CLEAR, 520 structural checks and 483 pipeline checks green, and the contract's
hardest requirement (final force row == final written state) independently confirmed on all 8
cases by re-integrating surface.csv.

The problem is confined to REPORT NUMBERS. Across two independent sweeps this audit found five
distinct stale-or-wrong quantitative claims (F1, F4, F5, F6, F7) that survived four prior
audits, two of them in the abstract. The single highest-priority concern is F4, which I rank
above the F1 lead I opened with: it puts three numbers in the abstract that match no submitted
run, one under the word 'exactness' while the mesh carrying 6 of 8 cases is 340x worse, and one
implying machine-precision conservation while five viscous cases sit at 1e-3 to 1.5e-1 for a
reason the report does not disclose. An examiner checking the abstract against stdout.log -
the first thing a skeptical reader does - finds it immediately.

Fix F1 and F4 (and ideally F2, F5, F6), then this is a strong submission.

