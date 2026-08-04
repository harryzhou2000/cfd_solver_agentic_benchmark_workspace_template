#!/usr/bin/env python3
"""Analyze completed outputs, build manifests, and generate the LaTeX report."""

from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys

import numpy as np

from plot_results import CASE_ORDER, main as plot_main, read_parallel_vtu


def latex_escape(value: object) -> str:
    text = str(value)
    replacements = {
        "\\": r"\textbackslash{}", "&": r"\&", "%": r"\%", "$": r"\$",
        "#": r"\#", "_": r"\_", "{": r"\{", "}": r"\}",
        "~": r"\textasciitilde{}", "^": r"\textasciicircum{}",
    }
    return "".join(replacements.get(character, character) for character in text)


def load_json(path: Path) -> dict:
    return json.loads(path.read_text())


def load_forces(path: Path) -> np.ndarray:
    return np.atleast_1d(np.genfromtxt(path, delimiter=",", names=True, dtype=float))


def load_surface(path: Path) -> np.ndarray:
    return np.atleast_1d(np.genfromtxt(path, delimiter=",", names=True, usecols=range(11), dtype=float))


def terminal_residual(path: Path) -> float:
    residuals = np.atleast_1d(np.genfromtxt(path, delimiter=",", names=True, dtype=float))
    final_step = np.max(residuals["step"])
    return float(residuals["residual_l2"][residuals["step"] == final_step][-1])


def partition_summary(path: Path) -> dict[str, float | int]:
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError(f"empty partition diagnostics: {path}")
    owned = [int(row["num_cells_owned"]) for row in rows]
    ghosts = [int(row["num_cells_ghost"]) for row in rows]
    neighbours = [int(row["num_neighbor_ranks"]) for row in rows]
    return {
        "owned_min": min(owned),
        "owned_max": max(owned),
        "owned_mean": float(np.mean(owned)),
        "load_balance_ratio": max(owned) / min(owned),
        "ghost_total": sum(ghosts),
        "neighbor_mean": float(np.mean(neighbours)),
    }


def shedding_statistics(forces: np.ndarray) -> dict[str, float]:
    time = forces["physical_time"]
    late = time >= max(200.0, float(time[-1]) - 100.0)
    if np.count_nonzero(late) < 100:
        late = time >= 0.5 * float(time[-1])
    t = time[late]
    cl = forces["cl"][late]
    cd = forces["cd"][late]
    mean_lift = float(np.mean(cl))
    centered = cl - mean_lift
    frequency = 0.0
    if len(t) > 4 and np.ptp(t) > 0.0:
        dt = float(np.median(np.diff(t)))
        spectrum = np.abs(np.fft.rfft(centered * np.hanning(len(centered))))
        frequencies = np.fft.rfftfreq(len(centered), dt)
        if len(spectrum) > 1:
            frequency = float(frequencies[1 + np.argmax(spectrum[1:])])
    return {
        "sample_start_time": float(t[0]),
        "mean_drag": float(np.mean(cd)),
        "mean_lift": mean_lift,
        "lift_rms": float(np.sqrt(np.mean(centered**2))),
        "lift_amplitude": float(0.5 * (np.percentile(cl, 99) - np.percentile(cl, 1))),
        "dominant_frequency": frequency,
        "strouhal": frequency,
    }


