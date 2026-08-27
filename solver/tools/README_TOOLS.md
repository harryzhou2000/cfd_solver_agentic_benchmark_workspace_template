# Post-processing toolchain

Python post-processing and plotting for the 2-D unstructured CFD solver.
Everything here depends only on numpy and matplotlib.

Interpreter: `/workspace/solver/.venv/bin/python`

## Modules

| File | Purpose |
|---|---|
| `vtu_reader.py` | numpy-only ASCII VTK XML (.vtu) reader; triangulation and area-weighted cell-to-point averaging |
| `plot_style.py` | shared publication style, colour maps, robust colour limits, `save_figure` |
| `plot_results.py` | all per-case figures (residuals, forces, cp, cf, Mach, pressure, velocity, vorticity) |
| `analyze_transient.py` | post-transient shedding statistics, Strouhal number, lift spectrum figure |
| `make_figures.py` | walks the results tree, plots every case, writes `figure_manifest.csv` |
| `make_test_fixture.py` | development fixture generator (synthetic case directory) |
| `check_pipeline.py` | end-to-end self-check, including the benchmark examiner's own manifest validator |

## Commands

Whole submission (the normal entry point):

```bash
/workspace/solver/.venv/bin/python make_figures.py \
    --results-root /workspace/solver/results \
    --out-dir      /workspace/solver/report/figures \
    --manifest     /workspace/solver/report/figure_manifest.csv
```

One case:

```bash
/workspace/solver/.venv/bin/python plot_results.py \
    --case-dir /workspace/solver/results/naca0012_m080_inviscid \
    --case-id  naca0012_m080_inviscid \
    --out-dir  /workspace/solver/report/figures
```

Optional flags: `--body cylinder|airfoil` (autodetected otherwise),
`--vorticity-clip 5.0`, `--levels 40`, `--no-farfield`.

Transient (Re 200) analysis:

```bash
/workspace/solver/.venv/bin/python analyze_transient.py \
    --case-dir /workspace/solver/results/cylinder_m010_laminar_re200 \
    --output   /workspace/solver/report/cylinder_m010_laminar_re200_transient.json \
    --figure-dir /workspace/solver/report/figures
```

Optional flags: `--window-fraction 0.4`, `--diameter 1.0`, `--velocity 1.0`.

Inspect a .vtu directly:

```bash
/workspace/solver/.venv/bin/python vtu_reader.py /workspace/solver/results/*/field_final.vtu
```

Development self-check (regenerates the fixture, runs everything, validates):

```bash
/workspace/solver/.venv/bin/python check_pipeline.py
```

## .vtu format contract for the C++ writer

The reader is deliberately strict about these points, because they are what
makes an ASCII .vtu unambiguous:

1. `<VTKFile type="UnstructuredGrid">` with exactly one `<Piece>`.
   Multiple pieces are rejected: write one file per case, gathered on rank 0.
2. Every `<DataArray>` must use `format="ascii"`. Appended, binary and
   compressed encodings are rejected with an explicit error.
3. Points: `NumberOfComponents="3"`, z written as 0. The reader keeps x, y.
4. Cells: `connectivity` (Int64), `offsets` (Int64), `types` (UInt8).
   `offsets` holds **cumulative end offsets**, so the last value equals the
   length of `connectivity`. A leading 0 with n+1 values is also accepted.
5. Node indices are 0-based and must lie inside the point array.
6. Cell types 5 (triangle) and 9 (quad); node counts must match the type.
   Quad nodes must be in cyclic (winding) order, not diagonal order, or the
   split into triangles will fold over.
7. Cell arrays are read by name: `density`, `velocity` (3 components),
   `pressure`, `mach`, `temperature`, `total_energy`, `vorticity`,
   `rank`. Every array must have exactly one entry per cell (times its
   component count). Lookup is case-insensitive.
8. Whitespace and line wrapping inside data blocks are free-form.
9. If `vorticity` is omitted the tools derive it from `velocity`, but writing
   it from the solver is preferred: the solver can use its own gradients.

## Conventions

* Airfoil cp: vs x/c, y-axis inverted, upper and lower surfaces separate.
* Cylinder cp/cf: vs wall angle in degrees from the front stagnation point
  (0 deg upstream, 180 deg rear, increasing anticlockwise through the upper
  surface over a single 0-360 deg curve).
* Surface rows are re-sorted along the body, so partition-ordered output still
  plots as a clean line.
* Field colour limits: 1st-99th percentile, with any clipping stated on the
  figure. Vorticity uses a symmetric range clipped to +/-5 by default.
* Manifest `variable` is exactly `mach` / `pressure` for those fields: the
  examiner checks both filename-vs-variable and exact per-case tokens.
* View windows: airfoil x[-0.5, 1.5] y[-0.75, 0.75]; cylinder x[-2, 8] y[-3, 3],
  each intersected with the actual mesh extent.

`probe_out/` holds development fixtures and scratch output only. It must
never be confused with `/workspace/solver/results/`, which holds real solver
output.
