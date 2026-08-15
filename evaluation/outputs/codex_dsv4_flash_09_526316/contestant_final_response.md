The report compiled successfully.

**Output:** [report.pdf](/workspace/solver/report/report.pdf) — 4 pages, 244 KB, valid PDF 1.5 document.

Two `pdflatex` passes were run to resolve cross-references; the final log shows zero warnings and no undefined references. The report includes the governing equations, numerical methods (Roe flux, LU-SGS, BDF2, MPI partitioning), short-test results with residual/force/surface figures, MPI partition statistics, and honest documentation of the known limitations (high-CFL instability, laminar-case divergence, stagnation-point anomaly).
