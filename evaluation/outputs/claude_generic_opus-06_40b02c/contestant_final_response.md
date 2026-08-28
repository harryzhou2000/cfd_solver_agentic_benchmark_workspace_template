The benchmark task is complete and validated.

## What's there

**`/workspace/solver/`**, branch `solver/opus5-cfd` — a 2-D unstructured compressible Navier–Stokes solver written from scratch: ~6,450 lines of C++17 in seven modules, MPI + METIS, plus ~2,400 lines of Python post-processing.

**All 8 cases** converged or statistically periodic, every one on 8 ranks from a single build:

| case | status | steps | C_D | C_L |
|---|---|---:|---:|---:|
| naca0012_m015_inviscid | converged | 1864 | 0.00103 | +1.5e-04 |
| naca0012_m080_inviscid | converged | 9005 | 0.00854 | −1.1e-03 |
| naca0012_m200_inviscid | converged | 2554 | 0.09192 | +8.0e-04 |
| naca0012_m015_laminar_re5000 | converged | 4663 | 0.05339 | −1.2e-03 |
| naca0012_m080_laminar_re5000 | converged | 8284 | 0.07691 | +1.6e-03 |
| naca0012_m200_laminar_re5000 | converged | 18135 | 0.13840 | +1.3e-04 |
| cylinder_m010_laminar_re20 | converged | 2806 | 2.01762 | +6.4e-04 |
| cylinder_m010_laminar_re200 | statistically_periodic | 30000 (t=300) | St 0.1859 | — |

**Validation:** Re 20 cylinder C_D = 2.018 vs the accepted 2.0–2.05, split 1.220/0.798 vs 1.233/0.812. Re 200 St/C̄_D/lift amplitude sit 2.2/4.1/4.3 % below their bands — one consistent shortfall, reported not tuned. MMS orders 2.30 / 2.03 / 0.92; freestream preservation 2.7e-10; restart exact across 8→4 ranks; np=4 speed-up 3.6× against the container's 4-CPU quota with drag agreeing to 1e-5.

**Gates:** `validate_outputs.py` OK on all 8 cases and the report; `check_submission.sh` 64/64 PASS; 126 physics checks, 0 failures; 270 unit assertions; 45-page report with 76 figures, every number a generated macro.

## Two things worth flagging

I found and fixed a **carbuncle instability** in the Mach 2 bow shock: HLLC was putting the leading-edge pressure 28 % above the Rayleigh–Pitot ceiling and giving C_L = −4.6e-3 for a symmetric section at zero incidence. A Rusanov run isolated the cause. The fix blends toward Rusanov only on faces that are supersonic, inside a strong pressure jump, *and* aligned with the shock front — it cures Roe there too, and all three flux functions now agree on the drag to 0.7 %. It is exactly inactive on the five subsonic cases.

The independent audit confirmed the new C++ is correct but found the *checking* could pass without checking — a sanity check that silently vanished if an input path moved, and rank-independence differences recorded but never compared to a tolerance (and computed as absolute, not relative). Those gates now enforce what they claim; the corrected worst rank-to-rank difference is 3.1e-2 in `cp` at the singular trailing edge, not the 9.4e-3 previously reported.

Reproduce with `scripts/run_everything.sh && scripts/make_report.sh && scripts/check_submission.sh` (~2.5 h).