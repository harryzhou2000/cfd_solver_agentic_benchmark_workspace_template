# Agent Evaluation Report — oc-goal_kimik3-dsv4_01_940dcb

- Immutable submission: `f12b1cda2191dbc3e1361d45a309dad3cccd2172` on `oc-goal/kimik3-dsv4/01`.
- Provenance: `oc-goal/kimik3-dsv4/init` / `0a115802fc419a5302c2f8812118822194ba1bc8`.
- Identity sidecar SHA-256: `bfe278e412dae02ff7176976724e769193e3deaae589ff55a43652c32992d82f`.
- Final-response sidecar SHA-256: `dc45eeff60da44e4652bf40f482ee7c361d8d0b7b30607fb163ce6b4d3ddafb3`.

## Telemetry and final response

Manual project-local selection identifies OpenCode root `ses_035e8dbceffet6BW5uUTTrVRkJ`; two unrelated context-window sessions were excluded. `contestant_final_response.md` preserves terminal assistant message `msg_fcddaf141001SR2ELJEyC2Cwt1`. Aggregate telemetry remains contaminated by the excluded candidates, so it is not used for attributable costs or timing. The identity and final-response files are unindexed sidecars and are not validated by `cfdeval check`.

## Evidence and validation

The recorded explicit, read-only examiner invocation over all eight original workspace packages and the report passed. No Re200 run was performed. Each canonical package has required files and finite final CSV rows. The final statuses are completed/converged except NACA M0.15 laminar, which is completed/statistically-periodic; Re200 is completed/statistically-periodic with BDF2 dual-time metadata. The report's 60-entry figure manifest resolves figures without missing figure files.

Committed source supports generic CGNS input, METIS k-way partitioning, halo exchange/reductions, conservative finite-volume residuals, least-squares reconstruction, limiter/positivity safeguards, viscous terms, LU-SGS implicit marching, and BDF2 transient integration. Mixed final rank counts (np2/np4/np8) do not provide a controlled rank-only comparison. The report is technically detailed and candid about plateau/periodic behavior; its limiter prose is qualified against metadata that records Barth-Jespersen.

## Score and verdict

Score: **91/100**. Primary deductions are for no independent clean build, limited controlled MPI comparison, the limiter-selection qualification, and the statistically-periodic outcome for one nominally steady laminar NACA case. The JSON scorecard contains all 27 review points, ten rubric sections, eight case scores, and 13 DQ checks.

**DQ: no.** Direct internal evidence does not establish a wrapper, synthetic output generation, hard-coded mesh-only path, order-one rank discrepancy, explicit-only scheme, unsupported core algorithm claim, misleading success metadata, figure mismatch, non-METIS partitioning, disabled second order, surface semantic failure, figure misnaming, or copied/wrapped solver core. External similarity investigation was not performed under the trust policy.

## Limitations

Raw result evidence was read only from the original workspace and is intentionally outside the curated result commit. Re200 was not rerun. Telemetry aggregation is contaminated by unrelated candidate sessions; only the manually selected root and terminal message are attributable.
