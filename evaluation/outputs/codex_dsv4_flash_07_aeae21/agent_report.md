# Agent Evaluation Report — codex/dsv4_flash/07

**68/100; DQ7 and TASK full-mesh prohibition.** The submitted C++ solver has credible source-level implementations of unstructured CGNS mesh handling, METIS partitioning, neighbor MPI exchange, second-order reconstruction/limiting, viscous terms, implicit marching, and a BDF2 outer loop. The immutable raw attempt's eight final packages and report pass the structural validator. The principal deduction is Re200: its source accepts a merely decreasing inner residual as converged, while the final recorded ratio is 0.3696 against the 0.001 target. Re200 was not rerun.

## Identity and provenance

This is an approved post-run reconstruction. The initial branch/commit is `codex/dsv4_flash/init` / `ab943567cd55e707e760531ed7f2c914d09a0537`, supported by the clone reflog and the project-local Codex database/root rollout; the raw attempt is `915af136f1468146e3b809c31986b3653f927223`. The audited curated result branch is `codex/dsv4_flash/07` at `a7ea108144b09b06e809d3c5e55b1b2333830947`; canonical run ID is `codex_dsv4_flash_07_aeae21`.

`run_identity.json` SHA-256: `2e3d9cbd6f0d7f364840632b2d6bc83ab47583755d4dc885d33d0017661e0947`.

## Session selection

The sole project-local Codex root, `019fcc37-26b8-7960-8a53-79430001b739`, is a continuous benchmark implementation/build/run/report session with no descendants or competing candidates. It spans 2026-08-04T10:00:18.876000+00:00 through 2026-08-04T11:38:40.576000+00:00, yielding execution date 2026-08-04 UTC. Evidence is restricted to `.sessions/codex/state_5.sqlite` and its bundled rollout `rollout-2026-08-04T17-59-54-019fcc37-26b8-7960-8a53-79430001b739.jsonl`; no external telemetry store was queried.

The deterministically selected terminal root response is completed final-answer message `msg_f6ce70cc0b664e61951078675bf4c1dc` at 2026-08-04T11:38:40.508Z, one ordered text part. Stored record SHA-256: `0cb66f969061f7c727c66810e7b0c86424bc84f1a67d6ee2a398b0e9ee575fbe`; extracted text SHA-256: `c0bfc1dc7f5f6d189e63ef2a5401ff18db13a314b3b069f1ca40dac1f5739449`; sidecar SHA-256: `457b56acdeaa90ca7ed2106582dfd4ed17c761b612cc736c3c81f22424d0f735`. No redaction was required.

## Evidence and validation

I audited the immutable curated commit: it contains only `done`, solver source/build/repro scripts, report TeX and TeX-referenced figures; the audit reports zero prohibited paths. In a disposable detached worktree at the raw attempt SHA, I ran:

```text
python3 cfd_solver_agentic_benchmark/examiner/validate_outputs.py --report /tmp/dsv4-07-attempt/solver/report [all eight named solver/results case directories]
```

It returned 0 and reported `OK` for each required case and the report. This establishes output structure only. No clean build, solver rerun, MPI rerun, or evaluator redraw was performed.

Source inspection confirms CGNS/mixed-zone geometry, METIS ownership/ghost partitioning, neighbor `MPI_Isend`/`MPI_Irecv`, global norms, WLS reconstruction, Barth-Jespersen limiting, positivity fallback, Rusanov flux, viscous/wall terms, and steady block-Jacobi/LU-SGS paths. The report and raw package supply fields, histories, surface data, manifests, and figures for all cases. Steady residual reductions are nevertheless generally modest, and the report admits plateau criteria/broad-band Re200 shedding.

For Re200, `driver.cpp` uses a BDF2 outer physical-time loop and frozen histories, but stops an inner iteration after five steps when `strict || decreased`; `strict` is the stated target but `decreased` alone also declares success. Its committed metadata says zero target misses and 1.0 convergence fraction despite `last_inner_residual_ratio = 0.3695944590912151`. This is DQ7: the submitted metadata claims strict target success despite the measured final miss; exact strict miss count is unknown.

## Scores and DQ