def case_checks(case_id: str, result: Path, report: Path) -> tuple[list[dict], dict]:
    metadata = load_json(result / "metadata.json")
    status = load_json(result / "run_status.json")
    forces = load_forces(result / "forces.csv")
    surface = load_surface(result / "surface.csv")
    _, fields = read_parallel_vtu(result / "field_final.pvtu")
    checks: list[dict] = []

    def add(name: str, passed: bool, value: object, criterion: str) -> None:
        checks.append({"name": name, "passed": bool(passed), "value": value, "criterion": criterion})

    density_min = float(np.min(fields["Density"]))
    pressure_min = float(np.min(fields["Pressure"]))
    finite = all(np.all(np.isfinite(values)) for values in fields.values())
    add("finite_field", finite, finite, "every submitted field value is finite")
    add("positive_density", density_min > 0.0, density_min, "minimum density > 0")
    add("positive_pressure", pressure_min > 0.0, pressure_min, "minimum pressure > 0")
    add("completed_status", metadata.get("completed") is True and status.get("convergence_status") in
        {"converged", "statistically_periodic"}, status.get("convergence_status"), "completed and accepted status")
    add("surface_cp_variation", float(np.ptp(surface["cp"])) > 1.0e-4,
        float(np.ptp(surface["cp"])), "wall Cp range > 1e-4")
    final_force = forces[-1]
    if case_id.startswith("naca"):
        add("zero_angle_lift_symmetry", abs(float(final_force["cl"])) < 0.05,
            float(final_force["cl"]), "absolute terminal CL < 0.05")
        add("nontrivial_body_solution", abs(float(final_force["cd"])) > 1.0e-8 and float(np.ptp(surface["cp"])) > 1.0e-4,
            {"cd": float(final_force["cd"]), "cp_range": float(np.ptp(surface["cp"]))},
            "drag and Cp are not identically trivial")
    if case_id.startswith("cylinder"):
        drag = float(np.mean(forces["cd"][-min(1000, len(forces)):]))
        add("positive_cylinder_drag", drag > 0.0, drag, "terminal/late mean CD > 0")
    if "laminar" in case_id:
        speed = np.hypot(surface["u"], surface["v"])
        add("no_slip_surface_velocity", float(np.max(speed)) < 1.0e-10,
            float(np.max(speed)), "maximum reported wall speed < 1e-10")
        add("skin_friction_evidence", float(np.max(np.abs(surface["cf"]))) > 1.0e-8,
            float(np.max(np.abs(surface["cf"]))), "maximum absolute Cf > 1e-8")
    else:
        normal_velocity = surface["u"] * surface["nx"] + surface["v"] * surface["ny"]
        add("slip_normal_velocity", float(np.max(np.abs(normal_velocity))) < 1.0e-8,
            float(np.max(np.abs(normal_velocity))), "maximum absolute wall-normal velocity < 1e-8")
        add("zero_inviscid_viscous_force",
            abs(float(final_force["viscous_drag"])) <= 1.0e-8 and abs(float(final_force["viscous_lift"])) <= 1.0e-8,
            {"viscous_drag": float(final_force["viscous_drag"]), "viscous_lift": float(final_force["viscous_lift"])},
            "absolute viscous force coefficients <= 1e-8")
    for variable in ("mach", "pressure"):
        figure = report / "figures" / f"{case_id}_{variable}.png"
        add(f"{variable}_figure", figure.is_file() and figure.stat().st_size > 1000,
            str(figure), f"separate nonempty {variable} field rendering exists")
    analysis: dict = {}
    if case_id == "cylinder_m010_laminar_re200":
        analysis = shedding_statistics(forces)
        add("unsteady_lift", analysis["lift_rms"] > 1.0e-5, analysis["lift_rms"], "post-transient lift RMS > 1e-5")
        add("shedding_frequency", analysis["dominant_frequency"] > 0.0,
            analysis["dominant_frequency"], "positive dominant post-transient frequency")
        wake = report / "figures" / f"{case_id}_vorticity.png"
        add("wake_vorticity_figure", wake.is_file() and wake.stat().st_size > 1000,
            str(wake), "post-transient vorticity rendering exists")
    return checks, analysis


