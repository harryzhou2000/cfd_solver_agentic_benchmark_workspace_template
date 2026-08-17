# Agent Evaluation Report — oc-goal_gpt56-dsv4_01_24edd3

- Immutable submission: `332ca95ef31bc8f694e699554f257729657f5d37` on `oc-goal/gpt56-dsv4/01`.
- Reconstructed provenance: `oc-goal/gpt56-dsv4/init` / `7bb0b026bbc59386143a6f8c46ed56e9e687e900`; post-run snapshot with unavailable original environment.
- Identity: `oc-goal_gpt56-dsv4_01_36a01a`; `run_identity.json` SHA-256: `e7cd756ebc4a78c081130a06f828def289795ec0ca7b5d04216000aaa3043799`.

## Telemetry and final response

Project-local OpenCode telemetry is attributable: the sole root `ses_027517d00ffenObonZzRSKHwwh` spans 2026-08-06T20:05:43.295Z–2026-08-10T07:01:44.847Z and has 38 direct descendants covering implementation, builds, production runs, rank checks, figures, and report work. All 39 database sessions record Docker cwd `/workspace`; it is mapped only to this contestant workspace's bundled `.sessions` database. The selected tree reports 430,551,708 aggregate tokens: 428,691,779 total input, including 406,617,211 cache reads and 21,861,162 cache writes; 22,074,568 input tokens are non-cache-read input, and output is 1,082,984 plus 776,945 reasoning tokens. Provider-reported database cost is $264.460781204. Its 36 main/subagent sessions use `us/azure/openai/eccn-gpt-5.6-sol` through `internal_openai_eccn`; two descendants use `deepseek-v4-flash` and one uses `MiniMax-M3`. The deterministic terminal completed root message is `msg_fea79e6af00121ZNMwpgazPUYC`, with one text part; its exact extracted prose is `contestant_final_response.md` (SHA-256 `12d6858a278cf64665f6ea386885223c77b96949c59aa09de43fb533b2e8e91b`). No home, evaluator, or external session store was queried. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars and are not validated by `cfdeval check`.

### Session accounting correction

The original session timeline omitted OpenCode's 21,861,162 cache-write tokens from its displayed input and non-cache-read categories even though the 430,551,708 total was correct. Regeneration now reconstructs input from the disjoint raw-input, cache-read, and cache-write message counters. The stored current-price estimate changes from $316.1733 to $316.0317 under the audited manager price table; provider-reported cost and all evaluation scores are unchanged.

## Evidence and validation

The explicit read-only examiner command over the eight original canonical workspace packages and the report returned rc 0. All seven steady cases report completed/converged status; cylinder Re200 reports completed/statistically-periodic at `t=300`, `dt=0.01`, and zero inner-target misses. Re200 was not rerun.

Committed source uses configurable `CFD_EXTERNALS_ROOT`, generic CGNS input, `METIS_PartGraphKway`, owned/ghost neighbor schedules, nonblocking `MPI_Isend`/`MPI_Irecv`, global `MPI_Allreduce`, Rusanov fluxes, least-squares reconstruction, Barth-Jespersen/positivity safeguards, LU-SGS/GMRES, and BDF2 dual time stepping. The report is detailed and includes figures/manifests, but its curated committed TeX does not compile because required PDFs were excluded. Its reported np4/np8 comparison also discloses an executable-hash mismatch, so it is not a pure rank-only comparison.

## Score and verdict

Score: **95/100**. Deductions: two points for independent clean-build/self-contained report reproducibility, two MPI points for the qualified rank comparison, and one report/visualization point for missing committed PDF dependencies. The 27 review points, ten rubric sections, eight independent case scores, and all 13 DQ checks are in `agent_scores.json`.

**DQ: no.** No trigger is established from direct internal evidence. In particular, source supports METIS/halo/reduction/implicit/BDF2 claims; metadata and run-status are consistent for the eight canonical packages; and no order-one rank discrepancy, figure mismatch, synthetic output generator, wrapper, or hard-coded mesh-only path was established. External similarity investigation was not performed under the trust policy.

## Limitations

Raw results were read only from the original workspace and are deliberately outside the curated submission commit. The project-local OpenCode database records container cwd `/workspace`, so its mapping to this host workspace is documented rather than inferred from any external path. No Re200 rerun occurred.