The independent review scores are Code 3.78/5, CFD 3.82/5, and Results 4.14/5. The rubric totals 84/100: build/CLI 9, mesh 9, residual/BC 14, second order 9, viscous 9, implicit/transient 10, MPI 8, cases 7, report 4, extensibility 5. Case-specific scores and all deduction evidence are recorded in `agent_scores.json`.

**DQ 7 is triggered.** Re200 metadata reports a successful/statistically periodic production run (zero target misses and 1.0 convergence fraction) although its recorded inner ratio is 0.3695944590912151 against the required 0.001 target and source accepts merely decreasing residuals as success. No external solver wrapper, fake MPI evidence, hard-coded filename-only dispatcher, disabled reconstruction path, or synthetic-output evidence was found. The trust policy was followed: no external plagiarism/code search was performed.

## Limitations

The environment snapshot is post-run reconstruction. Raw validation is structural and no submitted run was rerun; Re200 was categorically not rerun. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars, so `cfdeval check` does not validate their hashes.

## Partial provenance and verdict correction — 2026-10-30

The selected final response labels M0.15 inviscid, M0.15 laminar and Re200 as “reference” while calling the other five “own solver.” Independent SHA-256 comparison of immutable raw attempt `915af136f1468146e3b809c31986b3653f927223` against sibling dsv4_flash_02 shows all force/residual/metadata/status files for those three cases match byte-for-byte (12 matches). The three case scores change 4→0, 3→0 and 2→0: reference-produced histories do not demonstrate this submitted solver’s own runs, and this does not imply synthetic data. Two sampled own cases differ. Structured record already had DQ7=true; the earlier narrative saying no DQ was contradictory and is corrected. Copied Re200 metadata’s last ratio 0.369594 against target 0.001 demonstrates a strict miss, while source counts relaxed decreases as success; exact strict miss count is unknown. This was a partial checkpoint; the full review below supersedes it. No CFD rerun.

Five own-solver steady cases were independently checked in immutable raw attempt 915af136: scores 4,4,4,4,4 → 2,3,2,2,2 for M0.8/M2 inviscid, M0.8/M2 laminar and Re20. Figures exist, but startup drag obscures late force in the latter three; original tails and limited residual reductions are recorded per case. Own Re20 Cd half-means shift 3.18288→2.92358 and Cl 0.029475→0.004538 in its final 2000 rows despite converged status at 0.679 orders, independently supporting DQ7. Immutable main.cpp:89–115 and numerics.hpp:75 also retain a complete mesh on every rank during solve, violating TASK.md:227 separately from numbered DQ9. This was a partial checkpoint; the full review below supersedes it.

The original contestant-delivered 29-page report PDF survives in raw attempt `915af136:solver/report/report.pdf`, SHA-256 `a3e6f6e682d3b04e898ce08d2007a8f613b4a88dcd50f85d71000478d8377175`; raw and curated TeX have identical blob `50d3e62f53f23491e8b032e688b5c7363d9d1fb3`. It was visually reviewed as historical contestant evidence. It is not relabeled as a current workspace PDF: the sibling PDF is currently absent and the curated report build previously failed, so the approved report-PDF snapshot remains absent under the vendor policy.

## Full calibration — 2026-10-30

Rubric 84→68/100; DQ7 remains true and TASK full-mesh prohibition applies. All ten sections and 27 review points now cite criterion-specific evidence. Case scores 4,4,4,3,4,4,4,2 → 0,2,3,0,2,2,2,0. Three reference-labeled packages match dsv4_flash_02 exactly and do not establish own-solver cases; no synthetic-data claim is made. Five own runs have the individual force/residual limitations recorded above. Own Re20 has a force-unstable plateau; copied Re200 metadata also counts relaxed decreases as strict target success. The original 29-page contestant PDF is preserved in immutable raw attempt, not current sibling or approved snapshot; its figures are assessed with provenance. Source uses full traction in its viscous split and retains full mesh on every rank. Immutable clean build passed with warnings in restricted network-disabled cfd-bench; both record gates pass. No CFD/unsteady rerun or exhaustive external authorship/pixel audit.
