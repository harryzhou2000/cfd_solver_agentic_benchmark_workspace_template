#!/usr/bin/env python3
"""Generate CFD benchmark report artifacts.

Usage:
    .venv/bin/python3 tools/generate_report.py [--results-dir results] [--report-dir report]

This script:
  1. Runs tools/plot_results.py for every case in the results directory.
  2. Writes report/figure_manifest.csv mapping each figure to its source data.
  3. Writes report/sanity_checks.json with physics validation checks.
  4. Writes report/run_manifest.md summarising all case runs.

All output is written under the report directory (default: report/).
The script is robust: missing files or failed plots are reported but do not
crash the overall report generation.
"""
from __future__ import annotations
import csv
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np

# ---------------------------------------------------------------------------
# Import plotting helpers (for sanity-check field reading)
# ---------------------------------------------------------------------------
_SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPT_DIR))
import plot_results as pr  # noqa: E402

# ---------------------------------------------------------------------------
# Figure metadata helpers
# ---------------------------------------------------------------------------

# Maps figure filename suffix to (figure_type, variable, source_file, caption)
_FIG_META = {
    "_residual.png": (
        "residual_history",
        "rho,rhou,rhov,rhoE",
        "residuals.csv",
        "Residual convergence history showing density and momentum residual norms versus iteration.",
    ),
    "_forces.png": (
        "force_history",
        "cl,cd",
        "forces.csv",
        "Lift and drag coefficient evolution showing convergence or vortex shedding behaviour.",
    ),
    "_surface_cp.png": (
        "surface_cp",
        "cp",
        "surface.csv",
        "Surface pressure coefficient distribution along the body wall.",
    ),
    "_mach.png": (
        "field_contour",
        "mach",
        "field_final_*.vtu",
        "Mach number field contour with percentile-clipped colour range.",
    ),
    "_pressure.png": (
        "field_contour",
        "pressure",
        "field_final_*.vtu",
        "Pressure field contour with percentile-clipped colour range.",
    ),
    "_vorticity.png": (
        "vorticity_contour",
        "vorticity",
        "field_final_*.vtu",
        "Vorticity wake visualization with clipped range [-5, 5] showing vortex shedding.",
    ),
}


def build_figure_manifest(report_dir, case_ids):
    """Scan the figures directory and write figure_manifest.csv."""
    figures_dir = report_dir / "figures"
    manifest_path = report_dir / "figure_manifest.csv"
    rows = []
    if not figures_dir.exists():
        print("  [warn] figures directory not found: %s" % figures_dir)
    else:
        for fig_path in sorted(figures_dir.glob("*.png")):
            fname = fig_path.name
            # Determine case_id by matching known case_ids
            case_id = None
            for cid in case_ids:
                if fname.startswith(cid + "_"):
                    case_id = cid
                    break
            if case_id is None:
                # Try to extract from filename
                for suffix in sorted(_FIG_META.keys(), key=len, reverse=True):
                    if fname.endswith(suffix):
                        case_id = fname[:-len(suffix)]
                        break
            if case_id is None:
                continue
            # Find matching suffix
            for suffix, (fig_type, variable, source, caption) in _FIG_META.items():
                if fname.endswith(suffix):
                    rows.append({
                        "figure_file": fname,
                        "case_id": case_id,
                        "figure_type": fig_type,
                        "variable": variable,
                        "source_file": source,
                        "caption": caption,
                    })
                    break
    with open(manifest_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "figure_file", "case_id", "figure_type", "variable", "source_file", "caption"
        ])
        writer.writeheader()
        for row in rows:
            writer.writerow(row)
    print("  [ok] figure_manifest.csv (%d entries)" % len(rows))
    return rows


# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------

def _safe_float(val, default=0.0):
    try:
        return float(val)
    except (ValueError, TypeError):
        return default


def _fmt_val(val):
    """Format a numeric value for the run manifest, marking diverged values."""
    try:
        v = float(val)
    except (ValueError, TypeError):
        return "-"
    if not np.isfinite(v):
        return "inf/nan"
    if abs(v) > 1e3:
        return "diverged"
    return "%.4f" % v


