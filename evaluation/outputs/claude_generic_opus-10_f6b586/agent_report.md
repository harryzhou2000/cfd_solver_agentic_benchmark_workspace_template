# Agent Evaluation Report — opus-10

## Calibrated assessment — 2026-10-30

The rubric changes from 99 to **87/100**, with criterion-specific source and numerical evidence in the scorecard. Review-area overalls are recomputed from 27 individually reviewed points. Independent case scores change from 4.5,4.5,4,4.5,4.5,4,4.5,2 to **3,2,2,4,4,2,4,3** in canonical order; these do not feed the rubric or review areas. The accepted contestant PDF supplies the Re200 numeric summary and a plot with visible late lift and drag, contrary to the prior absence note. Its underpredicted shedding and documented t=2–12 inflow perturbation limit case quality.

DQ is **true** under trigger 7 for the M0.8 inviscid case: the reported 2.08-order residual plateau is marked converged without a force-stability check, while last-500-row lift half-window means shift from +0.028976 to −0.007761. TASK.md:172 requires stable forces for a justified plateau. This is separate from Re200, where actual inner target ratios are counted. The M2 laminar report force-table value 0.05097 is the final-quarter mean 0.0509662, but the caption calls it final; the actual final row is 0.0359543. This is a mislabeled averaging window, not evidence of altered data.

Original eight-case/report structural validation passed. An immutable-source clean CMake build passed in network-disabled scratch with configured externals, two CPUs, 4 GiB, 128-process limit and 240-second timeout. No CFD case was rerun; no exhaustive external authorship or pixel-level figure audit was performed. The selected workspace-local Claude root `54c28431-7c49-49b5-898e-22000924875f` and its subagents cover the work through the terminal response. Accepted sibling PDF SHA matches indexed bytes; missing curated figure assets remain a separate reproducibility limitation.

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-10` branch `claude/generic/opus-10` commit `d25cd3cb7c1c75e75812670fd717eca3b61d68ce`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-28T01:43:36.307000+00:00 → 2026-08-28T04:41:41.816000+00:00
- Time: goal unavailable (Claude has no goal timer), wall 10685.5s
- Tokens: 121,129,727 (main 119,862,076 / subagents 1,267,651); cost est. $76.37

### Session analysis (auto)

- Source: project — codex 0 threads / opencode 0 sessions
- Window: 2026-08-28T01:43:36.307000+00:00 → 2026-08-28T04:41:41.816000+00:00
- Idle excluded: 0 gaps, 0s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: unavailable; ask-tool events: 0)
  - limitation: Claude project JSONL does not provide a stable explicit permission-wait contract; idle gaps are not classified
- Whole-session: tokens 121,129,727, cache hit 0.9933, tools 396

### Structural result checks (auto)

- Required cases: 8; found: 12; missing: none
- Figure manifest: {"manifest_present": true, "entries": 57, "missing_figures": [], "missing_sources": ["residuals.csv", "forces.csv", "surface.csv", "field_final.vtu", "field_final.vtu", "residuals.csv", "forces.csv", "surface.csv", "field_final.vtu", "field_final.vtu", "residuals.csv", "forces.csv", "surface.csv", "field_final.vtu", "field_final.vtu", "residuals.csv", "forces.csv", "surface.csv", "surface.csv", "f
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic
- `naca0012_m015_inviscid`: missing=[] completed=True status=converged
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m080_inviscid`: missing=[] completed=True status=converged
- `naca0012_m080_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m200_inviscid`: missing=[] completed=True status=converged
- `naca0012_m200_laminar_re5000`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re20_np1`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re20_np2`: missing=[] completed=True status=converged
- `naca0012_m015_inviscid_np1`: missing=[] completed=True status=converged
- `naca0012_m015_inviscid_np2`: missing=[] completed=True status=converged

## Methodology

Read-only source, terminal-response, raw-package, report-PDF and explicit validator review, plus the isolated clean build described above. No CFD case rerun.

