# cfd-fv2d

2-D cell-centered unstructured finite-volume solver for the compressible
Navier-Stokes equations of a calorically perfect gas, written in C++17 with
MPI domain decomposition. This is the solution directory for the
`cfd_solver_agentic_benchmark` task.

## Status

The build, mesh pipeline, distributed partition, residual/flux/BC kernels,
time-integration loops, and the full output contract are implemented. The
steady cases converge on a force plateau or on the supplied residual target,
and the Reynolds 200 transient case is run with a true BDF2 outer loop. See
`DEV_NOTES.md` for the numerical details and diagnostics.

## Dependencies

- CMake >= 3.16, a C++17 compiler, MPI (Open MPI 4 tested)
- CGNS, HDF5, zlib, METIS under the benchmark convention
  `external/cfd_externals/install/{include,lib}`
- nlohmann/json header-only package
- Python 3.10+ with numpy/matplotlib for post-processing (created in `.venv`)

## Build

```bash
cd solver
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCFD_EXTERNALS_ROOT=../external/cfd_externals/install
cmake --build build
```

The dependency root can also be passed through the `CFD_EXTERNALS_ROOT`
environment variable.

## Run

```bash
mpirun -np <ranks> ./build/cfd_fv2d solve \
  --case <path/to/case.json> --output <output-dir> \
  [--restart restart_final.json] [--report-level brief|full]
```

Documented diagnostic overrides (also exposed as CLI options):

- `--max-steps N`  limit nonlinear/physical steps
- `--final-time T` / `--time-step DT`  transient overrides
- `--cfl-cap C`  conservative ceiling on the pseudo-time CFL

Environment switches used during development: `CFD_CFL_MAX`, `CFD_RELAX`,
`CFD_DIAG_RHO_FACTOR`, `CFD_BC_RHO_FACTOR`, `CFD_INNER_MAX`, `CFD_GMRES_M`,
`CFD_FIRST_ORDER`, `CFD_NO_LIMITER`, `CFD_RUSANOV`, `CFD_INVISCID`,
`CFD_SIMPLE_FF`, and the `CFD_DEBUG_*` diagnostics.

## Python environment

```bash
python3 -m venv .venv
.venv/bin/pip install numpy matplotlib
```

## Post-processing

```bash
.venv/bin/python tools/plot_histories.py results/<case>
.venv/bin/python tools/plot_surface.py   results/<case>
.venv/bin/python tools/plot_fields.py    results/<case>
.venv/bin/python tools/make_sanity_checks.py results
.venv/bin/python tools/make_manifests.py     results
```

## Layout

- `src/` solver implementation
- `tools/` mesh probe, plotting and report automation
- `report/` LaTeX report, figures, manifests
- `results/` per-case result packages

## Numerical method summary

- Conservative variables `U = [rho, rho u, rho v, rho E]`, calorically perfect
  gas with configurable `gamma`, `R`, `Pr`.
- Mixed TRI_3/QUAD_4 CGNS meshes; zones merged by exact-coordinate face
  matching; element-based boundary sections mapped to farfield/slip/no-slip.
- METIS k-way partitioning of the cell adjacency graph; solver iterations use
  rank-local owned cells plus one ghost layer and neighbor `Isend/Irecv`
  exchange of conservative states and primitive gradients.
- Piecewise-linear least-squares reconstruction of `[rho,u,v,p]` with a
  Venkatakrishnan limiter and a first-order positivity fallback.
- Rusanov/LLF or Roe (Harten-Yee entropy fix) inviscid flux; consistent
  gradient-based Newtonian viscous flux and Fourier heat flux with
  constant-viscosity `mu = rho_inf U_inf L/Re`.
- Roe flux with the Harten--Yee entropy fix for all production cases
  (Rusanov/LLF is retained behind `CFD_RUSANOV=1` and is used for the
  cylinder Re=20 case, whose low-Mach shear layers are better matched by the
  Rusanov dissipation on the supplied mesh).
- Backward-Euler local pseudo-time stepping for steady cases; BDF2 dual-time
  stepping with a true physical-time outer loop for the Re 200 transient.
- Inner implicit relaxation: forward/backward flux-split Gauss-Seidel
  (LU-SGS) with a spectral diagonal and a 4x4 block-diagonal variant
  (`CFD_BLOCK_DIAG=1`); a matrix-free GMRES variant is retained in source.
- For the final phase of steady runs the least-squares gradients and
  Venkatakrishnan limiter can be frozen (`CFD_FREEZE_RECON_AFTER=N`). This
  removes limiter limit cycles without reducing the spatial order of the
  frozen state; the production launchers document the chosen freeze step.

## License / originality

All residual, flux, limiter, time-integration, and MPI-halo logic is original
code written for this benchmark. External libraries (CGNS/HDF5, METIS,
nlohmann/json) are used only for mesh I/O, partitioning, and configuration.
