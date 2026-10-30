# Agent Evaluation Report — 14

- Evaluated at: 2026-08-27T13:54:42.105602+00:00
- Evaluating agent: codex
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/14` branch `codex/dsv4_flash/14` commit `05261f847a01729953e3d05caeaf4544c724d77f`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-16T18:11:45.163000+00:00 → 2026-08-18T08:25:31.656000+00:00
- Time: goal 95297.0s, wall 137626.5s
- Tokens: 1,653,739,626 (main 1,051,317,722 / subagents 602,421,904); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 11 threads / opencode 0 sessions
- Window: 2026-08-16T18:11:45.163000+00:00 → 2026-08-18T08:25:31.656000+00:00
- Idle excluded: 3 gaps, 42298s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 1,653,739,626, cache hit 0.9955, tools 5251

### Structural result checks (auto)

- Required cases: 8; found: 299; missing: none
- Figure manifest: {"manifest_present": true, "entries": 50, "missing_figures": [], "missing_sources": ["results/naca0012_m015_inviscid/forces.csv", "results/naca0012_m015_inviscid/field_final.vtu", "results/naca0012_m015_inviscid/field_final.vtu", "results/naca0012_m015_inviscid/field_final.vtu", "results/naca0012_m015_inviscid/residuals.csv", "results/naca0012_m015_inviscid/surface.csv", "results/naca0012_m080_inv
- `m015_dump18`: missing=[] completed=True status=failed
- `m015_dump3`: missing=[] completed=True status=failed
- `m015_inner5`: missing=[] completed=True status=failed
- `m015_nofreeze`: missing=[] completed=True status=failed
- `m015_point`: missing=[] completed=True status=failed
- `m015_pwall`: missing=[] completed=True status=failed
- `m015_simpleff`: missing=[] completed=True status=failed
- `m015_trace`: missing=[] completed=True status=failed
- `m080_trace`: missing=[] completed=True status=failed
- `backup_re200_rusanov`: missing=[] completed=True status=statistically_periodic
- `bx2_venkat`: missing=[] completed=True status=failed
- `bx_none`: missing=[] completed=True status=failed

## Methodology

I read the contestant final response, task/contract/rubric, committed source and the untracked final result packages read-only. The explicit validator was run against the eight named `solver/results/<case>` directories and `solver/report`, returning exit 0. I inspected the rendered 41-page PDF and representative M2 Mach and Re200 vorticity figures, source locations for METIS, MPI halo exchange, residual reductions, and BDF2, and result CSV/JSON terminal values. I did not rerun any solver case; in particular Re200 was not rerun.

The identity sidecar SHA-256 is `b7b99a32688ad8ed7fa88d76f79cccf5bee8bff7f903f0d8769d03166765016a`; the contestant final-response sidecar SHA-256 is `0e89ec7e8cfeb9d8b1db4c71711e61c2691a744a2937953f26341d9236128b17`. These are deliberately unindexed sidecars, so `cfdeval check` does not validate either one.

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

Not disqualified. Source/result review found no invocation of another solver, no generated-without-solve shortcut, no filename-only mesh dispatch, no full-state Allgather, no explicit-only implementation, no unsupported claimed algorithm, and no successful label for a marked failed final case. METIS partitioning (`solver/src/partition.cpp`) and neighbor `MPI_Isend/Irecv` exchange (`solver/src/solver.cpp`) are direct evidence. The report manifest maps figures to source files, and inspected Mach/vorticity filenames match their displayed variables. Per trust policy, no external authorship search was performed.

## Metadata answers & session selection

Only workspace-local `.sessions/codex` telemetry was used. Root `01a00bc5-383f-7722-be28-d3f43d0ca3df` begins the benchmark objective; the ten listed child roots are its recorded sequential implementation/audit continuations. Terminal response comes from the completed `task_complete` record in `01a013cc-4d6d-7a72-92b6-d7e503c3237f` at 2026-08-18T08:04:52.506Z. Start time is the root's first persisted event, 2026-08-16T18:11:45.163Z, giving execution date 2026-08-16 UTC.

## Limitations

No independent evaluator build or steady rerun was performed. The raw result packages and generated PDF are preserved as workspace evidence but excluded from the curated result-branch tip by the artifact policy. Three production cases use documented `CFD_RECON_SCALE=0.5`; M2 inviscid additionally uses `CFD_GRAD_DAMP=0.5`, which is not disclosed by the report's reconstruction/limitations discussion. Re200's 2% asymmetry seed was not sensitivity-tested.

## Verdict

87.5/100, DQ=false. This is a substantively strong complete package with real source/result/MPI evidence and credible Re200 transient output. Deductions reflect reconstruction blending, the undisclosed M2 gradient damping, and limits of independent rerun verification, not a structural-validator failure.


## Partial Re200/source provenance review — 2026-10-30

Canonical results/cylinder_m010_laminar_re200/forces.csv SHA-256 bd62af0571b755f2c52b8bd3e51bb3353fbfd35a39e01cb94c1c7f3826bec36b has 30001 rows to t300; t≥200 mean Cd 1.2323515, Cd range 1.14503–1.32126, Cl range -1.05970–1.06052. Positive lift crossings at 200.465, 205.186, 209.911 through 299.686 have periods 4.720–4.729, giving frequency≈0.212, consistent with results_summary.tex:24 St≈0.211. Previous note erroneously called final time 300 a shedding period. Committed force figure shows lift but startup Cd≈30 compresses late drag; no separate late Cd plot in report figure inventory, so Re200 score stays 2.

Curated solver.cpp:1875–1950,2240–2311 implements BDF1 startup, BDF2 and fresh-gradient total residual acceptance with miss counting. Canonical metadata SHA f05c949dccda64e869040de0a63fda12719ed121eb49533972a76a7c1767d2cc reports zero misses/fraction 1.0, but git_revision=null; contestant final verification explicitly mentions pre-existing uncommitted solver.cpp edits. Curated and c10299c versions differ by 21 steady-statistics lines, not transient code, but that comparison does not prove the exact production binary source. Thus actual strict misses remain unverified rather than assumed fabricated. main.cpp:287–320 constructs GlobalMesh inside rank-0 scope, which ends before solver invocation; no all-rank full-mesh prohibition finding. Other cases, source/rubric/DQ remain partial; no CFD rerun.


## Partial steady case and rank comparison review — 2026-10-30

Case scores 4,4.5,4,4.5,4,4,4,2 → 3,3.5,3.5,2,2,2,2,2. Canonical histories, force tails and figure limits are documented individually in agent_scores.json. The three inviscid report figures show readable force evolution and case-specific residual/field contours; four viscous steady force graphs are dominated by Cd startup spikes around 160–320, making their late force histories unreadable, with no alternate contestant-delivered late plots located. Metadata git_revision=null and terminal verification reports uncommitted source edits; exact production binary source is not established. Report tables and original rankcmp results agree: NACA M0.15 np1/8 Cd 0.03473845/0.03438938, Cl -0.00012298/0.00381635; Re20 Cd 2.12662443/2.10340547, Cl 0.00570481/0.00554458. The ~1.1% cylinder drag difference is not DQ4 order-one. Full source/rubric/DQ review remains partial; no CFD rerun.
