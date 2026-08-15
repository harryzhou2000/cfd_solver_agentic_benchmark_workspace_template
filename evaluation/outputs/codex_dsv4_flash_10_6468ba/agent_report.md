# Agent Evaluation Report — codex/dsv4_flash/10

Curated submission `47dff36bef32a0a978e276e3a2683e14444c2acc` was audit-clean against initial `e757c4e0bac4065c4a5ab7ead230b236e4a9984c`; canonical run ID is `codex_dsv4_flash_10_6468ba`.

## Evidence and session attribution

Selected workspace-local Codex root `019fe243-274e-7961-abdb-42fcc139142e` has eight descendants and persisted event window 2026-08-08T16:46:09.587000Z through 2026-08-09T10:03:05.428000Z; execution date is 2026-08-08. The terminal response reports all production and clean-checkout checks. The curated branch intentionally excludes all raw outputs. The preserved contestant result source is immutable attempt commit `71528746943881ca8ba746fd37b66ed9ec4c4f95`: a disposable worktree at that exact commit passed `validate_outputs.py` for all eight canonical directories and the report (rc 0). Those are contestant_result evidence; neither raw result nor its manifest was copied into this manager snapshot or result branch.

## Scores and verdict

Score **86/100**; Code 3.78/5, CFD 4.00/5, Results 4.69/5; DQ=false. Every rubric, review-point and case note is in `agent_scores.json`; case scores are 5, 5, 4, 5, 5, 4, 5, 5. Strong source/report and validator evidence support a high result, while no evaluator solver rerun or independent redraw was performed. Re200 was not rerun.

No disqualification is established. The source contains the claimed core methods; no internal solver wrapper is evident. Trust policy excluded external similarity search.

`run_identity.json` SHA-256: `7317c40ba3aab6dff476a4af01e3f6c726cd16bb037c4e3db6b489998543b1ff`.

`contestant_final_response.md` SHA-256: `4b412bd2c6f85b2d8f901194102b3cf4d585aaa2b96bd144358892438d8d968a`.

These are unindexed sidecars; `cfdeval check` does not validate them.
