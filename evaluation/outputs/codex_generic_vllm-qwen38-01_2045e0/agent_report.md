# Evaluation — codex_generic_vllm-qwen38-01_2045e0

Submission `codex/generic/vllm-qwen38-01` at `e185a0a43f42781f93348a4dc85c7f8aecfb046c`; checkpoint `18e70a9a25d525feb88d49a83712a1146684754d`.

## Verdict

**62.5/100; DQ=false (partial review).** One production case reaches its residual target; seven are honestly failed and the explicit final validator rejects incomplete metadata.

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


## Build, restart and transient limits — 2026-10-30

Immutable `e185a0a` clean build succeeded in network-disabled `cfd-bench` with two CPUs, 3 GiB and 256 pids and explicit image library/header roots; compiler warnings include unchecked `fread` results. CTest reported no registered tests. No production CFD or Re200 rerun was performed. State/time/step restart exists, but `driver.cpp:1044–1045` initializes the prior BDF state to the loaded current state, so uninterrupted two-level transient continuation is not preserved. Build/CLI8→7.5.

The selected Re200 result honestly reports failed at step3000/t30, with 3000 target misses and 0 converged fraction; metadata reports observed maximum3142 inner evaluations against configured maximum1000. The final source records actual `k_used` outcomes and contains a frozen BDF anchor; exact selected production binary is not bound because revision is `unknown`. Extensibility rubric1→3 because distinct mesh, flux, gradient, residual, partition, halo and output modules exist, while 2-D state and a monolithic driver remain limits. Total63→64.5; other source and DQ review remains partial.


## Failed-case evidence — 2026-10-30

Each of the seven failed selected force CSVs matches contestant checkpoint `18e70a9`; exact SHA256 values, final-400 force ranges, reported status and relevant physics are recorded in each case note. The accepted main PDF includes their histories and fields, and the immutable report's force table includes all eight numerical summaries. The M080 laminar tail is exactly constant during a disclosed failed state, which is not by itself proof of synthetic history. Re200 remains a recognizable partial run at t30/300, not a completed shedding result. Scores 1 remain for independent failed-case limitations, not the earlier false claim of missing plots or numbers. No CFD rerun.


## Rank-study anomaly and source binding — 2026-10-30

Checkpoint `18e70a9` contains the selected Re20 rank-study force CSVs, matching workspace bytes. At common step300 np1 Cd1.615080/Cl−0.338803 and np2 Cd−0.451645/Cl−0.335014 differ by 2.066725 in absolute drag. The np1 force CSV SHA256 is `9d6163f4b2a2a08de8af65c41b03aca79db01069a8d834af075c47ec2ccc92d2`; np2 SHA256 is `623371f9ccd46d5ea24886b63b0fb3b131cc10c15f832c56c8d1d4c70a3a471b`. Their case JSON files are byte-identical SHA256 `b7eda45660d0292b73af6dd27b9701bacfcd6208929a35a70f60f2e755888b7e`; recorded commands select Rusanov without restart, and stdout reports the same freestream and no-slip initial profile.

Both runs are explicitly failed and revisions are `unknown`. The np1 run was 2026-08-26 03:44–08:16 UTC, np2 2026-08-27 02:15–06:54 UTC; source fix `4722f3a` was committed between them on Aug 26 at 13:09 UTC. This prevents binding the executions to the same binary and does not prove rank alone caused the order-one difference. DQ4 remains unasserted pending decisive source/binary evidence; MPI credit and numerical anomaly are documented separately. No CFD rerun.


## Source and review calibration — 2026-10-30

`mesh.cpp:158–176` assumes an HDF5 `/Base` group and discovers multi-zone meshes, with unsupported element types skipped; geometry is less general than the old full mesh score suggested. `grad.cpp:9–14,97–125` computes owned-cell least-squares gradients and a Barth-style limiter, while `residual.cpp:16–21` uses an unreconstructed ghost state across partition boundaries. This is a concrete local accuracy limitation, not proof that all production reconstruction was disabled. Mesh9→8 and second-order7→6; total64.5→62.5.

`main.cpp:102–112` constructs the full mesh only on root during setup; `partition.cpp:355` invokes METIS and `halo.cpp:23–33` exchanges neighbor state. `forces.cpp:24–37` integrates tangential wall shear, while `output.cpp:69–72` writes zero no-slip boundary velocity. The examiner validator independently passed M200 inviscid and the report and rejected seven packages because their metadata honestly says completed=false. All criterion notes now distinguish source implementation from failed results. DQ4 and historical binary binding remain unresolved as documented above; no production CFD rerun.


## DQ boundaries — 2026-10-30

The seven failed selected cases are explicitly marked `completed=false` and `failed`, so their failures alone do not establish DQ7 false success. The sole successful M200 inviscid case reaches the reported residual target and has the force variations documented in its case note; no affirmative failure is established solely from those variations. The order-one Re20 rank-study anomaly is documented above with identical case input/initialization but failed runs separated by an intervening source fix and unknown binary revisions; DQ4 rank-only causation is not established.

Root-only global mesh setup, METIS and neighbor halos do not establish the full-state/full-mesh shortcut; owned-cell second-order reconstruction is active even though ghost faces are first order, so DQ9/10 are not inferred. Tangential wall shear is integrated and no-slip surface output is boundary-valued. No specific final report figure has been proven unrelated to its source or misnamed, and no synthetic force/residual generation or external-solver call was identified in the inspected source. No exhaustive external-authorship search was performed. Historic production binary attribution and full figure-source review remain qualified; this is a partial review, not proof every DQ trigger is impossible. No CFD rerun.