def check_positive_density_pressure(field):
    """Check that all field density and pressure values are positive."""
    result = {"passed": True, "details": {}}
    if field is None:
        result["passed"] = False
        result["details"]["error"] = "No field data available"
        return result
    cell_data = field["cell_data"]
    rho = cell_data.get("Density")
    pres = cell_data.get("Pressure")
    if rho is None or pres is None:
        result["passed"] = False
        result["details"]["error"] = "Density or Pressure not found in field data"
        return result
    n_neg_rho = int(np.sum(rho <= 0))
    n_neg_pres = int(np.sum(pres <= 0))
    result["details"]["min_density"] = float(np.min(rho))
    result["details"]["min_pressure"] = float(np.min(pres))
    result["details"]["num_negative_density"] = n_neg_rho
    result["details"]["num_negative_pressure"] = n_neg_pres
    result["passed"] = (n_neg_rho == 0 and n_neg_pres == 0)
    return result


def check_naca_zero_aoa_lift(force_rows, info):
    """Check that NACA cases at AoA=0 have near-zero lift."""
    result = {"passed": True, "details": {}}
    if not info["is_naca"]:
        result["details"]["skipped"] = "Not a NACA case"
        return result
    if abs(info["aoa"]) > 1e-6:
        result["details"]["skipped"] = "AoA != 0, symmetry check not applicable"
        return result
    if not force_rows:
        result["passed"] = False
        result["details"]["error"] = "No forces data"
        return result
    cl = np.array([_safe_float(r["cl"]) for r in force_rows])
    # Use last 10% of data for steady, last 50% for transient
    n = len(cl)
    tail = cl[max(0, int(n * 0.9)):]
    mean_cl = float(np.mean(tail)) if len(tail) > 0 else 0.0
    result["details"]["mean_cl_last10pct"] = mean_cl
    result["details"]["threshold"] = 0.05
    result["passed"] = abs(mean_cl) < 0.05
    return result


def check_cylinder_positive_drag(force_rows, info):
    """Check that cylinder cases have positive mean drag."""
    result = {"passed": True, "details": {}}
    if not info["is_cylinder"]:
        result["details"]["skipped"] = "Not a cylinder case"
        return result
    if not force_rows:
        result["passed"] = False
        result["details"]["error"] = "No forces data"
        return result
    cd = np.array([_safe_float(r["cd"]) for r in force_rows])
    # Skip startup transient: use last 50% of data
    n = len(cd)
    tail = cd[max(0, int(n * 0.5)):]
    mean_cd = float(np.mean(tail)) if len(tail) > 0 else 0.0
    result["details"]["mean_cd_last50pct"] = mean_cd
    # Positive but non-physical (diverged) values must also fail
    result["passed"] = mean_cd > 0 and mean_cd < 1e3
    return result


def check_re200_unsteady_lift(force_rows, info):
    """Check that Re200 has nonzero unsteady lift variation."""
    result = {"passed": True, "details": {}}
    if not info["is_re200"]:
        result["details"]["skipped"] = "Not a Re200 case"
        return result
    if not force_rows:
        result["passed"] = False
        result["details"]["error"] = "No forces data"
        return result
    cl = np.array([_safe_float(r["cl"]) for r in force_rows])
    # Use last 50% to skip startup transient
    n = len(cl)
    tail = cl[max(0, int(n * 0.5)):]
    if len(tail) < 10:
        result["passed"] = False
        result["details"]["error"] = "Insufficient data points"
        return result
    cl_std = float(np.std(tail))
    cl_amp = float((np.max(tail) - np.min(tail)) / 2.0)
    result["details"]["cl_std_last50pct"] = cl_std
    result["details"]["cl_amplitude"] = cl_amp
    result["details"]["threshold"] = 0.01
    result["passed"] = cl_std > 0.01
    return result


def check_surface_cp_variation(surf_rows):
    """Check that surface pressure coefficient varies along body walls."""
    result = {"passed": True, "details": {}}
    if not surf_rows:
        result["passed"] = False
        result["details"]["error"] = "No surface data"
        return result
    cp = np.array([_safe_float(r["cp"]) for r in surf_rows])
    if len(cp) < 2:
        result["passed"] = False
        result["details"]["error"] = "Too few surface points"
        return result
    cp_range = float(np.max(cp) - np.min(cp))
    cp_std = float(np.std(cp))
    result["details"]["cp_range"] = cp_range
    result["details"]["cp_std"] = cp_std
    result["details"]["num_surface_points"] = len(cp)
    result["details"]["threshold_range"] = 0.01
    result["passed"] = cp_range > 0.01
    return result