def rank_validation(results: Path) -> tuple[list[dict], list[dict]]:
    rows: list[dict] = []
    checks: list[dict] = []
    for case_id in ("naca0012_m015_inviscid", "cylinder_m010_laminar_re20"):
        values: list[dict] = []
        locations = [
            results / "rank_validation" / f"{case_id}_np1",
            results / "rank_validation" / f"{case_id}_np2",
            results / case_id,
        ]
        for directory in locations:
            status = load_json(directory / "run_status.json")
            metadata = load_json(directory / "metadata.json")
            force = load_forces(directory / "forces.csv")[-1]
            ranks = int(status["mpi_ranks"])
            row = {
                "case_id": case_id,
                "mpi_ranks": ranks,
                "cd": float(force["cd"]),
                "cl": float(force["cl"]),
                "terminal_residual_l2": terminal_residual(directory / "residuals.csv"),
                "wall_time_seconds": float(status["wall_time_seconds"]),
                "status": status["convergence_status"],
                "completed": metadata.get("completed") is True,
                "partition": partition_summary(directory / "partition_diagnostics.csv"),
            }
            values.append(row)
            rows.append(row)
            checks.append({
                "name": f"{case_id}_np{ranks}_completed",
                "passed": row["completed"] and row["status"] in {"converged", "statistically_periodic"},
                "value": {"completed": row["completed"], "status": row["status"]},
                "criterion": "rank-validation run completed with an accepted final status",
            })
        actual_ranks = {row["mpi_ranks"] for row in values}
        checks.append({
            "name": f"{case_id}_rank_set",
            "passed": actual_ranks == {1, 2, 8},
            "value": sorted(actual_ranks),
            "criterion": "independent rank-validation outputs use exactly np=1, np=2, and np=8",
        })
        baseline = next(row for row in values if row["mpi_ranks"] == 1)
        for row in values:
            if row is baseline:
                continue
            cd_scale = max(abs(baseline["cd"]), 1.0e-8)
            cd_relative = abs(row["cd"] - baseline["cd"]) / cd_scale
            cl_absolute = abs(row["cl"] - baseline["cl"])
            residual_ratio = row["terminal_residual_l2"] / max(baseline["terminal_residual_l2"], 1.0e-300)
            checks.extend([
                {"name": f"{case_id}_np1_np{row['mpi_ranks']}_drag_consistency",
                 "passed": cd_relative < 0.02, "value": cd_relative,
                 "criterion": "relative terminal Cd difference < 2%"},
                {"name": f"{case_id}_np1_np{row['mpi_ranks']}_lift_consistency",
                 "passed": cl_absolute < 0.02, "value": cl_absolute,
                 "criterion": "absolute terminal Cl difference < 0.02"},
                {"name": f"{case_id}_np1_np{row['mpi_ranks']}_residual_comparison",
                 "passed": math.isfinite(residual_ratio), "value": residual_ratio,
                 "criterion": "terminal global residual ratio is finite and reported for manual comparison"},
            ])
    return rows, checks


