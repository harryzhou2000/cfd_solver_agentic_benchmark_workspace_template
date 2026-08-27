# cns2d — 2-D unstructured compressible Navier–Stokes finite-volume solver

`cns2d` is an original, from-scratch cell-centred finite-volume solver for the
two-dimensional compressible Navier–Stokes equations on mixed unstructured
(triangle + quadrilateral) meshes. It is written in C++17, parallelised with MPI,
and partitioned with METIS. It is the submission for the CFD solver agentic
benchmark in `../cfd_solver_agentic_benchmark/`, and it runs all eight required
benchmark cases from their supplied JSON files with no source edits and no
per-case special-casing.

Everything in this document was checked against the source, the scripts and the
recorded run logs in this tree. Where a number is a measurement on this
particular container rather than a property of the code, it is labelled as such.

## What is implemented

Discretization

- Cell-centred finite volumes on the mesh dual, mixed TRI/QUAD, a single unified
  code path for both cell types.
- Second-order reconstruction by piecewise-linear inverse-distance-weighted
  least-squares gradients of the **primitive** variables, with a
  Venkatakrishnan (default) or Barth–Jespersen limiter.
- Inviscid flux: Roe with a Harten–Hyman entropy fix extended to the linear
  (entropy/shear) waves; HLLC and Rusanov are also implemented, and HLLC doubles
  as a positivity fallback.
- Viscous flux: Newtonian stress tensor and Fourier heat flux built from the
  same least-squares primitive gradients. Viscosity is derived from the case
  Reynolds number, not hard-coded.
- Boundary conditions: characteristic (Riemann-invariant) farfield, slip wall,
  no-slip adiabatic wall. Each BC supplies two distinct states — a mirrored
  *ghost* state for the Riemann flux and the physical *face* state used by the
  gradient stencil, the limiter envelope and `surface.csv` — so wall output
  reports true boundary values rather than near-wall cell averages
  (`src/physics/boundary_conditions.h`).

Time integration

- Steady cases: implicit pseudo-time march with local time stepping, a CFL ramp
  taken from the case file, and a matrix-free LU-SGS inner solve.
- Transient case (cylinder Re 200): true dual-time BDF2 — an outer physical-time
  loop with frozen history levels and an inner nonlinear solve driven to a
  relative-residual target on the *total* (spatial + physical-time) residual.

Parallelism

- METIS k-way partitioning of the cell dual graph, done once at start-up.
- Iteration-time communication is strictly neighbour-scoped non-blocking
  point-to-point (`MPI_Isend`/`MPI_Irecv`) halo exchange. No rank holds the
  full mesh or the full conservative state during iterations.
- Mesh reading handles the multi-zone cylinder mesh, including
  `GridConnectivity_t`/`Abutting1to1` zone interfaces (that mesh reports **zero**
  `1to1` connectivities, so `cg_nconns`/`cg_conn_read` must be used).

Source layout under `src/`:

| Directory | Contents |
|---|---|
| `core/` | types, case-file parsing and validation, CLI options, logging, path helpers, exceptions |
| `mesh/` | CGNS reader, global mesh assembly and node merge, geometry metrics, geometric verification |
| `parallel/` | METIS partitioner, distributed mesh, halo exchange |
| `physics/` | perfect-gas relations, boundary-condition states |
| `numerics/` | gradients, limiters, Riemann solvers, viscous flux, residual assembly, solution field |
| `solve/` | local time step, LU-SGS, solver context, steady driver, transient (BDF2 dual-time) driver |
| `io/` | output writer (CSV/JSON contract files), ASCII VTU writer, restart I/O |
| `post/` | force integration, surface distributions |

Other top-level directories: `tests/` (unit tests), `tools/` (Python
post-processing plus the run driver and a standalone CGNS probe), `report/`
(LaTeX report and its data harvesters), `results/` (one directory per case),
`scratch/` (development experiments and run logs; provenance, not a deliverable).

## Dependencies

Toolchain (versions verified in this container):

