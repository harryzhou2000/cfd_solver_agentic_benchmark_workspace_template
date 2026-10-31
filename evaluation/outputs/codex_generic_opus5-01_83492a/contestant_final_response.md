The benchmark is complete. All eight cases ran to their required end states and the submission passes every gate.

## Results

| Case | Status | Steps | Residual orders |
|---|---|---|---|
| cylinder Re200 | **statistically_periodic** | 30000 (t=300) | 3.01 |
| cylinder Re20 | converged | 2529 | 5.58 |
| NACA M0.15 inviscid | converged | 11591 | 4.00 |
| NACA M0.80 inviscid | converged | 6532 | 4.00 |
| NACA M2.00 inviscid | converged (plateau) | 40000 | 2.55 |
| NACA M0.15 lam Re5000 | converged | 3917 | 5.17 |
| NACA M0.80 lam Re5000 | converged | 7891 | 4.23 |
| NACA M2.00 lam Re5000 | converged | 25753 | 4.77 |

The transient case ran the full 5.4 hours at the **supplied** pseudo-CFL of 1.0 — costing ~160 inner iterations per step instead of ~30 — and met the 1e-3 inner target on **all 30000 steps with zero misses**. It gives St = 0.1824 and mean C_D = 1.2359 over 46 clean shedding cycles. The Re=20 cylinder drag of 2.0181 sits inside the literature range of 2.0–2.1.

Validation: examiner OK for all 8 cases plus the report, 63 figures agreeing across disk/manifest/report, 8/8 unit tests, 52-page PDF with zero undefined references.

## What this session actually changed

The audits found four defects where the code was quietly wrong rather than merely undocumented:

- **`numerics_required` was never parsed.** Every case file states required capabilities and the solver ignored all of it, relying on a hard-coded policy that happened to satisfy all 8 cases. Now enforced — `--first-order` is correctly rejected, and four synthetic negative cases fail loudly.
- **`positivity_fallbacks_total` was rank-local and double-counted at partition interfaces**, and the transient driver never tracked it at all — so the headline case asserted second-order accuracy without measuring it. The supersonic inviscid case reports 595 fallbacks on 324 of 40000 steps: measured, not assumed.
- **`git_revision` was null everywhere** — it read an environment variable nothing set.
- **`stdout.log` was excluded from all 8 cases** by a blanket `*.log` rule. Every file existed on disk, so only cloning the repo exposed it.

Two report claims were factually wrong and are now corrected: the partition table cited `prod_*` directories that had become np=4, and a calibration passage justified a test using numbers the current run contradicts. Both are now generated from artifacts, so that class of drift can't recur.

## Two things worth your judgment

**`naca0012_m200_inviscid` reports "converged" at 2.55 of a requested 3.0 orders.** It stopped on a pre-declared plateau branch: ≥2.5 orders *and* settled forces, with C_D stable in the fourth digit while the bow-shock limiter keeps switching a few faces. The report states it as a shortfall, marks it with a dagger, and audits it in its own section — but a grader reading only `run_status.json` can't distinguish it from a clean convergence, since the contract permits just three status values.

**np=8 is genuinely slower than np=4 here.** The container is capped at 4 CPUs (55% of scheduling periods throttled, per kernel counters). The rank sweep therefore demonstrates *solution consistency* — cylinder drag agreeing to four significant figures across all rank counts — and the report explicitly declines to claim parallel efficiency the hardware can't support.

I also verified reproducibility the hard way: a fresh clone builds, passes 8/8 tests, runs a case to convergence, and reproduces a submitted `forces.csv` **byte-for-byte**.

Final usage: 11,019,947 tokens over about 22.5 hours.