## Disqualification assessment

Trigger 7 is established for M0.8 inviscid as documented below; remaining trigger assessments are recorded in the scorecard.

## Metadata answers & session selection

The selected root and its persisted Claude subagent tree are recorded above; unavailable metadata remains unavailable.

## Limitations

Clean immutable build passed in restricted scratch; no CFD or Re200 rerun.

## Verdict

The M0.8 force-unstable plateau is disqualified under trigger 7; case and method limitations are recorded in the scorecard.

Integrity: `run_identity.json` SHA-256 `b765245146108e6c2f91e808652dd5730eea829135927daf504e6df285ba14d3`; `contestant_final_response.md` SHA-256 `5c5961e7635b3bd813668b015108ac4dd0ff01cd8a4bb02d46eef0846ea5219f`. These unindexed sidecars are not validated by `cfdeval check`.


## Partial Re200 correction — 2026-10-30

The prior note incorrectly said no case numeric summary and no late periodic force plot. The accepted contestant sibling PDF and indexed copy share SHA-256 `4900dabfa70aba691f1dfb3575ab4ae5daa08c5a7c19cbacf63a48164ff222ac`; main TeX matches immutable submission `d25cd3cb7c1c75e75812670fd717eca3b61d68ce`, and the PDF creation time is inside the selected session. PDF page 27 reports mean Cd≈1.1233, lift amplitude ±0.2426 and St≈0.1611; high-resolution inspection of page 28 resolves small late Cd oscillations as well as lift, with startup clipped, and pages 29–30 show the wake. Original t≥150 force mean Cd=1.123268 and Cl range −0.240739–0.244489; 24 interpolated upward mean-lift crossings yield 23 periods averaging 6.20753 (frequency≈0.1611). Score 2→3: the contestant-delivered floor is met, but frequency/drag/amplitude underpredict cited references and grid convergence was not independently demonstrated. Source `fvsolver_run.cpp:485–491` and report.tex:151–153 disclose a 4-degree farfield perturbation only at t=2–12; actual strict inner ratios are counted by source :495–511. This does not assume unforced shedding from the outset. Other cases, rubric and DQ were subsequently reviewed as described above. Missing committed figure assets remain a reproducibility limitation; no Re200 rerun.


## Partial steady-case review — 2026-10-30

The accepted PDF contains per-case summaries and plots for all seven steady cases. Scores in canonical order change **4.5,4.5,4,4.5,4.5,4,4.5 → 3,2,2,4,4,2,4** after checking original forces. M0.8 inviscid reports a 2.08-order plateau but last-10% Cl spans −0.01173 to 0.03139; M2 inviscid reports only 0.60 orders and Cd spans 0.08656–0.09027 (~4.1% of final); M2 laminar last-10% Cd 0.03595–0.04313 spans ~20% of final and differs from report Cd≈0.05097. These indicate incomplete force stationarity/traceability despite structurally accepted packages. Remaining source, rubric and DQ were subsequently reviewed; no CFD rerun.


## Partial steady-status and force-table correction — 2026-10-30

DQ changes false→true under trigger 7 for NACA M0.8 inviscid. `fvsolver_run.cpp:382–399` labels a residual-stall case converged without checking force stability. Original metadata/run status reports converged at 2.08396 orders against the required four; its last 500 lift rows span −0.0117325 to +0.0790092, with first-half mean +0.0289760 and second-half mean −0.0077608. Report.tex:232 says mean forces are steady, while TASK.md:172 requires stable forces for a justified plateau. This finding concerns that steady case and does not invalidate the separately measured Re200 inner-target statistics.

The report table caption calls M2 laminar Cd=0.05097 a final value, but the original final row is 0.0359543. It matches the final-quarter mean 0.0509662 computed by committed `tools/analyze.py`; this is a mislabeled averaging window rather than evidence of altered data. Source/rubric and remaining trigger review were subsequently completed; no CFD rerun.
