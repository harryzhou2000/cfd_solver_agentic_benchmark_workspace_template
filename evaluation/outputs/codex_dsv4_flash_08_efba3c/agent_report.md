# Agent Evaluation Report — codex/dsv4_flash/08

Submission: `e5d4a7a8a595c9e19db2e9551cfc393b9e646dba` on `codex/dsv4_flash/08`; immutable identity: `codex_dsv4_flash_08_efba3c`. The collision gate queried the canonical manager upstream directly: the workspace origin remained absent, the result ref was absent locally and upstream, and the branch was available.

## Methodology and session attribution

I audited the immutable submission delta against initial commit `e757c4e0bac4065c4a5ab7ead230b236e4a9984c`; the audit passed with 31 allowed curated files. I inspected `solver/src` for the numerical, MPI, and output paths, read the committed report, and compiled it twice with `pdflatex` in a disposable worktree at the immutable submission commit (rc 0, 11 pages). I ran the examiner validator explicitly on all eight final directories and the report. The seven steady directories passed; Re200 failed exactly because its metadata says `completed=false`.

The manually selected project-local Codex root is `019fdf7d-fd63-79b1-99a9-61a5ea0a6156`, using `workspace/codex/dsv4_flash/08/.sessions/codex/state_5.sqlite` and the matching bundled rollout. It records `/workspace`, the authoritative initial branch, and the benchmark work; its 20 direct spawned descendants cover implementation and reporting. Start/end events are 2026-08-08T03:51:01.200000Z and 2026-08-09T10:02:06.456000Z, so execution date is 2026-08-08. The terminal root message is `msg_5d75e59d0c6a41c190e9f8f6a0e628ca`, reporting successful report compilation.

## Scores

Calibrated rubric total: **71/100** (previously 76). Section scores: build/CLI/output 7/10; mesh 9/10; residual/BC 13/15; second order 8/10; viscous 7/10; implicit/transient 9/15; MPI 6/10; results 5/10; report 3/5; extensibility 4/5. Detailed evidence and all section notes are in `agent_scores.json`.

Weighted review areas are recorded in agent_scores.json and regenerated summaries. Current per-case scores are M0.15 inviscid 3, M0.80 inviscid 3, M2 inviscid 3, M0.15 laminar 2, M0.80 laminar 2, M2 laminar 1, Re20 3, and Re200 0. The M2 first-order limitation and Re200 inner-loop failure are both candidly documented in the submitted report. No submitted np=8 NACA/cylinder comparison is present.

## Disqualification assessment

DQ7 is established for the M2 laminar final status, which claims convergence at step 33 despite rapidly changing drag; see the partial evidence review below. Source contains METIS partitioning, neighbor exchange, global reductions, LU-SGS, and BDF2 dual-time code, so the corresponding shortcut flags are not supported. Re200 is truthfully incomplete rather than claimed successful. Under the trust policy I did not perform an external similarity search; there is no internal wrapper/executable evidence. Figure semantics and solver output were not fully independently re-derived, so they are limitations rather than affirmative shortcut findings.

## Limitations and sidecars

A clean immutable-source solver build succeeds in the network-disabled cfd-bench container using /opt/external; the delivered external symlink itself is dangling. No CFD or unsteady Re200 rerun was performed. Raw result data were used only for validation and remain excluded from the curated result commit. `run_identity.json` SHA-256 is `5340bfba43c31c9cdb82946acb55c027ba3aae4bc8f554f8bcc9801000de8c35`; `contestant_final_response.md` SHA-256 is `58fc59a37fe51e4ae0857bc6563e0844bf518a89a8b2dc9e3f3cfd6f355c6359`. These are unindexed sidecars: `cfdeval check` does **not** validate them.

## Verdict

The solver has seven structurally valid steady result packages and an honestly failed Re200 package. M2 laminar is incorrectly marked converged while drag is still changing rapidly; further calibration remains in progress.

## Partial Re200 evidence review — 2026-10-30

