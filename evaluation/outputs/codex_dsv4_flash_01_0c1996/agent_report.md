# Agent Evaluation Report — codex_dsv4_flash_01_0c1996

- Result branch: `codex/dsv4_flash/01`; submission commit: `f6d8a1839a70c98d83a1e5baff9e2d6e111f7e2f`.
- Initial branch/commit: `codex/dsv4_flash/init` / `b41b8393db18740b1b1568c0bef78491eb89297d`.
- Identity: `codex_dsv4_flash_01_0c1996` (v2); `run_identity.json` SHA-256 `66d3e169d85845ecb7bef0dcb407dc4bec86b372f632e01edc7be88f89c9330e`.
- Benchmark submodule: `ffc314f7dd886d03e5c8172f44272f2f58f9c7cc`.

## Legacy provenance

The pre-run snapshot was absent. With explicit operator authorization, `.eval/env_snapshot.json` was reconstructed after the run only to supply the verified initial branch and commit. It is explicitly marked `legacy-reconstructed-post-run-v1`, `pre_run_authority: false`; original environment, harness-version, network, dirty-tree, and capture-time evidence are unavailable. The initial values are supported by all migrated `state_5.sqlite` thread rows and local Git verification. The exact user-approved upstream `https://github.com/harryzhou2000/cfd_solver_agentic_benchmark_workspace_template.git` had no `refs/heads/codex/dsv4_flash/01` collision; local collision was also absent.

The submission was curated to source, CMake/README, helper scripts, report TeX, and `done`. It excludes `.sessions`, `.eval`, raw result directories, logs, restarts, fields, generated PDF, build products, manifests, and figures. PNGs were not committed because `report.tex` does not reference them. The immutable-delta audit passed: 22 allowed changed paths, zero violations.

## Session selection

Only workspace-bundled `.sessions` evidence was used. The primary benchmark root is orphan rollout `019fb900-dcf6-7673-860a-d91822679d89`, which begins `2026-07-31T16:28:03.347Z`; it contains the sustained benchmark implementation and terminal claim. Its formal DB row is absent, so it cannot be extracted by the supported tree extractor. The selected continuation `019fb926-a333-7b03-b02b-37e86d1dd2e3` is the independently evidenced MPI-validation continuation, `2026-07-31T17:09:05.035Z` to `19:35:56.498Z`, and is the extractable session tree in `sessions.json`.

Execution date is `2026-07-31` UTC from the primary root's first persisted `session_meta` timestamp. Excluded candidates: `019fb8ad`, `019fb8af`, `019fb8b1`, `019fb8b2`, `019fb8b6`, and `019fb8fa` are probes; `019fb8b7`, `019fb8d9`, and `019fb900-762` are aborted benchmark attempts. The continuation's terminal final answer is preserved verbatim in `contestant_final_response.md`: message `msg_7a3c2300a18542c8a1cc237a0e0b8fa7`, timestamp `2026-07-31T19:35:56.447Z`, one text part, extracted text SHA-256 `894eb60276fb2486420d21afed504bf8f5dd6d1169f50aca06ba7b3f53e58d8f`, sidecar SHA-256 `f164fa9e8dd77b52822cd928cdb9f6bb1e4b520cd62e9c41ac2f6cf55c136b1a`, and original stored-record SHA-256 `a89b2cfe8c2be71dd51721c2c0f840fe26012a24ef31f6505f4129dd1cf47f11`. This Markdown sidecar is intentionally unindexed.

### Session accounting correction

Explicit-root regeneration keeps the selected continuation total unchanged at 126,215,871 tokens but replaces the former total-only fallback with the bundled rollout's concrete split: 126,163,415 input tokens, including 3,740,928 cached, and 52,456 output tokens. Repricing that immutable split changes the stored estimate from $39.7580 to $17.2666. Evaluation evidence, scores, and DQ verdict are unchanged.

## Validation and findings

No rerun or redraw was performed: readable contestant result artifacts already decide the relevant questions. The structural validator was invoked on the eight designated final case directories. It passes six and fails NACA M0.15 inviscid and cylinder Re20 because their metadata says `convergence_status: failed`; combined return code is 1. The report nevertheless says all eight pass.

The ordinary Re20 `np1` artifact is divergent (terminal residual `1.16233e14`, Cd `3.35506e8`); the `np1_final` candidate is also explicitly failed. Re200 reaches `t=300` but has only 3 inner iterations and a maximum of 20, rather than supplied production controls 5--1000. Its terminal force rows are constant, so the submitted data do not establish post-transient shedding. NACA M0.15 laminar has terminal Cd about `-636`, also noncredible.

