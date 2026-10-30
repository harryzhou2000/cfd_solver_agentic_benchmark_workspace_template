# Evaluation — oc-goal_kimik3-dsv4_04_60cc7d

Curated submission `f45bb5967a6ffb5419140eeb6b10ef3d9090f9ae` was audited clean. All eight contestant-result packages and report validate in immutable attempt `7c1d0bf2856abe49342721ab9e591beb32abb746`; raw artifacts are intentionally excluded from the result commit and manager snapshot. The selected sole OpenCode root is `ses_004294f0cffe1r1FQyzhLt47Ym`; its latest completed assistant stop message `msg_ffd6f2ad5001crpfyEfmsbOUbN` is preserved exactly. The earlier sidecar was user-role goal continuation `msg_ffd6ea227001Ackyt1ZEikuvR7`, not a contestant answer; it remains in Git history (file SHA256 `83c8d49a2b999ec8397fa68d2863e7f1a75fce6e4c63d97683e09ae575be8642`). The actual assistant message raw JSON SHA256 is `f95759efd5c399e43acf0600583ed1bc6ffe1bfef48f4de700146a34b4891f32`; its single stored text part JSON SHA256 is `512c6b8602cda2d7e2bef93ee82e94dbd25b4a4567d826707a61c5a5dc631721`. No Re200 rerun occurred. The former 72/100, DQ=false is superseded by the full calibrated verdict below.

`run_identity.json` SHA-256: `0274d17f4f8e5870416d75a64294ab0b41f2e971f541efda86c9a174af0a3be7`.

`contestant_final_response.md` SHA-256: `17e83c6928019cc91ae58b4e6aef60320d79931302161f8fa6a2e2ced76d2d04`.

These are unindexed sidecars; `cfdeval check` does not validate unindexed sidecars.

## Session accounting correction

The immutable assistant-message rows show that the selected session entered as `kimi-for-coding/k3` with `max` reasoning, then switched to `deepseek/deepseek-v4-pro` for its final three assistant requests. The original snapshot used the mutable final `session.model` row and therefore misattributed all 89,918,249 tokens to DeepSeek. Correct message-level attribution is 88,769,378 Kimi tokens and 1,148,871 DeepSeek tokens. Provider-reported database cost remains $0.170458; the manager current-price estimate changes from $5.8051 to $31.3119. Submission evidence, evaluation prose, scores, and DQ verdict are unchanged.


## Full qualified calibration

**86/100; DQ=true, trigger 7**, superseding 72/100, DQ=false. Original M2 laminar is marked converged at step12219 while its last5000-step drag rises monotonically 0.0963453→0.1107485. Its real 4.5-order residual drop does not establish stable forces. Individual case scores are 4, 3.5, 4, 3.5, 3.5, 2, 4.5 and 4 in canonical order. Re20 Cd2.10159 is plausible; original Re200 data t≥150 give mean Cd1.27018, lift range −0.56686..0.56880 and St≈0.18621. The report's stated lift amplitude0.282 is incorrect; measured half peak-to-peak is0.56783. The original attempt includes the Re200 residual plot omitted by curation and the committed force plot resolves late lift/drag, correcting the old case floor.

The immutable source clean-builds in a network-disabled benchmark container (2 CPUs, 3 GiB, 256 processes), with no registered CTests. Root-only METIS preprocessing, tangential wall shear and full transient residual/limiter reevaluation are implemented. Original Re20 rank-study Cd2.108486–2.101592 across np1/2/4/8 shows no order-one change; M015 np8 and comparison metadata name different revisions and exact binary hashes are unbound. Re200 was np16, and its original partition field has ranks0–15 matching the report image. Re200 metadata revision predates material transient changes, so historical binary and exact control attribution remain qualified. Isolated Re20 plotting rerenders differ by 1–2 pixels; no figure misrepresentation is inferred. Both gates pass; no unsteady solver rerun or external similarity investigation.
