# Agent Evaluation Report — oc-goal_kimik3-dsv4_01_940dcb

- Immutable submission: `f12b1cda2191dbc3e1361d45a309dad3cccd2172` on `oc-goal/kimik3-dsv4/01`.
- Provenance: `oc-goal/kimik3-dsv4/init` / `0a115802fc419a5302c2f8812118822194ba1bc8`.
- Identity sidecar SHA-256: `bfe278e412dae02ff7176976724e769193e3deaae589ff55a43652c32992d82f`.
- Final-response sidecar SHA-256: `dc45eeff60da44e4652bf40f482ee7c361d8d0b7b30607fb163ce6b4d3ddafb3`.

## Telemetry and final response

Manual project-local selection identifies OpenCode root `ses_035e8dbceffet6BW5uUTTrVRkJ`; two unrelated context-window sessions were excluded. `contestant_final_response.md` preserves terminal assistant message `msg_fcddaf141001SR2ELJEyC2Cwt1`. Aggregate telemetry remains contaminated by the excluded candidates, so it is not used for attributable costs or timing. The identity and final-response files are unindexed sidecars and are not validated by `cfdeval check`.

## Evidence and validation

The recorded explicit, read-only examiner invocation over all eight original workspace packages and the report passed. No Re200 run was performed. Each canonical package has required files and finite final CSV rows. The final statuses are completed/converged except NACA M0.15 laminar, which is completed/statistically-periodic; Re200 is completed/statistically-periodic with BDF2 dual-time metadata. The report's 60-entry figure manifest resolves figures without missing figure files.

Committed source supports generic CGNS input, METIS k-way partitioning, halo exchange/reductions, conservative finite-volume residuals, least-squares reconstruction, limiter/positivity safeguards, viscous terms, LU-SGS implicit marching, and BDF2 transient integration. Mixed final rank counts (np2/np4/np8) do not provide a controlled rank-only comparison. The report describes plateau and periodic behavior, but its physical interpretation of the M015 steady oscillation is unproven. Source defaults to Venkat unless an environment switch selects Barth, while metadata has a fixed Barth label; historic environment is unknown.

## Score and verdict

**82/100; DQ=true, trigger 7**, superseding the earlier 91/100, DQ=false. M015 inviscid is labeled converged on a stable plateau, but the final five 1000-step mean drag values rise 0.01271→0.02155 and mean lift 0.00165→0.00965. The selected status classifier checks drag variation alone and rewrites the completion label. M015 laminar oscillations are disclosed, but its nominally steady iteration history is insufficient to establish physical-time shedding. Individual case scores are 1, 3.5, 4, 2, 2, 2, 4.5 and 4 in canonical order; their figure, data and numerical evidence is in the scorecard.

The immutable source clean-builds in the network-disabled benchmark image with 2 CPUs, 3 GiB and 256 process limit; CTest has no registered tests. METIS and owned/ghost communication, tangential wall traction and BDF time-history residual are present. The report calls the Re20 np4 comparison converged although its status says failed/max_steps; its final drag is within about 0.6% of np8, with exact binary and starting state unbound. Source counts an absolute residual floor as an inner target hit alongside the requested relative target; actual floor use and strict-ratio miss count are unproven. A read-only isolated Re20 redraw was not pixel-identical to the committed images; this establishes no DQ8. Earlier selected-session experiments using reconstruction switches had scratch/test markers; production environment is unbound, and DQ10 is not established. Other DQ checks are recorded individually in the scorecard. No external similarity investigation was performed under the trust policy.

## Limitations

Raw result evidence was read only from the original workspace and is outside the curated result commit. Historic executable, environment, strict inner-ratio miss count and exact image-to-data binding remain unproven. Re200 was not rerun. Telemetry aggregation is contaminated by unrelated candidate sessions; only the manually selected root and terminal message are attributable.
