# cfd2d — a 2-D unstructured compressible Navier–Stokes solver

`cfd2d` is a cell-centred finite-volume solver for the two-dimensional
compressible Navier–Stokes equations of a calorically perfect gas, written from
scratch in C++17 with MPI domain decomposition. It reads unstructured CGNS
meshes (mixed triangles/quadrilaterals, multi-zone with 1-to-1 interfaces),
partitions the cell graph with METIS, and marches either a steady implicit
pseudo-time problem or a true dual-time BDF2/trapezoidal transient.

This repository is the submission for the `cfd_solver_agentic_benchmark`
task; see `report/report.pdf` for the full technical report and results.

## Method summary

| Component | Implementation |
|---|---|
| Equations | 2-D compressible Navier–Stokes, conservative form, calorically perfect gas |
| Mesh | CGNS unstructured; TRI_3/QUAD_4/MIXED; multi-zone merged through vertex 1-to-1 connectivity |
| Partitioning | METIS k-way on the cell (dual) adjacency graph, one ghost layer, RCM ordering inside each part |
| Parallel | neighbour-scoped `MPI_Isend`/`MPI_Irecv` halo exchange of state, gradients and limiters; global reductions for residuals/forces |
| Reconstruction | piecewise-linear, inverse-distance-weighted least-squares gradients of the primitive variables |
| Limiter | Venkatakrishnan (default, `K=5`) or Barth–Jespersen, with a first-order positivity fallback |
| Inviscid flux | HLLC (default), Roe with Harten–Yee entropy fix, or Rusanov/LLF |
| Viscous flux | Newtonian stress + Fourier heat flux from corrected face gradients of `(u,v,T)` |
| Boundary conditions | characteristic farfield, mirrored slip wall, no-slip adiabatic wall |
| Steady solve | implicit backward Euler in local pseudo time with a CFL ramp and a residual-based CFL back-off safeguard |
| Transient solve | BDF2 (or trapezoidal) dual time: outer physical-time loop, inner nonlinear iterations, frozen histories |
| Linear solve | matrix-free LU-SGS / symmetric Gauss–Seidel with a spectral-radius diagonal |

## Dependencies

The build follows the DNDSR external-resource convention.

* A C++17 compiler and CMake ≥ 3.16
* MPI (tested with Open MPI 4.1)
* CGNS + HDF5 + zlib, and METIS — expected under `$CFD_EXTERNALS_ROOT`
  (`include/`, `lib/`)
* Header-only `nlohmann/json` (and optionally Eigen, doctest) under
  `$CFD_HEADER_ROOT`

Both roots are CMake cache variables and environment variables:

```bash
export CFD_EXTERNALS_ROOT=<workspace>/external/cfd_externals/install
export CFD_HEADER_ROOT=<workspace>/external
```

They default to `../external/cfd_externals/install` and `../external` relative
to this directory, which is the layout used by the benchmark runner
(`scripts/env.sh` sets them to the absolute paths of this machine's install so
the run scripts work from any directory).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCFD_EXTERNALS_ROOT=$CFD_EXTERNALS_ROOT \
      -DCFD_HEADER_ROOT=$CFD_HEADER_ROOT
cmake --build build -j
ctest --test-dir build          # unit tests + MPI parallel-consistency test
```

The executable is `build/cfd2d`.

## Run

```bash
mpirun -np 8 ./build/cfd2d solve \
    --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
    --output results/naca0012_m015_inviscid
```

`./build/cfd2d help` lists every option. All eight benchmark cases run with the
same executable and the same options; the differences come from the case JSON
files. The only per-case command-line options used for the submitted results
are documented in `report/run_manifest.md` and in the report:

* `--cfl-scale 30 --inner-target 1e-4` for the Reynolds 200 transient
  (a larger *pseudo*-time CFL with a ten-times **stricter** inner convergence
  target than the supplied one; §7 of the report shows this converges the same
  BDF2 problem more tightly and more cheaply).

Limiter freezing is automatic and uniform: every steady run holds the limiter
values fixed from pseudo-time step `3 * pseudo_cfl_ramp_steps` (taken from the
case file), which removes the residual limit cycle caused by the
non-differentiable min/max stencil on the shock cases. The limiter is *frozen,
not disabled* — the frozen values keep multiplying the reconstruction. Use
`--freeze-limiter-step 0` to switch the behaviour off.

Other subcommands:

```bash
./build/cfd2d inspect-mesh --mesh <file.cgns>     # mesh/boundary summary
./build/cfd2d verify --levels 4 --base 16 \
    --case <case.json> --out report/verification.json   # order-of-accuracy study
```

`verify` runs a method-of-manufactured-solutions convergence study on
internally generated mixed triangle/quadrilateral meshes, plus freestream
preservation and linear-reconstruction exactness checks on the supplied mesh.

## Tests

| Test | What it covers |
|---|---|
| `ctest -R unit` | polygon geometry, equation of state, all three Riemann solvers (including exactness of the Roe flux across a stationary Rankine–Hugoniot shock), the wall-flux identities, the farfield freestream-preservation property, the Newtonian stress/adiabatic viscous flux, and both limiter functions |
| `ctest -R mpi_consistency` | runs the same case on 1, 2 and 4 ranks and checks partition completeness, non-empty symmetric halos and agreement of the global residual and force coefficients |
| `./build/cfd2d verify` | order of accuracy, freestream preservation, linear-reconstruction exactness |

### MPI notes

`mpirun` is invoked with `--bind-to none --oversubscribe` (see
`scripts/env.sh`). The benchmark container advertises 64 logical cores but has
a 4-CPU cgroup quota, so pinning ranks to cores collapses performance and the
`np=8` demonstration needs oversubscription.

## Reproducing the submitted results

```bash
python3 -m venv .venv && . .venv/bin/activate && pip install numpy matplotlib scipy
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

