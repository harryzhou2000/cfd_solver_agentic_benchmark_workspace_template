# Agent Evaluation Report — 04

- Evaluated at: 2026-08-15T04:03:44.683301+00:00
- Evaluating agent: codex
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52-m3/04` branch `codex/glm52-m3/04` commit `8615cddbc8d507f20c559d9659eccbf41d8021f1`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-06T19:19:45.817000+00:00 → 2026-08-09T15:47:17.600000+00:00
- Time: goal 202643.0s, wall 246451.8s
- Tokens: 1,034,234,520 (main 991,993,711 / subagents 42,240,809); cost est. $127.85

### Session analysis (auto)

- Source: project — codex 20 threads / opencode 0 sessions
- Window: 2026-08-06T19:19:45.817000+00:00 → 2026-08-09T15:47:17.600000+00:00
- Idle excluded: 5 gaps, 45830s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 1 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 1,034,234,520, cache hit 0.9476, tools 5058

### Structural result checks (auto)

- Required cases: 8; found: 0; missing: cylinder_m010_laminar_re20, cylinder_m010_laminar_re200, naca0012_m015_inviscid, naca0012_m015_laminar_re5000, naca0012_m080_inviscid, naca0012_m080_laminar_re5000, naca0012_m200_inviscid, naca0012_m200_laminar_re5000
- Figure manifest: {"manifest_present": false}

## Methodology

Evaluated immutable curated submission `8615cddbc8d507f20c559d9659eccbf41d8021f1` against reconstructed initial commit `a14f203babe6794ad032aea760aed8e302e62b5a`. The commit audit passed and confirms that raw case outputs were intentionally excluded. Contestant checkpoint `783eb18534a278714a4f7e2fe9e392038a95e2f2` contains the actual case packages and figures delivered immediately before the selected final response; curated-tip omission does not remove them as contestant evidence. Source and report match the curated tip except for a removed backup. Historical executable attribution is qualified. No Re200 or other unsteady rerun was performed.

The sole project-local Codex root `019fd883-6a9d-7c53-a1b1-96a168fb91ae` and its 19 descendants were selected from the bundled `.sessions/codex` database/rollouts. Its UTC start is 2026-08-06. Terminal final prose is stored in `contestant_final_response.md` (SHA-256 `ef6734a0152173302fdae290d7cc186f7eb8d5a968e9d2fc914f3811c5ae53f9`). The identity sidecar SHA-256 is `6ce469cd7a9a3f78ed74399634ee9a0a15fb6467bdac32fdd3dd116c8bab4d9c`.

## Rubric scores (100 points, SCORING_RUBRIC.md)

| Section | Max | Score | Notes |
|---------|----:|------:|-------|
| Build, CLI, Output Contract | 10 | 7 | Source is credible; final output package is absent from submission. |
| Mesh And Geometry | 10 | 8 | CGNS/unstructured mesh paths are source-supported. |
| Finite-Volume Residual And Boundary Conditions | 15 | 10 | Conservative flux and BC branches inspected. |
| Second-Order Spatial Scheme | 10 | 5 | M2 laminar is reported first-order. |
| Viscous Terms | 10 | 7 | Source-supported, raw wall proof excluded. |
| Implicit And Transient Methods | 15 | 11 | BDF2/inner controls exist; Re200 prose conflicts. |
| MPI | 10 | 3 | Halo/METIS exist; old rank comparison differs in options and evolving source. |
| Case Results And Validation | 10 | 3.5 | Re200 and M015 have major force/convergence failures; remaining cases under review. |
| Report, Visualization, Analysis | 5 | 2 | M015 force figure depicts a different run; Re200 claims conflict with data. |
| Extensibility | 5 | 4 | Reasonable module split. |

## Review-area scores (0-5 weighted, review_*.json)

| Area | Overall | Notes |
|------|--------:|-------|
| Code | 2.83 | Source structure is reasonable, but MPI result dependence is a DQ trigger. |
| CFD methods | 3.24 | Major methods exist; shock/second-order and transient claims are limited. |
| Results | 2.65 | Raw attempt supports limited evidence only; curated submission has no raw packages. |

## Per-case scores (independent 0-5)

Eight contestant checkpoint packages and their available force/residual/field figures were independently inspected. Scores and exact hashes are in `agent_scores.json`; selected M015/M080 inviscid and M015 laminar report force plots depict other runs. The Re200 history has extreme nonperiodic force spikes, while several steady results have force or physical-credibility limits. Historic executable and broader figure binding remain partial.

## Disqualification assessment

DQ remains **true** on triggers 7 and 8. Re200 metadata calls the run statistically periodic despite a final strict target miss and severe nonperiodic force spikes; M015 steady force remains substantially unsettled. The report's M015 force figure depicts an older run rather than the selected final history. The former DQ4 comparison is not controlled: the two recorded launch commands used different CFL/ramp options, and source/binary attribution is uncertain; rank-only causation is not established.

## Metadata answers & session selection

There are no unanswered metadata questions. This is a post-run legacy reconstruction with `run_environment_available=false`; capture-time environment values are not asserted as execution facts. The final-response and identity Markdown/JSON sidecars are intentionally unindexed: `cfdeval check` validates indexed artifacts only, not those sidecars.

## Limitations

The original runtime environment is incomplete. Curated result tip omits raw packages, but contestant checkpoint `783eb18` preserves them and they are considered as contestant-delivered evidence. The approved report PDF is absent pending provenance review. Re200 was not rerun, per policy.

## Verdict

Provisional rubric: **60.5/100**. DQ is true for false successful-result metadata and a specific report-figure/data mismatch. Remaining case, source and PDF review is partial; no rank-only cause is inferred from the old comparison.