def check_wall_velocity(surf_rows, info):
    """Check wall boundary condition consistency.

    For no-slip walls (laminar): wall velocity should be near zero.
    For inviscid slip walls: normal velocity near zero, viscous forces negligible.
    """
    result = {"passed": True, "details": {}}
    if not surf_rows:
        result["passed"] = False
        result["details"]["error"] = "No surface data"
        return result
    u = np.array([_safe_float(r["u"]) for r in surf_rows])
    v = np.array([_safe_float(r["v"]) for r in surf_rows])
    vel_mag = np.sqrt(u**2 + v**2)
    result["details"]["max_wall_velocity"] = float(np.max(vel_mag))
    result["details"]["mean_wall_velocity"] = float(np.mean(vel_mag))
    if not info["is_inviscid"]:
        # No-slip wall: velocity should be near zero
        result["details"]["wall_type"] = "no_slip"
        result["passed"] = float(np.max(vel_mag)) < 0.1
    else:
        # Slip wall: check viscous forces from forces.csv instead
        result["details"]["wall_type"] = "slip_wall_inviscid"
    return result


def check_inviscid_viscous_forces(force_rows, info):
    """For inviscid cases, viscous drag and lift must be negligible."""
    result = {"passed": True, "details": {}}
    if not info["is_inviscid"]:
        result["details"]["skipped"] = "Not an inviscid case"
        return result
    if not force_rows:
        result["passed"] = False
        result["details"]["error"] = "No forces data"
        return result
    last = force_rows[-1]
    vdrag = _safe_float(last.get("viscous_drag", 0.0))
    vlift = _safe_float(last.get("viscous_lift", 0.0))
    result["details"]["final_viscous_drag"] = vdrag
    result["details"]["final_viscous_lift"] = vlift
    result["details"]["threshold"] = 1e-8
    result["passed"] = abs(vdrag) < 1e-8 and abs(vlift) < 1e-8
    return result


def build_sanity_checks(results_dir, report_dir, case_ids):
    """Run physics sanity checks for all cases and write sanity_checks.json."""
    results_dir = Path(results_dir)
    all_checks = {}
    overall_passed = True

    for case_id in case_ids:
        case_dir = results_dir / case_id
        if not case_dir.exists():
            all_checks[case_id] = {"status": "missing", "checks": {}}
            overall_passed = False
            continue

        info = pr.get_case_info(case_dir)
        res_rows = pr.read_csv_file(case_dir / "residuals.csv")
        force_rows = pr.read_csv_file(case_dir / "forces.csv")
        surf_rows = pr.read_csv_file(case_dir / "surface.csv")
        field = pr.load_field(case_dir)

        checks = {}
        checks["positive_density_pressure"] = check_positive_density_pressure(field)
        checks["naca_zero_aoa_lift"] = check_naca_zero_aoa_lift(force_rows, info)
        checks["cylinder_positive_drag"] = check_cylinder_positive_drag(force_rows, info)
        checks["re200_unsteady_lift"] = check_re200_unsteady_lift(force_rows, info)
        checks["surface_cp_variation"] = check_surface_cp_variation(surf_rows)
        checks["wall_velocity"] = check_wall_velocity(surf_rows, info)
        checks["inviscid_viscous_forces"] = check_inviscid_viscous_forces(force_rows, info)

        case_passed = all(c["passed"] for c in checks.values())
        if not case_passed:
            overall_passed = False

        all_checks[case_id] = {
            "status": "passed" if case_passed else "failed",
            "checks": checks,
        }

    output = {
        "overall_passed": overall_passed,
        "num_cases": len(case_ids),
        "cases": all_checks,
    }
    out_path = report_dir / "sanity_checks.json"
    with open(out_path, "w") as f:
        json.dump(output, f, indent=2)
    print("  [ok] sanity_checks.json (%d cases, overall: %s)" %
          (len(case_ids), "passed" if overall_passed else "failed"))
    return output


# ---------------------------------------------------------------------------
# Run manifest
# ---------------------------------------------------------------------------

