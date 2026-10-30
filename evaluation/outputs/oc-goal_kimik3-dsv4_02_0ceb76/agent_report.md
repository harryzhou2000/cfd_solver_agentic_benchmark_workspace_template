# Agent Evaluation Report — oc-goal_kimik3-dsv4_02_0ceb76

- Immutable submission: `4698ba1578ec446c6037bdbfa40e30b6ce82c9e1` on `oc-goal/kimik3-dsv4/02`.
- Provenance: `oc-goal/kimik3-dsv4/init` / `7bb0b026bbc59386143a6f8c46ed56e9e687e900`.
- Identity sidecar SHA-256: `a784dcd3ad21d6961e01852b38529825ca26bbde58085fc111f190fcafc3f2db`.
- Final-response sidecar SHA-256: `8642a00982ffec7d163f8c2cd102a491f819a2e9122b4345085f0197217191a8`.

## Telemetry and final response

Manual project-local selection identifies OpenCode root `ses_031c10218ffe8ch055n9dsopMA`, the long benchmark-continuity session from `2026-08-04T19:27:42.568000+00:00` to `2026-08-13T09:17:43.730000+00:00`. Earlier short root `ses_03222a213ffekSXtTAfiqJgU5r` is excluded as a setup/attempt. `contestant_final_response.md` preserves selected terminal assistant message `msg_ffa6976bc001ItcYeGkH1gjLnL`. The project-local aggregate sidecars include both roots, therefore their time, token, cost, tool and activity aggregates are contaminated and are not used as attributable primary-run measurements. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars and are not validated by `cfdeval check`.

## Evidence and validation

The recorded explicit, read-only examiner invocation over all eight original workspace packages and the report passed (rc 0). Re200 was never rerun. Seven steady finals are completed/converged; Re200 is completed/statistically-periodic with readable finite histories and delivered BDF2 dual-time metadata reporting a 1.0 inner-target-converged fraction. The report's 66-entry manifest has no missing figure files.

Committed source supports generic CGNS input, METIS k-way partitioning, owned/ghost communication and reductions, conservative finite-volume residuals, weighted least-squares reconstruction, Venkatakrishnan limiting and positivity safeguards, viscous terms, implicit steady iteration, and BDF2 transient integration. The final response's stated limitations — low-Mach LLF dissipation and the M0.80 inviscid limiter-shock limit cycle — are retained as qualifications, not hidden by the structural validator pass.

## Score and verdict

**83/100; DQ=true, trigger 7**, superseding 94/100, DQ=false. M2 laminar is marked converged at step1000, but Cd falls 0.673434→0.569866 over its final100 steps; the actual three-order residual reduction does not establish stable early forces. Individual case scores are 2.5, 3.5, 4, 1.5, 2, 1, 2 and 4 in canonical order. The report candidly identifies low-Mach over-dissipation: Re20 Cd6.03 compared with its cited literature≈2 and M015 laminar Cd0.489 remain stable yet inaccurate, so those physics deductions do not allege false convergence.

Immutable source clean-builds in a network-disabled 2 CPU/3 GiB/256-process benchmark container, with no registered CTests; compiler reports unchecked restart reads. METIS, local halos, tangential wall traction and BDF2 time-history loop are present. Re200's original delivered final commit and figure manifest include a force plot resolving both late lift and drag; the retained segment runs from t600.01 to900 after a Roe transient predecessor, and its raw St0.173 corroborates the report. The predictor-frozen limiter is not refreshed for a target recheck, so the fully refreshed miss fraction is unknown. The selected session records build, commit and launch in sequence but the exact historical binary hash is unbound. Re20 force, residual and Mach images reproduce pixel-identically from retained data in an isolated plotting environment; other figure bindings remain unverified. NACA and cylinder rank-study coefficients agree closely in absolute terms; no order-one rank difference is established. Other DQ checks are recorded individually in the scorecard. External similarity investigation was not performed under the trust policy.

## Limitations

Raw result evidence was read only from the original workspace and remains outside the curated result commit. The original contestant final commit preserves figures omitted from the curated tip. Historic executable hashes, the refreshed-limiter target miss fraction and other figure bindings remain unproven. Re200 was not rerun. Aggregate telemetry is contaminated by the excluded setup session; only the selected root and terminal response are attributable.