Designated case metadata and status explicitly mark failed/completed=false at step 6889, t=68.9/300; force history remains grossly nonphysical, Cd approximately 36.68 initially and 29.98 by t≈68.79, so score remains 0. Report.tex:195–196 and :452–455 incorrectly say failure occurred at the first step, while the run manifest and actual status give t=68.9. Metadata says observed inner min/max 1000/1000, yet residuals.csv contains many smaller inner counts (including 5 and 10), and last ratio is null. Immutable driver.cpp:825–826,861 would not count a NaN ratio as a miss; the precise actual strict miss count is unknown. This is a traceability/accounting defect, not an assertion of a falsely successful final status. Source/rubric and other case review remain partial; no CFD rerun.

## Partial M2 laminar result review — 2026-10-30

The submitted M2 laminar status and metadata mark converged at step 33, reporting 3.005 residual-reduction orders. The submitted forces.csv shows Cd 0.25879026082, 0.24105224449, 0.22615804657 and 0.21366941568 at steps 30–33: a 17.44% fall over those steps and 5.52% on the last step. TASK.md:140 requires a configured max_steps budget of at least 40000 unless convergence is clearly achieved earlier; :172 requires a converged state or a justified plateau with stable forces. Meeting the residual target does not establish a settled result here. Case score 2→1, and DQ7 applies to the false convergence status. M2 first-order operation is disclosed at report.tex:273–282,490. Figure provenance, other cases and rubric remain under review. No CFD rerun.

## Partial steady case review — 2026-10-30

Scores 2,4,2,4,4,2,4,0 → 3,3,3,2,2,1,3,0. Each numerical basis and limitation is in case_scores. The accepted compiled report contains four steady cases; original workspace solver/report/figure_manifest.csv maps other delivered case figures to forces.csv, residuals.csv and field_final.vtk. Its SHA-256 is 5d32a9f362a27b2696be277619803fba325208a0986a4a577dfb7d02da5721e3; M0.15 and M2 inviscid force-figure SHA-256 values are db40f09eb9b79f4a49ea678e37c2588dcafe5da495976129898fcb33f4cc55f4 and ffc3bb281d45e61a095a98796caa40cb6b1b04183c63a40d477df6c4266983d9. Figure timestamps 2026-08-09 07:35 UTC precede selected terminal response 10:02 UTC, which explicitly reports regenerated figures; figures were visually checked against case values. Prior cap based solely on absent committed/referenced plots is not the current contestant-delivered evidence standard. Full source/rubric remains partial; no rerun.

## Partial source review — 2026-10-30

Immutable e5d4a7a clean CMake build succeeds in network-disabled cfd-bench with two CPUs, 3 GiB, -DCFD_EXTERNALS_ROOT=/opt/external/cfd_externals/install and -DEXTERNALS_DIR=/opt/external; the initial attempt without these explicit container dependency paths failed to find CGNS. driver.cpp:997–1058 receives restart_file but never uses it; the existing reader at :267 is not called, so --restart is accepted without restoring state. report.tex:516 acknowledges proper transient restart as future work. numerics.cpp:1130–1173 includes full viscous traction in the viscous force split, whereas surface skin friction at :713 is tangential; this is a force-split accuracy limitation, not evidence of an absent algorithm. mesh.cpp:771–773 clears the root-built global mesh before iterations. Remaining source, rubric and DQ review partial; no solver run.

## Full calibration — 2026-10-30

All ten rubric sections and 27 review points now include criterion-specific evidence. The case scores and DQ7 finding are detailed above. Clean immutable build passed with explicit container dependencies; CLI restart is accepted but ignored. Root-built mesh is cleared before iterations, METIS and halo exchange are implemented, and report explicitly discloses M2 first-order mode. Full viscous traction is used as the viscous force split; NaN transient ratio can escape miss counting, but true strict miss count is unknown and Re200 status itself is failed. driver.cpp:1070–1116 includes a linear LSQ diagnostic that reports error but returns success unconditionally. No solver rerun, external authorship search, exhaustive pixel audit, or independent rank comparison was performed. Both structural/completion gates pass.
