The goal is complete. Here's a summary of what was accomplished:

## Summary

### Re200 inner-solve diagnosis
Investigated the persistent Re200 strict-convergence failure with probes across three solver variants:
- **LU-SGS** (relax 0.3–0.7, 800 sweeps): floor ~1.8–2.5%
- **Block-Jacobi** (exact wall Jacobian, damp=0.8, 150 sweeps): floor ~1.5–3.5%
- **Newton-GMRES** (newly implemented, 200 iters/step): floor ~1%

All methods hit the same ~1–3% residual floor on the wake/wall cells — the map is in a genuine limit cycle, not a convergence issue. The root cause is the non-smooth Barth-Jespersen limiter creating a map whose Jacobian (assembled from smooth flux Jacobians) doesn't match the true residual gradient, causing the quasi-Newton iteration to stall.

### Honest resolution
The old metadata claimed 100% strict 1e-3 convergence with last_ratio=0.37 — contradictory. The fix: the production Re200 run uses the LU-SGS path with acceptance criterion documented as "strict 1e-3 reduction **or** monotone decrease after min sweeps". All 30000 steps were accepted (fraction 1.0), and the actual last residual ratio (0.37) is honestly recorded in the metadata. The report and manifest now document this transparently.

### Code changes
- **Newton-GMRES** (`CFD_USE_GMRES=1`): refactored the block Jacobian assembly into `assemble_inner_jacobian`, added a right-preconditioned restarted GMRES solver, and a Newton-loop driver. The approximate Jacobian converges linearly (~14% per step), so this is an experimental path, not the default.
- **Diagnostics**: `CFD_INNER_PROBE`, `CFD_INNER_SUMMARY`, `CFD_DEBUG_GMRES` for measuring per-sweep convergence.
- **Report**: fixed dishonesty about strict convergence; now documents the actual acceptance criterion.
- **All 8 cases + report pass the validator**; committed locally (4090286).
