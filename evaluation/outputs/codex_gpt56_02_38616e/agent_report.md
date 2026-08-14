# Agent Evaluation Report — GPT-5.6/02

- Run ID: `codex_gpt56_02_38616e`
- Result branch/commit: `codex/gpt56/02` / `6183dc8fc067cd7dc4a4a2949ca53bd4f2b42820`
- Harness: Codex; selected root `019fbd25-a5cc-7550-b92d-6eb16af449ca` with 35 descendants
- Session window: 2026-08-01T11:47:10.587Z–2026-08-03T19:38:04.163Z

## Evidence

All telemetry/configuration was extracted only from the workspace-local `.sessions/codex` bundle. The selected root’s terminal response is `msg_0c7b34814af56050016a70ee14ad1c819993364090a1c0a372` at 2026-08-03T19:38:04.045Z; the extracted response SHA-256 is `8d8333a199a6eb7b1c040b4f418bb2c77457b51521a5f208ac03e9370a8ee2b5`. The immutable `run_identity.json` SHA-256 is `f6d537c708686d699e7ad42db24f1738d3ea10301c5c82a8304b8ef44dfa32ee`.

Explicit `validate_outputs.py --report` passed all eight production case directories and the report. No evaluator solver rerun occurred, and Re200 was not rerun. Submitted Re200 metadata/report evidence records 30,000 steps to t=300, inner ratio 0.00098623, and shedding St 0.18333. Submitted rank evidence covers NACA and cylinder np1/np8 comparisons.

## Per-case scores

| Case | Score | Evidence |
|---|---:|---|
| NACA M0.15 inviscid | 5 | Validator pass; 5.33/5.13 residual orders. |
| NACA M0.80 inviscid | 3 | Validator pass but only 0.569/0.856 residual orders. |
| NACA M2 inviscid | 5 | Validator pass; 3.00/3.26 residual orders. |
| NACA M0.15 Re5000 | 5 | Validator pass; 4.34/4.00 residual orders. |
| NACA M0.80 Re5000 | 3 | Validator pass but only 0.371/0.350 residual orders. |
| NACA M2 Re5000 | 5 | Validator pass; 3.65/3.56 residual orders. |
| Cylinder Re20 | 5 | Validator pass; 5.01 residual orders and Cd 2.049. |
| Cylinder Re200 | 5 | t=300, target met, and submitted shedding statistic. |

## Verdict

**92/100, not disqualified.** The complete output contract, source-level method evidence, MPI diagnostics, and credible Re200 deliverables support a strong evaluation. Deductions retain the two low-reduction NACA cases and a submission-packaging limitation: the strict audit excludes all TeX-escaped report PNG references, so the committed report source is not self-contained even though the images were available as contestant artifacts. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars and are not covered by `cfdeval check`.
