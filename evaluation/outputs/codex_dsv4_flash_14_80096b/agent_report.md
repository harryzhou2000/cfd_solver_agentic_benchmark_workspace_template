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

## Scores

Qualified rubric **79.5/100** (previously 87.5); detailed sections, weighted review scores and independent case scores are in `agent_scores.json`. All applicable sections, cases, review points and DQ triggers have been examined; producing-binary and rank-control provenance remain unresolved.

## Disqualification assessment

No DQ trigger is established from available evidence; each rule has a specific note in agent_scores.json. Re200 strict target misses and producing binary identity remain unverified, so this is not a complete exoneration. METIS, root-only mesh and neighbor communication are source-evident.

## Metadata answers & session selection

Only workspace-local `.sessions/codex` telemetry was used. Root `01a00bc5-383f-7722-be28-d3f43d0ca3df` begins the benchmark objective; the ten listed child roots are its recorded sequential implementation/audit continuations. Terminal response comes from the completed `task_complete` record in `01a013cc-4d6d-7a72-92b6-d7e503c3237f` at 2026-08-18T08:04:52.506Z. Start time is the root's first persisted event, 2026-08-16T18:11:45.163Z, giving execution date 2026-08-16 UTC.

## Limitations

A clean immutable-source CMake build passed with warnings in the network-disabled cfd-bench container; no CFD rerun was performed. The raw result packages and generated PDF are preserved as workspace evidence but excluded from the curated result-branch tip by the artifact policy. Three production cases use documented `CFD_RECON_SCALE=0.5`; M2 inviscid additionally uses `CFD_GRAD_DAMP=0.5`, which is not disclosed by the report's reconstruction/limitations discussion. Re200's 2% asymmetry seed was not sensitivity-tested.

## Verdict

Qualified **79.5/100, DQ=false**; no trigger is established by the available evidence. Original histories and report remain evidence, with historical production-source/binary and rank-control provenance limitations.


## Partial Re200/source provenance review — 2026-10-30

Canonical results/cylinder_m010_laminar_re200/forces.csv SHA-256 bd62af0571b755f2c52b8bd3e51bb3353fbfd35a39e01cb94c1c7f3826bec36b has 30001 rows to t300; t≥200 mean Cd 1.2323515, Cd range 1.14503–1.32126, Cl range -1.05970–1.06052. Positive lift crossings at 200.465, 205.186, 209.911 through 299.686 have periods 4.720–4.729, giving frequency≈0.212, consistent with results_summary.tex:24 St≈0.211. Previous note erroneously called final time 300 a shedding period. Committed force figure shows lift but startup Cd≈30 compresses late drag; no separate late Cd plot in report figure inventory, so Re200 score stays 2.

Curated solver.cpp:1875–1950,2240–2311 implements BDF1 startup, BDF2 and fresh-gradient total residual acceptance with miss counting. Canonical metadata SHA f05c949dccda64e869040de0a63fda12719ed121eb49533972a76a7c1767d2cc reports zero misses/fraction 1.0, but git_revision=null; contestant final verification explicitly mentions pre-existing uncommitted solver.cpp edits. Curated and c10299c versions differ by 21 steady-statistics lines, not transient code, but that comparison does not prove the exact production binary source. Thus actual strict misses remain unverified rather than assumed fabricated. main.cpp:287–320 constructs GlobalMesh inside rank-0 scope, which ends before solver invocation; no all-rank full-mesh prohibition finding. Other cases, source/rubric/DQ remain partial; no CFD rerun.


## Partial steady case and rank comparison review — 2026-10-30

Case scores 4,4.5,4,4.5,4,4,4,2 → 3,3.5,3.5,2,2,2,2,2. Canonical histories, force tails and figure limits are documented individually in agent_scores.json. The three inviscid report figures show readable force evolution and case-specific residual/field contours; four viscous steady force graphs are dominated by Cd startup spikes around 160–320, making their late force histories unreadable, with no alternate contestant-delivered late plots located. Metadata git_revision=null and terminal verification reports uncommitted source edits; exact production binary source is not established. Report tables and original rankcmp results agree: NACA M0.15 np1/8 Cd 0.03473845/0.03438938, Cl -0.00012298/0.00381635; Re20 Cd 2.12662443/2.10340547, Cl 0.00570481/0.00554458. The ~1.1% cylinder drag difference is not DQ4 order-one. Full source/rubric/DQ review remains partial; no CFD rerun.


## Partial build and rubric review — 2026-10-30

Immutable 05261f8 clean CMake build succeeds with warnings in network-disabled cfd-bench, 2 CPUs/3 GiB and /opt/external; no solver run. io.cpp:61–65 writes only current state despite history parameters, and solver.cpp:1872–1873 resets both previous histories and the transient loop starts from physical step 1 after restart; the continued transient is not a faithful BDF2 restart. solver.cpp:487–542 projects viscous wall traction tangentially. Provisional rubric 87.5→82.5: implicit/transient 13→11 for restart limitations, results 9→7 for case/source uncertainty, report 4→3 for compressed force plots and undisclosed M2 gradient damping. Further criteria/DQ remain partial; both gates pass.


## Partial source and provenance boundary — 2026-10-30

All ten rubric sections and 27 review points now have criterion-specific evidence; provisional rubric 82.5→79.5 after residual 14→13, second-order 7→6 and MPI 9→8 to reflect production source uncertainty and disclosed reconstruction controls. DQ assessment covers each numbered rule, but cannot conclusively evaluate production-specific behavior without binding the actual binaries. Re200 metadata says it ran 2026-08-18 01:30:47–04:18:32 UTC with git_revision=null; a151896 was committed at 01:27:55 UTC, while 3ec9924 (04:51:49 UTC) and a3ef6bd (05:17:30 UTC) subsequently changed numerical code. The tracked a151896 versus curated 05261f8 diff spans 225 added/31 removed lines across four solver files, including residual/reconstruction options. Current build/cfd_solver was rebuilt at 08:11:42 UTC after the Re200 job ended. These timestamps constrain provenance but do not establish the source of the production executable. Selected verification also reports uncommitted source/tool edits at HEAD c10299c. Seven steady run timestamps span 01:30–05:59 UTC and likewise have git_revision=null. Further review requires actual production binary/build provenance or an authoritative saved source/binary artifact; retain partial status. Both gates pass; no CFD rerun.


## Qualified review and steady-plot correction — 2026-10-31

Independent visual comparison with glm53-04 shows the three NACA laminar force plots merge both curves into the startup-dominated baseline, unlike the separately resolved lift panels in that precedent; their 2-point floor caps remain. Re20 force figure at 05261f8 has blob `3ec9879a92e56b89a763611e7e0ff57dac4ca83a` and visibly shows drag settling above lift even though a startup spike reduces late resolution. `results_summary.tex:21` gives Cd 2.10341/Cl 0.00554, residual reduction 6.97 orders, and references its force/residual/wake figures. The final 10% of original drag rows spans 2.10338502–2.10340547. The steady floor does not demand a late-drag zoom, so the earlier automatic cap was inconsistent: Re20 2→3.5, retaining deductions for scale and numerical/provenance limitations. Re200 has a separate explicit late-lift/drag requirement and remains 2.

Ten sections, eight cases, 27 review points and 13 DQ rules have now been reviewed to the available evidence limit. Original run metadata has null git_revision; current build and verify executables were rebuilt after the Re200 run, and earlier ASan/UBSan binaries do not establish the production executable. Git tracked history contains no archived `cfd_solver` binary. The actual historic binary and exact rank-test controls remain unbound; no claim that final source produced the results is made. No CFD or Re200 rerun.
