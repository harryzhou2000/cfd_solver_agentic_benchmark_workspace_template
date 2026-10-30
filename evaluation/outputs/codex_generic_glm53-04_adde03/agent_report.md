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
