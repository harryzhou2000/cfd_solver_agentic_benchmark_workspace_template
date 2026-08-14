# Distributed 2-D Compressible Finite-Volume Solver

This directory contains an original C++17 implementation of a cell-centred,
unstructured finite-volume solver for the supplied compressible-flow cases. It
uses CGNS for mesh input, METIS for cell-graph partitioning, and MPI for
rank-local owned/ghost mesh storage and neighbor-scoped halo exchange.

## Dependencies

The build requires CMake, a C++17 compiler, MPI, CGNS/HDF5, METIS, zlib, and
the supplied header-only `nlohmann_json` tree. By default the project resolves
the standard benchmark layout:

```text
../external/cfd_externals/install
../external/nlohmann
```

Set `CFD_EXTERNALS_ROOT` or pass the matching CMake cache variable when using a
different install prefix.

## Build

From the parent workspace:

```bash
cmake -S solver -B solver/build \
  -DCFD_EXTERNALS_ROOT="$PWD/external/cfd_externals/install"
cmake --build solver/build -j
```

## Solve a case

The executable has the benchmark-required interface:

```bash
mpirun -np 8 solver/build/cfd_solver solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  --output solver/results/naca0012_m015_inviscid \
  --report-level full
```

Use a fresh output directory for each completed invocation. A restart may be
loaded from a previous restart manifest or its containing directory, provided
the MPI partition count and local cell ordering match:

```bash
mpirun -np 8 solver/build/cfd_solver solve \
  --case <case.json> --output <new-output-dir> \
  --restart <old-output-dir>/restart_final.manifest.json
```

## Numerical method

- Conservative state: `[rho, rho*u, rho*v, rho*E]` for a calorically perfect
  gas with case-configured gamma, `R`, and Prandtl number.
- Cell-centred mixed-triangle/quad finite volume with least-squares primitive
  gradients, active Barth--Jespersen limiting of inviscid face reconstruction,
  and face-state positivity fallback. Viscous gradients are retained during
  any diagnostic reconstruction continuation, so a first-order face-state
  continuation does not silently change the laminar equations.
- Rusanov approximate Riemann flux; Newtonian stress/Fourier heat conduction
  for laminar cases with viscosity calculated from the requested Reynolds
  number.
- Farfield, inviscid slip-wall, and no-slip adiabatic-wall boundary treatments.
- Multi-sweep rank-local LU--SGS Rusanov corrections with local convective and
  viscous spectral-radius time scales. The Re200 path is a frozen-history BDF2
  physical-time outer loop with full spatial-plus-BDF residual checks,
  extrapolated nonlinear guesses, and retry-without-history-advance on an inner
  target miss.
- Rank zero preprocesses and METIS-partitions the global cell graph, sends
  compact rank-local partitions, and then releases the global mesh. Iterations
  retain only owned cells plus a one-ring ghost layer and exchange halo values
  through neighbor `MPI_Isend/Irecv` requests.

## Outputs and report generation

Each successful `solve` writes the CSV/JSON contract, rank-local VTU pieces,
a `field_final.pvtu` distributed index, and a complete multi-piece
`field_final.vtu` artifact for tools that require the literal final VTU name.
The root-only multi-piece assembly occurs after the distributed solve, so it
does not replicate mesh or solution state during iterations. MeshIO does not
directly open PVTU indices, so the included report tool reads and merges the
referenced rank-local VTU pieces without changing their field values.

Create the required local Python environment before generating figures and the
LaTeX report:

```bash
python3 -m venv solver/.venv
solver/.venv/bin/pip install -r solver/tools/requirements.txt
solver/.venv/bin/python solver/tools/generate_report.py --report solver/report \
  solver/results/<case-output> [...]
pdflatex -output-directory solver/report solver/report/report.tex
```

The report tool reads completed solver outputs only; it rejects missing,
non-finite, or failed output packages rather than manufacturing figures.

## Validation

After all eight required cases have completed, validate their outputs and
generated report with:

```bash
python3 cfd_solver_agentic_benchmark/examiner/validate_outputs.py \
  solver/results/naca0012_m015_inviscid \
  solver/results/naca0012_m080_inviscid \
  solver/results/naca0012_m200_inviscid \
  solver/results/naca0012_m015_laminar_re5000 \
  solver/results/naca0012_m080_laminar_re5000 \
  solver/results/naca0012_m200_laminar_re5000 \
  solver/results/cylinder_m010_laminar_re20 \
  solver/results/cylinder_m010_laminar_re200 \
  --report solver/report
```

The structural validator is deliberately only one gate. Review convergence,
force histories, wall values, fields, and rank-count consistency before calling
a result complete.
