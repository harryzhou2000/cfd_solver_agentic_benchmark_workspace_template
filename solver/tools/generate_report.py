#!/usr/bin/env python3
"""Generate reproducible tables, sanity checks, and an evidence-backed report."""
from __future__ import annotations

import argparse
import csv
import json
import math
import shutil
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BENCHMARK = ROOT.parent / "cfd_solver_agentic_benchmark"
CASE_DIR = BENCHMARK / "inputs" / "cases"
EXPECTED_CASE_IDS = {path.stem for path in CASE_DIR.glob("*.json")}


def rows(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def number(value: object, default: float = 0.0) -> float:
    try:
        out = float(value)
        return out if math.isfinite(out) else default
    except (TypeError, ValueError):
        return default


def esc(value: object) -> str:
    text = str(value)
    return (text.replace("\\", r"\textbackslash{}")
                .replace("_", r"\_")
                .replace("&", r"\&")
                .replace("%", r"\%")
                .replace("#", r"\#"))


def latex_num(value: object, digits: int = 4) -> str:
    x = number(value)
    return f"{x:.{digits}g}" if math.isfinite(x) else "--"


def case_input(case_id: str) -> dict:
    path = CASE_DIR / f"{case_id}.json"
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text())
    except json.JSONDecodeError:
        return {}


def finite_rows(data: list[dict[str, str]], keys: list[str]) -> bool:
    return bool(data) and all(math.isfinite(number(row.get(key), math.nan)) for row in data for key in keys)


def field_scalar(path: Path, name: str) -> list[float]:
    """Read one scalar from an ASCII legacy VTK or ASCII VTU field file.

    This deliberately checks the submitted field file rather than inferring
    positivity from surface rows.  Solver output is ASCII in the benchmark,
    so a compact reader keeps report generation dependency-free.
    """
    wanted = name.lower()
    if path.suffix.lower() == ".vtu":
        root = ET.parse(path).getroot()
        for element in root.iter():
            if element.tag.endswith("DataArray") and element.get("Name", "").lower() == wanted:
                return [float(value) for value in (element.text or "").split()]
        return []
    tokens = path.read_text(errors="replace").split()
    active_size = 0
    for i, token in enumerate(tokens):
        if token in {"POINT_DATA", "CELL_DATA"} and i + 1 < len(tokens):
            try:
                active_size = int(tokens[i + 1])
            except ValueError:
                active_size = 0
        if token == "SCALARS" and i + 1 < len(tokens) and tokens[i + 1].lower() == wanted:
            pos = i + 2
            while pos < len(tokens) and tokens[pos] != "LOOKUP_TABLE":
                pos += 1
            if pos + 1 >= len(tokens) or active_size <= 0:
                return []
            try:
                return [float(value) for value in tokens[pos + 2:pos + 2 + active_size]]
            except ValueError:
                return []
    return []


def field_positive_density_pressure(folder: Path) -> bool:
    files = sorted(folder.glob("field_final.*"))
    if not files:
        return False
    density, pressure = field_scalar(files[0], "density"), field_scalar(files[0], "pressure")
    return bool(density and pressure and all(math.isfinite(x) and x > 0.0 for x in density + pressure))