| Component | Version | Notes |
|---|---|---|
| CMake | 3.28.3 | `CMakeLists.txt` requires >= 3.16 |
| C++ compiler | g++ 13.3.0 | C++17, no compiler extensions |
| MPI | Open MPI 4.1.6 | mandatory; `find_package(MPI REQUIRED COMPONENTS CXX)` |
| Python | 3.12.3 | post-processing only, in a venv (below) |
| LaTeX | pdfTeX 3.141592653 (TeX Live 2023), latexmk 4.83 | report build only |

Compiled third-party libraries, expected under one install prefix
(`/opt/external/cfd_externals/install` in this container; `/workspace/external`
is a symlink to `/opt/external`):

| Library | Version found | Used for |
|---|---|---|
| CGNS | 4.5 (`CGNS_DOTVERS 4.50`) | reading the benchmark `.cgns` meshes |
| HDF5 | 1.14.3 | CGNS backing store |
| METIS | 5.1.0 | `METIS_PartGraphKway` partitioning |
| zlib | 1.3.1 | linked transitively by HDF5/CGNS |

Header-only libraries, expected under a separate header root (`/opt/external`
here):

| Library | Version found | Used for |
|---|---|---|
| nlohmann/json | 3.12.0 | case-file parsing, `metadata.json` / `run_status.json` writing |
| Eigen | 5.0.1 | detected opportunistically and sets `CNS2D_HAVE_EIGEN`, but no solver source includes Eigen, so the build does not actually depend on it |

ParMETIS is present in the externals tree but is **not** linked or used: the
partitioner is serial METIS k-way applied to the dual graph before distribution.
`fmt` is likewise present and unused — formatting goes through
`src/core/logging.h`.

Python packages in the venv: numpy 2.5.2, matplotlib 3.11.1 (plus their
transitive dependencies). Nothing else is needed; the `.vtu` reader is
hand-written numpy, with no VTK dependency.

## Build

From `/workspace/solver`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCFD_EXTERNALS_ROOT=/opt/external/cfd_externals/install \
      -DCFD_EXTERNALS_HEADER_ROOT=/opt/external
cmake --build build -j
```

This produces `build/cns2d` (the solver) and `build/cns2d_tests` (unit tests).

Path configuration, in priority order (see `CMakeLists.txt`):

- `-DCFD_EXTERNALS_ROOT=<prefix>` — the CMake cache variable wins.
- `CFD_EXTERNALS_ROOT` environment variable.
- Autodetection, which probes `<source>/../external/cfd_externals/install` and
  then `/opt/external/cfd_externals/install` for `include/cgnslib.h`. Because
  `/workspace/external` symlinks to `/opt/external`, autodetection succeeds in
  this container and the two flags above are strictly optional. They are
  documented anyway, because relying on autodetection is exactly the
  machine-specific assumption the benchmark asks solvers to avoid.
- `-DCFD_EXTERNALS_HEADER_ROOT=<dir>` — directory containing `nlohmann/`,
  `eigen/`, and so on. Defaults to `<CFD_EXTERNALS_ROOT>/../..`.
- `-DCNS2D_EXTRA_RELEASE_FLAGS=...` — optimisation flags appended for Release
  builds. Default `-O3`. No `-march=native`, so binaries stay portable.

CMake records `git rev-parse --short=12 HEAD` into `build/generated/version.h`,
which is what appears as `git_revision` in every `metadata.json`. Building from
a tarball without `.git` simply leaves that field empty.

`build/cns2d` is linked with a `RUNPATH` covering both the Open MPI and the
externals library directories, so it runs with no environment setup.
**Manually compiled** helper binaries (anything under `scratch/`, or a
hand-rolled `g++` line) may need:

```bash
export LD_LIBRARY_PATH=/opt/external/cfd_externals/install/lib:$LD_LIBRARY_PATH
```

## Run

### One case

```bash
cd /workspace/solver
mpirun --allow-run-as-root -np 4 ./build/cns2d solve \
    --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
    --output results/naca0012_m015_inviscid \
    --log-every 250
