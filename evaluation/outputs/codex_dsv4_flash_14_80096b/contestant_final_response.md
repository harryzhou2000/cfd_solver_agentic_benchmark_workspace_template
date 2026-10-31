The benchmark task is complete and the goal is marked achieved.

**Completion audit results**

- All 8 required case directories pass the structural validator, plus the report: `OK` for every case and `OK report`. Each dir contains the full output contract (`metadata.json`, partition diagnostics, `residuals.csv`, `forces.csv`, `surface.csv`, `field_final.vtu`, `restart_final.*`, `stdout.log`, `run_status.json`).
- `report/sanity_checks.json` now has 57 checks with 0 failures — including positivity, NACA zero-lift symmetry, slip-wall normal velocity (~1e-16), laminar nonzero skin friction, inviscid negligible viscous forces, cylinder mean drag, and Re200 lift variation.
- `report/report.pdf` rebuilt: 41 pages, 0 LaTeX errors, 0 undefined references, all 50 figures referenced in text and traced to CSV/VTU sources by the figure manifest.
- Clean from-scratch build verified (`cmake` configure + build with 0 errors against the external CGNS/HDF5/METIS install), plus CLI smoke at np=2, bad-input nonzero-exit handling, and the `--restart` path.
- Independent source audits (MPI/physics/numerics and build/CLI) found no disqualification triggers: real METIS k-way partitioning, neighbor-scoped `Isend/Irecv` halos, `Allreduce` norms, no full-state replication, active second-order Venkatakrishnan reconstruction with positivity fallback, steady LU-SGS and Re200 true BDF2 two-loop with matrix-free Newton-GMRES (t=300, 30000 steps, inner target 1e-3 met on every step).
- Rank-count comparison: NACA M0.15 inviscid and cylinder Re20 at np=1 vs np=8, with the measured deltas honestly reported (3.5e-4 and 2.3e-2).
- Extensibility documented in the report and README (EOS/BC seams, 3-D port surface, RANS/species extension points); the benchmark input repo is untouched and all work is on branch `solver/attempt`.

**Final commits**

- `c10299c` — final deliverables: corrected surface outputs, truthful rank-cmp and report wording, run-script fixes, complete report artifacts.
- `f46616e` — completion-audit fixes: 57-check sanity gate, all figures cited, extensibility docs, truthful inner-solve bookkeeping, `completed` flag and `--report-level` validation.

Goal accounting: the goal consumed 7,478,812 tokens over about 26.5 hours of elapsed run time.