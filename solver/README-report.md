# Reproducible reporting

The reporting workflow reads solver outputs; it never fabricates missing field,
force, surface, or convergence data. It recognizes the eight required case
directories under `solver/results/` and writes an honest failed status for a
missing, malformed, or sanity-check-failing case.

Create the required local environment once:

```bash
cd solver
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

Regenerate all figures, manifests, sanity checks, and `report/report.tex`:

```bash
.venv/bin/python tools/generate_report.py --results results --report report
```

To build a PDF when a local LaTeX installation supplies `pdflatex`:

```bash
.venv/bin/python tools/generate_report.py --results results --report report --compile-pdf
```

For an explicit location of the supplied case JSON files (used to convert the
Re200 lift-spectrum frequency to Strouhal number), add:

```bash
.venv/bin/python tools/generate_report.py --results results --report report \
  --case-inputs ../cfd_solver_agentic_benchmark/inputs/cases
```

The generator consumes ASCII polygon `field_final_rank*.vtu` files (or a
single `field_final.vtu`), CSV histories, `metadata.json`, and `run_status.json`.
`figure_manifest.csv` names each plot's exact source and variable;
`run_manifest.csv` comes from the result metadata/status; and
`sanity_checks.json` records values computed from submitted fields/surfaces/
forces. Mach and pressure images are polygon-filled continuous field plots,
not point clouds. It also writes near-body NACA and cylinder wake detail views,
plus a Re200 velocity-magnitude wake plot.

## MPI comparison evidence

Do not reconstruct rank comparisons from debug logs. Preserve the actual run
records in one or more CSV files under `results/rank_comparisons/`, each with
this exact header:

```text
case_id,mpi_ranks,command,wall_time_seconds,final_step,final_physical_time,cd,cl,cmz,residual_reduction_orders,owned_cells_min,owned_cells_max,ghost_cells_mean,load_balance_ratio,partition_edge_cut,notes
```

Each row is one completed run and must contain the exact command. The generator
copies the evidence into `report/rank_comparison_manifest.csv`, renders timing,
final-force, residual, owned/ghost, balance, and edge-cut values in the report,
and prints every command verbatim. Include at least an `np=8` and a smaller
rank count for one NACA case and one cylinder case.

For Re200, the generator FFTs the final half of the submitted finite,
physical-time lift history after linear resampling. It writes
`report/re200_lift_spectrum.csv`, creates a spectrum figure, and reports the
dominant frequency and $St=fL_{ref}/U_\infty$ only when the supplied case JSON
contains positive reference length and freestream velocity.