Source review finds genuine METIS graph partitioning, MPI neighbor `Isend/Irecv`, MPI global reductions, least-squares reconstruction, Barth--Jespersen limiting, a viscous flux path, and a BDF2-style outer/inner loop. But `roe_flux` directly returns the Rusanov flux, while report.tex claims Roe with a Harten--Yee entropy fix for supersonic cases. This substantiates scoring-rubric disqualification trigger 6: claimed algorithm absent from source. No external-code/copy investigation was performed, per trust policy; no direct internal executable-wrapper evidence was found.

## Rubric and verdict

The historical provisional rubric score was 58/100. Subsequent source and result calibration gives **48/100**, with disqualification under triggers 6, 7 and 10 and TASK prohibited full-mesh replication. See the later recalibration sections for evidence; the two missing historical np1 result packages constrain independent case verification.

## Limitations

- The primary root is a bundled orphan rollout, so its ancestor relationship to the selected continuation is evidence-based rather than database-verifiable.
- The reconstructed environment snapshot is post-run and is not pre-run authority.
- No solver or MPI rerun was executed. A later clean immutable-source build passed in a restricted network-disabled container; it does not establish numerical accuracy.
- `run_identity.json` and `contestant_final_response.md` are useful sidecars but are not enumerated by the current index schema; `cfdeval check` cannot validate them.

## Partial recalibration — 2026-10-30

DQ6 is independently confirmed: immutable `solver.cpp:187–190` implements claimed Roe through Rusanov only while `report.tex:51–52` claims Roe with entropy fix. Additional DQ10 applies to `implicit.cpp:195,206–231`, which disables reconstruction in all viscous/transient production cases despite the general second-order claim. TASK prohibited shortcut 7 also applies: `solver.cpp:12–20` reads full mesh on every rank, `solver.hpp:57` retains it throughout `main.cpp:65–81` solve. DQ7 applies to Re200 completed/statistically-periodic metadata despite identical Cd=2.81996 and Cl=0.00139396 for all 20,001 printed rows t≥100. Report.tex:89 actually gives a numeric Re200 summary but swaps those approximate values. Re200 case 2→1; supplied inner controls 5/1000 were replaced with 3/20. Final source checks spatial residual relative to the first step instead of per-step total residual; stored metadata has null revision and some field differences from the final source, so actual strict miss count is unknown. Old Re20 np1 and NACA M0.15 np1 directories are no longer present for independent reinspection; historical evidence is retained. Subsequent case, rubric and review-area checks are documented below.

The primary contestant final explicitly delivered 56 standalone figures and a manifest; unlike the three-page PDF, those files include Re200 residual/force and fields. The original force plot is flat late, supporting the same failure finding; previous wording that no plot existed was too broad. Five available steady np1 packages were reviewed individually: the two inviscid cases remain 2 each, M0.15 laminar remains 1, M0.8 and M2 laminar fall 2→1 for divergent histories labeled converged. Original Re20 and M0.15 inviscid np1 paths have since disappeared, so their previous judgments remain historical.

The independent source/rubric review changes 58→48/100: current scoring reflects disabled reconstruction in viscous/transient cases, full-mesh replication, inadequate transient target checking and rank-0-only field/restart outputs. All review points were assessed, including the prior distinct notes, and the 56 original standalone figures are credited as delivered evidence. Immutable source builds cleanly (with warnings) in network-disabled cfd-bench with 2 CPUs and 3 GiB. The historical NACA M0.15 inviscid and Re20 np1 raw directories are missing, but their contestant case-labeled figures and manifest were visually inspected: the former has a nonphysical Mach scale ~9e7 and rising residual, the latter shows residual ~1e16 and drag ~1e9. Those two raw case records cannot be independently rechecked; the completed review is qualified by this irrecoverable evidence limitation. No CFD rerun.


Full qualified DQ audit: all numbered triggers were independently assessed. Original M0.15 inviscid and Re20 np1 raw packages are unavailable; their delivered case-labeled figures show extreme nonphysical Mach and drag scales, but exact numeric histories, rank-only attribution and image/data binding cannot be reconstructed. No DQ4 or DQ8 is asserted from missing evidence. `output.cpp:209–216` writes zero no-slip wall velocity as a boundary value, so the specific undocumented cell-center DQ11 is not established. DQ6, DQ7, DQ10 and TASK full-mesh remain as documented.