```

`--allow-run-as-root` is needed only because this container runs MPI as a
privileged user; drop it elsewhere. The output directory is created if absent.
Mesh paths inside the case files are relative to the case file
(`"file": "../meshes/CylinderB1.cgns"`), so the supplied case files work in
place, with no copying or editing.

### The whole suite

`tools/run_cases.sh` is the production driver. It runs each case with **exactly**
the parameters in its JSON file — no CLI overrides other than `--log-every 250`.

```bash
cd /workspace/solver
tools/run_cases.sh -n 4                              # all eight cases
tools/run_cases.sh -n 4 naca0012_m080_inviscid       # selected case ids
tools/run_cases.sh -n 4 -o /path/to/other_results    # alternate results root
```

Options and environment:

| Knob | Meaning |
|---|---|
| `-n <ranks>` | MPI rank count (script default 8; production used 4 — see the CPU-quota note) |
| `-o <dir>` | results root (default `<solver>/results`) |
| positional args | case ids to run; none means all eight |
| `CNS2D_EXECUTABLE` | override the solver binary path |
| `CNS2D_BENCHMARK_DIR` | override the benchmark repo location (default `<solver>/../cfd_solver_agentic_benchmark`) |
| `CNS2D_CPU_LIST` | if set, wraps `mpirun` in `taskset -c <list>` and adds `--bind-to none`, to give concurrent jobs disjoint CPUs |

Each case writes `<results>/<case_id>/` plus a launch log
`<results>/<case_id>.launch.log`. The script exits nonzero if any case fails.

`CNS2D_CPU_LIST` exists because Open MPI's default binding maps every
independent job onto the same cores starting from the first socket, so N
concurrent jobs oversubscribe those cores while the rest of the machine idles.
`taskset` is used rather than `mpirun --cpu-set` because the latter rejects CPU
ids outside the first NUMA node on this machine and fails with a bare exit
status 1.

The exact production schedule behind the submitted results is
`scratch/production.sh`: the seven steady cases sequentially at `-n 4`, then the
Re 200 transient alone at `-n 4`.

## CLI contract

```text
mpirun -np <ranks> cns2d solve --case <case.json> --output <dir>
       [--restart <file>] [--report-level brief|full]
```

That is the form required by `OUTPUT_CONTRACT.md`, and `solve` is the only
supported command. `--case` and `--output` are mandatory; a missing case file or
a missing restart file is diagnosed before any work starts.

Debug and verification overrides — all default to "use the case file", and
**production runs use none of them**:

| Flag | Effect |
|---|---|
| `--max-steps N` | override `run_control.max_steps`. **Steady cases only** (`src/solve/steady_driver.cpp:28`). |
| `--final-time T` | override `run_control.final_time`. **Transient cases only** (`src/solve/transient_driver.cpp:34`). |
| `--log-every N` | progress-log cadence in steps (default 100) |
| `--no-intermediate-fields` | suppress the periodic `field_NNNNN.vtu` dumps of a transient run |
| `--spatial-order 1\|2` | force first- or second-order reconstruction |
| `--flux roe\|hllc\|rusanov` | force the inviscid Riemann solver |
| `--limiter barth_jespersen\|venkatakrishnan\|none` | force the limiter |
| `--venkat-k K` | Venkatakrishnan smoothing constant |
| `-h`, `--help` | usage text |

**The `--max-steps` / `--final-time` split is a real trap.** `--max-steps` caps
*pseudo-time* steps and is read only by the steady driver; `--final-time` bounds
the *physical-time* horizon and is read only by the transient driver. Passing
`--max-steps` to the Re 200 transient does nothing at all — the run will still go
to t = 300. To shorten the transient for a smoke test, use `--final-time`:

```bash
mpirun --allow-run-as-root -np 4 ./build/cns2d solve \
    --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json \
    --output scratch/smoke_re200 --final-time 0.5 --no-intermediate-fields
