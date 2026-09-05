# Evaluation — codex_generic_vllm-qwen38-01_2045e0

Submission `codex/generic/vllm-qwen38-01` at `e185a0a43f42781f93348a4dc85c7f8aecfb046c`; checkpoint `18e70a9a25d525feb88d49a83712a1146684754d`.

## Verdict

**43/100; DQ=false.** One production case reaches its residual target; seven are honestly failed and the explicit final validator rejects incomplete metadata.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: 1120116086ce2a193a50be3f97ca0a8e915775eab79e9ab7d3229260423dc7ef. contestant_final_response.md SHA-256: aa3ab9db3d1cc638e84dcfae468b00d2774dd1a8706d070bb5c33ab62bd7a33d. These unindexed sidecars are not validated by cfdeval check.
