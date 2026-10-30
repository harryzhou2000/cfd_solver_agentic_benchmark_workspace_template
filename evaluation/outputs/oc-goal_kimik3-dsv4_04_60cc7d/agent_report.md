## Calibration checkpoint (partial)

**70.5/100; DQ=true (provisional, trigger 7).** M2 laminar is marked converged at12219 although final5000 Cd rises 0.0963453→0.1107485; its real 4.5-order residual reduction is not force stability. Re200 2→4 after verifying the original attempt residual PNG, readable late lift/drag and St; report amplitude≈0.282 understates measured half peak-to-peak≈0.568. Other steady case histories have been checked from original blobs: three cases score 4→3.5 for observed long-window drift, Re20 4→4.5 for plausible drag. Full source/rubric and other DQ review continue.

# Evaluation — oc-goal_kimik3-dsv4_04_60cc7d

Curated submission `f45bb5967a6ffb5419140eeb6b10ef3d9090f9ae` was audited clean. All eight contestant-result packages and report validate in immutable attempt `7c1d0bf2856abe49342721ab9e591beb32abb746`; raw artifacts are intentionally excluded from the result commit and manager snapshot. The selected sole OpenCode root is `ses_004294f0cffe1r1FQyzhLt47Ym`; its latest completed assistant stop message `msg_ffd6f2ad5001crpfyEfmsbOUbN` is preserved exactly. The earlier sidecar was user-role goal continuation `msg_ffd6ea227001Ackyt1ZEikuvR7`, not a contestant answer; it remains in Git history (file SHA256 `83c8d49a2b999ec8397fa68d2863e7f1a75fce6e4c63d97683e09ae575be8642`). The actual assistant message raw JSON SHA256 is `f95759efd5c399e43acf0600583ed1bc6ffe1bfef48f4de700146a34b4891f32`; its single stored text part JSON SHA256 is `512c6b8602cda2d7e2bef93ee82e94dbd25b4a4567d826707a61c5a5dc631721`. No Re200 rerun occurred. The former 72/100, DQ=false is superseded by the provisional checkpoint above.

`run_identity.json` SHA-256: `0274d17f4f8e5870416d75a64294ab0b41f2e971f541efda86c9a174af0a3be7`.

`contestant_final_response.md` SHA-256: `17e83c6928019cc91ae58b4e6aef60320d79931302161f8fa6a2e2ced76d2d04`.

These are unindexed sidecars; `cfdeval check` does not validate unindexed sidecars.

## Session accounting correction

The immutable assistant-message rows show that the selected session entered as `kimi-for-coding/k3` with `max` reasoning, then switched to `deepseek/deepseek-v4-pro` for its final three assistant requests. The original snapshot used the mutable final `session.model` row and therefore misattributed all 89,918,249 tokens to DeepSeek. Correct message-level attribution is 88,769,378 Kimi tokens and 1,148,871 DeepSeek tokens. Provider-reported database cost remains $0.170458; the manager current-price estimate changes from $5.8051 to $31.3119. Submission evidence, evaluation prose, scores, and DQ verdict are unchanged.
