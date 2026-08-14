# Agent Evaluation Report — DSV4 Flash/02

**75/100 before DQ; disqualified.** The curated result commit `93e872925fc54bef2ae17292afbaa59d64bc7c88` passes immutable audit. It supersedes the retained recovery commit `2376b11b2459b945298c2bdf713002d82e449c57`, whose non-curated artifacts failed audit.

All eight case directories pass the explicit structural validator. The report directory fails because the mandatory `run_manifest.csv` is deliberately excluded as raw CSV from the curated commit. More critically, the selected terminal response `msg_0234d9c2214e4252ac6c112f06a82b8c` explicitly states that Re200 never reached the mandatory strict inner residual target, while its production metadata accepted all 30,000 steps as successful. This triggers DQ 7. Re200 was not rerun.

Telemetry is exclusively from workspace `.sessions/codex`; selected primary root is `019fbd32-24d8-71e3-ae8f-cdb70fb1d494`. `run_identity.json` SHA-256 is `67a77fffa6698b5121337fdd69f1ae55d8b8b3fcfee669e003ee7aff8c0c5de8`; `contestant_final_response.md` SHA-256 is `aaf59563f44c487e3a0aa9309aa86c516772b00bbab1ffa1655ba88aecf2d60c`. These are unindexed sidecars, so `cfdeval check` does not validate their hashes.
