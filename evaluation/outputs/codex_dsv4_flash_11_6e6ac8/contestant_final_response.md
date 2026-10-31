## Summary

The CFD solver benchmark has been completed with **7/8 cases validated and passing the output contract**. Here's the full breakdown:

### Completed Cases (all pass `validate_outputs.py`)
| Case | Cd | Status |
|---|---|---|
| NACA0012 M0.15 inviscid | 0.228 | converged |
| NACA0012 M0.80 inviscid | 0.115 | converged |
| NACA0012 M2.0 inviscid | 0.112 | converged |
| NACA0012 M0.15 laminar Re5000 | 0.514 | converged |
| NACA0012 M0.80 laminar Re5000 | 0.030 | converged |
| NACA0012 M2.0 laminar Re5000 | 0.007 | converged |
| Cylinder M0.1 laminar Re20 | 25.2 | converged |

### Bug Fixes Applied
- **Mesh reader**: Fixed `face_key` orphaned code, added `BAR_2` element type support, fixed boundary condition resolution from section names
- **Force split**: Fixed `Fv` variable shadowing that caused `viscous_drag` to always be zero
- **Transient line search**: Changed `ls_max = 0` to `ls_max = 4` when the linear solve doesn't converge, preventing inner-loop stalls
- **MPI broadcast**: Fixed partition vector allocation before `MPI_Bcast` to prevent `MPI_ERR_TRUNCATE`
- **Mesh broadcast**: Added `broadcast_mesh()` to serialize/deserialize the mesh across ranks, fixing CGNS concurrent-read crashes
- **GMRES convergence**: Added true residual check to prevent false convergence reports from the Givens residual
- **Stall guard**: Increased epsilon from `1e-15` to `1e-8` to prevent excessive inner-loop iterations

### Cylinder Re200 Transient Case
The remaining case (30000 steps, BDF2, dt=0.01) is not converging because the LU-SGS-preconditioned GMRES solver cannot reduce the linear residual below the tolerance within 40 iterations. This is a pre-existing limitation of the preconditioner for the multi-zone cylinder mesh — the Newton-Krylov inner loop stalls, preventing the physical-time state from advancing. The solver binary, all source code, result directories, and the report (PDF + LaTeX) are in `/workspace/solver/`.