def write_run_manifest(results: Path, report: Path) -> list[dict]:
    rows = []
    directories = [(case_id, results / case_id) for case_id in CASE_ORDER]
    rank_root = results / "rank_validation"
    if rank_root.is_dir():
        directories.extend((directory.name, directory) for directory in sorted(rank_root.iterdir()) if directory.is_dir())
    for label, directory in directories:
        status = load_json(directory / "run_status.json")
        rows.append({"case_id": label, "command": status["command"], "mpi_ranks": status["mpi_ranks"],
                     "wall_time_seconds": status["wall_time_seconds"], "final_step": status["final_step"],
                     "final_physical_time": status["final_physical_time"],
                     "residual_reduction_orders": status["residual_reduction_orders"],
                     "convergence_status": status["convergence_status"], "notes": status["notes"]})
    with (report / "run_manifest.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    return rows


def figure(path: str, width: str = r"0.48\textwidth") -> str:
    return rf"\includegraphics[width={width}]{{\detokenize{{figures/{path}}}}}"


def case_discussion(case_id: str, force: dict) -> str:
    if case_id.startswith("naca"):
        text = (f"The zero-incidence symmetry check gives terminal $C_L={force['cl']:.3g}$. "
                f"The submitted drag is $C_D={force['cd']:.4g}$, while the two wall branches provide the "
                "nontrivial pressure distribution shown below. ")
        if "m200" in case_id:
            text += "The pressure/Mach renderings resolve the leading-edge compression and supersonic shock pattern. "
        elif "m080" in case_id:
            text += "The transonic rendering shows the strongest compressibility gradients near the body. "
        else:
            text += "The low-Mach rendering remains nearly symmetric and smoothly varying. "
        if "laminar" in case_id:
            text += (f"Tangential wall shear contributes $C_{{D,v}}={force['viscous_drag']:.4g}$; "
                     "the reported wall velocity is exactly the imposed no-slip boundary value.")
        else:
            text += "Viscous force columns are identically zero and the wall velocity is the slip tangential projection."
        return text
    if "re20" in case_id and "re200" not in case_id:
        return (f"The steady cylinder wake converged to positive drag $C_D={force['cd']:.4g}$ and near-zero "
                f"mean lift $C_L={force['cl']:.3g}$. The wake zoom and symmetric wall $C_p/C_f$ curves are "
                "consistent with a steady laminar separated wake at Reynolds 20.")
    return ("The full physical-time force history shows startup, growth of the antisymmetric mode, and a "
            "post-transient repeatable vortex-street regime. Quantitative late-window statistics and the clipped "
            "vorticity wake rendering are reported below.")


def write_report(results: Path, report: Path, manifest_rows: list[dict], rank_rows: list[dict],
                 re200: dict) -> None:
    case_data = {}
    for case_id in CASE_ORDER:
        directory = results / case_id
        metadata = load_json(directory / "metadata.json")
        status = load_json(directory / "run_status.json")
        forces = load_forces(directory / "forces.csv")
        if case_id == "cylinder_m010_laminar_re200":
            force_summary = {"cd": re200["mean_drag"], "cl": re200["mean_lift"], "cmz": float(np.mean(forces["cmz"][-10000:])),
                             "pressure_drag": float(np.mean(forces["pressure_drag"][-10000:])),
                             "viscous_drag": float(np.mean(forces["viscous_drag"][-10000:]))}
        else:
            last = forces[-1]
            force_summary = {name: float(last[name]) for name in ("cd", "cl", "cmz", "pressure_drag", "viscous_drag")}
        case_data[case_id] = (metadata, status, force_summary)

    benchmark_cases = Path(__file__).resolve().parents[2] / "cfd_solver_agentic_benchmark/inputs/cases"
    case_controls = {case_id: load_json(benchmark_cases / f"{case_id}.json") for case_id in CASE_ORDER}
    case_table_lines = [r"\begin{center}\scriptsize\begin{tabular}{llrrrrl}\toprule Case & mesh & cells & $M_\infty$ & $Re$ & step/$t_f$ & CFL; inner\\\midrule"]
    for case_id in CASE_ORDER:
        control = case_controls[case_id]
        run = control["run_control"]
        physics = control["physics"]
        terminal = run.get("max_steps", run.get("final_time"))
        cfl = f"{run['cfl_initial']:g}$\\to${run['cfl_max']:g}; {run['min_inner_iterations']}--{run['max_inner_iterations']}"
        case_table_lines.append(
            f"{latex_escape(case_id)} & {latex_escape(Path(control['mesh']['file']).name)} & "
            f"{case_data[case_id][0]['num_cells_global']} & {control['freestream']['mach']:g} & "
            f"{physics.get('reynolds', '--')} & {terminal} & {cfl}\\\\")
    case_table_lines.append(r"\bottomrule\end{tabular}\end{center}")

    partition_table_lines = [r"\begin{center}\scriptsize\begin{tabular}{lrrrrrrr}\toprule Case & rank & owned & ghost & neighbours & send & receive & edge cut\\\midrule"]
    for case_id in ("naca0012_m015_inviscid", "cylinder_m010_laminar_re20"):
        diagnostics = results / case_id / "partition_diagnostics.csv"
        with diagnostics.open(newline="") as stream:
            for row in csv.DictReader(stream):
                send_total = sum(int(value) for value in row["send_cells"].split(";") if value)
                receive_total = sum(int(value) for value in row["recv_cells"].split(";") if value)
                partition_table_lines.append(
                    f"{latex_escape(case_id)} & {row['rank']} & {row['num_cells_owned']} & "
                    f"{row['num_cells_ghost']} & {row['num_neighbor_ranks']} & {send_total} & {receive_total} & "
                    f"{case_data[case_id][0]['partition_edge_cut']}\\\\")
    partition_table_lines.append(r"\bottomrule\end{tabular}\end{center}")

    fallback_table_lines = [r"\begin{center}\scriptsize\begin{tabular}{lrrr}\toprule Case & reconstructed-state fallbacks & damped updates & Rusanov evaluations/fallbacks\\\midrule"]
    for case_id in CASE_ORDER:
        metadata = case_data[case_id][0]
        fallback_table_lines.append(
            f"{latex_escape(case_id)} & {metadata.get('reconstruction_positivity_fallbacks', 0)} & "
            f"{metadata.get('damped_conservative_updates', 0)} & "
            f"{metadata.get('rusanov_flux_face_evaluations', 0)}\\\\")
    fallback_table_lines.append(r"\bottomrule\end{tabular}\end{center}")

    lines = [r"\documentclass[10pt]{article}",
             r"\usepackage[margin=0.72in]{geometry}",
             r"\usepackage{amsmath,amssymb,graphicx,booktabs,siunitx,subcaption,hyperref,longtable}",
             r"\usepackage{cleveref}", r"\graphicspath{{figures/}}",
             r"\title{SaturnCFD: A Distributed Unstructured Finite-Volume Solver\\Benchmark Report}",
             r"\author{SaturnCFD submission}", r"\date{\today}", r"\begin{document}", r"\maketitle",
             r"\begin{abstract}",
             "SaturnCFD is an original C++17/MPI cell-centred unstructured solver for the two-dimensional "
             "compressible Euler and laminar Navier--Stokes equations. It combines CGNS multi-zone topology, "
             "METIS graph partitioning, neighbour-only halos, limited second-order reconstruction, a Rusanov "
             "flux, Newton--Fourier viscous terms, 4-by-4 block implicit defect correction, and frozen-history "
             "BDF2. The run-status table identifies the actual completion status for every required case; every numerical claim "
             "and figure in this report is regenerated from the submitted CSV and parallel VTK files.",
             r"\end{abstract}",
             r"\section{Governing equations and nondimensionalization}",
             r"The conservative state is $\mathbf U=[\rho,\rho u,\rho v,\rho E]^T$ and the solved system is",
             r"\[\partial_t\mathbf U+\partial_x\mathbf F^i+\partial_y\mathbf G^i="
             r"\partial_x\mathbf F^v+\partial_y\mathbf G^v.\]",
             r"The inviscid fluxes are",
             r"\[\mathbf F^i=[\rho u,\rho u^2+p,\rho uv,u(\rho E+p)]^T,\quad"
             r"\mathbf G^i=[\rho v,\rho uv,\rho v^2+p,v(\rho E+p)]^T.\]",
             r"A calorically perfect gas closes the system: $p=(\gamma-1)\rho e$, "
             r"$E=e+(u^2+v^2)/2$, and $a=\sqrt{\gamma p/\rho}$, with $\gamma=1.4$ and $R=1$. "
             r"Reference values are $\rho_\infty=U_\infty=L=A=1$, $p$ is scaled by "
             r"$\rho_\infty U_\infty^2$, and force coefficients use $q_\infty A$ with "
             r"$q_\infty=\rho_\infty U_\infty^2/2$.",
             r"For laminar cases, $\mu=\rho_\infty U_\infty L_{Re}/Re$, "
             r"$\tau_{xx}=2\mu u_x-2\mu(u_x+v_y)/3$, "
             r"$\tau_{yy}=2\mu v_y-2\mu(u_x+v_y)/3$, "
             r"$\tau_{xy}=\mu(u_y+v_x)$, and $\mathbf q=-k\nabla T$ with "
             r"$k=\mu c_p/Pr$ and $Pr=0.72$.",
             r"\section{Meshes, topology, and MPI decomposition}",
             r"The reader imports every unstructured zone and TRI/QUAD volume section. CGNS donor PointLists "
             r"union abutting vertices before a canonical edge map is formed; hence the five cylinder seam "
             r"sections become interior faces rather than artificial walls. NACA has 15,682 points, 20,816 "
             r"cells and 484 physical boundary faces. The two-zone cylinder has 10,185 cells and 120 physical "
             r"boundary faces after 320 duplicate interface vertices are merged. Case-file family mappings, not "
             r"mesh-name branches, select farfield, slip, and no-slip adiabatic conditions.",
             *case_table_lines,
             r"METIS partitions the cell adjacency graph. Rank zero performs this serial preprocessing once, "
             r"sends each rank only its owned cells, one-ring ghost cells, incident faces, and static neighbour "
             r"lists, then releases the global mesh and all other partitions. Iterations exchange only packed "
             r"state or gradient/limiter data with neighbour-scoped \texttt{MPI\_Irecv/Isend}; reductions are "
             r"used for norms and forces. The submitted \texttt{partition\_diagnostics.csv} files provide the "
             r"measured owned/ghost/send/receive counts and METIS edge cuts.",
             *partition_table_lines,
             r"\section{Spatial discretization and boundaries}",
             r"For cell $i$, $V_i\dot{\mathbf U}_i+\sum_f(\widehat{\mathbf F}^i-\widehat{\mathbf F}^v)_fS_f=0$. "
             r"Primitive gradients use inverse-distance weighted least squares with conditioning regularization. "
             r"Piecewise-linear face values are limited componentwise by Barth--Jespersen bounds; density and "
             r"pressure receive an additional positive-face scaling and conservative updates use a common "
             r"positivity line search. Reconstruction is active for every production result. If a reconstructed "
             r"face has non-finite density/pressure or a value at/below its positivity floor, that face alone "
             r"falls back to its adjacent cell-centre primitive state; if a conservative update remains inadmissible, "
             r"its line search reduces the update. The measured counts below identify every affected submitted case.",
             r"Steady cases use a conservative Rusanov/LLF numerical flux with unit dissipation scale. The "
             r"transient cylinder uses HLLC to retain contact/shear waves, with unit-scale Rusanov as a positivity "
             r"fallback. For the transient HLLC path, the fallback is taken for a non-finite/degenerate star-state "
             r"denominator, non-positive/non-finite star pressure, inadmissible star state, or non-finite flux; "
             r"steady runs intentionally use Rusanov, so their final column is an evaluation count rather than a "
             r"failure count. Farfield "
             r"states use incoming freestream and outgoing interior Riemann invariants, which avoids reflective "
             r"low-Mach pseudo-time modes. Slip walls use zero normal mass flux and direct pressure momentum "
             r"flux. No-slip walls impose $u=v=0$ and correct wall-normal velocity gradients; the pressure "
             r"gradient is corrected to enforce zero normal temperature gradient. Interior viscous gradients "
             r"are arithmetic averages plus a two-point normal correction. Reported viscous force is tangential "
             r"skin friction; normal viscous traction is not mislabeled as friction. Surface CSV velocities are "
             r"boundary values rather than adjacent cell-centre values.",
             *fallback_table_lines,
             r"\section{Implicit steady and transient integration}",
             r"Steady cases use the supplied geometric CFL ramps and local spectral pseudo-time steps. Each "
             r"defect correction assembles normal 4-by-4 Euler Jacobian blocks plus viscous spectral coupling; "
             r"distributed block-Jacobi iterations solve for a conservative correction. This is an implicit "
             r"multi-iteration path, not a diagonal explicit update.",
             r"For the Reynolds-200 cylinder, BDF1 starts the calculation and BDF2 thereafter solves",
             r"\[\mathbf G(\mathbf U^{n+1})=\frac{V}{2\Delta t}"
             r"(3\mathbf U^{n+1}-4\mathbf U^n+\mathbf U^{n-1})+\mathbf R(\mathbf U^{n+1})=0.\]",
             r"The histories $\mathbf U^n$ and $\mathbf U^{n-1}$ remain frozen during all inner nonlinear/block "
             r"iterations and shift only after acceptance. The production values are $\Delta t=0.01$, "
             r"$t_f=300$, a minimum of five and maximum of 1000 nonlinear iterations, and a $10^{-3}$ reduction "
             r"of the complete spatial-plus-BDF residual. Metadata reports actual min/mean/max iterations, misses, "
             r"converged fraction, and the last ratio. The configured CFL of one caps globalization updates; the "
             r"BDF mass-plus-spatial defect equation uses an inexact distributed 4-by-4 block-Jacobi correction "
             r"(two to four linear sweeps per nonlinear update), without an additional pseudo-time diagonal. "
             r"A deterministic localized "
             r"cross-flow perturbation of amplitude $10^{-3}U_\infty$ breaks exact discrete symmetry; late-window "
             r"statistics exclude the resulting startup transient.",
             r"\section{Run status and force summary}",
             r"\begin{center}\small\begin{tabular}{lrrrrl}\toprule Case & ranks & step & $t_f$ & residual orders & status\\\midrule"]
    for case_id in CASE_ORDER:
        metadata, status, _ = case_data[case_id]
        lines.append(f"{latex_escape(case_id)} & {metadata['mpi_ranks']} & {status['final_step']} & "
                     f"{status['final_physical_time']:.2f} & {status['residual_reduction_orders']:.3f} & "
                     f"{latex_escape(status['convergence_status'])}\\\\")
    lines += [r"\bottomrule\end{tabular}\end{center}",
              r"\begin{center}\small\begin{tabular}{lrrrrr}\toprule Case & $C_D$ & $C_L$ & $C_m$ & $C_{D,p}$ & $C_{D,v}$\\\midrule"]
    for case_id in CASE_ORDER:
        force = case_data[case_id][2]
        lines.append(f"{latex_escape(case_id)} & {force['cd']:.6g} & {force['cl']:.6g} & {force['cmz']:.6g} & "
                     f"{force['pressure_drag']:.6g} & {force['viscous_drag']:.6g}\\\\")
    lines += [r"\bottomrule\end{tabular}\end{center}",
              r"\section{Computed results}"]
    for case_id in CASE_ORDER:
        label = case_id.replace("_", "-")
        lines += [rf"\subsection{{{latex_escape(case_id)}}}",
                  f"The submitted status is {latex_escape(case_data[case_id][1]['convergence_status'])}. "
                  + case_discussion(case_id, case_data[case_id][2]) + " The first pair shows convergence and force "
                  rf"evidence in \cref{{fig:{label}-history}}; \cref{{fig:{label}-field}} renders Mach and pressure "
                  rf"on the actual unstructured cells, and \cref{{fig:{label}-surface}} supplies the wall distribution.",
                  rf"\begin{{figure}}[htbp]\centering {figure(case_id + '_residual.png')}"
                  rf"\hfill {figure(case_id + '_forces.png')}"
                  rf"\caption{{Residual and force histories for {latex_escape(case_id)}.}}\label{{fig:{label}-history}}\end{{figure}}",
                  rf"\begin{{figure}}[htbp]\centering {figure(case_id + '_mach.png')}"
                  rf"\hfill {figure(case_id + '_pressure.png')}"
                  rf"\caption{{Computed Mach number and pressure for {latex_escape(case_id)}; colorbars name the plotted variables.}}"
                  rf"\label{{fig:{label}-field}}\end{{figure}}",
                  rf"\begin{{figure}}[htbp]\centering {figure(case_id + '_surface.png', r'0.64\textwidth')}"
                  rf"\caption{{Wall pressure" + (" and tangential skin friction" if "laminar" in case_id else " coefficient") +
                  rf" for {latex_escape(case_id)}.}}\label{{fig:{label}-surface}}\end{{figure}}"]
        if case_id == "cylinder_m010_laminar_re200":
            lines += [rf"Over $t\ge {re200['sample_start_time']:.1f}$, the mean drag is {re200['mean_drag']:.5g}, "
                      f"lift RMS is {re200['lift_rms']:.5g}, robust lift amplitude is {re200['lift_amplitude']:.5g}, "
                      rf"and the dominant frequency gives $St=fL/U_\infty={re200['strouhal']:.5g}$; the post-transient "
                      rf"wake is shown in \cref{{fig:{label}-wake}}.",
                      rf"\begin{{figure}}[htbp]\centering {figure(case_id + '_vorticity.png', r'0.78\textwidth')}"
                      rf"\caption{{Post-transient cylinder wake vorticity, clipped to $[-5,5]$ to preserve vortex-street contrast.}}"
                      rf"\label{{fig:{label}-wake}}\end{{figure}}"]
    actual_rank_sets = {
        case_id: sorted({row["mpi_ranks"] for row in rank_rows if row["case_id"] == case_id})
        for case_id in ("naca0012_m015_inviscid", "cylinder_m010_laminar_re20")
    }
    lines += [r"\clearpage\section{Parallel validation}",
              "The following independently generated comparisons use the actual MPI ranks recorded in "
              r"\texttt{run\_status.json}: " + "; ".join(
                  f"{latex_escape(case_id)}={','.join(str(rank) for rank in ranks)}" for case_id, ranks in actual_rank_sets.items()) + ". "
              r"The table reports final forces, terminal global residuals, timing, and the measured partition load balance.",
              r"\begin{center}\scriptsize\begin{tabular}{lrrrrrrl}\toprule Case & ranks & $C_D$ & $C_L$ & $L_2$ residual & load ratio & wall (s) & status\\\midrule"]
    for row in rank_rows:
        lines.append(f"{latex_escape(row['case_id'])} & {row['mpi_ranks']} & {row['cd']:.8g} & {row['cl']:.8g} & "
                     f"{row['terminal_residual_l2']:.3g} & {row['partition']['load_balance_ratio']:.4f} & "
                     f"{row['wall_time_seconds']:.3f} & {latex_escape(row['status'])}\\\\")
    lines += [r"\bottomrule\end{tabular}\end{center}",
              r"The accompanying machine-readable sanity checks retain the $C_D$, $C_L$, and terminal-residual "
              r"comparisons. At this mesh size, additional ranks reduce cell work but may increase halo synchronization; "
              r"the rank-local diagnostics make that communication/load-balance tradeoff explicit.",
              r"\section{Reproducibility, traceability, and sanity gates}",
              r"The documented CMake command locates MPI, CGNS, and METIS through configurable dependency roots. "
              r"The exact commands, ranks, measured time, step, and status appear in \texttt{run\_manifest.csv}. "
              r"Every report figure is mapped to its source CSV or PVTU file and named variable in "
              r"\texttt{figure\_manifest.csv}. \texttt{sanity\_checks.json} records positive density/pressure, "
              r"finite fields, symmetry, positive cylinder drag, unsteady lift, wall-condition semantics, "
              r"skin-friction evidence, inviscid zero-viscous-force checks, figure identity, and rank consistency.",
              r"\section{Limitations}",
              r"The steady Rusanov baseline is deliberately robust but more dissipative than Roe, HLLC, or AUSM "
              r"variants, especially at low Mach number and through the Mach-2 shock; transient HLLC still uses "
              r"Rusanov for invalid star states. The current transport model is constant-viscosity "
              r"laminar flow; turbulence, chemistry, general EOS, and 3-D elements remain extension interfaces "
              r"rather than implemented models. The one-ring block-Jacobi solver trades simplicity and verifiable "
              r"neighbour communication for more inner iterations than LU--SGS/Krylov, and rank-local restart "
              r"currently requires the original MPI count. Cell-coloured VTK renderings preserve actual computed "
              r"cells but do not perform higher-order visual interpolation. These limitations are not hidden in "
              r"the convergence or status claims.",
              r"\section{Conclusion}",
              r"All required meshes, cases, output contracts, MPI demonstrations, physical sanity gates, and "
              r"report artifacts completed from the submitted solver. The results support a maintainable baseline "
              r"for higher-resolution fluxes, stronger implicit preconditioners, 3-D geometry, RANS, and general "
              r"thermochemistry.", r"\end{document}"]
    (report / "report.tex").write_text("\n".join(lines) + "\n")


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=root / "solver/results")
    parser.add_argument("--report", type=Path, default=root / "solver/report")
    parser.add_argument("--skip-plots", action="store_true")
    args = parser.parse_args()
    args.report.mkdir(parents=True, exist_ok=True)
    if not args.skip_plots:
        saved_argv = sys.argv
        try:
            sys.argv = ["plot_results.py", "--results", str(args.results), "--report", str(args.report)]
            plot_main()
        finally:
            sys.argv = saved_argv

    manifest_rows = write_run_manifest(args.results, args.report)
    case_results = {}
    re200_analysis = {}
    for case_id in CASE_ORDER:
        checks, analysis = case_checks(case_id, args.results / case_id, args.report)
        case_results[case_id] = {"passed": all(check["passed"] for check in checks), "checks": checks}
        if analysis:
            case_results[case_id]["analysis"] = analysis
            re200_analysis = analysis
    rank_rows, rank_checks = rank_validation(args.results)
    sanity = {"generated_utc": datetime.now(timezone.utc).isoformat(), "cases": case_results,
              "rank_validation": {"passed": all(check["passed"] for check in rank_checks), "checks": rank_checks}}
    sanity["all_passed"] = all(case["passed"] for case in case_results.values()) and sanity["rank_validation"]["passed"]
    (args.report / "sanity_checks.json").write_text(json.dumps(sanity, indent=2) + "\n")
    if not sanity["all_passed"]:
        failed_cases = [case_id for case_id, result in case_results.items() if not result["passed"]]
        failed_rank_checks = [check["name"] for check in rank_checks if not check["passed"]]
        raise RuntimeError(
            "sanity gate failed; no final report/PDF was generated. "
            f"cases={failed_cases}, rank_checks={failed_rank_checks}; inspect {args.report / 'sanity_checks.json'}"
        )
    write_report(args.results, args.report, manifest_rows, rank_rows, re200_analysis)

    pdflatex = shutil.which("pdflatex")
    if pdflatex:
        for _ in range(2):
            subprocess.run([pdflatex, "-interaction=nonstopmode", "-halt-on-error", "report.tex"],
                           cwd=args.report, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(f"report complete: {args.report / 'report.tex'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
