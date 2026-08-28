The benchmark is complete. All 8 cases ran to convergence, and the official validator returns OK on every case plus the report.

## What landed this session

The long-running cylinder Re 200 transient finished at 17:02 — 30 000 physical steps to t=300, `statistically_periodic`, inner target met on 100% of steps with zero misses. That was the last piece of compute; everything after was verification.

**Final results, all at revision `cc42ab4facfd`, np=4:**

| case | status | steps | orders | C_D |
|---|---|---|---|---|
| cylinder_re20 | converged | 2450 | 5.86 | 2.0184 |
| cylinder_re200 | statistically_periodic | 30000 | — | 1.2465 mean |
| naca_m015_inviscid | converged | 1861 | 5.26 | 0.000988 |
| naca_m080_inviscid | converged | 2546 | 3.92 | 0.008771 |
| naca_m200_inviscid | converged | 4854 | 2.90 | 0.087493 |
| naca_m015_laminar | converged | 3839 | 5.47 | 0.055181 |
| naca_m080_laminar | converged | 4639 | 4.00 | 0.081473 |
| naca_m200_laminar | converged | 37022 | 4.78 | 0.137141 |

Shedding: St=0.1829, period 5.4667 (spread 0.0007 over 53 estimates), C_L ±0.5304, 27.3 cycles in the analysis window.

## The part worth your attention

Most of the session's value came from catching things that were *wrong but plausible*. Two audits and a targeted sweep found nine defects that structural validation passes cleanly:

- A convergence claim that called a **converged** case "not converged", contradicting the solver's own recorded note — the report said decay ratio 1.019, the data says 0.9195.
- A physics table asserting a bound violation had been fixed when **9 of 404 faces still exceed it**. I localized them to the first 0.18% of chord, 8 of 9 on one side of a symmetric body at zero incidence.
- Three "verification constants" in the abstract that were the *cylinder* mesh's values presented as global, while the NACA mesh carrying six of eight cases is **455× worse**.

The root cause was consistent: numbers typed by hand rather than derived. That category is now closed — the report harvests 498 macros from the result files, including an assertion that fails loudly if two cases sharing a mesh ever disagree on a mesh property. I exercised that assertion by perturbing a copied tree; it aborts and names the disagreeing case rather than averaging.

Two of my own analyses were wrong and `report_author` caught both: I measured a decay ratio on a trajectory the solver had discarded, and I used `1/(1-r)` where the solver uses `r/(1-r)`. Its corrections held up against the source.

I also closed the one gap the final audit flagged as unclosable locally — originality — by probing the source against documented SU2, OpenFOAM, and DNDSR idioms: **zero foreign-idiom hits across 8 818 lines**, with a self-consistent independent style instead.

**Final state:** [report.pdf](/workspace/solver/report/report.pdf) at 76 pages, 0 errors, 0 undefined references, no placeholders; 143 manifest rows with every displayed figure mapped; 139 unit checks and 483 pipeline checks green; all 13 disqualification triggers clear. Committed through `f22bc25` on `solver/cns2d-fvm`, worktree clean.

One honest caveat: the np=8 timings are *slower* than np=4 (8.88 s vs 6.26 s for the cylinder). That's a hard 4-CPU cgroup quota, not a solver defect — `nproc` reports 64 and nothing hints at the cap. The report documents this explicitly rather than presenting the regression as a communication cost.

Elapsed: ~14.5 hours, 3.56M tokens.