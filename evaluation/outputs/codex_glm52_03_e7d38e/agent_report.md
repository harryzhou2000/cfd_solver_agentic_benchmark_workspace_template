# Agent Evaluation Report — 03

- Evaluated at: 2026-08-15T06:12:23.761057+00:00
- Evaluating agent: codex-evaluator
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52/03` branch `codex/glm52/03` commit `9532e19d7552900782e8835c441cbed2303178f5`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-01T16:28:54.124000+00:00 → 2026-08-01T19:59:29.319000+00:00
- Time: goal 10600.0s, wall 12635.2s
- Tokens: 138,043,451 (main 124,783,892 / subagents 13,259,559); cost est. $14.93

### Session analysis (auto)

- Source: project — codex 12 threads / opencode 0 sessions
- Window: 2026-08-01T16:28:54.124000+00:00 → 2026-08-01T19:59:29.319000+00:00
- Idle excluded: 0 gaps, 0s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 138,043,451, cache hit 0.9886, tools 851

### Structural result checks (auto)

- Required cases: 8; found: 0; missing: cylinder_m010_laminar_re20, cylinder_m010_laminar_re200, naca0012_m015_inviscid, naca0012_m015_laminar_re5000, naca0012_m080_inviscid, naca0012_m080_laminar_re5000, naca0012_m200_inviscid, naca0012_m200_laminar_re5000
- Figure manifest: {"manifest_present": false}

## Methodology

Immutable curated submission `9532e19d7552900782e8835c441cbed2303178f5` was audited against reconstructed initial `de4960b2c72f0217b8e526539e02badbeebae9ca`. A detached temporary worktree at immutable attempt `0305e4500d5c62cf325680114e794ba1d97a4e9b` was validated explicitly: all eight canonical case directories and report returned OK. Those historical contestant-delivered raw results are evidence and were not copied into the submission or snapshot. Selected project-local Codex root is `019fbe25-8c45-7f20-a5e8-9501b5d23ba3`; terminal final response is preserved exactly.

## Calibration verdict

Provisional rubric 68/100, disqualified under trigger 7. The case and review scores are recorded in agent_scores.json; the broader review remains partial.

Immutable contestant workspace Git attempt 0305e4500d5c62cf325680114e794ba1d97a4e9b metadata SHA256 8d7ecfc0402e458b04753158bfd0a60386cfca231a3cd3eea73a2841c1b0d3d0 reports completed/statistically_periodic, target0.001, fraction1, zero misses and last ratio1.082751244. Attempt report.tex:149–156 candidly says no vortex shedding; immutable force CSV SHA256 a01cd785e7c748d82b950aef81e5514cce03177a84131f19993f7c6ffb88c9d3 t180–300 Cd4.022308..4.052735, Cl−0.000918793..−0.000368475 confirms nearly steady force. Curated 9532e19d solver.cpp:672–686 accepts any per-iteration reduction or eight iterations as converged; :731–734 labels transient periodic unless diverged; :797–800 counts this as strict target success. Source matches attempt aside from removed .bak. Final ratio directly misses strict target, while full strict miss count and historical binary are unbound.

## Metadata answers & session selection

No metadata questions remain. The sole continuous project-local Codex root and descendants were selected; start date is 2026-08-01 UTC. The legacy env snapshot is a post-run reconstruction with unavailable original runtime.

## Limitations

Curated submission excludes raw output packages, manifests and PDF. The original run environment is unavailable; raw validation is from an immutable temporary attempt worktree. No solver rerun was performed, and Re200 was never rerun.

## Verdict

The attempt structurally validates all eight cases and includes substantive source/report material, but delivered Re200 evidence admits no vortex shedding and the curated submission cannot itself reproduce raw results. The independent score layers record those limitations.

Identity sidecar SHA-256: `25d332395b95e073c4f1a151feeb81b18fb6dc0b81214e208c757c924d573077`; final-response sidecar SHA-256: `035c606e6a20e27d6d06c83d42545ba99a3e7ec232a8ddcf91216cd3632bee7d`. These sidecars are unindexed, and `cfdeval check` validates indexed JSON artifacts rather than them.

Historical contestant attempt 0305e450 contains main PDF SHA256 4512e198581b136bf025d12cfb5617ce21afac35876507a3cfdaf8a3fdc8bc92; all four readable pages were visually reviewed as contestant-delivered evidence, separately from the approved snapshot. No sibling PDF exists in the curated workspace, and a clean immutable 9532e19d latexmk build fails at results_table.tex:1 due unescaped underscore (Missing $ inserted). The historical PDF embeds text/tables but no figures; 51 standalone attempt figures remain separate evidence.

The historical attempt supplies final CL/CD in sanity_checks.json and standalone case force/residual/field images, contrary to the prior blanket missing-evidence rationale. All eight force, residual and Mach plots were visually reviewed: NACA Mach/pressure images are sparse full-domain scatter with the airfoil almost invisible, and cylinder field views collapse to a narrow strip. Case notes record individual hashes, late windows and the remaining visualization floor. Re200 remains 1 for no shedding and no readable late lift/drag; scores for the other seven remain 2.

Clean immutable archive builds in network-disabled cfd-bench with bundled externals (2 CPUs, 3 GiB, 256 processes); CTest registers no tests. Restart handling returns on rank0 before collective broadcast for a missing/invalid file and can hang peers; valid restart omits time and BDF history. Wall shear is tangentially projected. Re200 Mach figure is pixel-identical to regeneration of the contestant plot code, which assigns 10,173 of 10,185 plotted triangle positions values from wrong parent cells. This is a concrete visualization defect; DQ8 classification remains under review because the historical main PDF embeds no figures.
