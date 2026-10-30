# Agent Evaluation Report — DSV4 Flash/02

**73/100 before DQ; disqualified.** The curated result commit `93e872925fc54bef2ae17292afbaa59d64bc7c88` passes immutable audit. It supersedes the retained recovery commit `2376b11b2459b945298c2bdf713002d82e449c57`, whose non-curated artifacts failed audit.

All eight case directories pass the explicit structural validator. The report directory fails because the mandatory `run_manifest.csv` is deliberately excluded as raw CSV from the curated commit. More critically, the selected terminal response `msg_0234d9c2214e4252ac6c112f06a82b8c` explicitly states that Re200 never reached the mandatory strict inner residual target, while its production metadata accepted all 30,000 steps as successful. This triggers DQ 7. Re200 was not rerun.

Telemetry is exclusively from workspace `.sessions/codex`; selected primary root is `019fbd32-24d8-71e3-ae8f-cdb70fb1d494`. `run_identity.json` SHA-256 is `67a77fffa6698b5121337fdd69f1ae55d8b8b3fcfee669e003ee7aff8c0c5de8`; `contestant_final_response.md` SHA-256 is `aaf59563f44c487e3a0aa9309aa86c516772b00bbab1ffa1655ba88aecf2d60c`. These are unindexed sidecars, so `cfdeval check` does not validate their hashes.

## Partial Re200 recalibration — 2026-10-30

DQ7 remains supported by immutable driver.cpp:778–782,818–832,930–964: the counted inner-converged flag accepts either strict target or merely a decrease. Metadata records target 0.001, last ratio 0.369594, zero misses and fraction 1.0, so its last step demonstrably missed the strict target and its fraction is not a strict-target success fraction. The report and terminal response disclose the relaxed rule; the exact count of strict misses is unknown. Original 3001-row force history reaches t=300; t≥150 mean Cd=1.90548, Cl range −0.08186–0.12022, and Hann-windowed spectral peaks include 0.2465/0.2798/0.3065, consistent with report St≈0.275 as a broad-band estimate. Score 0→1 for this partial computed evidence; strict convergence remains unmet and currently no rendered contestant plots or suitable sibling PDF are present. No CFD rerun or evaluator redraw. Other cases, rubric and review areas remain partial.
