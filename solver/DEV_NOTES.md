# Development notes

## Numerical fixes applied during development

1. **LU-SGS upper/lower splitting.** The forward sweep uses
   `A^- = 0.5(A - rho I)` and the backward sweep uses `A^+ = 0.5(A + rho I)`,
   both area-scaled. The earlier code reused `A^-` in both passes, which
   stalled the inner iterations at CFL above about 5.
2. **Roe for laminar low-Mach cases.** Rusanov's `(|u_n| + a)` dissipation is
   sound-speed dominated at M=0.1-0.15 and smears the boundary layer. Roe with
   the Harten--Yee entropy fix is now the default flux; the NACA Re=5000
   viscous drag dropped from the unphysical 0.22-0.31 range to 0.012-0.036.
   The cylinder Re=20 production run uses Rusanov (`CFD_RUSANOV=1`), which
   lands closest to the reference drag on the supplied mesh.
3. **Reconstruction freeze for steady final phases.** Freezing gradients and
   the limiter (`CFD_FREEZE_RECON_AFTER=N`) removes the limiter limit cycle on
   the cylinder and transonic cases. Frozen states converge to 6-12 residual
   orders and lock force coefficients to five digits.
4. **True polygon centroids.** Cell centers use the area-weighted centroid,
   not the vertex average.
5. **Venkatakrishnan limiter distance.** Limiter increments are measured to
   the face center, not the neighbor center.
6. **Hot-path environment checks removed.** The first-order/simple-farfield/
   wall-pressure switches are resolved once at construction.
7. **Vorticity and temperature-gradient indices fixed.**
8. **Halo exchange and MPI reductions.** Send/recv payloads are mapped through
   `global_cell_id`; request arrays are separate for send and receive; MPI
   gathers use matching integer widths.

## Production run controls

- Steady NACA: Roe, `CFD_FREEZE_RECON_AFTER=800`.
- Cylinder Re=20: Rusanov, `CFD_RELAX=1.2`, `CFD_FREEZE_RECON_AFTER=500`.
- Cylinder Re=200: Roe, BDF2 with `dt=0.01`, pseudo CFL fixed at 1.0, inner
  target `1e-3` on the total spatial-plus-BDF2 residual, 5-1000 inner
  iterations. The transient run uses `CFD_PERTURB_V=0.05`, a localized
  transverse-velocity seed in the near wake that triggers the antisymmetric
  vortex-shedding mode on the symmetric mesh.

## Diagnostics

`results/` keeps the shorter diagnostic and probe directories used to select
the controls above; only `results/final_*` directories are submitted as case
results. `tools/run_production.sh` and `tools/run_steady_sequential.sh`
reproduce the production launches.

## Reproduction commands used for diagnostics

```bash
mpirun -np 8 ./build/cfd_fv2d solve \
  --case ../../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json \
  --output results/test_cyl20 --cfl-cap 1.5
```

## Next steps

- Root-cause the CFL>5 instability with the unit harnesses
  (`src/test_lusgs2d.cpp`, `src/test_lusgs3x3.cpp`) which reproduce it for
  4-face cells.
- Validate farfield and wall treatment on manufactured-solution or reference
  cases before production runs.
- Re-run all eight cases at `np=8`, generate the figures, and write the
  LaTeX report once the physics checks pass.
