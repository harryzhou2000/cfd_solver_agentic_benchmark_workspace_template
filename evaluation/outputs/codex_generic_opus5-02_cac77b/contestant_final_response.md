The benchmark is complete. This turn I audited the submission against the spec rather than trusting my prior summary — and that caught four real defects the passing gates had not.

## What the audit found

**The run manifest was not reproducible.** TASK.md requires a manifest of the *exact* commands, and reproducibility from a clean checkout. The manifest recorded `./build_fix/fvmcns ... --output results_fix/<case>` for all 7 steady cases — scratch paths from an earlier fix that are gitignored and don't exist in a fresh clone. So I rebuilt `build/` from current source (it predated the duplicate-row fix) and re-ran all 7 cases through the documented `scripts/run_production.sh`. Every case reproduced **bit-identically**: same step counts, residual reduction equal to 16 digits, cd/cl exactly equal.

**Two clean-checkout verifications then found what a grader would actually hit.** The benchmark inputs are a git submodule, so a plain clone leaves them empty and every documented run command dies with "case file does not exist" — undocumented until now. `make_report.sh` advertised a system-python fallback that cannot work, dying partway with `ModuleNotFoundError`; it now fails fast with the venv command. And the README's own examiner invocation was broken: `results/*` globs in two run logs and aborts.

**The most substantive one:** `rank_study/` was gitignored, but it *is* the required MPI rank-independence evidence. On a clean checkout the rank table silently degraded to "no rank-count study data available" and 6 of 7 timing macros became em-dashes — meaning a rebuilt report was **weaker** than the committed one, with no script to recreate the data. Now committed (~1.5 MB of CSV/JSON; the unused per-run binaries stay excluded) and documented.

One thing worth flagging: because the machine is quieter now, wall times dropped and the generated np=4→8 ratios moved from 1.11/1.17 to 3.58/1.87. Since those numbers are generated rather than typed, the change surfaced immediately. 3.58× is *superlinear* for a rank doubling and therefore cannot be a parallel effect — so the timing section now says exactly that, and uses the instability of the ratio across load conditions as the argument that these wall times can't support a scaling claim.

## Verified end state

All 8 cases complete and honest: 7 converged, the Re 200 transient statistically periodic at 30000 steps to t=300 with the inner target met on 99.44% of steps, 0 failed. All six gates pass — 31688/31688 tests, 0 LaTeX errors, 0 undefined references, 0 overfull boxes, 42 pages, examiner OK on all 8 cases plus the report, 89/89 sanity checks. I also confirmed directly what the gates don't test: malformed inputs exit 1 with clear messages, no-slip walls report identically zero velocity while slip walls preserve 0.999 tangential, no full mesh or state replication, and all 79 figures have captions naming the plotted variable with filenames matching.

Final usage: 9,568,522 tokens over roughly 11 hours 48 minutes.

The one judgment call remaining is the `--cfl-max 25` deviation on the supersonic inviscid case. It's stricter than the case file's 50, disclosed in the report with a dagger in the generated table, and the spec permits documented conservative equivalents — but if a grader reads that clause more narrowly, it's the single place the submission departs from the supplied parameters.