def build_run_manifest(results_dir, report_dir, case_ids):
    """Write run_manifest.md summarising all case runs."""
    results_dir = Path(results_dir)
    out_path = report_dir / "run_manifest.md"
    lines = []
    lines.append("# Run Manifest\n")
    lines.append("This file is auto-generated by `tools/generate_report.py`.\n")
    lines.append("## Case Summary\n")
    lines.append("| Case ID | MPI Ranks | Final Step | Physical Time | "
                 "Residual Reduction | Wall Time (s) | Status |")
    lines.append("|---------|-----------|------------|---------------|"
                 "-------------------|---------------|--------|")

    for case_id in case_ids:
        case_dir = results_dir / case_id
        if not case_dir.exists():
            lines.append("| %s | - | - | - | - | - | missing |" % case_id)
            continue

        metadata = {}
        meta_path = case_dir / "metadata.json"
        if meta_path.exists():
            try:
                metadata = json.loads(meta_path.read_text())
            except (json.JSONDecodeError, OSError):
                pass

        status_data = {}
        status_path = case_dir / "run_status.json"
        if status_path.exists():
            try:
                status_data = json.loads(status_path.read_text())
            except (json.JSONDecodeError, OSError):
                pass

        mpi_ranks = metadata.get("mpi_ranks", status_data.get("mpi_ranks", "-"))
        final_step = status_data.get("final_step", "-")
        phys_time = status_data.get("final_physical_time", "-")
        res_red = status_data.get("residual_reduction_orders", "-")
        wall_time = status_data.get("wall_time_seconds", "-")
        conv_status = status_data.get("convergence_status",
                                       metadata.get("convergence_status", "-"))

        if isinstance(res_red, float):
            res_red = "%.2f" % res_red
        if isinstance(wall_time, float):
            wall_time = "%.1f" % wall_time
        if isinstance(phys_time, float):
            phys_time = "%.2f" % phys_time

        lines.append("| %s | %s | %s | %s | %s | %s | %s |" %
                      (case_id, mpi_ranks, final_step, phys_time,
                       res_red, wall_time, conv_status))

    lines.append("\n## Force Summary\n")
    lines.append("| Case ID | Final/Mean CL | Final/Mean CD | "
                 "Final CMZ | Pressure Drag | Viscous Drag |")
    lines.append("|---------|---------------|---------------|"
                 "-----------|---------------|--------------|")

    for case_id in case_ids:
        case_dir = results_dir / case_id
        if not case_dir.exists():
            lines.append("| %s | - | - | - | - | - |" % case_id)
            continue
        force_rows = pr.read_csv_file(case_dir / "forces.csv")
        if not force_rows:
            lines.append("| %s | - | - | - | - | - |" % case_id)
            continue
        info = pr.get_case_info(case_dir)
        cl = np.array([_safe_float(r["cl"]) for r in force_rows])
        cd = np.array([_safe_float(r["cd"]) for r in force_rows])
        cmz = np.array([_safe_float(r["cmz"]) for r in force_rows])
        pdrag = np.array([_safe_float(r["pressure_drag"]) for r in force_rows])
        vdrag = np.array([_safe_float(r["viscous_drag"]) for r in force_rows])
        if info["is_re200"]:
            n = len(cl)
            tail = slice(max(0, int(n * 0.5)), n)
            cl_val = _fmt_val(np.mean(cl[tail]))
            cd_val = _fmt_val(np.mean(cd[tail]))
            cmz_val = _fmt_val(np.mean(cmz[tail]))
            pd_val = _fmt_val(np.mean(pdrag[tail]))
            vd_val = _fmt_val(np.mean(vdrag[tail]))
        else:
            cl_val = _fmt_val(cl[-1])
            cd_val = _fmt_val(cd[-1])
            cmz_val = _fmt_val(cmz[-1])
            pd_val = _fmt_val(pdrag[-1])
            vd_val = _fmt_val(vdrag[-1])
        lines.append("| %s | %s | %s | %s | %s | %s |" %
                      (case_id, cl_val, cd_val, cmz_val, pd_val, vd_val))

    lines.append("\n## Metadata Details\n")
    for case_id in case_ids:
        case_dir = results_dir / case_id
        if not case_dir.exists():
            continue
        meta_path = case_dir / "metadata.json"
        if not meta_path.exists():
            continue
        try:
            metadata = json.loads(meta_path.read_text())
        except (json.JSONDecodeError, OSError):
            continue
        lines.append("### %s\n" % case_id)
        lines.append("- Solver: %s v%s" %
                      (metadata.get("solver_name", "?"),
                       metadata.get("solver_version", "?")))
        lines.append("- Mesh: %s" % metadata.get("mesh_file", "?"))
        lines.append("- Cells (global): %s" %
                      metadata.get("num_cells_global", "?"))
        lines.append("- Partitioner: %s" % metadata.get("partitioner", "?"))
        lines.append("- Halo exchange: %s" %
                      metadata.get("halo_exchange", "?"))
        lines.append("- Time integrator: %s" %
                      metadata.get("time_integrator", "?"))
        lines.append("- Implicit solver: %s" %
                      metadata.get("implicit_solver", "?"))
        lines.append("- Reconstruction: %s" %
                      metadata.get("reconstruction", "?"))
        lines.append("- Limiter: %s" % metadata.get("limiter", "?"))
        lines.append("- Spatial order: %s" %
                      metadata.get("spatial_order_claimed", "?"))
        lines.append("- Convergence: %s\n" %
                      metadata.get("convergence_status", "?"))

    out_path.write_text("\n".join(lines) + "\n")
    print("  [ok] run_manifest.md (%d cases)" % len(case_ids))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def run_plotting_for_case(python_exe, results_dir, case_dir, report_dir):
    """Run plot_results.py for a single case and return generated figure names."""
    case_path = Path(case_dir)
    cmd = [
        str(python_exe),
        str(_SCRIPT_DIR / "plot_results.py"),
        str(case_path),
        "--output-dir", str(report_dir / "figures"),
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        if result.returncode != 0:
            print("  [warn] plot_results.py returned %d for %s" %
                  (result.returncode, case_path.name))
            if result.stderr:
                print("    stderr: %s" % result.stderr[-300:])
        else:
            print(result.stdout.strip())
    except subprocess.TimeoutExpired:
        print("  [warn] plot_results.py timed out for %s" % case_path.name)
    except Exception as exc:
        print("  [warn] plot_results.py failed for %s: %s" % (case_path.name, exc))


def discover_cases(results_dir):
    """Find all case result directories (those containing metadata.json or case_modified.json)."""
    results_dir = Path(results_dir)
    cases = []
    if not results_dir.exists():
        return cases
    for d in sorted(results_dir.iterdir()):
        if not d.is_dir():
            continue
        if (d / "metadata.json").exists() or (d / "case_modified.json").exists():
            cases.append(d.name)
    return cases


def main():
    import argparse
    parser = argparse.ArgumentParser(
        description="Generate CFD benchmark report artifacts (figures, manifests, checks)."
    )
    parser.add_argument("--results-dir", type=str, default="results",
                        help="Root directory containing per-case result subdirectories.")
    parser.add_argument("--report-dir", type=str, default="report",
                        help="Output directory for report artifacts.")
    parser.add_argument("--skip-plotting", action="store_true",
                        help="Skip running plot_results.py (use existing figures).")
    args = parser.parse_args()

    solver_root = _SCRIPT_DIR.parent
    results_dir = (solver_root / args.results_dir).resolve()
    report_dir = (solver_root / args.report_dir).resolve()
    report_dir.mkdir(parents=True, exist_ok=True)
    (report_dir / "figures").mkdir(parents=True, exist_ok=True)

    python_exe = solver_root / ".venv" / "bin" / "python3"

    case_ids = discover_cases(results_dir)
    if not case_ids:
        print("No case results found in %s" % results_dir)
        print("Run the solver first (tools/run_all.py), then re-run this script.")
        sys.exit(0)

    print("Found %d case(s): %s" % (len(case_ids), ", ".join(case_ids)))
    print("Results dir: %s" % results_dir)
    print("Report dir:  %s\n" % report_dir)

    # Step 1: Generate figures for each case
    if not args.skip_plotting:
        print("=== Step 1: Generating figures ===")
        for case_id in case_ids:
            print("\n--- %s ---" % case_id)
            run_plotting_for_case(python_exe, results_dir, results_dir / case_id, report_dir)
    else:
        print("Skipping figure generation (--skip-plotting)")

    # Step 2: Build figure manifest
    print("\n=== Step 2: Building figure manifest ===")
    build_figure_manifest(report_dir, case_ids)

    # Step 3: Run sanity checks
    print("\n=== Step 3: Running sanity checks ===")
    build_sanity_checks(results_dir, report_dir, case_ids)

    # Step 4: Build run manifest
    print("\n=== Step 4: Building run manifest ===")
    build_run_manifest(results_dir, report_dir, case_ids)

    print("\n=== Report generation complete ===")
    print("Output directory: %s" % report_dir)
    print("Artifacts:")
    print("  - figures/         (PNG plots)")
    print("  - figure_manifest.csv")
    print("  - sanity_checks.json")
    print("  - run_manifest.md")


if __name__ == "__main__":
    main()
