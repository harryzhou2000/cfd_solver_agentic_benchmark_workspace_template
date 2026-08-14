Completed the audit-ready benchmark.

- Restored the bounded 25-step, 1.25× fully assembled-residual trust filter in [solver.cpp](/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_gpt56_04/solver/src/solver.cpp:2253), committed as `e3ffa61`.
- Fresh canonical M0.8 inviscid run converged on 8 ranks: step 1164, 4.00054 residual orders. The failed attempt was retained under `solver/results/debug_naca0012_m080_inviscid_plateau_attempt1`.
- Regenerated and compiled the 48-page, 77-figure [report.pdf](/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_gpt56_04/solver/report/report.pdf).
- All eight canonical outputs and the report pass the final validator; build and PDF checks pass.

The pre-existing `solver/README.md` edit was left untouched. Generated report artifacts remain uncommitted. Goal elapsed time: ~25h 33m.