scripts/run_everything.sh        # every run below, in dependency order
#   or individually:
scripts/run_all.sh steady        # 7 steady cases at np=8    -> results/
scripts/run_all.sh transient     # cylinder Re 200 at np=8   -> results/
scripts/run_all.sh mpi           # rank study np = 1,2,4,8   -> studies/mpi/
scripts/run_all.sh verify        # flux and limiter checks   -> studies/verify/
scripts/run_verify_extra.sh      # Roe at Mach 2, restart round trip
scripts/run_cfl_study.sh         # dual-time CFL/target study -> studies/cflstudy/

scripts/make_report.sh           # verification, figures, manifests, LaTeX report
.venv/bin/python ../cfd_solver_agentic_benchmark/examiner/validate_outputs.py \
       results/* --report report
```

`results/` contains exactly the eight required case directories. The transient
case additionally writes `results/cylinder_m010_laminar_re200/fields/` with the
snapshot every `write_field_every_time = 1.0` requested by the case file (300
files, about 330 MB); those are present on disk but are excluded from git
because they are regenerable intermediate output. The required
`field_final.vtu` of every case is tracked. Supporting
runs that are not benchmark deliverables (the MPI rank study and the numerical
cross-checks) live under `studies/`, so `validate_outputs.py results/*` sees
only the submitted cases.

Python tooling (all under `.venv`):

| Script | Purpose |
|---|---|
| `tools/vtu_reader.py` | reader for the solver's VTU output |
| `tools/plot_style.py` | shared publication plot style and contour helpers |
| `tools/make_figures.py` | every report figure + `report/figure_manifest.csv` |
| `tools/analyze_transient.py` | Strouhal/mean-drag analysis of the Re 200 wake |
| `tools/mpi_study.py` | rank-count timing and consistency tables/figure |
| `tools/sanity_checks.py` | `report/sanity_checks.json` physics gate |
| `tools/run_manifest.py` | `report/run_manifest.csv` and `report/run_manifest.md` |
| `tools/plot_verification.py` | manufactured-solution convergence figure |
| `tools/limiter_study.py` | Mach 2 limiter limit-cycle figure |

Python dependencies: `numpy`, `matplotlib`, `scipy`.

## Repository layout

```text
src/core/       types, case-file parsing and validation, logging
src/mesh/       CGNS reader, global mesh, METIS partitioner, distributor, rank-local mesh
src/parallel/   halo exchange and global reductions
src/physics/    equation of state, transport, Riemann solvers, viscous flux, boundary conditions
src/numerics/   gradients/limiter/residual, LU-SGS, steady and transient drivers, verification
src/io/         VTU/CSV/JSON/restart output
src/post/       wall force integration and surface sampling
tests/          doctest unit tests
tools/          Python post-processing
scripts/        build/run/report orchestration
results/        one directory per required case (OUTPUT_CONTRACT.md layout)
studies/        MPI rank study and numerical cross-checks (not scored deliverables)
report/         LaTeX report, figures, manifests, sanity checks
```

## Extensibility

The dimension and the number of conserved variables are the compile-time
constants `kDim`/`kNVar` in `src/core/Types.hpp`; the mesh, partitioning, halo
exchange, limiter and implicit solver are written against them rather than
against hard-coded 2-D/4-variable assumptions. The equation of state is behind
the `PerfectGas` interface (`pressure`, `soundSpeed`, `toPrimitive`,
`normalFlux`, …), so a real-gas or Cantera-backed model is a sibling class.
Boundary conditions are two pure functions per type (`ghostState`,
`boundaryValue`) selected by an enumerator, so adding, e.g., an isothermal wall
or a subsonic inlet is a local change. Transport is behind `TransportModel`
(constant and Sutherland laws are implemented), which is where a turbulent
eddy-viscosity contribution from a RANS model would enter. No solver logic
branches on `case_id`.

## Notes on metadata

`metadata.json` records `git_revision` as the repository revision captured when
CMake was configured, and `solver_version` from `CMakeLists.txt`. The effective
pseudo-time CFL of a run is recorded separately from the case-file value
(`cfl_scale`, `effective_cfl_initial`, `effective_cfl_max`), so a run that
deviates from the case schedule is visible in the metadata and not only in the
recorded command line.

## Originality

The finite-volume residual, the Riemann solvers, the least-squares
reconstruction and limiters, the boundary treatments, the LU-SGS implicit
solver, the dual-time loop, the CGNS import with multi-zone merging, the mesh
partitioning/distribution and every MPI communication routine in this
repository were written for this submission. Published algorithms (Roe, HLLC,
Barth–Jespersen, Venkatakrishnan, Yoon–Jameson LU-SGS, BDF2 dual time) were
re-implemented from their formulations; no code was copied or adapted from an
existing CFD package. External libraries are used only for infrastructure:
CGNS/HDF5 (mesh I/O), METIS (graph partitioning), `nlohmann/json` (JSON),
doctest (unit tests), MPI, and NumPy/Matplotlib/SciPy for post-processing.