def dominant_frequency(times: list[float], values: list[float]) -> float:
    if len(values) < 8:
        return 0.0
    # Keep report generation dependency-free.  A bounded autocorrelation
    # search is sufficient for the low-frequency Re=200 wake and avoids
    # requiring NumPy in the benchmark's minimal Python environment.
    y = [float(value) for value in values]
    t = [float(value) for value in times]
    if not all(math.isfinite(value) for value in y + t):
        return 0.0
    mean = sum(y) / len(y)
    y = [value - mean for value in y]
    diffs = sorted(t[i + 1] - t[i] for i in range(len(t) - 1) if t[i + 1] > t[i])
    dt = diffs[len(diffs) // 2] if diffs else 0.0
    if dt <= 0 or not any(abs(value) > 1.0e-14 for value in y):
        return 0.0
    stride = max(1, len(y) // 2000)
    y = y[::stride]
    dt *= stride
    if len(y) < 8:
        return 0.0
    # Upward zero crossings avoid the trivial lag-one maximum of a raw
    # autocorrelation.  Linear interpolation makes the estimate insensitive
    # to the sampling phase; a short-interval filter rejects limiter noise.
    crossings = []
    for i in range(len(y) - 1):
        if y[i] <= 0.0 < y[i + 1] and y[i + 1] != y[i]:
            crossings.append(i - y[i] / (y[i + 1] - y[i]))
    min_interval = max(2.0, 0.002 * len(y))
    periods = [b - a for a, b in zip(crossings, crossings[1:]) if b - a >= min_interval]
    if periods:
        period = sorted(periods)[len(periods) // 2] * dt
        return 1.0 / period if period > 0.0 else 0.0
    return 0.0


def inspect_case(folder: Path) -> dict:
    metadata = json.loads((folder / "metadata.json").read_text()) if (folder / "metadata.json").exists() else {}
    status = json.loads((folder / "run_status.json").read_text()) if (folder / "run_status.json").exists() else {}
    residual = rows(folder / "residuals.csv")
    forces = rows(folder / "forces.csv")
    surface = rows(folder / "surface.csv")
    cid = str(metadata.get("case_id", status.get("case_id", folder.name)))
    inp = case_input(cid)
    phy = inp.get("physics", {})
    far = inp.get("boundary_conditions", {})
    control = inp.get("run_control", {})
    partition = rows(folder / "partition_diagnostics.csv")
    owned = [number(r.get("num_cells_owned")) for r in partition]
    ghosts = [number(r.get("num_cells_ghost")) for r in partition]
    tail = forces[max(0, len(forces) // 2):]
    cd_tail = [number(r.get("cd")) for r in tail]
    cl_tail = [number(r.get("cl")) for r in tail]
    metrics = {
        "mean_cd": sum(cd_tail) / len(cd_tail) if cd_tail else 0.0,
        "mean_cl": sum(cl_tail) / len(cl_tail) if cl_tail else 0.0,
        "lift_amplitude": 0.5 * (max(cl_tail) - min(cl_tail)) if cl_tail else 0.0,
        "dominant_frequency": 0.0,
        "strouhal": 0.0,
    }
    if "re200" in cid.lower() and tail:
        times = [number(r.get("physical_time")) for r in tail]
        metrics["dominant_frequency"] = dominant_frequency(times, cl_tail)
        metrics["strouhal"] = metrics["dominant_frequency"] * number(inp.get("reference", {}).get("length"), 1.0) / max(number(inp.get("freestream", {}).get("velocity_magnitude"), 1.0), 1.0e-12)
    checks: dict[str, object] = {
        "has_contract_outputs": all((folder / name).exists() for name in ["metadata.json", "run_status.json", "residuals.csv", "forces.csv", "surface.csv"]),
        "finite_residuals": finite_rows(residual, ["residual_l2", "residual_linf"]),
        "finite_forces": finite_rows(forces, ["cd", "cl", "cmz"]),
        "finite_surface": finite_rows(surface, ["pressure", "rho", "mach"]),
        "field_present": bool(list(folder.glob("field_final.*"))),
        "field_positive_density_pressure": field_positive_density_pressure(folder),
        "status_matches_completed": metadata.get("completed") is True and status.get("convergence_status") in {"converged", "statistically_periodic"},
        "owned_load_balance_ratio": max(owned) / min(owned) if owned and min(owned) > 0 else 0.0,
        "inner_target_fraction": number(metadata.get("inner_target_converged_fraction")),
    }
    if surface:
        p = [number(r.get("pressure")) for r in surface]
        rho = [number(r.get("rho")) for r in surface]
        checks["positive_density_pressure"] = min(p) > 0.0 and min(rho) > 0.0
        checks["surface_cp_varies"] = max(number(r.get("cp")) for r in surface) - min(number(r.get("cp")) for r in surface) > 1.0e-8
        if "laminar" in cid.lower():
            wall_speed = [math.hypot(number(r.get("u")), number(r.get("v"))) for r in surface]
            checks["no_slip_wall_velocity"] = max(wall_speed) <= 1.0e-8
            checks["laminar_skin_friction_evidence"] = max(abs(number(r.get("cf"))) for r in surface) > 1.0e-12
        if "inviscid" in cid.lower():
            normal_velocity = [number(r.get("u")) * number(r.get("nx")) + number(r.get("v")) * number(r.get("ny")) for r in surface]
            checks["slip_wall_normal_velocity"] = max(abs(value) for value in normal_velocity) <= 1.0e-8
    if "inviscid" in cid.lower() and forces:
        checks["inviscid_viscous_force_negligible"] = abs(number(forces[-1].get("viscous_drag"))) <= 1.0e-8 and abs(number(forces[-1].get("viscous_lift"))) <= 1.0e-8
    if "cylinder" in cid.lower() and forces:
        checks["cylinder_positive_drag"] = metrics["mean_cd"] > 0.0
    if "re200" in cid.lower():
        checks["re200_production_time"] = number(status.get("final_physical_time")) >= 300.0
        checks["re200_unsteady_lift"] = metrics["lift_amplitude"] > 1.0e-5
        checks["re200_inner_target_strict"] = number(metadata.get("inner_residual_reduction_target"), 1.0) <= 1.0e-3
        checks["re200_inner_target_fraction"] = number(metadata.get("inner_target_converged_fraction")) >= 0.95
        checks["re200_best_effort_free"] = number(metadata.get("transient_best_effort_continuation_steps")) <= 0.0
        checks["re200_status_honest"] = status.get("convergence_status") == ("statistically_periodic" if checks["re200_unsteady_lift"] and checks["re200_inner_target_strict"] and checks["re200_inner_target_fraction"] and checks["re200_best_effort_free"] else "failed")
    if "naca" in cid.lower() and forces:
        checks["naca_symmetry_lift"] = abs(metrics["mean_cl"]) < 0.05
    if "laminar" in cid.lower() and forces and "re200" not in cid.lower():
        checks["laminar_positive_drag"] = metrics["mean_cd"] > 0.0
    checks["passed"] = all(bool(value) for key, value in checks.items() if key not in {"passed", "owned_load_balance_ratio", "inner_target_fraction"})
    return {"case_id": cid, "folder": folder, "input": inp, "metadata": metadata, "status": status, "residual": residual, "forces": forces, "surface": surface, "partition": partition, "metrics": metrics, "checks": checks, "physics": phy, "boundary_conditions": far, "controls": control}


def table_mesh(cases: list[dict]) -> str:
    lines = []
    for item in cases:
        inp = item["input"]
        freestream = inp.get("freestream", {})
        phy = inp.get("physics", {})
        bcs = "; ".join(f"{k}: {v}" for k, v in item["boundary_conditions"].items())
        lines.append("{} & {} & {} & {} & {} & {} & {} \\\\".format(
            esc(item["case_id"]), item["metadata"].get("num_cells_global", "--"), item["metadata"].get("num_faces_global", "--"),
            latex_num(freestream.get("mach")), latex_num(phy.get("reynolds", "--")), esc(phy.get("mode", "--")), esc(bcs)))
    return "\n".join(lines)


def table_controls(cases: list[dict]) -> str:
    lines = []
    for item in cases:
        c = item["controls"]
        m = item["metadata"]
        lines.append("{} & {} & {} & {} & {} & {} & {} & {} & {} \\\\".format(
            esc(item["case_id"]), c.get("max_steps", "--"), m.get("actual_steps", item["status"].get("final_step", "--")),
            latex_num(c.get("cfl_initial")), latex_num(c.get("cfl_max")), c.get("min_inner_iterations", "--"),
            c.get("max_inner_iterations", "--"), latex_num(c.get("inner_residual_reduction_target"), 2),
            latex_num(m.get("observed_mean_inner_iterations"), 3)))
    return "\n".join(lines)


def table_partition(cases: list[dict]) -> str:
    lines = []
    for item in cases:
        for row in item["partition"]:
            lines.append("{} & {} & {} & {} & {} & {} & {} \\\\".format(
                esc(item["case_id"]), row.get("rank", "--"), row.get("num_cells_owned", "--"), row.get("num_cells_ghost", "--"),
                row.get("num_neighbor_ranks", "--"), row.get("send_cells", "--"), row.get("recv_cells", "--")))
    return "\n".join(lines)


def table_partition_summary(cases: list[dict]) -> str:
    """Compact load-balance and communication summary for the report."""
    lines = []
    for item in cases:
        partition = item["partition"]
        owned = [number(row.get("num_cells_owned")) for row in partition]
        ghosts = [number(row.get("num_cells_ghost")) for row in partition]
        neighbors = [number(row.get("num_neighbor_ranks")) for row in partition]
        send = [number(row.get("send_cells")) for row in partition]
        recv = [number(row.get("recv_cells")) for row in partition]
        ratio = max(owned) / min(owned) if owned and min(owned) > 0.0 else 0.0
        lines.append("{} & {} & {} & {} & {} & {} \\\\".format(
            esc(item["case_id"]), latex_num(ratio, 4), latex_num(sum(ghosts) / len(ghosts) if ghosts else 0.0, 4),
            latex_num(max(neighbors) if neighbors else 0.0, 3), latex_num(sum(send), 5), latex_num(sum(recv), 5)))
    return "\n".join(lines)


def table_rank_comparison(report: Path) -> str:
    """Render the independently-run np=2/np=8 evidence table."""
    data = rows(report / "rank_count_comparison.csv")
    lines = []
    for row in data:
        lines.append("{} & {} & {} & {} & {} & {} & {} & {} & {} \\\\".format(
            esc(row.get("case_id", "--")), row.get("ranks", "--"),
            latex_num(row.get("wall_time_seconds"), 5), row.get("final_step", "--"),
            latex_num(row.get("residual_l2", "--")), latex_num(row.get("cd")), latex_num(row.get("cl")),
            row.get("num_cells_owned_local", "--"), row.get("num_cells_ghost_local", "--")))
    return "\n".join(lines) if lines else "No rank-count comparison file was available."


def rank_comparison_summary(report: Path) -> str:
    """State measured np=2-to-np=8 deltas without implying unavailable data."""
    grouped: dict[str, list[dict[str, str]]] = {}
    for row in rows(report / "rank_count_comparison.csv"):
        grouped.setdefault(str(row.get("case_id", "unknown")), []).append(row)
    summaries = []
    for cid, values in grouped.items():
        ordered = sorted(values, key=lambda row: number(row.get("ranks")))
        if len(ordered) < 2:
            continue
        ref, other = ordered[0], ordered[-1]
        dc_d = abs(number(other.get("cd")) - number(ref.get("cd")))
        dc_l = abs(number(other.get("cl")) - number(ref.get("cl")))
        residual_available = all(str(row.get("residual_l2", "")).strip() for row in (ref, other))
        residual_text = (f"$|\\Delta R_2|={abs(number(other.get('residual_l2')) - number(ref.get('residual_l2'))):.3g}$"
                         if residual_available else "$R_2$ was not retained for both rank counts")
        speedup = number(ref.get("wall_time_seconds")) / max(number(other.get("wall_time_seconds")), 1.0e-12)
        summaries.append(f"{esc(cid)}: np={ref.get('ranks', '--')} to np={other.get('ranks', '--')} gives "
                         f"$|\\Delta C_D|={dc_d:.3g}$, $|\\Delta C_L|={dc_l:.3g}$, {residual_text}, and measured speedup {speedup:.3g}.")
    return " ".join(summaries) if summaries else "No paired rank-count comparison was available."


def terminal_assessment(item: dict) -> str:
    """Render terminal behavior without hiding an unmet residual target."""
    status = str(item["status"].get("convergence_status", "missing"))
    metadata = item["metadata"]
    behavior = str(metadata.get("terminal_behavior", ""))
    if behavior == "stable_plateau" and metadata.get("residual_target_reached") is False:
        return "stable plateau; residual target not reached"
    if behavior == "residual_target":
        return "requested residual target reached"
    if behavior == "resolved_lift_oscillation" and status == "statistically_periodic":
        return "statistically periodic lift"
    if status == "failed":
        return behavior.replace("_", " ") if behavior else "failed"
    return status.replace("_", " ")


def table_status(cases: list[dict]) -> str:
    lines = []
    for item in cases:
        s = item["status"]
        lines.append("{} & {} & {} & {} & {} & {} & {} \\\\".format(
            esc(item["case_id"]), s.get("mpi_ranks", "--"), s.get("final_step", "--"), latex_num(s.get("final_physical_time")),
            latex_num(s.get("wall_time_seconds"), 5), latex_num(s.get("residual_reduction_orders")),
            esc(s.get("convergence_status", "missing") + " (" + terminal_assessment(item) + ")")))
    return "\n".join(lines)


def table_forces(cases: list[dict]) -> str:
    lines = []
    for item in cases:
        f = item["forces"][-1] if item["forces"] else {}
        status = str(item["status"].get("convergence_status", "missing"))
        behavior = str(item["metadata"].get("terminal_behavior", ""))
        if behavior == "stable_plateau" and item["metadata"].get("residual_target_reached") is False:
            note = "stable force plateau; residual target not reached"
        elif behavior == "residual_target":
            note = "residual target reached"
        elif status == "statistically_periodic" and behavior == "resolved_lift_oscillation":
            note = "statistically periodic lift"
        elif status == "failed":
            note = behavior.replace("_", " ") if behavior else "terminal evidence insufficient"
        else:
            note = "terminal state accepted"
        lines.append("{} & {} & {} & {} & {} & {} & {} & {} \\\\".format(
            esc(item["case_id"]), latex_num(item["metrics"]["mean_cd"]), latex_num(item["metrics"]["mean_cl"]),
            latex_num(f.get("cmz")), latex_num(f.get("pressure_drag")), latex_num(f.get("viscous_drag")),
            latex_num(item["metrics"]["lift_amplitude"]), esc(note)))
    return "\n".join(lines)


def figure_sections(cases: list[dict], figure_rows: list[dict[str, str]]) -> tuple[str, str]:
    refs: list[str] = []
    figures: list[str] = []
    for case in cases:
        selected = [r for r in figure_rows if r.get("case_id") == case["case_id"]]
        if not selected:
            continue
        refs.append(esc(case["case_id"]) + ": " + ", ".join(f"Figure~\\ref{{fig:{safe_label(r.get('figure_file', 'figure'))}}}" for r in selected) + ".")
        for row in selected:
            name = row.get("figure_file", "figure.png")
            label = safe_label(name)
            figures.append("\\begin{figure}[H]\\centering\\includegraphics[width=.84\\linewidth]{figures/%s}\\caption{%s}\\label{fig:%s}\\end{figure}" % (esc(name), esc(row.get("caption", "")), label))
    return "\n".join(refs), "\n".join(figures)


def safe_label(name: str) -> str:
    return "".join(ch if ch.isalnum() else "-" for ch in name)


def report_tex(cases: list[dict], figure_rows: list[dict[str, str]], rank_comparison: str) -> str:
    refs, figures = figure_sections(cases, figure_rows)
    partition_summary = table_partition_summary(cases)
    rank_summary = rank_comparison_summary(ROOT / "report")
    re200 = next((c for c in cases if "re200" in c["case_id"].lower()), None)
    if re200:
        met = re200["metrics"]
        best_effort_steps = number(re200["metadata"].get("transient_best_effort_continuation_steps"))
        periodic = (bool(re200["checks"].get("re200_unsteady_lift")) and
                    bool(re200["checks"].get("re200_inner_target_strict")) and
                    bool(re200["checks"].get("re200_inner_target_fraction")) and
                    best_effort_steps <= 0.0 and
                    re200["status"].get("convergence_status") == "statistically_periodic")
        re200_text = f"The Re=200 history has mean $C_D={met['mean_cd']:.4g}$ and lift half-amplitude ${met['lift_amplitude']:.4g}$. "
        if periodic:
            re200_text += f"Its dominant frequency is ${met['dominant_frequency']:.4g}$ and $St={met['strouhal']:.4g}$; the measured lift signal is classified statistically periodic. "
        else:
            re200_text += "The lift signal does not meet the resolved-shedding criterion; no frequency or Strouhal claim is made, and the case remains failed. "
        inner_fraction = number(re200["metadata"].get("inner_target_converged_fraction"))
        inner_misses = number(re200["metadata"].get("inner_target_misses"))
        fallback_steps = number(re200["metadata"].get("transient_be_halfstep_fallback_steps", re200["metadata"].get("transient_bdf1_fallback_steps")))
        fallback_halfsteps = number(re200["metadata"].get("transient_be_halfsteps"), 2.0 * fallback_steps)
        integrator_description = str(re200["metadata"].get("time_integrator", ""))
        fallback_ladder = "2/4/8/16/32/64/128" if "128" in integrator_description else "2/4/8"
        be_recovery_steps = number(re200["metadata"].get("transient_be_recovery_steps"))
        damping_retry_steps = number(re200["metadata"].get("transient_damping_retry_steps"))
        damping_retry_attempts = number(re200["metadata"].get("transient_damping_retry_attempts"))
        be_damping_retry_attempts = number(re200["metadata"].get("transient_be_damping_retry_attempts"))
        seed = number(re200["metadata"].get("transient_startup_seed"))
        rusanov = number(re200["metadata"].get("rusanov_dissipation_scale"), 1.0)
        cutoff = number(re200["metadata"].get("transient_low_mach_acoustic_cutoff"), 1.0)
        slope = number(re200["metadata"].get("transient_reconstruction_slope_factor"), 1.0)
        gain = number(re200["metadata"].get("transient_scalar_correction_gain"), 1.0)
        stationary_wall_start = bool(re200["metadata"].get("transient_wall_start_stationary", False))
        restart_used = bool(re200["metadata"].get("restart_used", False))
        resume_step = number(re200["metadata"].get("resume_step"))
        history_restored = bool(re200["metadata"].get("restart_bdf2_history_restored", False))
        re200_text += f" Measured inner target fraction: {inner_fraction:.4g}; target misses: {inner_misses:.0f}; bounded pseudo-time damping retries: {damping_retry_steps:.0f} steps ({damping_retry_attempts:.0f} BDF2 attempts, {be_damping_retry_attempts:.0f} BE-gain retries); bounded backward-Euler fallback intervals: {fallback_steps:.0f} ({fallback_halfsteps:.0f} substeps, using {fallback_ladder} substeps as needed); reported BE recovery-marker count: {be_recovery_steps:.0f} (this historical counter is not a one-time restart-history count; only the first resumed interval reconstructs missing BDF2 history); best-effort target-miss continuations: {best_effort_steps:.0f}; restart used: {restart_used} at accepted step {resume_step:.0f}; BDF2 history sidecar restored: {history_restored}; startup perturbation amplitude: {seed:.4g}; Rusanov dissipation scale: {rusanov:.4g}; all-speed acoustic cutoff: {cutoff:.4g}; reconstruction-slope factor: {slope:.4g}; scalar correction gain: {gain:.4g}; stationary transient wall start: {stationary_wall_start}."
    else:
        re200_text = "No Re=200 directory was available when this report was generated."
    limitations = []
    for c in cases:
        if c["status"].get("convergence_status") == "failed":
            limitations.append(f"{c['case_id']} is explicitly marked failed because its terminal residual/force evidence did not meet the completion gate")
        if (c["metadata"].get("terminal_behavior") == "stable_plateau" and
                c["metadata"].get("residual_target_reached") is False):
            limitations.append(f"{c['case_id']} is a stable force/residual plateau; the requested residual target was not reached")
        if c["status"].get("residual_reduction_orders", 0) < 0:
            limitations.append(f"{c['case_id']} terminates on a measured residual plateau rather than the requested orders-of-magnitude reduction")
        if "re200" in c["case_id"].lower() and not c["checks"].get("re200_unsteady_lift", False):
            limitations.append("the Re=200 run does not demonstrate a resolved vortex-street lift signal")
        if "re200" in c["case_id"].lower() and number(c["metadata"].get("inner_residual_reduction_target"), 1.0) > 1.0e-3:
            limitations.append("the Re=200 inner target is looser than the supplied 1e-3 requirement")
        if "re200" in c["case_id"].lower() and number(c["metadata"].get("inner_target_converged_fraction")) < 0.95:
            limitations.append("the Re=200 full-residual inner target is missed on most or all physical steps")
        if "re200" in c["case_id"].lower() and number(c["metadata"].get("transient_best_effort_continuation_steps")) > 0.0:
            limitations.append("the Re=200 run used best-effort target-miss continuation and is diagnostic-only")
    limit_text = "; ".join(limitations) if limitations else "No additional limitations were detected by the automated checks."
    implicit_note = "The current steady implementation uses scalar Jacobi/Richardson with an MPI-global Armijo backtracking safeguard; the checked-in canonical steady packages predate that safeguard, so their plateau histories are retained as historical evidence. An optional frozen 4x4 block-Jacobi correction is implemented and recorded when explicitly enabled." \
        if not any(float(c["metadata"].get("steady_block_coupling", 0.0) or 0.0) > 0.0 for c in cases) \
        else "Steady runs may use the recorded frozen 4x4 block-Jacobi correction with a scalar spectral-radius backbone."
    return (r"""\documentclass[11pt]{article}
\usepackage[margin=0.75in]{geometry}
\usepackage{amsmath,amssymb,graphicx,booktabs,longtable,hyperref,float}
\hypersetup{hypertexnames=false}
\title{Aurora-FV: 2-D Unstructured Compressible Navier--Stokes Benchmark}
\author{Submitted solver team}\date{\today}
\begin{document}
\maketitle
\begin{abstract}
This report is generated from the submitted JSON, CSV, VTK, and manifest files. It documents the finite-volume method, distributed METIS mesh, requested versus observed controls, force histories, and the Re=200 transient diagnostic. Terminal steady cases are described as measured plateaus when the requested residual reduction is not reached.
\end{abstract}

\section{Governing equations and nondimensionalization}
The conservative state is $\mathbf U=[\rho,\rho u,\rho v,\rho E]^T$. The equations solved are
\[
\frac{\partial\mathbf U}{\partial t}+\frac{\partial\mathbf F^i}{\partial x}+\frac{\partial\mathbf G^i}{\partial y}=\frac{\partial\mathbf F^v}{\partial x}+\frac{\partial\mathbf G^v}{\partial y},
\]
with $\mathbf F^i=[\rho u,\rho u^2+p,\rho uv,u(\rho E+p)]^T$ and $\mathbf G^i=[\rho v,\rho uv,\rho v^2+p,v(\rho E+p)]^T$. The calorically perfect closure is $p=(\gamma-1)\rho e$, $E=e+(u^2+v^2)/2$, and $a=\sqrt{\gamma p/\rho}$. Reference coefficients use $q_\infty=\rho_\infty U_\infty^2/2$ and $C_D,C_L=F_{x,y}/(q_\infty A_{ref})$. Laminar runs use $\mu=\rho_\infty U_\infty L_{ref}/Re$, Newtonian stresses, and $\mathbf q=-\mu c_p\nabla T/Pr$.

\section{Meshes, boundary conditions, and case parameters}
The CGNS reader handles TRI/QUAD zones, geometrically pairs coincident multi-zone faces, computes cell areas and oriented face normals, and retains section names for boundary dispatch. The submitted case table is:
\begin{center}\scriptsize\begin{longtable}{lrrrrll}\toprule Case & cells & faces & $M_\infty$ & $Re$ & mode & boundary tags\\\midrule
%s
\bottomrule\end{longtable}\end{center}
Farfield faces use the configured freestream state; inviscid walls use a mirrored normal velocity and exact zero mass/energy wall flux; no-slip adiabatic walls use zero wall velocity, pressure traction, and gradient-based viscous traction. Surface rows are boundary values: no-slip $u=v=0$, while slip walls retain tangential velocity and have zero normal velocity.

\section{Spatial discretization and positivity}
Each owned cell accumulates oriented face fluxes. The Rusanov local Lax--Friedrichs flux supplies the approximate Riemann solve. For the low-Mach Re=200 transient, an acoustic-scaled all-speed Rusanov signal applies the measured cutoff from metadata only to the artificial acoustic contribution; the conservative physical flux and supplied Rusanov scale remain unchanged. This is not a full pressure--velocity preconditioned flux. Primitive variables are reconstructed with a least-squares gradient, exchanged on the halo, and limited by a Barth--Jespersen bound. Density and pressure are protected by a bounded primitive-variable line search before conservative storage. The same reconstructed gradients provide Newtonian stress and Fourier heat flux; the force postprocessor separates pressure and tangential viscous traction.

\section{Implicit and transient integration}
%s The requested CFL continuation and the effective cap used by each run are recorded in metadata; conservative caps were selected for the stiff high-Mach/laminar cases. The diagonal update contains the standard $(1+CFL)^{-1}$ implicit denominator and a conservative positivity line search. The requested and observed controls are:
\begin{center}\tiny\begin{longtable}{lrrrrrrrr}\toprule Case & supplied max & actual & $CFL_0$ & $CFL_{max}$ & inner min & inner max & inner target & observed mean\\\midrule
%s
\bottomrule\end{longtable}\end{center}
The Re=200 directory uses nominal BDF2 outer steps with $\Delta t=0.01$ and frozen $U^n,U^{n-1}$ histories in the normal residual; documented converged backward-Euler substeps replace rejected nominal intervals. Histories are updated only after each accepted physical step. Its scalar Jacobi/Richardson inner corrections use the measured reconstruction slope, gain, and all-speed cutoff recorded in metadata. Fresh transient starts in the current source retain the freestream plus an explicit localized symmetry-breaking phase seed and do not apply the synthetic steady wall-profile initializer; no-slip is imposed through the wall flux and gradient treatment. The seed is a one-time initial condition, not persistent forcing. The checked-in 30,000-step package predates the current seed-shape and recovery corrections, so its failed force history is retained as historical production evidence; corrected-seed and corrected-recovery runs are diagnostic-only and are not mixed into the final statistics. The implementation records actual inner minima, maxima, means, target misses, and converged-step fraction, keeps the supplied 1000-iteration allowance as an upper bound, may abandon a clearly stagnant trial before trying a lower pseudo-time gain, and retries a nonlinear stall with converged backward-Euler substeps of 2, 4, 8, 16, 32, 64, or 128 over exactly one nominal interval before rejecting the physical step. After an accepted fallback, the next interval resumes from the accepted state and permits the ordinary BDF2 extrapolated predictor before retrying the normal residual. New transient checkpoints also write an optional $U^{n-1}$ sidecar, so a restart can restore the exact BDF2 pair; legacy state-only checkpoints without that sidecar use one backward-Euler residual to reconstruct the missing history. Restart metadata records usage, resume step, and whether the sidecar was restored; a requested seed may be applied once to a legacy restarted initial state. The fallback interval, substep, recovery-marker, damping-retry, and best-effort continuation counts are recorded in metadata. Any best-effort continuation is diagnostic-only and excludes a final statistically-periodic claim.

\section{MPI/METIS decomposition}
Rank zero performs one-time CGNS preprocessing and METIS graph partitioning, writes compact rank-local topology, then releases the full mesh. Iterations store owned cells plus one-ring ghosts and exchange only neighbor state/gradient payloads with nonblocking MPI. Residuals and forces use reductions; final fields are gathered only after the solve. The per-rank evidence table (owned, ghost, neighbor, send, and receive counts) is:
\begin{center}\scriptsize\begin{longtable}{lrrrrrr}\toprule Case & rank & owned & ghost & neighbors & send & receive\\\midrule
%s
\bottomrule\end{longtable}\end{center}
The aggregate load-balance and communication evidence is:
\begin{center}\scriptsize\begin{longtable}{lrrrrr}\toprule Case & owned ratio & mean ghosts & max neighbors & total send & total receive\\\midrule
%s
\bottomrule\end{longtable}\end{center}
The independent rank-count runs used the same executable and case inputs. Their measured timings, terminal residuals, and final forces are reported below. The first rank count for a case is the reference; the measured pairwise differences are: %s The production iteration path uses only neighbor-scoped nonblocking state and gradient exchanges; the final gather is outside the iteration loop.
\begin{center}\scriptsize\begin{longtable}{lrrrrrrrr}
\toprule Case & ranks & wall s & steps & $R_2$ & $C_D$ & $C_L$ & owned & ghost\\\midrule
%s
\bottomrule\end{longtable}\end{center}

\section{Results and force analysis}
\begin{center}\scriptsize\begin{longtable}{lrrrrrr}\toprule Case & ranks & steps & time & wall s & residual orders & status / assessment\\\midrule
%s
\bottomrule\end{longtable}\end{center}
\begin{center}\tiny\begin{longtable}{lrrrrrrp{0.16\linewidth}}\toprule Case & mean $C_D$ & mean $C_L$ & final $C_m$ & pressure drag & viscous drag & lift amplitude & convergence note\\\midrule
%s
\bottomrule\end{longtable}\end{center}
NACA0012 runs are at zero incidence, so the reported lift is near the symmetry level. Inviscid force columns have zero viscous contribution by construction; laminar runs show a positive pressure/skin-friction split. Cylinder Re=20 is assessed from its measured residual and force history. A steady row labelled ``stable plateau'' has bounded forces but did not meet the requested residual reduction; it is not silently promoted to residual-target convergence.

\subsection{Re=200 diagnostic}
%s

\section{Figures and source traceability}
Every generated image is introduced below with a label and a reference; filenames and source paths are recorded in \texttt{figure\_manifest.csv}.
%s
\section{Limitations and reproducibility}
The automated evidence identifies these limitations: %s. The dominant remaining numerical limitation is the bounded scalar Jacobi/Richardson relaxation (with the optional block correction disabled for the canonical packages): some steady cases plateau before the requested residual orders, and the Re=200 lift spectrum must be judged from the measured amplitude rather than assumed periodicity. Regenerate figures and this document with \texttt{tools/postprocess.py} and \texttt{tools/generate\_report.py}; run commands, rank counts, wall times, and any launcher-observed return codes are in \texttt{run\_manifest.csv}. A blank return code with source ``not recorded'' means the package was produced outside \texttt{run\_cases.py}; it is not an inferred successful launch.
\end{document}
""" % (table_mesh(cases), implicit_note, table_controls(cases), table_partition(cases), partition_summary, rank_summary, rank_comparison, table_status(cases), table_forces(cases), re200_text, refs + "\n" + figures, esc(limit_text)))


def build_run_manifest(report: Path, cases: list[dict]) -> None:
    """Enrich, rather than replace, the launcher manifest with terminal status.

    ``run_cases.py`` records the exact command and process return code.  The
    report generator runs later, after the solver has written its terminal
    classification, so this is the right place to add that classification
    without fabricating a successful launcher result.  Direct solver runs do
    not leave an observable shell exit status in their output directory; they
    are explicitly marked ``not recorded`` rather than being guessed as zero.
    """
    path = report / "run_manifest.csv"
    fields = [
        "case_id", "mpi_ranks", "case_file", "output_dir", "command",
        "return_code", "return_code_source", "wall_time_seconds", "started_utc", "convergence_status",
    ]
    prior_rows = rows(path)
    by_case = {str(c["case_id"]): c for c in cases}
    emitted: set[str] = set()
    output_rows: list[dict[str, object]] = []

    # Preserve launcher-supplied facts verbatim for rows already recorded by
    # run_cases.py.  Only add the authoritative terminal classification.
    for prior in prior_rows:
        case_id = str(prior.get("case_id", ""))
        case = by_case.get(case_id)
        # A direct solver launch can replace a package after the original
        # run_cases row was written.  Prefer the newer package metadata in
        # that situation; retaining the old wall time/command would make the
        # manifest trace a different field file than the report analyzes.
        if case is not None:
            prior_started = str(prior.get("started_utc", ""))
            current_started = str(case["metadata"].get("start_time_utc", ""))
            if current_started and prior_started and current_started > prior_started:
                continue
        row = {field: prior.get(field, "") for field in fields}
        # Old manifests predate explicit provenance.  A nonblank code still
        # came from the launcher schema, while blanks are intentionally
        # unknown rather than successful.
        if not row["return_code_source"]:
            row["return_code_source"] = "run_cases launcher" if str(row["return_code"]).strip() else "not recorded"
        if case is not None:
            row["convergence_status"] = case["status"].get("convergence_status", "")
            emitted.add(case_id)
        output_rows.append(row)

    # A report can also be generated from an externally-run result directory.
    # In that case record known solver metadata but leave the unobserved
    # launcher return code blank rather than claiming a made-up success.
    for case in cases:
        case_id = str(case["case_id"])
        if case_id in emitted:
            continue
        status = case["status"]
        output_rows.append({
            "case_id": case_id,
            "mpi_ranks": status.get("mpi_ranks", ""),
            "case_file": str(CASE_DIR / f"{case_id}.json"),
            "output_dir": str(case["folder"]),
            "command": status.get("command", ""),
            "return_code": "",
            "return_code_source": "not recorded (direct/external run)",
            "wall_time_seconds": status.get("wall_time_seconds", ""),
            "started_utc": case["metadata"].get("start_time_utc", ""),
            "convergence_status": status.get("convergence_status", ""),
        })

    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(output_rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path, nargs="?", default=ROOT / "results")
    parser.add_argument("report", type=Path, nargs="?", default=ROOT / "report")
    parser.add_argument("--compile", action="store_true")
    args = parser.parse_args()
    args.report.mkdir(parents=True, exist_ok=True)
    # Preserve audit/probe directories on disk without treating them as final
    # submission cases in sanity checks, tables, or the run manifest.
    cases = [inspect_case(path) for path in sorted(args.results.iterdir())
             if path.is_dir() and path.name in EXPECTED_CASE_IDS] if args.results.exists() else []
    manifest_path = args.report / "figure_manifest.csv"
    figure_rows = rows(manifest_path)
    sanity = {"generated_by": "tools/generate_report.py", "cases": [{"case_id": c["case_id"], "metrics": c["metrics"], "checks": c["checks"]} for c in cases]}
    (args.report / "sanity_checks.json").write_text(json.dumps(sanity, indent=2) + "\n")
    (args.report / "report.tex").write_text(report_tex(cases, figure_rows, table_rank_comparison(args.report)))
    build_run_manifest(args.report, cases)
    if args.compile:
        latex = shutil.which("pdflatex")
        if latex:
            for _ in range(2):
                subprocess.run([latex, "-interaction=nonstopmode", "report.tex"], cwd=args.report, check=False)
        else:
            print("pdflatex unavailable; wrote report.tex")
    print(f"wrote report artifacts for {len(cases)} cases to {args.report}")


if __name__ == "__main__":
    main()
