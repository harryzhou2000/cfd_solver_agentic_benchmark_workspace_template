The CFD benchmark submission is finished. Final token usage for this goal: 47,000,602 tokens over roughly 55.9 hours of elapsed session time (no explicit budget was set).

Summary of where everything landed:

**6 of 8 cases converged / statistically periodic** (all pass the benchmark's structural validator): the three inviscid NACA0012 sweeps (M0.15/0.8/2.0) and three laminar Re5000 sweeps. Two of the laminar cases use a documented earlier stop on a verified stable plateau, consistent with the spec's explicit allowance.

**2 of 8 cases are honestly reported as failed**, each with rigorous, evidence-backed writeups rather than forced or fabricated passes:
- `cylinder_m010_laminar_re20` — a subagent caught and corrected an automated heuristic's false-positive "periodic" label by restarting the run and tracking both the residual (accelerating, never stabilizing) and the physical drag coefficient (decaying through zero into unphysical negative territory).
- `cylinder_m010_laminar_re200` — a dedicated feasibility investigation confirmed the transient case's inner BDF2 solver is genuinely too slow to reach its target within any practical session (real MPI np=4 speedup measured at 4.6x still implies a ~10-day full run), so the honest partial 74-step result stands, explicitly without a fabricated Strouhal number.

**Report** (`report/report.tex` → `report.pdf`, 19 pages, clean LaTeX build): all 22 `TODO[RESULTS]` markers filled with real data traced to source files, including corrected technical details (the production flux is Roe/Harten-Hyman, not the originally-drafted Rusanov baseline), 69 figures, run/figure manifests, and a full physics sanity-check pass across all 8 cases.

**Repository hygiene**: 113 one-off scratch tools removed, a case-ID hardcoding audit came back clean, `README.md` was written and verified against the actual build, and everything is committed to `solver/attempt-1` locally — nothing was pushed, and the read-only benchmark submodule was never touched.

Three subagents ran in parallel for the compute-heavy final stretch (the last cylinder case, cleanup/audit, and the Re200 feasibility investigation), each returning solid, well-evidenced findings that I verified independently before folding into the final report.