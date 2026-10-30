# Evaluation — codex_generic_vllm-qwen38-01_2045e0

Submission `codex/generic/vllm-qwen38-01` at `e185a0a43f42781f93348a4dc85c7f8aecfb046c`; checkpoint `18e70a9a25d525feb88d49a83712a1146684754d`.

## Verdict

**63/100; DQ=false.** One production case reaches its residual target; seven are honestly failed and the explicit final validator rejects incomplete metadata.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: 1120116086ce2a193a50be3f97ca0a8e915775eab79e9ab7d3229260423dc7ef. contestant_final_response.md SHA-256: aa3ab9db3d1cc638e84dcfae468b00d2774dd1a8706d070bb5c33ab62bd7a33d. These unindexed sidecars are not validated by cfdeval check.


## Contestant PDF and case-evidence corrections — 2026-10-30

All 50 pages of the accepted sibling main PDF were rendered and visually inspected; the PDF SHA256 `c8943305dfb4e6fdae87518654f1ea6b23f2940fe348a354ac58a794e31dd8c8` matches checkpoint `18e70a9`, and tracked main TeX matches submission `e185a0a`. Contrary to prior notes, the report includes case residual/force histories and computed fields; curated-tip omission of figure blobs remains a reproducibility limitation. Immutable report.tex:942–951 also lists all three laminar numeric CL/CD values.

The sole completed M200 inviscid case has readable numeric summary, residual/force plots (PDF p29) and Mach/pressure contours (p30). Checkpoint-matched force SHA256 `2ef8f5d9559e9e0c69de0fc544c6a50edfd8f4fb8e3b95cc6fe32003329670a0` gives final400 Cd0.0920658–0.0931395 and Cl−0.0021059..−0.0003083; its score rises 2→3 with residual/force variation limiting confidence. The old Re200 note's St0.16 is a reference range, while report.tex:1528–1544 states partial t30 FFT St≈0.1333 and identifies the result as failed; score1 remains. Other case physics, source/rubric/DQ review remains partial. No Re200 rerun.
