# Agent Evaluation Report — vllm-qwen38-02

- Evaluated at: 2026-08-27T15:21:06.296064+00:00
- Evaluating agent: codex
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/vllm-qwen38-02` branch `codex/generic/vllm-qwen38-02` commit `84ccf503fa1dcb716fbbf830f6645288a488bb9b`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-22T05:40:14.992000+00:00 → 2026-08-25T13:08:45.197000+00:00
- Time: goal 222054.0s, wall 286110.2s
- Tokens: 455,190,499 (main 336,955,344 / subagents 118,235,155); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 10 threads / opencode 0 sessions
- Window: 2026-08-22T05:40:14.992000+00:00 → 2026-08-25T13:08:45.197000+00:00
- Idle excluded: 26 gaps, 109278s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 455,190,499, cache hit 0.9608, tools 3982

### Structural result checks (auto)

- Required cases: 8; found: 215; missing: none
- Figure manifest: {"manifest_present": true, "entries": 49, "missing_figures": [], "missing_sources": ["results/cylinder_m010_laminar_re200/surface.csv", "results/cylinder_m010_laminar_re200/surface.csv", "results/cylinder_m010_laminar_re200/forces.csv", "results/cylinder_m010_laminar_re200/field_final.vtu", "results/cylinder_m010_laminar_re200/field_final.vtu", "results/cylinder_m010_laminar_re200/residuals.csv", 
- `diag_re200_base`: missing=[] completed=False status=failed
- `diag_re200_beta0`: missing=[] completed=False status=failed
- `diag_re200_beta1`: missing=[] completed=False status=failed
- `diag_re200_ext3000`: missing=[] completed=False status=failed
- `diag_re200_fix6`: missing=[] completed=False status=failed
- `diag_re200_fix8`: missing=[] completed=True status=statistically_periodic
- `diag_re200_fix_bjonly`: missing=[] completed=False status=failed
- `diag_re200_resdump`: missing=[] completed=False status=failed
- `diag_re200_resdump40`: missing=[] completed=False status=failed
- `exp_cylre20_cfl05`: missing=[] completed=False status=failed
- `exp_m015_cap`: missing=[] completed=False status=failed
- `exp_m015_cap25`: missing=[] completed=False status=failed

## Methodology

Read immutable source/report and all eight raw packages; explicit examiner invocation passed. The audited result tip is `84ccf503fa1dcb716fbbf830f6645288a488bb9b`; raw probes/fixtures were excluded index-only. No evaluator rerun was performed, including no Re200 rerun.

## Rubric scores (100 points, SCORING_RUBRIC.md)

| Section | Max | Score | Notes |
|---------|----:|------:|-------|
| Build, CLI, Output Contract | 10 |  |  |
| Mesh And Geometry | 10 |  |  |
| Finite-Volume Residual And Boundary Conditions | 15 |  |  |
| Second-Order Spatial Scheme | 10 |  |  |
| Viscous Terms | 10 |  |  |
| Implicit And Transient Methods | 15 |  |  |
| MPI | 10 |  |  |
| Case Results And Validation | 10 |  |  |
| Report, Visualization, Analysis | 5 |  |  |
| Extensibility | 5 |  |  |

## Review-area scores (0-5 weighted, review_*.json)

| Area | Overall | Notes |
|------|--------:|-------|
| Code |  |  |
| CFD methods |  |  |
| Results |  |  |

## Per-case scores (independent 0-5)

These scores do not contribute to the review-area scores or the 100-point rubric total.

| Case | Score | Notes |
|------|------:|-------|
| NACA0012 M0.15 inviscid |  |  |
| NACA0012 M0.80 inviscid |  |  |
| NACA0012 M2.00 inviscid |  |  |
| NACA0012 M0.15 Re5000 |  |  |
| NACA0012 M0.80 Re5000 |  |  |
| NACA0012 M2.00 Re5000 |  |  |
| Cylinder M0.10 Re20 |  |  |
| Cylinder M0.10 Re200 |  |  |

## Disqualification assessment

DQ=true, trigger 4: README lines 180-187 documents exact M2 laminar `np=8` production convergence while explicitly admitting the same first-order pinned-CFL configuration is ordering-sensitive at `np=4`. Source otherwise supports METIS/neighbor halos/implicit/BDF2; structural validation passing does not cure the rank-dependent steady result.

## Metadata answers & session selection

No unanswered metadata questions. Selected root `01a027fa-eaf9-77b3-97ad-e7b14452aa8d` plus nine descendants from workspace-local Codex telemetry. `run_identity.json` SHA-256: `807845d81cfc213edf863236046e52ed26bd5b0a8506b05fcca5db0192ace169`; `contestant_final_response.md` SHA-256: `0c3bb0847e32ac09329fbe0e8dee53dc66035d47ca0d936a7fce266b5ad9f3c7`. Both are unindexed sidecars and are not validated by `cfdeval check`.

## Limitations

No evaluator clean build or MPI rerun. M2 laminar uses first-order B0VOL in 18353/20816 cells; several steady results are documented plateau stops; Re200 was not rerun and lacks a resolved street.

## Verdict

Score: **82/100; disqualified: true.** Strong source/report coverage and structurally complete packages are outweighed by the directly documented rank-sensitive M2 production configuration and major physics/convergence limitations.
