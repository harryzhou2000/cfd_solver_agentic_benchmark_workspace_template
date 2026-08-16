# Agent Evaluation Report — codex_glm52_01_1307c9

- Result branch / commit: `codex/glm52/01` / `17a5ef3c78cda02d6707e1f94858184060318216`.
- Reconstructed initial: `codex/glm52/init` / `838ec1ddee2568f72541f73edafac7497c170495`.
- v2 identity SHA-256: `run_identity.json` `cd291e732efb2f3c3c50f668aab641f2a3939b46afe9711756ebcc22f565fec6`.

## Provenance and sessions

The operator authorized the explicit post-run reconstruction. `env_snapshot.json` says `capture_phase: post_run` and `run_environment_available: false`; initial Git object and ancestry were verified. Exact upstream collision check confirmed `refs/heads/codex/glm52/01` absent locally and at the canonical upstream. The curated 27-path submission audit passed with no prohibited changed paths.

Only workspace-bundled Codex evidence was used. Main root `019fb902-4a31-7af3-89ec-063a8039a580` ran from `2026-07-31T16:29:22.874Z` through terminal final message `msg_6c5511f3b58548629bd195a1a7828b17` at `2026-07-31T19:01:52.878Z`; descendants `019fb92d-6399-7732-a8b6-657d3a073ce5` (plotting) and `019fb92d-9bec-7b12-aa98-32a5cc9697e2` (report) are included. Setup/preliminary roots `019fb8bb`, `019fb8d0`, and unrelated Slurm root `019fb8e4` are excluded. Final-response stored record SHA-256 is `adbbc84efc3bdbb04a68acd3d2be29ecff05cfcae87be8ae05bbaf8a30602366`; extracted text SHA-256 is `b60b29aa9142c991aefe83d13e07ac0d81b829d1412738f4f56d0d71299d2102`; sidecar SHA-256 is `bda47d9eb448f879b3c35a0456450b437e6ec8005dff46fb0c8cb4bf8a1addee`; one text part. The Markdown sidecar is unindexed.

### Session accounting correction

Explicit-root regeneration keeps the selected tree total unchanged at 136,301,271 tokens but replaces total-only fallback accounting with the concrete rollout split. It attributes 130,530,375 tokens to the main root and 5,770,896 to its two descendants, with 136,034,354 input and 266,917 output tokens overall. The current-price estimate changes from $95.4109 to $41.2127. Evaluation evidence, scores, and DQ verdict are unchanged.

## Evidence and findings

No rerun or redraw was performed. Re200 was never rerun. Historical raw results were read from immutable `f9b3d2d86be7691191d72b8b323de4c503a2597a`; they are deliberately excluded from the curated result commit.

Source inspection finds CGNS mesh input, METIS graph partitioning, nonblocking neighbor halo exchange, MPI reductions, Roe/Harten--Yee with Rusanov fallback, Green--Gauss/Barth-Jespersen/positivity, viscous terms, point-implicit LU-SGS-like updates, and BDF2 outer/inner structure. This supports substantial source-level credit.

But Re200 source caps `maxInner` at 15. It declares strict target `1e-3` but increments `innerTargetMisses_` only when ratio is at least 1.0. The recorded Re200 metadata therefore marks `completed` and `statistically_periodic` with last ratio `0.928488`, observed 15 fixed inner iterations, and a misleading 97.56% fraction. The report claims periodic shedding. This is a failed transient solve presented as periodic.

The report also claims all cases converge while its own table lists M0.8/M2 laminar forces of order 1e9--1e10 and negative residual-reduction values, and says Re200 has zero ranks/steps/status unknown. It conditionally references 13 flattened report figures, but only six exact paths exist in historical commit; seven render as `Figure pending` in the curated report. These are not credible report deliverables.

## Score and verdict

Rubric: build/output 7, mesh 8, residual/BC 12, second order 8, viscous 4, implicit/transient 6, MPI 9, cases 1, report/viz 0, extensibility 3: **58/100 before DQ**.

**Disqualified** under trigger 4: Re200 reaches final time while missing inner target on most steps yet is presented as statistically periodic. Trigger 8 is also substantiated: report figures required by the delivered report are absent/inconsistent. No external-copy investigation was performed, per trust policy.

## Limitations

The environment snapshot is post-run only. No clean evaluator build, steady rerun, or redraw was needed to establish these decisive artifact/source contradictions. `run_identity.json` and `contestant_final_response.md` are unindexed, so `cfdeval check` does not validate those sidecars.
