# Agent Evaluation Report — codex/dsv4_flash/09

Submission `11adaa1f2ddb33753cb6c9ed5d1c7e9c481050fb` was audited clean against initial `e757c4e0bac4065c4a5ab7ead230b236e4a9984c`; canonical run ID is `codex_dsv4_flash_09_526316`.

## Evidence and session selection

I inspected the curated source/report, compiled the committed report twice in a disposable immutable worktree (pdflatex rc 0; 4 pages), and explicitly invoked the examiner validator on all eight canonical result paths. It stopped at NACA M0.15 because submitted metadata has `convergence_status: failed`. The report itself candidly documents high-CFL instability, laminar divergence, and a stagnation anomaly; no Re200 rerun was performed.

The selected workspace-local Codex root is `019fdf88-1057-7ea1-8471-24edfbc48dc3`, with four descendants, from `.sessions/codex/state_5.sqlite` and its bundled rollout. Its persisted event window is 2026-08-08T04:01:49.402000Z to 2026-08-09T10:00:52.074000Z (execution date 2026-08-08). Terminal message `msg_bc879bbfd8114d91b7b8c16db76aee7b` reports the successful report build.

## Scores and verdict

Provisional score: **53/100** (previously 55); current weighted reviews are in agent_scores.json; DQ remains under review. All detailed rubric, review-point, and per-case evidence is in `agent_scores.json`. Canonical case scores are 1, 1, 1, 0, 0, 0, 1, 0 respectively. This is a modular attempt with source-level method evidence, but it lacks credible completed final results and the validator fails immediately on a claimed canonical output.

## Disqualification and limitations

No disqualification is established: no internal executable wrapper was found; the core reported methods have source counterparts; failed metadata is not misrepresented as success. I did not perform external similarity search under the trust policy. Clean immutable-source CMake configuration was attempted with container dependencies and failed because tracked CMakeLists.txt names missing src/restart.cpp. No CFD evaluator rerun was done; raw results remain excluded from the result branch.

`run_identity.json` SHA-256: `616391f65e0b53a07b3fa7ab90b616e0f9fc7ed80ee530b1a2666b51dd668098`.

`contestant_final_response.md` SHA-256: `44911bcbea7e17fd941dc3b0e642d1d8aaad534904abfe9de713564a23ff93b2`.

Both are unindexed sidecars; `cfdeval check` does not validate them.


## Partial case evidence review — 2026-10-30

All seven canonical steady status files mark failed at step 500. Final Cd of M0.15/M0.8/M2 inviscid is -0.898684, -0.207379, -0.118422; their respective residual reduction orders are 0.2523, 0.2033, 0.1870. All three laminar NACA cases and Re20 end with NaN Cl/Cd. Re200 directory has only field_t1.000.vtu and stdout.log, without status/metadata or force/residual histories. Case scores stay 1,1,1,0,0,0,1,0. The previous M0.15 note falsely asserted no report numeric summary: report.tex:78–101 explicitly gives Cd≈0.11 for a separate 100-step eight-rank short test, not the failed canonical run. Immutable main.cpp:91–99 loads full mesh on every rank during preprocessing but resets it before solver iterations; the TASK iteration-time replication prohibition is not established. Source/rubric/DQ review remains partial. No CFD rerun.


## Partial immutable build review — 2026-10-30

An archive of submission 11adaa1 was configured inside network-disabled cfd-bench with two CPUs/3 GiB and explicit /opt/external dependency paths. CMake finds MPI and gets past dependency checks but cannot generate because CMakeLists.txt:51 requires src/restart.cpp, absent from immutable submission. Workspace has an untracked 15-byte `// placeholder` file, SHA-256 be179fcf7bbc53ebd35461556b67f6c1c77272b540d511bb365a039d5a387cc0, mtime 2026-08-15 02:36:58 UTC after selected session ended 2026-08-09 10:00:52 UTC. Historical raw commit bc8888f contains the same placeholder, but it is not an ancestor of the curated submission; it cannot silently repair the immutable source. Build rubric 5→3 and code.build 3→1. Remaining rubric/DQ review partial; no solver rerun.
