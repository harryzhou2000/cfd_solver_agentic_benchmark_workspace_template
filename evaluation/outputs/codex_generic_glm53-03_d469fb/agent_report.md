# Evaluation — codex_generic_glm53-03_d469fb

Submission `codex/generic/glm53-03` at `17724cd96bf90e2cf9be2568f38a9a418e9fbcae`; checkpoint `60cf3aa302290576824d3c8923d5305411417861`.

## Verdict

**84/100; DQ=true.** Validator passes structurally, but the report marks M0.80 and M2 inviscid runs converged at 0.81 and 0.14 residual orders.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: aa393a07443338d935b60d9fd3d2e322d8603473610404519cb37945cebcf4cb. contestant_final_response.md SHA-256: 90ed619ff06eaa0f3dafcb535ef5e197e8b506204cb786e29a8fdd1a977cb3bd. These unindexed sidecars are not validated by cfdeval check.

## Re200 reassessment

The independent Re200 score changes from **4.00 to 2.00**. Its own report says termination at t=287, post-t≈267 inner-solve failure, no vortex shedding and lift amplitude approximately zero, contradicting the t=300 statistically-periodic status. In contrast, glm53-01 retains 4.50: it has t=300.01/30001 status, 301 time fields, force/lift-history/vorticity evidence and an FFT-derived St≈0.113; that low St prevents an increase.
