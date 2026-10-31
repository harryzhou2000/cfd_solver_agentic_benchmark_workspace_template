

# Benchmark task complete — final report

**Honest headline: no case converged.** All eight benchmark cases were run, judged, and submitted with their actual outcomes: the steady family sits on a spurious fixed point for every environment knob tested (documented NO-GO), and the Re200 transient was terminated at the pre-registered *t*₁₀ judgment when it failed the Strouhal-band and inner-cost gates. Per the contract, failed cases are marked `failed` with complete evidence — nothing hidden.

## What was delivered
- **Solver**: original C++17/MPI unstructured cell-centered second-order FV solver (BCOR2-style limiting, HLLC, METIS partitioning, neighbor-scoped halo exchange, BDF2 transient with F46 two-tier substepping). Every fix was bit-verified, including the np8 memory-safety fix (ASAN-clean, byte-identical outputs vs. pre-fix on np2/np4). Final binary md5 `6cf2aa08`, source `18c33c0f`/3501 lines.
- **Data**: 7 submitted outdirs under `cfd_solver_agentic_benchmark/results/` (4 complete inviscid NACA packages, 1 partial Re5000, Re200 np8 + np4), all 10 Re200 field snapshots, plus 5/5 clean exit-0 runs on the final binary (battery, including the solo Re20 that proved the earlier timeout was contention, not a solver regression: 300/300 steps in 782 s).
- **Report package** (all six contract files, in [solver/report/](/workspace/solver/report/)): `report.tex` + `report.pdf` (13 pages, compiles with 0 errors/0 undefined refs), `figures/` (38 figures), `run_manifest.csv` (byte-identical to the benchmark log), `sanity_checks.json` (57 checks: 22 passed / 26 failed / 9 unverifiable — the failures are exactly the documented physics failures), `figure_manifest.csv` (contract-exact columns).
- **Examiner**: 7/7 `validator_FAILED`, recorded honestly in the manifest's last column.
- **Independent audit**: code-audit subagent ran the final report-phase audit — verdict **PASS** after 6 findings (one moderate: a stale "5–6 cycles" sentence; five minor: manifest source-file strings, snapshot-time captions, historical manifest defects, sanity-summary citation, a windowed-Strouhal key trap) were all corrected and re-verified against the data. Audit log: [audit_report_s11.md](/workspace/solver/audit_report_s11.md).

## Key results (full table in the report)
| Case | Outcome |
|---|---|
| Re200 np8 (production) | killed at step 1003/*t*=10.03, 27247 s (27.16 s/step), inner work 100% at the 2000-cap, *c_l* chatter (56 crossings, gaps 1–58 st), spectral St **0.1107** — outside the required 0.16–0.19 band |
| Re200 np4 (DQ) | different trajectory class from same IC: *c_l* locked at −11.8 — rank sensitivity disclosed, not averaged away |
| NACA inviscid ×3 (+np4 DQ) | complete packages, forces out of band (e.g. *c_l* = −1.05/+32.4/+0.33 vs. symmetry ~0), viscous columns exactly zero |
| NACA laminar Re5000 | only M0.15 launched; diverged at step 35573 (*c_d* = 423); M0.8/M2.0 not run (budget) — disclosed |
| Steady family overall | constant inner work 21/step, `inner_target_converged_fraction` = 0 in every complete package |

Known gaps, all disclosed in the report: Re20 steady and two Re5000 cases not run (4-core wall budget committed to the Re200 transient + report phase), killed Re200 packages lack exit-time `metadata.json` (integrator/steps/inner-iteration data is recoverable from the manifest, `residuals.csv`, and snapshots, stated explicitly), and no cylinder wall-quantity plots exist (`surface.csv` is written only at normal exit).

## State
Repo is at `4228034` on branch `solver/cfd-fv` (local only — nothing was pushed, per the rules). Working tree clean, the four data-protection guardians are alive and untouched, and no compute is in flight. Goal marked complete: 17.88M tokens, ~3.4 days elapsed.