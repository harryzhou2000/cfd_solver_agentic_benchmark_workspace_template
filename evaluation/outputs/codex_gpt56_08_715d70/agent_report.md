# Evaluation — codex_gpt56_08_715d70

Submission `04cd1eee69785ebf1e228d51a81e8a2cf1c262d0` on
`codex/gpt56/08` was curated from initial
`835bc07eaa03beeed2db88c13089b8e3e639f13b`; its path audit passed. The
selected workspace-local Codex root
`019fe5fc-728f-7580-81ba-d6e2995347e0` has 68 descendant threads. Its
captured final response explicitly reports seven steady cases pass and Re200
remains honestly failed: it reaches t=300 but has flat lift and failed
metadata. Re200 was not rerun.

The immutable source/report support a substantial solver effort, but a clean independent build succeeded in a restricted container with bundled dependencies; CTest has no registered tests.
Result scoring therefore retains the seven steady packages as limited
contestant evidence and assigns Re200 zero because its terminal status is
incomplete. No disqualification trigger was established; incomplete Re200 is
scored rather than relabeled.

## Session accounting correction

The selected tree ran from `2026-08-09T10:06:24.505000+00:00` through
`2026-08-16T00:26:56.617000+00:00`. Goal time is 523,046 seconds and wall time
is 570,032 seconds.

The previous expense sidecar reported 165,563,577,113 tokens because every
forked rollout replayed its parent's cumulative token history and the extractor
summed each descendant's terminal cumulative counter. In the current bundled
state those inherited-inclusive counters sum to 166,388,880,969 for the same
69-thread selected tree.

The corrected extractor identifies each child's first owned task using the
fork metadata, creation epoch, and a task turn ID absent from the parent. It
uses the last preceding token counter as the inherited baseline and sums only
owned counter deltas. The corrected total is 5,477,942,644 tokens:

- main root: 4,975,794,470;
- descendant threads: 502,148,174;
- input: 5,470,539,942, of which 5,381,461,888 is cached;
- output: 7,402,702, including 3,302,573 reasoning-output tokens.

The corrected current-price estimate is $679.8115. The measurement sidecar
likewise excludes inherited replay and records 35,982 owned tool calls. No
evaluation, review-area, rubric, per-case, or DQ score was changed by this
telemetry repair.

## Recorded scoring

The provisional rubric is **69/100**, replacing the old unsupported 3-point assignment in every section. Seven steady cases remain low due to weak 300-step plateaus and implausible forces; M2 inviscid receives 1.5/5, and the honestly failed Re200 remains 0/5. DQ is not established. The original report figures are present; current workspace packages include later replacements and are not attributed to the curated report. Full review remains partial. Detailed evidence is recorded in `agent_scores.json` and the ledger.

## Integrity and limitations

`run_identity.json` SHA-256:
`45a3937acdd6126367ec7137ced2a8e782bbbeb734f4303bf2ef3bcf871acc90`.

`contestant_final_response.md` SHA-256:
`cda1f369e5a481889edae5bb18775516fd98bc64cb19c8e43e3a6ee5d19336b1`;
stored response-item record SHA-256:
`2976972490954ff66a391d98197ece19434a1c5d13a807cb592927e50ae78170`.

Those two Markdown/identity sidecars are unindexed by this snapshot's current
index contract; `cfdeval check` does not validate unindexed sidecars.
