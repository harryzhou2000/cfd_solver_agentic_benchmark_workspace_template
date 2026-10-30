# Agent Evaluation Report — codex_dsv4_flash_03_294b64

- Evaluated at: 2026-08-13T15:55:22.688902+00:00
- Evaluating agent: Codex
- Harness: codex

## Run summary (auto)


### Session analysis (auto)

- Source: project — codex 1 threads / opencode 0 sessions
- Window: 2026-08-02T13:48:57.708000+00:00 → 2026-08-03T15:03:35.090000+00:00
- Idle excluded: 0 gaps, 0s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 503,136,423, cache hit 0.9962, tools 1853

## Methodology

Legacy provenance: initial `codex/dsv4_flash/init` commit `ab943567cd55e707e760531ed7f2c914d09a0537`, reconstructed from clone reflog and migration manifest. Immutable submission is `9772c16083472abe27089e17fe87bb5329e68f4d`; explicit validator returned OK for all eight results and `--report`.

Manual session selection used the sole manifest rollout root `019fc2bb-3f78-7202-a0de-cfa25e061493`, from 2026-08-02T13:48:57.708Z to 2026-08-03T15:03:35.090Z. It has no descendants. Its terminal `final_answer` response item `msg_3d2145b98d5940c8ae8992400faed78f` reports complete build, validation, MPI rank checks and honest Re200/laminar limitations. No Re200 rerun occurred.

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

## Disqualification assessment

DQ7 applies to Re20: original force histories drift materially while status and report claim a stable converged plateau. METIS, neighbor communication and a root-only preprocessing mesh are present; disclosed first-order production modes do not establish DQ10. No affirmative copied-core, executable-wrapper or figure-variable mismatch was found in this review; no exhaustive external authorship or pixel audit was performed.

## Metadata answers & session selection

The migration manifest is authoritative session inventory: one Codex root and no alternate harness candidate. Direct project extraction supersedes automatic aggregate discovery.

## Limitations

The initial environment is reconstructed post-run. The report's figures are referenced through an included TeX file, not directly by `report.tex`; strict immutable audit excluded the PNGs, so this committed report is not self-contained. Re200 reached the horizon but did not demonstrate vortex shedding. No evaluator rerun was needed or performed. `run_identity.json` SHA-256 is `fa21fee5e832be6e8679a61f4a065d2b06f147bfd98848fd5474d2b0bfc7d546`; `contestant_final_response.md` SHA-256 is `5f534388dfa7fc777b3f9c2af03aae1edf214501372ba3fab8d50e871f22892d`. These unindexed sidecars are not validated by `cfdeval check`.

## Verdict

Score: **74/100; DQ7 true** for Re20 false-success plateau. The original 87/100 no-DQ verdict is superseded by the evidence below.

## Partial Re20/Re200 recalibration — 2026-10-30

The accepted sibling PDF SHA-256 `9c9ca05b3cd8884b19028461b4c05b123b4e86184a588c2c8a1ac26906e52e1f` matches indexed bytes and immutable TeX blob `61ca3a2a50982a99887fab725ea53e5f2871395d`. PDF pp14–16 renders Re20 and Re200 force/residual plots and fields despite old committed-only notes; excluded figure assets remain a separate report rebuild limitation. Re20 status converged and report.tex:266–267 describes a stable plateau, but original last 2000 force rows have Cd half means 1.96276→2.12848 and Cl −0.84047→−1.11943. Source solver.cpp:1015–1022 accepts residual stall without checking forces; DQ7 applies and Re20 case 2→1. Re200 report honestly discloses converged-to-steady and absence of shedding; its t≥last10% Cd and Cl ranges are 1.01762162–1.01764043 and 0.193048723–0.19307527, so case remains 1. Do not infer unmeasured strict target misses. This was a partial checkpoint; the full review below supersedes it. No CFD rerun.

Accepted PDF pp8–14 also renders the six NACA cases. After inspection against original histories, M0.8 and M2 inviscid scores rise 2→3 for readable report evidence and broadly credible force tails; their limited residual reduction or disclosed first-order reconstruction prevent higher scores. M0.15 inviscid remains 2 for oscillatory lift and drag; all three laminar scores remain 2 because startup peaks obscure late force curves and residual/physics defects remain. Case-specific values are in the score notes. This was a partial checkpoint; the full review below supersedes it.

## Full calibration — 2026-10-30

Rubric 87→74/100, DQ false→true under trigger 7 for Re20: residual stall is accepted without force checking and the last 2000 force rows drift strongly. All ten rubric sections and 27 review points now have source/result-specific evidence. Case scores 2,2,2,2,2,2,2,1 → 2,3,3,2,2,2,1,1. Accepted sibling PDF genuinely contains case figures; missing curated assets still impair report rebuild. Re200 reaches t300 with a steady wake and the report honestly says shedding was not achieved; no exact strict-target miss count is inferred from the absolute-residual fallback. Production first-order modes are disclosed and do not substantiate DQ10. Root-only preprocessing mesh is freed before solving; original np1/2/4/8 continuation force records agree at common steps. Immutable clean build passed with warnings in network-disabled cfd-bench, 2 CPUs/3 GiB. Both record gates pass; no CFD/unsteady rerun or exhaustive external authorship/pixel audit.
