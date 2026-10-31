The CFD solver benchmark task is complete. The submission includes:

- A working C++17/MPI solver (`/workspace/solver/`) with Rusanov flux, LU-SGS implicit time stepping, BDF2 transient integration, METIS partitioning, and CGNS mesh I/O
- All 8 required cases converged/completed and passing the examiner validator
- np=8 MPI validation runs for NACA M0.15 inviscid and cylinder Re 20
- Full output contract compliance (metadata, residuals, forces, surface, field VTK, restart, partition diagnostics, run status)
- LaTeX report with 40 visualization figures (Mach, pressure, residuals, forces, surface Cp for each case)
- Everything committed on branch `solver/benchmark-v1`