```

Each override that changes the scheme logs a `verification override:` warning
into `stdout.log`, and `metadata.json` describes the scheme the run *actually*
used, derived from the configured objects rather than from a hard-coded string.
An overridden run therefore cannot silently masquerade as a production run.

`--report-level` is accepted and validated because the contract requires it, but
it is currently inert: the solver always writes the full output set. It is
parsed and stored in `src/core/options.cpp` and no other module reads it. Stated
plainly rather than dressed up as a feature.

Exit status: `0` on normal completion; `1` if `MPI_Init` fails; `2` for malformed
input, a missing mesh, an unsupported boundary condition, or a failed
initialisation; `3` if the loop ran but the result is marked numerically failed.
The status is `MPI_Allreduce`d with `MPI_MAX` before `MPI_Finalize`, so a single
diverged partition fails the whole job instead of letting `mpirun` report
success.

## The eight cases

Both meshes come from `../cfd_solver_agentic_benchmark/inputs/meshes/`.
`NACA0012_H2.cgns` is single-zone, 20816 cells / 36498 faces (10752 TRI + 10064
QUAD); `CylinderB1.cgns` is two-zone and becomes 10185 cells / 20180 faces after
the interface node merge.

| Case id | Mesh | Type | Controls taken from the case file |
|---|---|---|---|
| `naca0012_m015_inviscid` | NACA | steady | `max_steps` 20000, target 4.0 orders |
| `naca0012_m080_inviscid` | NACA | steady | `max_steps` 30000, target 4.0 |
| `naca0012_m200_inviscid` | NACA | steady | `max_steps` 40000, target 3.0 |
| `naca0012_m015_laminar_re5000` | NACA | steady | `max_steps` 40000, target 4.0 |
| `naca0012_m080_laminar_re5000` | NACA | steady | `max_steps` 40000, target 4.0 |
| `naca0012_m200_laminar_re5000` | NACA | steady | `max_steps` 50000, target 3.0 |
| `cylinder_m010_laminar_re20` | cylinder | steady | `max_steps` 30000, target 5.0 |
| `cylinder_m010_laminar_re200` | cylinder | transient | `dt` 0.01, `final_time` 300.0 (30000 physical steps), inner 5..1000, inner target 1e-3 |

The inviscid NACA cases use `slip_wall` on boundary family `bc-4` and `farfield`
on `bc-2`; the laminar NACA cases use `no_slip_adiabatic_wall` on `bc-4`. The
cylinder cases use families `WALL` and `FAR`. All of that comes from the case
JSON, never from the code.

## Outputs produced per case

Written into `--output`, matching `OUTPUT_CONTRACT.md`:

| File | Contents |
|---|---|
| `metadata.json` | case and solver identity, mesh and partition sizes, the method description derived from the configured scheme, inner-iteration statistics, timestamps, `completed`, `convergence_status` |
| `run_status.json` | the verbatim command, ranks, final step, final physical time, residual orders, wall time, status, and a human-readable `notes` string explaining *why* the run stopped |
| `partition_diagnostics.csv` and `.json` | per-rank owned/ghost cells, boundary faces, neighbour ranks, send/recv counts; the JSON adds global edge cut and load balance |
| `residuals.csv` | contract header `step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf` |
| `forces.csv` | contract header `step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift` |
| `surface.csv` | contract header **exactly** `x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag` |
| `surface_cell_center.csv` | companion diagnostic: cell-centre versus boundary-value velocity and Mach, plus tangential wall shear, side by side |
| `field_final.vtu` | single-piece ASCII VTK XML; cell arrays `density, velocity(3), pressure, mach, temperature, total_energy, vorticity, rank` |
| `restart_final.bin` | binary restart, usable via `--restart` |
| `stdout.log` | full solver log including setup, geometry verification and progress lines |

The contract validator compares the `surface.csv` header for **exact** equality,
so the extra diagnostic columns live in `surface_cell_center.csv` instead of
being appended to `surface.csv`. That split is deliberate; do not tidy them back
together.

Transient runs additionally write `field_NNNNN.vtu` every
`outputs.write_field_every_time` of physical time (1.0 for the Re 200 case, so
about 300 files at roughly 3.5 MB each, around 1 GB in total).
`--no-intermediate-fields` suppresses them; they are wake-visualisation
material, not a substitute for `field_final.vtu`.

## Tests

```bash
./build/cns2d_tests
```

A self-contained, non-MPI harness (`tests/unit_tests.cpp`) covering polygon
geometry, the perfect-gas conversions, Riemann-solver consistency and
conservation, limiter bounds, the viscous stress algebra, and the wall-flux
identities. It prints `cns2d unit tests: <N> checks, <M> failure(s)` and returns
nonzero if any check fails. Last recorded run in this tree: **130 checks, 0
failures** (`scratch/t4.log`). It is also registered with CTest, so
`ctest --test-dir build` works.

The heavier verification exercises — the analytic Jacobian against central
finite differences over 20000 random states, LU-SGS spectral-radius
measurements, the discrete conservation probe — are separate programs under
`scratch/`. They are development instruments, kept for provenance, and are
described in the report rather than wired into the unit-test binary.

## Python tooling

A virtualenv lives at `.venv`. Always call it by path; do not rely on an
activated shell.

```bash
python3 -m venv .venv
.venv/bin/pip install numpy matplotlib
.venv/bin/python --version    # Python 3.12.3
```

| Tool | Purpose |
|---|---|
| `tools/make_figures.py` | walks the results tree, plots every case, writes the figure manifest — the normal entry point |
| `tools/plot_results.py` | per-case figures (residuals, forces, cp, cf, Mach, pressure, velocity, vorticity) |
| `tools/plot_style.py` | shared figure style, colour maps, robust colour limits |
| `tools/vtu_reader.py` | numpy-only strict ASCII `.vtu` reader; also runnable directly to inspect a field file |
| `tools/analyze_transient.py` | post-transient shedding statistics, Strouhal number, lift spectrum |
| `tools/transient_status.py` | quick shedding-state report for the Re 200 case (period, Strouhal number, amplitude, completed cycles); tolerant of being run while the solver is mid-write |
| `tools/make_test_fixture.py` | synthetic results tree for developing the plotting code without a solver run |
| `tools/check_pipeline.py` | end-to-end self-check of the post-processing chain, including the benchmark's own manifest validator |
| `tools/build_probe.sh`, `tools/probe_cgns.cpp` | standalone CGNS mesh probe used to establish the mesh facts quoted in the report |

Figures for the whole submission:

```bash
cd /workspace/solver
.venv/bin/python tools/make_figures.py \
    --results-root results \
    --out-dir report/figures \
    --manifest report/figure_manifest.csv
