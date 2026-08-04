# Solver post-processing tools

These tools create benchmark-report artifacts **only from completed solver
output directories**. They never create, pad, or interpolate a missing solver
history or field. A missing CSV column, an invalid final field, or a failed
physics sanity check is an error and must be fixed in the solver run.

Create the Python environment once:

```bash
python3 -m venv .venv
.venv/bin/pip install -r solver/tools/requirements.txt
```

After running the solver for all cases, generate a report directory. The
arguments are the actual directories passed to `solve --output` (they may have
any directory name):

```bash
.venv/bin/python solver/tools/generate_report.py \
  --report solver/report \
  results/naca0012_m015_inviscid results/cylinder_m010_laminar_re200
```

The command writes `report.tex`, `generated_results.tex`, `run_manifest.csv`,
`figure_manifest.csv`, `sanity_checks.json`, and PNG figures below
`solver/report/figures/`. It replaces only these generated report artifacts.
Run it with every required case to make a final submission. Compile the
generated report if LaTeX is installed:

```bash
cd solver/report && latexmk -pdf report.tex
```

`meshio` reads VTK/VTU/CGNS field files. The solver must write named density,
pressure, Mach, and velocity fields (case-insensitive aliases are accepted;
velocity may be `u`/`v`, component names, or a two-component `velocity`
vector).
For scalar data at mesh vertices, the tool writes filled contours; for
cell-centred data it writes filled unstructured-cell renderings. It does not
fall back to a scatter plot.
