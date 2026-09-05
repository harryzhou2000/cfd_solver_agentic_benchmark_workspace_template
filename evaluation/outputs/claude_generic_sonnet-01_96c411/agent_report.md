# Evaluation — claude_generic_sonnet-01_96c411

Submission `claude/generic/sonnet-01` at `64bbe1075e1413091acf116733e27241441ed6e2`; checkpoint `7f6a2ad77bc9acb93484a6bf099dfc2ae6887342`.

## Verdict

**64/100; DQ=false.** Only two steady cases are reported converged; the other steady cases and Re200 are explicitly failed, and the explicit validator fails final metadata.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: 3e8776452c691a5a3a608f1170f80b5341d3b86a2ff130923ccffd46eaa74b63. contestant_final_response.md SHA-256: 0e36955b931a6f839ce4ae97cc18302bbe2639e23842c75fa7ebb63032db3da3. These unindexed sidecars are not validated by cfdeval check.