```

Useful flags: `--case <id>` (repeatable) to restrict the set,
`--vorticity-clip 5.0`, `--levels 40`, `--window-fraction 0.4`, `--no-farfield`.

Shedding state of the transient at any time, including while it is still
running:

```bash
.venv/bin/python tools/transient_status.py \
    --forces results/cylinder_m010_laminar_re200/forces.csv
```

More detail in `tools/README_TOOLS.md`, including the `.vtu` format contract the
reader enforces on the C++ writer.

## Report

```bash
cd /workspace/solver/report
./refresh.sh
```

`refresh.sh` re-harvests every number from `results/` and then rebuilds the PDF.
It runs `harvest_numbers.py` (writing `numbers_auto.tex` and the four `tab_*.tex`
table bodies) and `make_artifacts.py` (writing `run_manifest.csv` and
`sanity_checks.json`), then `latexmk`. It finishes by printing the pdflatex error
count, the undefined-reference count and the `Output written on` line, so a
silent failure is visible.

Plain rebuild without re-harvesting:

```bash
cd /workspace/solver/report && latexmk -pdf report.tex
```

Two things about `refresh.sh` worth knowing before editing it:

- It exports `CNS_HARVEST_MIN_MTIME`, derived from `CNS_TRUSTED_AFTER` inside the
  script. Any case directory older than that timestamp is reported as *run in
  progress* instead of being quoted, because an earlier build had a defective
  steady termination test and its results must not be presented as final. Raise
  the cutoff when re-runs supersede earlier ones; never lower it.
- `harvest_numbers.py` computes no physics. Every value it emits is copied or
  reduced from a file the solver wrote, and the shedding statistics are obtained
  by shelling out to `tools/transient_status.py`, so the report and the run-time
  diagnostic cannot disagree. Unfinished cases expand to a visible
  `[run in progress]` marker, never to a plausible-looking placeholder.

`report.tex` pulls in `preamble.tex`, `numbers.tex` (fixed verification
measurements), `numbers_auto.tex` (harvested per-case numbers) and the `sec_*.tex`
sections. The rank-count scaling table is harvested from `results/scaling/*/`, so
the scaling study must exist before `refresh.sh` can fill it in.

## Validation

The benchmark ships its own structural validator. Run it over the case
directories and the report together:

```bash
cd /workspace/solver
.venv/bin/python ../cfd_solver_agentic_benchmark/examiner/validate_outputs.py \
    results/naca0012_m015_inviscid \
    results/naca0012_m080_inviscid \
    results/naca0012_m200_inviscid \
    results/naca0012_m015_laminar_re5000 \
    results/naca0012_m080_laminar_re5000 \
    results/naca0012_m200_laminar_re5000 \
    results/cylinder_m010_laminar_re20 \
    results/cylinder_m010_laminar_re200 \
    --report report
```

It checks file existence, exact CSV headers, required JSON fields and finiteness;
for the report it checks that `report.tex`, `run_manifest.csv`,
`sanity_checks.json`, `figures/` and `figure_manifest.csv` all exist, that every
manifested figure is on disk, and that each case has both a `mach` and a
`pressure` manifest entry (plus a `vorticity` or `velocity` entry for Re 200).
Note that the manifest `variable` column is matched by **substring** against the
filename but by **exact set membership** per case, so a Mach figure must be
described as exactly `mach`.

Passing this validator is necessary but not sufficient — physics plausibility,
MPI correctness, originality and report quality are scored separately.

Independent end-to-end check of the post-processing chain, which builds a
synthetic results tree and validates the figures and manifest produced from it:

```bash
.venv/bin/python tools/check_pipeline.py
```

## Performance and the 4-CPU quota (read before benchmarking)

This container advertises far more parallelism than it can deliver, and it will
mislead you if you trust `nproc`.

```text
nproc                      -> 64
lscpu                      -> Intel Xeon Gold 6326, 16 cores/socket x 2 sockets
/sys/fs/cgroup/cpu.max     -> "400000 100000"   i.e. a hard quota of 4 CPUs
```

The cgroup quota is the binding constraint: **4 CPUs of aggregate throughput**,
no matter how many ranks are launched or how many jobs run at once. Two
consequences shaped every timing in the submission.

- `np=4` is the measured optimum. `np=8` oversubscribes the quota and is
  *slower*. Measured on `naca0012_m015_laminar_re5000` at a fixed 1500-step
  budget: 63.3 s at np=1, 43.7 s at np=2, 27.6 s at np=4, 70.4 s at np=8
  (`report/tab_scaling.tex`). The np=8 rows are kept because they demonstrate
  rank-count-independent answers, not because np=8 is a sensible choice here.
- Cases run **strictly sequentially**. Concurrent cases merely split the same 4
  CPUs, so there is no throughput gain and every stream finishes later. That is
  why `scratch/production.sh` serialises the suite instead of fanning it out.

On real hardware, re-measure. The np=4 choice is a property of this container's
quota, not of the solver, and a genuine 32-core machine should be expected to
scale considerably further. Solution consistency across rank counts is a
separate matter and does hold: force coefficients agree to within 5e-4 relative
across np = 1, 2, 4, 8 (worst case 4.7e-4 on the cylinder at np=8), while the
METIS edge cut grows from 0 to 471.

Measured wall times at np=4 under the quota:

- Seven steady cases: 22 to 67 s each; the whole steady suite took 5 min 14 s
  (07:58:47 to 08:04:01 in `scratch/production_progress.txt`).
- Cylinder Re 200 transient: **roughly 4 to 5 hours.** Observed in flight, it
  reached t = 75.0 of 300 after 59 minutes at about 140 inner iterations per
  physical step, which extrapolates to close to 4 h for the full 30000 physical
  steps at dt = 0.01. Budget accordingly: this single case dominates the cost of
  the whole submission.

The rank-count study itself is `scratch/scaling_study.sh`, which runs
`cylinder_m010_laminar_re20` and `naca0012_m015_laminar_re5000` at np = 1, 2, 4, 8
with a fixed `--max-steps 1500` budget into `results/scaling/<case>_np<N>/`. A
fixed step budget makes the comparison like-for-like: wall time per step is the
performance measure and the force coefficient at the common final step is the
consistency measure.

## Reproduce everything from a clean checkout

Assumes the benchmark repository at `../cfd_solver_agentic_benchmark` and the
externals tree at `/opt/external` (or a `../external` symlink to it). Every step
runs from `/workspace/solver` unless stated. Total cost is dominated by step 6.

```bash
cd /workspace/solver

# 1. Build.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCFD_EXTERNALS_ROOT=/opt/external/cfd_externals/install \
      -DCFD_EXTERNALS_HEADER_ROOT=/opt/external
cmake --build build -j

# 2. Unit tests must pass before any result is trusted.
./build/cns2d_tests

# 3. Python environment for post-processing.
python3 -m venv .venv
.venv/bin/pip install numpy matplotlib

# 4. Smoke test: a short transient that exercises the dual-time path in ~1 min.
mpirun --allow-run-as-root -np 4 ./build/cns2d solve \
    --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json \
    --output scratch/smoke_re200 --final-time 0.5 --no-intermediate-fields

# 5. The seven steady cases, sequentially, at the measured optimum (~5 min).
tools/run_cases.sh -n 4 \
    cylinder_m010_laminar_re20 \
    naca0012_m015_inviscid \
    naca0012_m080_inviscid \
    naca0012_m200_inviscid \
    naca0012_m015_laminar_re5000 \
    naca0012_m080_laminar_re5000 \
    naca0012_m200_laminar_re5000

# 6. The Re 200 transient, alone, with the whole quota (~4-5 h).
tools/run_cases.sh -n 4 cylinder_m010_laminar_re200

# 7. Rank-count study feeding the scaling table (writes results/scaling/).
bash scratch/scaling_study.sh

# 8. All figures and the figure manifest.
.venv/bin/python tools/make_figures.py \
    --results-root results --out-dir report/figures \
    --manifest report/figure_manifest.csv

# 9. Harvest numbers, write run_manifest.csv + sanity_checks.json, build the PDF.
cd report && ./refresh.sh && cd ..

# 10. Validate every case directory and the report.
.venv/bin/python ../cfd_solver_agentic_benchmark/examiner/validate_outputs.py \
    results/naca0012_m015_inviscid results/naca0012_m080_inviscid \
    results/naca0012_m200_inviscid results/naca0012_m015_laminar_re5000 \
    results/naca0012_m080_laminar_re5000 results/naca0012_m200_laminar_re5000 \
    results/cylinder_m010_laminar_re20 results/cylinder_m010_laminar_re200 \
    --report report
```

Steps 5 to 7 are independent in principle, but run them sequentially: under the
4-CPU quota, overlapping them makes everything slower without finishing anything
sooner. Step 9 must follow steps 5 to 8, because the report's tables and sanity
checks are harvested from `results/` and its figure list from `report/figures/`.

Deliverables produced: `results/<case_id>/` for all eight cases,
`report/figures/*.png` with `report/figure_manifest.csv`,
`report/run_manifest.csv`, `report/sanity_checks.json`, and `report/report.pdf`
built from `report/report.tex`.

## Caveats

- `--report-level` is parsed and validated but does not change the output set.
- ParMETIS, Eigen and fmt are located by the build but not used by the solver
  sources. Only CGNS, HDF5, METIS, zlib, nlohmann/json and MPI are real
  dependencies.
- `scratch/` holds development experiments, one-off probes and run logs. It is
  provenance, not a deliverable, and nothing in `src/`, `tools/` or `report/`
  depends on it — except that `production.sh` and `scaling_study.sh`, the two
  scripts that produced the submitted runs, happen to live there and are
  referenced above so the exact schedules stay reproducible.
- The `--allow-run-as-root` flag on every `mpirun` line is specific to this
  container's privileged user and should be dropped elsewhere.
