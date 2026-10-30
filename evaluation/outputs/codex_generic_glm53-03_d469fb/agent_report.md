# Evaluation — codex_generic_glm53-03_d469fb

Submission `codex/generic/glm53-03` at `17724cd96bf90e2cf9be2568f38a9a418e9fbcae`; checkpoint `60cf3aa302290576824d3c8923d5305411417861`.

## Verdict

**84/100; DQ=true under trigger 7 (partial calibration).** Current Re200 metadata labels an essentially steady t300 result periodic and Re20 is labeled converged while drag is still changing; the report describes a different Re200 generation.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: aa393a07443338d935b60d9fd3d2e322d8603473610404519cb37945cebcf4cb. contestant_final_response.md SHA-256: 90ed619ff06eaa0f3dafcb535ef5e197e8b506204cb786e29a8fdd1a977cb3bd. These unindexed sidecars are not validated by cfdeval check.

## Re200 reassessment

The independent Re200 score changes from **4.00 to 2.00**. Its own report says termination at t=287, post-t≈267 inner-solve failure, no vortex shedding and lift amplitude approximately zero, contradicting the t=300 statistically-periodic status. In contrast, glm53-01 retains 4.50: it has t=300.01/30001 status, 301 time fields, force/lift-history/vorticity evidence and an FFT-derived St≈0.113; that low St prevents an increase.


## Partial current-package DQ7 review — 2026-10-30

Current selected solver/results/cylinder_m010_laminar_re200 metadata/status label completed/statistically_periodic at t300, but last 3000 force rows have Cd 4.06744774746–4.067458670148 and Cl −0.0009113144295156 to −0.0009111552227523: essentially steady with no vortex street. Case score 2→1 and DQ7 remains true under TASK.md:173. report.tex:164–169 instead describes an incomplete t287 run with inner failures/no shedding; that historical report generation must not be conflated with the current t300 package. Current Re20 status claims converged at step56 but last five Cd rows span 3.2248058–3.9752864 (23% relative to first) and lift also changes, inconsistent with TASK.md:172 stable-force criterion; case remains1.

The prior DQ evidence citing M080/M2 inviscid 0.81/0.14 orders refers to a different report/result state; current selected metadata instead shows steps85/3121 with 4.009/2.645 orders. Those cases and their plateau evidence remain under review. Current metadata git_revision=26ef1cb points to an initial workspace commit with no solver source, not proof of production source; other cases/rubric/DQ partial. No CFD rerun.
