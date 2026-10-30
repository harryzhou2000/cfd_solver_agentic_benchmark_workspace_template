# Evaluation — codex_generic_glm53-04_adde03

Submission `codex/generic/glm53-04` at `c06741d9dc5739d9f4a6d6c0285e3c69c8cf8052`; checkpoint `e09ae688025722b07af8a347eba5913c9970ea84`.

## Verdict

**88/100; DQ=true under DQ7 (partial calibration).** A separate M2 laminar case is labeled converged while drag remains materially unstable; the prior inviscid residual-only rationale is under review.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: 8dca1b41479ef4204805826f21d444b00ed15a81cf27eeaf89373df24f7f3051. contestant_final_response.md SHA-256: ac8b63ecc8c47ddd70e7292e6b5117e6614873d2806bf3a05dabba5932f9c8db. These unindexed sidecars are not validated by cfdeval check.


## Partial M2 laminar and Re200 review — 2026-10-30

Current selected M2 laminar metadata says completed/converged at step406 with 3.004 residual orders, while last40 Cd rows fall 0.8537653491→0.5291502491 (38.0%); first/second half means 0.76240268→0.59653982. TASK.md:140,172 require clearly achieved early convergence or a justified stable-force plateau. DQ7 applies to this case and its case score 4→1. Report.tex:311 says Cd1.025, inconsistent with current raw final Cd0.529; metadata git_revision=26ef1cb points to an initial commit without solver source, so exact production binding/generation is unproved. The prior DQ7 rationale based on below-target inviscid residual orders alone is insufficient without independent force/plateau assessment, which remains open.

Re200 case4.5→2: immutable c06741d forces.png visually has startup drag excursions roughly −120 to +40, compressing late drag oscillations; no alternate delivered late force plot found. Numeric St0.175 is present in report.tex:497, and independently computed t>=100 raw lift FFT peak0.174991 with Cl range −0.450674 to 0.4611 supports oscillatory lift; these facts do not satisfy the separate plotted late lift/drag floor. The 167 reported inner misses out of 30000 do not imply most steps missed. Other rubric/DQ and report/source provenance partial; no CFD rerun.


## Partial steady plots and plateau review — 2026-10-30

Visually inspected all seven immutable c06741d steady force figures and searched report/figure directory for alternatives. M015 inviscid late Cd≈0.001 is compressed by startup >1; M015/M080 laminar late Cd≈0.057/0.076 is compressed by startup ≈320; Re20 late Cd≈2.072 is compressed by startup ≈156. The interim scores 4→2, 5→2, 4→2, 5→2 were subsequently revised in the comparison below; numerical force tails and report/source limits are in the scorecard. The M080 and M2 inviscid force plots show more late behavior and their existing scores remain unchanged.

Last-10% M080 inviscid residual half-means0.01870→0.01562, Cd0.0094542→0.0094535, but Cl0.002022→−0.000442; M2 residual0.01896→0.01888, Cd0.0886825→0.0886591 and Cl0.001679→0.001681 with bounded oscillations. TASK.md:172 permits justified stable plateaus; residual-order shortfall alone is not a DQ7 basis, and M080 lift stability remains qualified. M2 laminar report contradicts itself: force table :311 gives Cd1.025, text :445 gives Cd2.162, while current raw final Cd0.529. Different generation/source binding remains unproved. Other rubric/DQ partial; no CFD rerun.


## Steady-plot comparison and score correction — 2026-10-30

The four steady plots show drag evolution from startup and independently resolved lift, even where late drag is compressed. Opus-04's immutable steady plots explicitly clip startup and show the settled region more clearly, but similarly penalize presentation rather than automatically cap every compressed force. The steady floor requires readable histories exposing behavior; it does not impose the separate Re200 requirement for readable late lift and drag. On that basis the interim 2 scores are revised to 3 for M015 inviscid, 3 for M015 laminar, 3 for M080 laminar and 4 for Re20; these remain below original 4, 5, 4, 5 for late-force presentation, numerical drift where present, and unresolved source binding. The Re200 cap remains.

For M2 laminar, report.tex:311's Cd=1.025 matches the current last-100-row mean 1.025063, although the table describes steady figures as final; report.tex:445 separately says Cd=2.162 and the raw final is 0.529150. This identifies a summary/claim discrepancy without asserting a different generation. Exact run/source binding remains unproved. No CFD rerun.
