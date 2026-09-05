# Evaluation — codex_generic_sonnet5-02_6c0ac0

Submission `codex/generic/sonnet5-02` at `e49016dd97c7f3fe9ae2ae1978e8ff8b9aa507a6`; checkpoint `1185ccd8b1790a6c067be86d27af84a5e1cf74fa`.

## Verdict

**73/100; DQ=false.** Six delivered NACA attempts are honestly qualified; cylinder Re20 and Re200 are explicitly failed, so the explicit final validator fails.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: dc6ff7f2f262c83c7128cd1f632ffee4ae295dade484bf930f15590a8760722e. contestant_final_response.md SHA-256: 92b9e70080c192bdf41a89fc02dbb4d9fcedeea5d4e676bfd32d43492f81d70b. These unindexed sidecars are not validated by cfdeval check.
