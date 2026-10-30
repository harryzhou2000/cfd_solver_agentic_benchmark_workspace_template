# Agent Evaluation Report — oc-goal/gpt56-dsv4/01

**85/100; DQ=true (trigger 7).** M0.15 and M2.0 laminar results are labeled converged even though their last 200 submitted drag samples fall from 0.114944 to 0.087589 and from 0.210556 to 0.141501, respectively. The report discloses M2.0's phase minima and some final drag drift, and the reported residual reductions are not disputed; the task also requires stable forces and clear early laminar convergence.

The evaluated immutable tip is `ade19533de3283fb2c92c36e723d34792afcdc90`. It contains 47 report figure PDFs, unlike the earlier `332ca95` snapshot. The old retrospective incorrectly capped all cases because it overlooked the later figures. The contestant's workspace postprocessing manifest matches all 80 canonical result files and all 47 figure PDF hashes. The accepted 18-page report PDF has SHA256 `d3acb03d5e66ecfc28564647507b7273d78139a697e9fd13ce61d90af7d19ec8`.

The Re200 figure resolves both late lift and drag. Its submitted force history over t≥200 gives mean Cd 1.110391 and St≈0.16226 from 16 mean upcrossings, consistent with the report's separate FFT and crossing estimates. Several steady plots flatten both late coefficients on a common scale; the corresponding independent case scores retain the presentation floor. The case scores, separate from the 100-point rubric, are 2, 4.5, 4, 1.5, 2, 1, 2 and 4 in canonical order. The previous M2 laminar note also misread Cl=1.5532×10⁻⁴ and the viscous drag component as Cl=1.5532 and total drag.

The committed solver implements METIS, neighbor communication, reconstruction, implicit GMRES/LU-SGS and BDF2. It reports full viscous traction in the force split while its skin-friction field is tangential; the report describes this distinction, but the rubric specifically rewards tangential force splitting. The transient source can count post-target residual-verification passes toward its minimum nonlinear iteration count. Historical stdout records five nonlinear counts on every step and 2,757 steps with three total linear iterations; it does not establish how many counted passes were verification only, and exact historical source-to-binary attribution remains qualified. The np4/np8 comparison uses different recorded executable hashes and is not a controlled rank-only test.

An isolated network-disabled clean build passed, as did `foundation_serial` (1/1); no full CFD case was rerun. All ten rubric sections, 27 review points, eight cases and DQ1–13 were examined; detailed evidence and limitations are in `agent_scores.json` and the calibration ledger. No external authorship investigation was performed under the trust policy.

Identity `oc-goal_gpt56-dsv4_01_f83d02`; `run_identity.json` SHA256 `25fb794aafdc69ede30b4c7e71b77c3b0c061b52b2e6e9c71615cf8a4c09b147`. Reconstructed init provenance is `oc-goal/gpt56-dsv4/init` / `7bb0b026bbc59386143a6f8c46ed56e9e687e900`, with the original environment unavailable.

## Telemetry and final response

Project-local OpenCode telemetry is attributable: the sole root `ses_027517d00ffenObonZzRSKHwwh` spans 2026-08-06T20:05:43.295Z–2026-08-10T07:01:44.847Z and has 38 direct descendants covering implementation, builds, production runs, rank checks, figures, and report work. All 39 database sessions record Docker cwd `/workspace`; it is mapped only to this contestant workspace's bundled `.sessions` database. The selected tree reports 430,551,708 aggregate tokens: 428,691,779 total input, including 406,617,211 cache reads and 21,861,162 cache writes; 22,074,568 input tokens are non-cache-read input, and output is 1,082,984 plus 776,945 reasoning tokens. Provider-reported database cost is $264.460781204. Its 36 main/subagent sessions use `us/azure/openai/eccn-gpt-5.6-sol` through `internal_openai_eccn`; two descendants use `deepseek-v4-flash` and one uses `MiniMax-M3`. The deterministic terminal completed root message is `msg_fea79e6af00121ZNMwpgazPUYC`, with one text part; its exact extracted prose is `contestant_final_response.md` (SHA-256 `12d6858a278cf64665f6ea386885223c77b96949c59aa09de43fb533b2e8e91b`). No home, evaluator, or external session store was queried. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars and are not validated by `cfdeval check`.

### Session accounting correction

The original session timeline omitted OpenCode's 21,861,162 cache-write tokens from its displayed input and non-cache-read categories even though the 430,551,708 total was correct. Regeneration now reconstructs input from the disjoint raw-input, cache-read, and cache-write message counters. The stored current-price estimate changes from $316.1733 to $316.0317 under the audited manager price table; provider-reported cost and all evaluation scores are unchanged.


## Limitations

Exact historical source-to-executable attribution, the count of verification-only transient passes, and unsampled figure generation remain qualified. No Re200 or unsteady solver rerun was performed.
