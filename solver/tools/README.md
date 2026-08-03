# solver/tools — Plotting and reporting utilities

Publication-style plots and report artifacts for the CFD solver benchmark
results. All scripts read the solver output files written into
`solver/results/<case_id>/`:

| File | Contents |
|------|----------|
| `residuals.csv` | step, physical_time, inner_iter, cfl, dt, rho, rhou, rhov, rhoE, residual_l2, residual_linf |
| `forces.csv`    | step, physical_time, cl, cd, cmz, pressure_drag, viscous_drag, pressure_lift, viscous_lift |
| `surface.csv`   | x, y, nx, ny, pressure, cp, cf, rho, u, v, mach, tag (wall faces) |
| `field_final.vtu` | ASCII XML UnstructuredGrid: points (x, y, 0), TRI/QUAD/POLYGON cells, cell data (density, velocity, pressure, mach, temperature, partition_rank) |
| `run_status.json` | case_id, command, mpi_ranks, wall_time_seconds, convergence_status, residual_reduction_orders, notes |

## Setup

```bash
cd solver
python3 -m venv .venv
source .venv/bin/activate
pip install numpy matplotlib
```

`vtk` is optional — the VTU reader in `plot_utils.py` parses the ASCII XML
directly with `xml.etree.ElementTree`.

## Generate everything

```bash
cd solver && source .venv/bin/activate
python tools/plot_all.py            # all figures for all cases -> report/figures/
python tools/generate_figure_manifest.py
python tools/generate_sanity_checks.py
python tools/generate_run_manifest.py
```

`plot_all.py` accepts optional `results_dir` and `figures_dir` arguments.

## Individual plot scripts

Each script takes `<case_dir> [case_id] [figures_dir]` and produces one PNG
per figure kind:

| Script | Output | Content |
|--------|--------|---------|
| `plot_residuals.py` | `<case>_residual.png` | Global L2/Linf residual and per-component residuals (log scale) |
| `plot_forces.py`    | `<case>_forces.png`  | C_l and C_d history (vs physical time for transient cases) |
| `plot_surface.py`   | `<case>_cp.png`      | Surface C_p: NACA vs x/c (sorted), cylinder vs theta |
| `plot_field.py`     | `<case>_mach.png`, `<case>_pressure.png`, `<case>_vorticity.png` | tricontourf field contours; vorticity for viscous cases, clipped to [-5, 5] for Re200 |

## Report artifacts

- `report/figures/*.png` — generated figures
- `report/figure_manifest.csv` — figure_file, case_id, figure_type, variable, source_file, caption
- `report/sanity_checks.json` — automated sanity checks (positivity, symmetric lift, positive drag, Re200 unsteadiness, Cp variation, no-slip wall velocity, figure existence)
- `report/run_manifest.csv` — per-case run summary

## Notes on the VTU reader

The solver writes each cell's vertices as fresh points (no sharing), so
cell-centered data is mapped to vertices by geometric-vertex averaging
(`cell_to_vertex`) for `tricontourf`. Vorticity (dv/dx - du/dy) is computed
per cell by least-squares gradients over edge-adjacent cells
(`compute_vorticity`); edge adjacency is detected via rounded coordinate
pairs. All readers tolerate partially-written trailing rows, so plots can be
generated while the solver is still running.
