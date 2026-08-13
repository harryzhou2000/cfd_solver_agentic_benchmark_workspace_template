#!/usr/bin/env python3
"""Create a traceable LaTeX benchmark report from submitted solver artifacts.

The generator deliberately does not calculate a CFD result or infer a successful
run.  Numerical values come from ``report/result_statistics.json``, case output
files, and ``report/run_manifest.csv``.  A missing artifact is printed as a
missing-evidence notice in the resulting report instead of being silently
omitted.  This makes the report useful both during a run campaign and at final
submission time.

Typical use (from the project root)::

    python3 tools/generate_report.py
    ./tools/build_report.sh

Use ``--strict`` in CI/final-submission checks to fail when required report
inputs, primary ``np=8`` evidence, rank-comparison evidence, or required
per-case figures are absent.  ``--strict`` does not change the generated TeX
file: the evidence audit is always rendered so a draft cannot hide gaps.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


REQUIRED_CASES = (
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
)

TRANSIENT_CASE = "cylinder_m010_laminar_re200"

CASE_TABLE_LABELS = {
    "naca0012_m015_inviscid": "NACA .15 inv.",
    "naca0012_m080_inviscid": "NACA .80 inv.",
    "naca0012_m200_inviscid": "NACA 2.0 inv.",
    "naca0012_m015_laminar_re5000": "NACA .15 Re5k",
    "naca0012_m080_laminar_re5000": "NACA .80 Re5k",
    "naca0012_m200_laminar_re5000": "NACA 2.0 Re5k",
    "cylinder_m010_laminar_re20": "Cylinder Re20",
    "cylinder_m010_laminar_re200": "Cylinder Re200",
}

# These report-audit thresholds mirror the signal-quality contract in
# postprocess.py.  Keeping them explicit here makes the generated report's
# strict evidence requirements readable without importing plotting machinery.
FREQUENCY_ESTIMATION_METHOD = "linear_detrend_hann_window_fft"
FREQUENCY_MIN_CYCLES = 3.0
FREQUENCY_MIN_PEAK_POWER_FRACTION = 0.10
FREQUENCY_MIN_PEAK_PROMINENCE = 5.0


def tex(value: Any) -> str:
    """Escape arbitrary artifact text for normal LaTex text mode."""
    text = str(value)
    replacements = {
        "\\": r"\textbackslash{}",
        "&": r"\&",
        "%": r"\%",
        "$": r"\$",
        "#": r"\#",
        "_": r"\_",
        "{": r"\{",
        "}": r"\}",
        "~": r"\textasciitilde{}",
        "^": r"\textasciicircum{}",
    }
    return "".join(replacements.get(character, character) for character in text).replace("\n", " ")


def code(value: Any) -> str:
    """Escape arbitrary artifact text in a monospaced report field."""
    return r"\texttt{" + tex(value) + "}"


def breakable_code(value: Any) -> str:
    """Render a path or identifier outside moving headings with line breaks."""
    text = str(value).replace("\n", " ")
    if any(character in text for character in "{}"):
        return code(text)
    return r"\nolinkurl{" + text + "}"


def caption_tex(value: Any) -> str:
    """Escape manifest captions while preserving a small allowlist of math."""
    text = str(value)
    math_tokens = {
        "$M$": r"$M$",
        "$p$": r"$p$",
        r"$\omega_z L/U_\infty$": r"$\omega_z L/U_\infty$",
        r"$|\mathbf{u}|/U_\infty$": r"$|\mathbf{u}|/U_\infty$",
    }
    placeholders: dict[str, str] = {}
    for index, (token, rendered) in enumerate(math_tokens.items()):
        placeholder = f"CAPTIONMATHTOKEN{index}"
        text = text.replace(token, placeholder)
        placeholders[placeholder] = rendered
    escaped = tex(text)
    for placeholder, rendered in placeholders.items():
        escaped = escaped.replace(placeholder, rendered)
    return escaped


def table_case(case_id: Any) -> str:
    """Use compact, unambiguous labels in dense evidence tables."""
    value = str(case_id)
    return tex(CASE_TABLE_LABELS.get(value, value))


def compact_method(value: Any) -> str:
    """Human-readable table rendering for fully documented metadata values."""
    raw = str(value) if value not in (None, "") else "not reported"
    labels = {
        "first_order_CFL_continuation_then_blended_weighted_least_squares_piecewise_linear":
            r"1st-order ramp $\to$ WLS",
        "steady_base_continuation_then_full_weighted_least_squares_piecewise_linear":
            r"steady base $\to$ WLS",
        "barth_jespersen_active": r"Barth--Jespersen",
        "face_reconstruction_scaling_and_cell_local_fraction_to_boundary":
            "face + cell scaling",
        "rusanov_local_lax_friedrichs": "Rusanov",
        "corrected_central_primitive_gradient_cell_center_normal_difference":
            "corrected central",
        "implicit_local_pseudo_time": "local pseudo-time",
        "bdf2_dual_time": "BDF2 dual-time",
        "matrix_free_gmres_pseudo_transient_newton": "matrix-free GMRES",
        "matrix_free_gmres_bdf2_pseudo_transient_newton": "matrix-free GMRES",
        "metis_kway": "METIS K-way",
        "neighbor_isend_irecv": "neighbour Isend/Irecv",
        "statistically_periodic": "stat. periodic",
        "final row": "final",
        "tail mean": "tail mean",
    }
    return labels.get(raw, tex(raw.replace("_", " ")))


def label(*parts: str) -> str:
    token = "-".join(parts).lower()
    token = re.sub(r"[^a-z0-9:-]+", "-", token).strip("-")
    return f"fig:{token}" if not token.startswith("fig:") else token


def latex_path(path: Path | str) -> str:
    """Pass a path literally to graphicx; filenames are never interpolated as TeX."""
    normalized = Path(path).as_posix().replace("\n", " ")
    return r"\detokenize{" + normalized.replace("}", r"\string}") + "}"


def number(value: Any, digits: int = 5) -> str:
    try:
        value = float(value)
    except (TypeError, ValueError):
        return "not reported"
    if not math.isfinite(value):
        return "not finite"
    if value == 0:
        return "0"
    magnitude = abs(value)
    if magnitude >= 1.0e4 or magnitude < 1.0e-3:
        return f"{value:.{digits - 1}e}"
    return f"{value:.{digits}g}"


def integer(value: Any) -> str:
    try:
        return str(int(value))
    except (TypeError, ValueError):
        return "not reported"


def load_json(path: Path, issues: list[str], title: str) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        issues.append(f"missing {title}: {path}")
    except json.JSONDecodeError as exc:
        issues.append(f"invalid {title}: {path} ({exc})")
    return None


def load_csv(path: Path, issues: list[str], title: str) -> list[dict[str, str]]:
    try:
        with path.open(newline="", encoding="utf-8") as stream:
            return list(csv.DictReader(stream))
    except FileNotFoundError:
        issues.append(f"missing {title}: {path}")
    except (csv.Error, UnicodeDecodeError) as exc:
        issues.append(f"invalid {title}: {path} ({exc})")
    return []


def mapping(value: Any) -> Mapping[str, Any]:
    return value if isinstance(value, Mapping) else {}


def sequence(value: Any) -> list[Any]:
    return list(value) if isinstance(value, list) else []


def relative_to(path: Path, root: Path) -> Path:
    try:
        return path.resolve().relative_to(root.resolve())
    except ValueError:
        return path


@dataclass(frozen=True)
class Figure:
    file: str
    case_id: str
    figure_type: str
    variable: str
    source_file: str
    caption: str


def parse_figures(rows: Iterable[Mapping[str, str]], issues: list[str]) -> list[Figure]:
    figures: list[Figure] = []
    required = {"figure_file", "case_id", "figure_type", "variable", "source_file", "caption"}
    for index, row in enumerate(rows, start=2):
        missing = sorted(key for key in required if not row.get(key))
        if missing:
            issues.append(f"figure manifest row {index} lacks {', '.join(missing)}")
            continue
        figures.append(Figure(*(row[key].strip() for key in (
            "figure_file", "case_id", "figure_type", "variable", "source_file", "caption"))))
    return figures


def read_partition_rows(case_dir: Path, issues: list[str]) -> list[dict[str, Any]]:
    csv_path = case_dir / "partition_diagnostics.csv"
    json_path = case_dir / "partition_diagnostics.json"
    if csv_path.exists():
        rows = load_csv(csv_path, issues, "partition diagnostics")
        required = {"rank", "num_cells_owned", "num_cells_ghost", "num_neighbor_ranks", "send_cells", "recv_cells"}
        for row in rows:
            absent = required.difference(row)
            if absent:
                issues.append(f"partition diagnostics {csv_path} lacks columns {', '.join(sorted(absent))}")
                break
        return [dict(row) for row in rows]
    document = load_json(json_path, issues, "partition diagnostics")
    if isinstance(document, list):
        return [dict(item) for item in document if isinstance(item, Mapping)]
    document_map = mapping(document)
    for key in ("ranks", "partitions", "diagnostics"):
        if isinstance(document_map.get(key), list):
            return [dict(item) for item in document_map[key] if isinstance(item, Mapping)]
    if document is not None:
        issues.append(f"unrecognised partition diagnostics schema: {json_path}")
    return []


def numeric_column(rows: Sequence[Mapping[str, str]], field: str) -> list[float]:
    values: list[float] = []
    for row in rows:
        try:
            value = float(row[field])
        except (KeyError, TypeError, ValueError):
            continue
        if math.isfinite(value):
            values.append(value)
    return values


def force_summary(case_dir: Path, case_id: str, issues: list[str]) -> Mapping[str, Any]:
    """Read the submitted force history without manufacturing a missing column.

    Steady cases use the final row.  The transient Re200 summary uses the final
    40 percent (the same post-transient convention used by postprocess.py).
    """
    rows = load_csv(case_dir / "forces.csv", issues, f"force history for {case_id}")
    if not rows:
        return {}
    required = {"cl", "cd", "cmz", "pressure_drag", "viscous_drag", "pressure_lift", "viscous_lift"}
    absent = sorted(key for key in required if key not in rows[0])
    if absent:
        issues.append(f"force history {case_dir / 'forces.csv'} lacks columns {', '.join(absent)}")
    selected = rows[max(0, int(0.6 * len(rows))):] if case_id == TRANSIENT_CASE else rows[-1:]
    result: dict[str, Any] = {"basis": "tail mean" if case_id == TRANSIENT_CASE else "final row"}
    for field in required:
        values = numeric_column(selected, field)
        if values:
            result[field] = sum(values) / len(values)
    return result


def parse_case_configs(case_root: Path, issues: list[str]) -> dict[str, Mapping[str, Any]]:
    configs: dict[str, Mapping[str, Any]] = {}
    for case_id in REQUIRED_CASES:
        document = load_json(case_root / f"{case_id}.json", issues, "case input")
        if isinstance(document, Mapping):
            configs[case_id] = document
            if document.get("case_id") != case_id:
                issues.append(f"case input id mismatch in {case_id}.json")
    return configs


def manifest_case_dirs(project_root: Path, results_root: Path,
                       manifest: Sequence[Mapping[str, str]]) -> dict[tuple[str, int], Path]:
    directories: dict[tuple[str, int], Path] = {}
    for row in manifest:
        case_id = row.get("case_id", "").strip()
        try:
            ranks = int(row.get("mpi_ranks", ""))
        except ValueError:
            continue
        output = row.get("output_directory", "").strip()
        if output:
            candidate = Path(output)
            if not candidate.is_absolute():
                candidate = project_root / candidate
        else:
            candidate = results_root / case_id
        directories[(case_id, ranks)] = candidate
    return directories


def choose_primary_dirs(results_root: Path, manifest_dirs: Mapping[tuple[str, int], Path],
                        stats: Mapping[str, Mapping[str, Any]]) -> dict[str, Path]:
    primary: dict[str, Path] = {}
    for case_id in REQUIRED_CASES:
        expected = results_root / case_id
        candidates = [(rank, path) for (name, rank), path in manifest_dirs.items() if name == case_id]
        exact = [(rank, path) for rank, path in candidates if path.resolve() == expected.resolve()]
        if exact:
            primary[case_id] = exact[0][1]
        elif case_id in stats:
            stat_ranks = stats[case_id].get("mpi_ranks")
            matching = [(rank, path) for rank, path in candidates if str(rank) == str(stat_ranks)]
            primary[case_id] = matching[0][1] if matching else expected
        elif candidates:
            primary[case_id] = max(candidates, key=lambda item: item[0])[1]
        else:
            primary[case_id] = expected
    return primary


def status_value(status: Mapping[str, Any], metadata: Mapping[str, Any]) -> str:
    value = status.get("convergence_status", metadata.get("convergence_status", "not reported"))
    return str(value)


def active_method(metadata: Mapping[str, Any], field: str, fallback: str = "not reported") -> str:
    value = metadata.get(field, fallback)
    if value in (None, ""):
        return fallback
    return str(value)


def make_tabular(headers: Sequence[str], rows: Sequence[Sequence[str]], spec: str) -> list[str]:
    font_size = r"\scriptsize" if len(headers) >= 7 else r"\small"
    lines = [r"\begin{center}", r"\begingroup", font_size,
             r"\setlength{\tabcolsep}{3pt}", rf"\begin{{tabular}}{{{spec}}}", r"\toprule"]
    lines.append(" & ".join(headers) + r" \\")
    lines.append(r"\midrule")
    if rows:
        lines.extend(" & ".join(row) + r" \\" for row in rows)
    else:
        lines.append(r"\multicolumn{" + str(len(headers)) + r"}{c}{No submitted rows.} \\")
    lines.extend([r"\bottomrule", r"\end{tabular}", r"\endgroup", r"\end{center}"])
    return lines


def case_control_rows(configs: Mapping[str, Mapping[str, Any]]) -> list[list[str]]:
    rows: list[list[str]] = []
    for case_id in REQUIRED_CASES:
        config = configs.get(case_id, {})
        physics = mapping(config.get("physics"))
        flow = mapping(config.get("freestream"))
        control = mapping(config.get("run_control"))
        mode = str(physics.get("mode", "not reported"))
        re = physics.get("reynolds")
        descriptor = f"M={number(flow.get('mach'))}, {mode}"
        if re is not None:
            descriptor += f", Re={number(re)}"
        if control.get("type") == "transient":
            controls = (
                f"dt={number(control.get('time_step'))}; tf={number(control.get('final_time'))}; "
                f"inner {integer(control.get('min_inner_iterations'))}--{integer(control.get('max_inner_iterations'))}; "
                f"target {number(control.get('inner_residual_reduction_target'))}; "
                f"pseudo CFL {number(control.get('cfl_initial'))}; Rusanov scale {number(control.get('rusanov_dissipation_scale'))}"
            )
        else:
            controls = (
                f"max {integer(control.get('max_steps'))}; CFL {number(control.get('cfl_initial'))}--{number(control.get('cfl_max'))}; "
                f"ramp {integer(control.get('pseudo_cfl_ramp_steps'))}; "
                f"inner {integer(control.get('min_inner_iterations'))}--{integer(control.get('max_inner_iterations'))}; "
                f"target {number(control.get('inner_residual_reduction_target'))}"
            )
        bc = ", ".join(f"{tag}: {kind}" for tag, kind in mapping(config.get("boundary_conditions")).items()) or "not reported"
        rows.append([table_case(case_id), tex(descriptor), tex(controls),
                     tex(bc.replace("_", " "))])
    return rows


def result_record(statistics: Mapping[str, Mapping[str, Any]], case_id: str) -> Mapping[str, Any]:
    return statistics.get(case_id, {})


def figure_variable_key(figure: Figure) -> str:
    variable = figure.variable.lower().strip()
    if variable == "residual_l2":
        return "residual"
    if variable in {"cl, cd", "cl,cd"}:
        return "force"
    if variable == "cp":
        return "surface"
    return variable


def has_required_figures(case_id: str, config: Mapping[str, Any],
                         figures: Sequence[Figure]) -> tuple[bool, list[str]]:
    keys = {figure_variable_key(item) for item in figures if item.case_id == case_id}
    missing = [key for key in ("residual", "force", "surface", "mach", "pressure") if key not in keys]
    if mapping(config.get("physics")).get("mode") == "laminar" and not ({"cf", "skin_friction"} & keys):
        missing.append("skin-friction distribution")
    if case_id.startswith("cylinder_") and "velocity_magnitude" not in keys:
        missing.append("velocity_magnitude wake field")
    if case_id == TRANSIENT_CASE and "vorticity" not in keys:
        missing.append("vorticity")
    return not missing, missing


def positive_integer(value: Any) -> int | None:
    try:
        integer_value = int(value)
    except (TypeError, ValueError):
        return None
    return integer_value if integer_value > 0 else None


def finite_number(value: Any) -> float | None:
    try:
        numeric = float(value)
    except (TypeError, ValueError):
        return None
    return numeric if math.isfinite(numeric) else None


def nonnegative_integer(value: Any) -> int | None:
    """Return a finite non-negative integer, preserving zero as evidence."""
    try:
        integer_value = int(value)
    except (TypeError, ValueError):
        return None
    return integer_value if integer_value >= 0 else None


def unit_interval(value: Any) -> float | None:
    numeric = finite_number(value)
    return numeric if numeric is not None and 0.0 <= numeric <= 1.0 else None


def partition_load_balance(rows: Sequence[Mapping[str, Any]]) -> Mapping[str, float]:
    """Summarize owned-cell balance without silently accepting an empty rank."""
    owned = numeric_column(rows, "num_cells_owned")
    if not owned or any(value <= 0.0 for value in owned):
        return {}
    minimum = min(owned)
    maximum = max(owned)
    return {"min_owned": minimum, "max_owned": maximum, "max_min_ratio": maximum / minimum}


def residual_reduction_orders(rows: Sequence[Mapping[str, Any]]) -> float | None:
    """Return the submitted history's end-to-end L2 reduction in decades."""
    residuals = numeric_column(rows, "residual_l2")
    if not residuals:
        return None
    return math.log10(max(residuals[0], 1.0e-300) /
                      max(residuals[-1], 1.0e-300))


def partition_communication_metrics(
        rows: Sequence[Mapping[str, Any]]) -> Mapping[str, float]:
    """Summarize count-based partition exposure without calling it MPI time."""
    balance = partition_load_balance(rows)
    owned = numeric_column(rows, "num_cells_owned")
    received = numeric_column(rows, "recv_cells")
    neighbors = numeric_column(rows, "num_neighbor_ranks")
    if not balance or not owned or len(received) != len(owned):
        return balance
    total_owned = sum(owned)
    if total_owned <= 0.0:
        return balance
    return {
        **balance,
        "recv_owned_ratio": sum(received) / total_owned,
        "maximum_neighbors": max(neighbors) if neighbors else 0.0,
    }


def completed_case_count(status_by_case: Mapping[str, Mapping[str, Any]],
                         metadata_by_case: Mapping[str, Mapping[str, Any]]) -> int:
    """Count cases whose independent status/metadata completion evidence agrees."""
    completed = 0
    for case_id in REQUIRED_CASES:
        status = str(status_by_case.get(case_id, {}).get("convergence_status", ""))
        metadata = metadata_by_case.get(case_id, {})
        metadata_status = str(metadata.get("convergence_status", ""))
        expected = "statistically_periodic" if case_id == TRANSIENT_CASE else "converged"
        completed += (status == expected and metadata_status == expected and
                      metadata.get("completed") is True)
    return completed


def sanity_case_map(sanity: Any) -> dict[str, Mapping[str, Any]]:
    """Index optional sanity records for narrative-only interpretation."""
    indexed: dict[str, Mapping[str, Any]] = {}
    for entry in sequence(mapping(sanity).get("cases")):
        item = mapping(entry)
        case_id = item.get("case_id")
        if isinstance(case_id, str):
            indexed[case_id] = item
    return indexed


def output_evidence_issues(case_id: str, case_dir: Path, expected_ranks: int | None,
                           metadata: Mapping[str, Any], status: Mapping[str, Any],
                           partition_rows: Sequence[Mapping[str, Any]]) -> list[str]:
    """Return submission-grade structural checks for one completed output.

    The report intentionally repeats key output-contract checks.  This prevents
    a run-manifest row from being treated as MPI-comparison evidence when it
    only names a directory, and keeps ``--strict`` useful independently of the
    separate examiner validator.
    """
    issues: list[str] = []
    prefix = f"{case_id} ({case_dir})"
    for filename in ("metadata.json", "run_status.json", "residuals.csv", "forces.csv",
                     "surface.csv", "field_final.vtu", "restart_final.bin"):
        if not (case_dir / filename).is_file():
            issues.append(f"{prefix} lacks required output {filename}")
    metadata_case = metadata.get("case_id")
    status_case = status.get("case_id")
    if metadata_case != case_id:
        issues.append(f"{prefix} metadata case_id is {metadata_case!r}, not {case_id!r}")
    if status_case != case_id:
        issues.append(f"{prefix} run_status case_id is {status_case!r}, not {case_id!r}")
    metadata_ranks = positive_integer(metadata.get("mpi_ranks"))
    status_ranks = positive_integer(status.get("mpi_ranks"))
    if metadata_ranks is None:
        issues.append(f"{prefix} metadata has no positive mpi_ranks")
    if status_ranks is None:
        issues.append(f"{prefix} run_status has no positive mpi_ranks")
    if metadata_ranks is not None and status_ranks is not None and metadata_ranks != status_ranks:
        issues.append(f"{prefix} metadata/run_status MPI rank counts disagree")
    if expected_ranks is not None and metadata_ranks != expected_ranks:
        issues.append(f"{prefix} metadata MPI ranks {metadata_ranks!r} do not match manifest rank count {expected_ranks}")
    for document_name, document in (("metadata", metadata), ("run_status", status)):
        if document.get("convergence_status") not in {"converged", "statistically_periodic"}:
            issues.append(f"{prefix} {document_name} is not a completed final status")
    if metadata.get("completed") is not True:
        issues.append(f"{prefix} metadata completed is not true")
    final_step = positive_integer(status.get("final_step"))
    if final_step is None:
        issues.append(f"{prefix} run_status has no positive final_step")
    if finite_number(status.get("wall_time_seconds")) is None:
        issues.append(f"{prefix} run_status has no finite wall_time_seconds")
    if finite_number(status.get("residual_reduction_orders")) is None:
        issues.append(f"{prefix} run_status has no finite residual_reduction_orders")
    if metadata.get("full_mesh_replication_during_iterations") is not False:
        issues.append(f"{prefix} does not affirm no full-mesh replication during iterations")
    if metadata.get("full_state_replication_during_iterations") is not False:
        issues.append(f"{prefix} does not affirm no full-state replication during iterations")
    if not partition_rows:
        issues.append(f"{prefix} lacks readable partition diagnostics")
    elif metadata_ranks is not None:
        # Rank zero is valid, whereas positive_integer deliberately excludes it.
        try:
            actual_ranks = {int(row.get("rank")) for row in partition_rows}
        except (TypeError, ValueError):
            actual_ranks = set()
        expected = set(range(metadata_ranks))
        if actual_ranks != expected:
            issues.append(f"{prefix} partition diagnostics ranks {sorted(actual_ranks)} do not cover {sorted(expected)}")
    return issues


def rank_comparison_evidence(project_root: Path, results_root: Path,
                             manifest: Sequence[Mapping[str, str]],
                             issues: list[str]) -> dict[str, list[dict[str, Any]]]:
    """Load and validate the required NACA/cylinder rank-comparison outputs."""
    compared: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for case_id in ("naca0012_m015_inviscid", "cylinder_m010_laminar_re20"):
        rows = [row for row in manifest if row.get("case_id", "").strip() == case_id]
        seen_ranks: set[int] = set()
        for row in rows:
            ranks = positive_integer(row.get("mpi_ranks"))
            if ranks is None:
                issues.append(f"rank-comparison manifest row for {case_id} has invalid mpi_ranks")
                continue
            if ranks in seen_ranks:
                issues.append(f"rank-comparison manifest has duplicate {case_id} np={ranks} rows")
                continue
            seen_ranks.add(ranks)
            output = row.get("output_directory", "").strip()
            case_dir = Path(output) if output else results_root / case_id
            if not case_dir.is_absolute():
                case_dir = project_root / case_dir
            comparison_issues: list[str] = []
            metadata = mapping(load_json(case_dir / "metadata.json", comparison_issues,
                                         f"comparison metadata for {case_id} np={ranks}"))
            status = mapping(load_json(case_dir / "run_status.json", comparison_issues,
                                       f"comparison run status for {case_id} np={ranks}"))
            partitions = read_partition_rows(case_dir, comparison_issues)
            comparison_issues.extend(output_evidence_issues(case_id, case_dir, ranks,
                                                            metadata, status, partitions))
            # A comparison must contain the raw signals used to discuss rank
            # consistency, not merely a completed-status JSON.
            force = force_summary(case_dir, case_id, comparison_issues)
            residuals = load_csv(case_dir / "residuals.csv", comparison_issues,
                                 f"comparison residual history for {case_id} np={ranks}")
            if not numeric_column(residuals, "residual_l2"):
                comparison_issues.append(f"{case_id} np={ranks} has no finite residual_l2 comparison evidence")
            if not force:
                comparison_issues.append(f"{case_id} np={ranks} has no finite force comparison evidence")
            issues.extend(comparison_issues)
            compared[case_id].append({
                "ranks": ranks,
                "path": case_dir,
                "metadata": metadata,
                "status": status,
                "force": force,
                "residuals": residuals,
                "partitions": partitions,
                "manifest": row,
            })
        available = {int(item["ranks"]) for item in compared[case_id]}
        if 8 not in available:
            issues.append(f"{case_id} lacks required np=8 rank-comparison evidence")
        if len(available) < 2:
            issues.append(f"{case_id} needs at least two distinct MPI rank counts for parallel validation")
    return compared


def render_figure(figure: Figure, report_root: Path, issues: list[str]) -> list[str]:
    source = report_root / "figures" / figure.file
    anchor = label(figure.case_id, figure_variable_key(figure), figure.file)
    if not source.exists():
        issues.append(f"missing figure named by manifest: {source}")
        return [
            r"\noindent\Cref{" + anchor + r"} is the declared " + code(figure.variable) + r" visualization for this case.",
            r"\begin{figure}[htbp]",
            r"\centering",
            r"\fbox{\parbox{0.88\linewidth}{\centering Missing submitted figure: " + code(figure.file) + r"}}",
            r"\caption{" + caption_tex(figure.caption) + r" Source declared as " + code(figure.source_file) + r".}",
            r"\label{" + anchor + r"}",
            r"\end{figure}",
        ]
    relative = relative_to(source, report_root)
    return [
        r"\noindent\Cref{" + anchor + r"} is the declared " + code(figure.variable) + r" visualization for this case.",
        r"\begin{figure}[htbp]",
        r"\centering",
        r"\includegraphics[width=0.92\linewidth]{" + latex_path(relative) + r"}",
        r"\caption{" + caption_tex(figure.caption) + r" Source: " + code(figure.source_file) + r"; variable: " + code(figure.variable) + r".}",
        r"\label{" + anchor + r"}",
        r"\end{figure}",
    ]


def report_text(project_root: Path, report_root: Path, results_root: Path,
                configs: Mapping[str, Mapping[str, Any]],
                statistics: Mapping[str, Mapping[str, Any]],
                manifest: Sequence[Mapping[str, str]], figures: Sequence[Figure],
                metadata_by_case: Mapping[str, Mapping[str, Any]],
                status_by_case: Mapping[str, Mapping[str, Any]],
                partitions: Mapping[str, Sequence[Mapping[str, Any]]],
                force_summaries: Mapping[str, Mapping[str, Any]],
                sanity: Any, rank_comparisons: Mapping[str, Sequence[Mapping[str, Any]]],
                issues: list[str]) -> str:
    del project_root, results_root
    completed_count = completed_case_count(status_by_case, metadata_by_case)
    sanity_passed = mapping(sanity).get("all_passed") is True
    if completed_count == len(REQUIRED_CASES) and sanity_passed:
        completion_statement = (
            "All eight required primary cases completed successfully according to their "
            "agreeing submitted status/metadata and sanity artifacts: the seven steady "
            "cases are converged, the Re200 case is statistically periodic, every metadata "
            "completion flag is true, and the aggregate sanity gate passes."
        )
    elif completed_count == len(REQUIRED_CASES):
        completion_statement = (
            "All eight primary status/metadata pairs agree on the required completion state, "
            "but the aggregate sanity gate does not pass; therefore successful completion "
            "of every required case is not affirmed."
        )
    else:
        completion_statement = (
            f"Only {completed_count} of {len(REQUIRED_CASES)} required primary cases have "
            "the required submitted completion status; therefore not every required case "
            "completed successfully."
        )
    naca_drag = [value for case_id in REQUIRED_CASES[:6]
                 for value in [finite_number(statistics.get(case_id, {}).get("final_cd"))]
                 if value is not None]
    naca_lift = [abs(value) for case_id in REQUIRED_CASES[:6]
                 for value in [finite_number(statistics.get(case_id, {}).get("final_cl"))]
                 if value is not None]
    if len(naca_drag) == 6 and len(naca_lift) == 6:
        naca_abstract = (
            "Across the submitted NACA cases, final $C_D$ spans " +
            number(min(naca_drag)) + "--" + number(max(naca_drag)) +
            " and the largest $|C_L|$ is " + number(max(naca_lift)) + ". "
        )
    else:
        naca_abstract = (
            "A complete submitted NACA force-coefficient range is unavailable; the "
            "evidence audit identifies missing numerical inputs. "
        )
    re20 = result_record(statistics, "cylinder_m010_laminar_re20")
    re200 = result_record(statistics, TRANSIENT_CASE)
    lines: list[str] = [
        r"\documentclass[11pt]{article}",
        r"\usepackage[margin=1in]{geometry}",
        r"\usepackage[T1]{fontenc}",
        r"\usepackage{lmodern}",
        r"\usepackage{amsmath,amssymb,graphicx,booktabs,siunitx,xurl}",
        r"\usepackage[hidelinks]{hyperref}",
        r"\usepackage[nameinlink,capitalise]{cleveref}",
        r"\graphicspath{{figures/}}",
        r"\sisetup{detect-all}",
        r"\title{2-D Unstructured Compressible Navier--Stokes Solver Benchmark Report}",
        r"\author{Generated from submitted solver artifacts}",
        r"\date{\today}",
        r"\begin{document}",
        r"\maketitle",
        r"\begin{abstract}",
        "AeroFV is a C++20/MPI cell-centred finite-volume solver for the two-dimensional "
        "compressible Navier--Stokes equations. Rank zero reads the supplied unstructured "
        "CGNS mesh, a METIS cell graph is distributed as owned cells plus one-ring ghosts, "
        "and iterations use neighbour-scoped halos. Production spatial discretization uses "
        "weighted least-squares reconstruction, Barth--Jespersen limiting, positivity "
        "protection, Rusanov inviscid fluxes, and corrected primitive-gradient viscous "
        "fluxes; the Re200 case uses a frozen-history dual-time BDF2 loop. " +
        completion_statement + " " + naca_abstract +
        "The Re20 tail mean is $\\overline C_D=" + number(re20.get("mean_cd_tail")) +
        "$; for Re200 the submitted tail mean is $\\overline C_D=" +
        number(re200.get("mean_cd_tail")) + "$ with lift amplitude " +
        number(re200.get("lift_amplitude_tail")) + ", dominant frequency " +
        number(re200.get("dominant_frequency")) + ", and $St=" +
        number(re200.get("strouhal_number")) + "$. These coefficients are artifact values, "
        "not external reference validation. Principal limitations are Rusanov diffusion, "
        "one supplied grid per geometry, no independent grid/reference study, clipped "
        "Re200 vorticity visualization, and MPI timings without a compute/communication split.",
        r"\end{abstract}",
        r"\section{Introduction}",
        "The benchmark exercises a cell-centred, distributed, two-dimensional compressible Navier--Stokes workflow on six NACA0012 and two circular-cylinder cases. "
        "The required cases are " + ", ".join(breakable_code(case_id) for case_id in REQUIRED_CASES) + ". "
        "Status words in this report are copied from submitted status artifacts; a missing or failed result is not called converged.",
        r"\section{Software Architecture and Extension Seams}",
        r"The implementation separates validated JSON case parsing, CGNS geometry, METIS partitioning, halo exchange, gas/flux physics, reconstruction/limiting, nonlinear integration, and output into distinct modules. Solver behavior is selected from physics, boundary, numerical, and run-control fields rather than from a branch on a supplied case identifier. This leaves explicit replacement seams: three-dimensional work would generalize \texttt{Vec2}, face geometry, and tensor operations; a general equation of state would replace the thermodynamic conversion/sound-speed service used by fluxes; and RANS or multispecies extensions would enlarge the state and add closure, diffusion, and source-term modules while reusing partition/halo/output infrastructure. The present implementation is nevertheless fixed to two dimensions, four conservative components, a calorically perfect gas, and inviscid/laminar modes; these are documented current limits, not claims that the future models already exist.",
        r"\section{Governing Equations and Nondimensionalization}",
        r"The conservative state is $\mathbf{U}=[\rho,\rho u,\rho v,\rho E]^T$. The solved equation set declared by the case files is",
        r"\begin{equation}\frac{\partial\mathbf U}{\partial t}+\frac{\partial\mathbf F^i}{\partial x}+\frac{\partial\mathbf G^i}{\partial y}=\frac{\partial\mathbf F^v}{\partial x}+\frac{\partial\mathbf G^v}{\partial y}.\label{eq:ns}\end{equation}",
        r"The inviscid fluxes are $\mathbf F^i=[\rho u,\rho u^2+p,\rho uv,u(\rho E+p)]^T$ and $\mathbf G^i=[\rho v,\rho uv,\rho v^2+p,v(\rho E+p)]^T$. "
        r"For the calorically perfect gas specified in every case input, $p=(\gamma-1)\rho e$, $E=e+\tfrac12(u^2+v^2)$, and $a=\sqrt{\gamma p/\rho}$. "
        "The input files set $\\gamma=1.4$, $R=1$, and $Pr=0.72$. They use $\\rho_\\infty=1$, $U_\\infty=1$, and reference length and area equal to one. The nondimensional pressure reference is $p_{ref}=\\rho_\\infty U_\\infty^2$ and the dynamic pressure is $q_\\infty=p_{ref}/2=\\tfrac12\\rho_\\infty U_\\infty^2$; hence $C_D=F_D/(q_\\infty A_{ref})$, $C_L=F_L/(q_\\infty A_{ref})$, and $C_m=M_z/(q_\\infty A_{ref}L_{ref})$.",
        r"For laminar cases the specified constant-viscosity relation is $\mu=\rho_\infty U_\infty L_{ref}/Re$. The Newtonian/Fourier model is $\tau_{xx}=2\mu u_x-\frac23\mu(u_x+v_y)$, $\tau_{yy}=2\mu v_y-\frac23\mu(u_x+v_y)$, $\tau_{xy}=\mu(u_y+v_x)$, $\mathbf q=-k\nabla T$, and $k=\mu c_p/Pr$. Inviscid case inputs explicitly disable viscous fluxes.",
        r"\section{Meshes, Boundary Tags, and Case Inputs}",
        r"Rank zero opens the CGNS file, requires a two-dimensional unstructured base, reads every zone's coordinates, and consumes TRI\_3/QUAD\_4 volume sections plus BAR\_2 boundary sections. Vertices coincident across zones are welded within the reader tolerance. BAR\_2 section names become mesh boundary-family tags; the case JSON maps those names to farfield, slip-wall, or no-slip adiabatic conditions, and solver construction rejects any physical face whose family is absent from that mapping.",
        r"Cell vertices are reversed when necessary to obtain counter-clockwise polygons. The shoelace formula supplies positive area $|\Omega_i|$ and the area-weighted polygon formula supplies the centroid (including quadrilaterals). Each unique cell edge becomes a face with midpoint, Euclidean length $|S_f|$, and unit normal $(\Delta y,-\Delta x)/|S_f|$ directed out of its stored left cell toward the stored right cell or exterior; a boundary face's edge length is also its wall-integration length. Topology validation rejects nonpositive areas or lengths, non-unit normals, nonmanifold edges, and untagged physical boundaries. The report generator reads case paths/mappings directly, while mesh counts and actual partition data come only from submitted metadata and diagnostics.",
    ]
    lines += make_tabular(
        ["case", "flow/model", "supplied control", "boundary mapping"],
        case_control_rows(configs), "p{0.18\\linewidth}p{0.17\\linewidth}p{0.31\\linewidth}p{0.25\\linewidth}")
    mesh_rows = []
    for case_id in REQUIRED_CASES:
        metadata = metadata_by_case.get(case_id, {})
        mesh_name = Path(active_method(metadata, "mesh_file")).name
        mesh_rows.append([
            table_case(case_id), code(mesh_name),
            integer(metadata.get("num_cells_global")), integer(metadata.get("num_faces_global")),
            integer(metadata.get("mpi_ranks")),
        ])
    lines += make_tabular(["case", "mesh", "global cells", "global faces", "primary np"], mesh_rows,
                          "p{0.23\\linewidth}p{0.28\\linewidth}r r r")
    lines += [
        r"\section{Spatial Discretization}",
        r"For each owned control volume $\Omega_i$, the semi-discrete finite-volume balance is $|\Omega_i|\,d\mathbf U_i/dt+\mathbf R_i=0$, where $\mathbf R_i=\sum_{f\subset\partial\Omega_i}(\widehat{\mathbf F}^{i}_f-\widehat{\mathbf F}^{v}_f)|S_f|$. A stored face normal points outward from its stored left cell; the stored right cell receives the equal and opposite contribution, so either side can be the rank-owned cell after partitioning. Boundary faces use the condition mapped from the mesh family in the case input.",
        r"\subsection{Second-Order Reconstruction, Limiter, and Positivity}",
        r"For each primitive component, the cell gradient minimizes $\sum_j |\mathbf d_{ij}|^{-2}[q_j-q_i-\nabla q_i\cdot\mathbf d_{ij}]^2$ over adjacent cells or boundary virtual neighbours. The reconstructed face value is $q_f=q_i+\phi_i\nabla q_i\cdot(\mathbf x_f-\mathbf x_i)$. The componentwise Barth--Jespersen factor starts at one and, over all incident faces, is reduced to keep every increment within the local neighbour extrema: for a positive increment the candidate ratio is $(q_{max}-q_i)/\Delta q_f$, with the analogous $(q_{min}-q_i)/\Delta q_f$ for a negative increment, clipped to $[0,1]$.",
        r"Candidate reconstructed states are then scaled toward the valid cell-centre state until density and pressure satisfy their floors; cell updates use a local fraction-to-boundary safeguard. For steady runs, the actual production sequence is first-order during the supplied CFL ramp, then a 500-pseudo-step blend to the full weighted-least-squares piecewise-linear reconstruction; acceptance requires at least 200 subsequent full-second-order iterations. The per-run metadata table is retained as the primary artifact evidence, rather than treating a nominal spatial-order input as proof of activation.",
    ]
    method_rows = []
    for case_id in REQUIRED_CASES:
        metadata = metadata_by_case.get(case_id, {})
        method_rows.append([
            table_case(case_id), compact_method(active_method(metadata, "reconstruction")),
            compact_method(active_method(metadata, "limiter")),
            compact_method(active_method(metadata, "positivity_preservation")),
            integer(metadata.get("spatial_order_claimed")),
        ])
    lines += make_tabular(["case", "reconstruction", "limiter", "positivity control", "order"], method_rows,
                          "p{0.20\\linewidth}p{0.23\\linewidth}p{0.18\\linewidth}p{0.23\\linewidth}r")
    lines += [
        r"\subsection{Inviscid and Viscous Fluxes}",
        r"Production inviscid interior and farfield faces use local Lax--Friedrichs/Rusanov: $\widehat{\mathbf F}=\tfrac12(\mathbf F_L+\mathbf F_R)-\tfrac12\lambda_{max}(\mathbf U_R-\mathbf U_L)$, where $\lambda_{max}=\max(|u_{n,L}|+a_L,|u_{n,R}|+a_R)$. No HLLC production-flux or entropy-fix claim is made. The recorded per-run selection is shown in the next table.",
        r"For a viscous face, let $\overline{\nabla q}=\tfrac12(\nabla q_L+\nabla q_R)$, $\mathbf d=\mathbf x_R-\mathbf x_L$, and let $\mathbf n$ be the unit face normal. The implemented non-orthogonal correction preserves the averaged tangential gradient and replaces its normal component by the endpoint difference: $\nabla q_f=\overline{\nabla q}+\mathbf n[(q_R-q_L)/(\mathbf d\!\cdot\!\mathbf n)-\overline{\nabla q}\!\cdot\!\mathbf n]$. This is applied to primitive density, velocity, and pressure; the temperature gradient follows from $T=p/(\rho R)$ before Newtonian stress and Fourier heat flux are evaluated.",
        r"At a boundary, the right endpoint is the mirrored virtual-cell centre $\mathbf x_R=2\mathbf x_f-\mathbf x_L$. A no-slip adiabatic ghost reverses both velocity components while retaining density and pressure: the midpoint wall velocity is zero, the temperature endpoint difference is zero, and the corrected wall-normal velocity gradient retains finite shear. Pressure traction and the tangent-projected body viscous traction are integrated separately, so the reported pressure and viscous drag/lift columns preserve their physical split and $C_f$ is tangential shear rather than full normal traction.",
    ]
    flux_rows = []
    for case_id in REQUIRED_CASES:
        metadata = metadata_by_case.get(case_id, {})
        flux_rows.append([
            table_case(case_id), compact_method(active_method(metadata, "inviscid_flux")),
            compact_method(active_method(metadata, "entropy_fix")),
            compact_method(active_method(metadata, "viscous_flux")),
        ])
    lines += make_tabular(["case", "inviscid flux", "entropy fix", "viscous flux"], flux_rows,
                          "p{0.21\\linewidth}p{0.20\\linewidth}p{0.18\\linewidth}p{0.28\\linewidth}")
    lines += [
        r"\section{Boundary Conditions}",
        r"The farfield ghost uses characteristic subsonic inflow/outflow invariants, with freestream data for supersonic inflow and interior data for supersonic outflow. At an inviscid slip or viscous no-slip impermeable wall, the convective residual uses the exact stationary-wall flux $[0,pn_x,pn_y,0]^T$, rather than a Rusanov penalty on iteration-level normal-velocity error. Slip-wall ghosts reflect normal velocity and retain tangential motion; no-slip adiabatic ghosts mirror velocity and copy density/pressure, while the reported boundary state has $u=v=0$. The zero-normal-temperature-gradient treatment is applied at this gradient level. Surface rows are boundary values, as recorded by \texttt{wall\_boundary\_output\_semantics}; the sanity record checks normal velocity for slip walls and speed for no-slip walls.",
        r"\section{Implicit and Transient Time Integration}",
        r"Steady cases use a CFL-controlled local pseudo-time march. For face $f$, the actual stability estimates are $\lambda_{c,f}=|S_f|\max(|u_{n,L}|+a_L,|u_{n,R}|+a_R)$ and, in laminar mode, $\lambda_{v,f}=(4/3+\gamma/Pr)(\mu/\rho_L)|S_f|/d_{n,f}$ with $d_{n,f}=|(\mathbf x_R-\mathbf x_L)\cdot\mathbf n_f|$. Thus $\Delta\tau_i=\mathrm{CFL}\,|\Omega_i|/\sum_f(\lambda_{c,f}+\lambda_{v,f})$ (equivalently the Newton pseudo-mass diagonal is the spectral sum divided by CFL). CFL grows geometrically from the case-file initial value to its cap over the supplied ramp length.",
        r"At each outer pseudo-time step, a matrix-free, diagonally left-preconditioned GMRES solve approximates $(J+M/\Delta\tau)\delta U=-R$; Jacobian-vector products are finite differences of the assembled residual and the limiter switches are frozen within each linear solve. The case-control table records the exact supplied maximum steps, CFL initial/cap/ramp, inner-iteration bounds, and inner target, while the actual inner statistics and solver name are copied from each output metadata file below.",
        r"A steady result reaches the normal acceptance path only after reconstruction is fully second order for at least 200 further iterations, the global residual has fallen by the case-file target number of decades, and $(C_{max}-C_{min})/\max(1,|\overline C|)<2\times10^{-4}$ for both $C_D$ and $C_L$ over a 200-sample window. At \texttt{max\_steps}, a separately documented plateau is accepted only if the reduction is at least $\max(2,\mathrm{target}-0.5)$ decades, the same force test passes, and the 200-sample residual envelope has $R_{max}/R_{min}<1.08$; otherwise the solver fails instead of writing a completed result. The status notes distinguish target convergence from this plateau path.",
    ]
    implicit_rows = []
    for case_id in REQUIRED_CASES:
        metadata = metadata_by_case.get(case_id, {})
        implicit_rows.append([
            table_case(case_id), compact_method(active_method(metadata, "time_integrator")),
            compact_method(active_method(metadata, "implicit_solver")),
            integer(metadata.get("typical_inner_iterations")),
            f"{integer(metadata.get('observed_min_inner_iterations'))}--{integer(metadata.get('observed_max_inner_iterations'))}",
            number(metadata.get("inner_residual_reduction_target")),
        ])
    lines += make_tabular(["case", "integrator", "inner method", "typical", "observed range", "inner target"], implicit_rows,
                          "p{0.20\\linewidth}p{0.18\\linewidth}p{0.23\\linewidth}r r r")
    recovery_rows: list[list[str]] = []
    for case_id in REQUIRED_CASES:
        metadata = metadata_by_case.get(case_id, {})
        if metadata.get("steady_recovery_activated") is True:
            recovery_rows.append([
                code(case_id), integer(metadata.get("steady_recovery_activation_step")),
                number(metadata.get("steady_recovery_cfl_cap")),
                number(metadata.get("steady_recovery_relaxation_cap")),
            ])
    lines += [
        r"\paragraph{Steady nonlinear recovery evidence.} This conservative, latched full-order safeguard is dormant unless a fully second-order solve has sustained max-inner-iteration target misses, a residual rebound greater than three times the best full-order value, and an unavailable or unstable force-history window. Once activated, it leaves the requested CFL schedule unchanged but caps the effective pseudo-CFL and nonlinear relaxation for the remaining steady iterations. It is therefore a conservative equivalent-CFL continuation measure, not a change to the requested case control or a return to first order.",
    ]
    if recovery_rows:
        lines += make_tabular(["activated case", "activation step", "effective CFL cap", "relaxation cap"], recovery_rows,
                              "p{0.40\\linewidth}r r r")
    else:
        lines.append("No submitted primary metadata marks the steady nonlinear-recovery safeguard as activated.")
    re200_meta = metadata_by_case.get(TRANSIENT_CASE, {})
    frozen = re200_meta.get("true_bdf2_inner_loop")
    if frozen is True:
        bdf_statement = "The Re200 metadata affirms a true BDF2 inner loop. During an attempted $U^{n+1}$ solve, $U^n$ and $U^{n-1}$ remain frozen throughout the inner iterations; history is updated only after acceptance of that physical step."
    elif frozen is False:
        bdf_statement = "The Re200 metadata explicitly does not affirm a true frozen-history BDF2 inner loop; this is a transient-method nonconformance, not a completed BDF2 claim."
    else:
        bdf_statement = "The Re200 metadata does not report whether a true frozen-history BDF2 inner loop was used; therefore this report cannot verify that requirement."
    lines += [bdf_statement,
        r"For Re200 without a restart, the solver first obtains a steady base flow (1,000 first-order pseudo steps followed by a 500-step reconstruction blend) before applying its documented generic transverse perturbation. The first physical step uses backward Euler. Later physical steps use the second-order predictor $2U^n-U^{n-1}$ and solve BDF2 with $U^n$ and $U^{n-1}$ frozen for every nonlinear/linear inner iteration; only accepted physical steps update the history.",
        r"\paragraph{Re200 observed inner-solve evidence.} The following values are copied from the submitted Re200 metadata, not inferred from the requested controls. A target miss means an accepted physical-step inner solve whose reported residual ratio did not reach the configured reduction target.",
    ]
    re200_inner_rows = [[
        f"{integer(re200_meta.get('observed_min_inner_iterations'))}--{integer(re200_meta.get('observed_max_inner_iterations'))}",
        number(re200_meta.get("observed_mean_inner_iterations")),
        number(re200_meta.get("inner_residual_reduction_target")),
        integer(re200_meta.get("inner_target_misses")),
        number(re200_meta.get("inner_target_converged_fraction")),
        number(re200_meta.get("last_inner_residual_ratio")),
    ]]
    lines += make_tabular(
        ["observed inner range", "observed mean", "target ratio", "target misses", "converged fraction", "last ratio"],
        re200_inner_rows, "r r r r r r")
    lines += [
        r"\section{MPI Parallelization and METIS Partitioning}",
        "The cell graph places one undirected adjacency edge across every interior mesh face and METIS K-way assigns cell owners. Rank zero alone reads the complete CGNS mesh, constructs and partitions this graph, serializes one compact LocalMesh payload per destination, and releases the global mesh immediately after distribution. Each local mesh retains owned cells, the one-ring off-rank ghosts required by face/reconstruction stencils, incident faces/vertices, global identifiers, and per-neighbour send-owned/receive-ghost lists.",
        r"Conservative states, primitive gradients, limiter factors, and Krylov increments are synchronized with nonblocking \texttt{MPI\_Isend/Irecv} calls only to listed neighbour ranks; global residual, force, and Krylov scalar products use reductions. Solver iterations replicate neither the full mesh nor the full conservative state. The following submitted metadata and diagnostics expose the partitioner, exchange method, edge cut, rank-local counts, and load balance for every primary run.",
    ]
    mpi_rows = []
    for case_id in REQUIRED_CASES:
        metadata = metadata_by_case.get(case_id, {})
        ranks = integer(metadata.get("mpi_ranks"))
        full_mesh = metadata.get("full_mesh_replication_during_iterations", "not reported")
        full_state = metadata.get("full_state_replication_during_iterations", "not reported")
        mpi_rows.append([
            table_case(case_id), ranks,
            compact_method(active_method(metadata, "partitioner")),
            compact_method(active_method(metadata, "halo_exchange")),
            "yes" if full_mesh is True else ("no" if full_mesh is False else "not reported"),
            "yes" if full_state is True else ("no" if full_state is False else "not reported"),
            integer(metadata.get("partition_edge_cut")),
        ])
    lines += make_tabular(["case", "np", "partitioner", "halo exchange", "full mesh?", "full state?", "edge cut"], mpi_rows,
                          "p{0.17\\linewidth}r p{0.13\\linewidth}p{0.20\\linewidth}p{0.09\\linewidth}p{0.09\\linewidth}r")
    balance_rows: list[list[str]] = []
    for case_id in REQUIRED_CASES:
        balance = partition_load_balance(partitions.get(case_id, []))
        balance_rows.append([
            table_case(case_id), number(balance.get("min_owned"), 7),
            number(balance.get("max_owned"), 7), number(balance.get("max_min_ratio"), 7),
        ])
    lines += [r"\paragraph{Owned-cell load balance.} The ratio is the submitted primary-run maximum owned-cell count divided by the minimum owned-cell count across ranks; it is an explicit load-balance measure, not a proxy inferred from wall time."]
    lines += make_tabular(["case", "min owned", "max owned", "max/min owned"], balance_rows,
                          "p{0.38\\linewidth}r r r")
    partition_rows: list[list[str]] = []
    for case_id in REQUIRED_CASES:
        case_partitions = partitions.get(case_id, [])
        owned = [int(row.get("num_cells_owned", 0)) for row in case_partitions]
        ghosts = [int(row.get("num_cells_ghost", 0)) for row in case_partitions]
        neighbours = [int(row.get("num_neighbor_ranks", 0)) for row in case_partitions]
        sends = [int(row.get("send_cells", 0)) for row in case_partitions]
        receives = [int(row.get("recv_cells", 0)) for row in case_partitions]
        partition_rows.append([
            table_case(case_id), integer(len(case_partitions)),
            (f"{min(owned)}--{max(owned)}" if owned else "not reported"),
            integer(max(ghosts)) if ghosts else "not reported",
            integer(max(neighbours)) if neighbours else "not reported",
            integer(max(sends)) if sends else "not reported",
            integer(max(receives)) if receives else "not reported",
        ])
    lines += [
        r"\paragraph{Partition diagnostics.} Compact extrema are shown here so every primary case remains legible; the complete per-rank rows remain in each submitted \texttt{partition\_diagnostics.csv}.",
    ]
    lines += make_tabular(["case", "ranks", "owned min--max", "max ghost", "max neighbours", "max send", "max receive"], partition_rows,
                          "p{0.18\\linewidth}r r r r r r")
    lines += [
        r"\section{Output, Reproducibility, and Sanity Checks}",
        r"The build requires CMake 3.20+, a C++20 compiler, MPI, CGNS, METIS, and Boost PropertyTree headers. System dependencies may be found through \texttt{CMAKE\_PREFIX\_PATH}; the benchmark dependency prefix is configurable as \nolinkurl{-DCFD_EXTERNALS_ROOT=/path/to/external/cfd_externals/install}, whose \texttt{include/}, \texttt{lib/}, and CMake package directories are searched. Python 3.10+, NumPy, and Matplotlib are installed from \texttt{requirements.txt}; report compilation additionally needs \texttt{latexmk}.",
        r"\paragraph{Exact campaign commands.} From the repository root, the documented production sequence is:",
        "\n".join((
            r"\begin{verbatim}",
            "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \\",
            "  -DCFD_EXTERNALS_ROOT=/path/to/external/cfd_externals/install",
            "cmake --build build --parallel",
            ".venv/bin/python tools/run_cases.py --executable build/aerofv \\",
            "  --results results --ranks 8 --concurrent 1 --rank-comparisons \\",
            "  --comparison-ranks 1 2 4",
            ".venv/bin/python tools/postprocess.py --results results --report report",
            ".venv/bin/python tools/generate_report.py --strict",
            "./tools/build_report.sh",
            r"\end{verbatim}",
        )),
        r"The runner's \texttt{report/run\_manifest.csv} is authoritative for every exact expanded \texttt{mpirun} command, MPI rank count, wall-clock time, status, and output directory; the results summary below lists rank count and wall time per required primary case, while the parallel section lists the comparison runs. No command is reconstructed from a figure filename.",
        r"Each completed case directory contains \nolinkurl{metadata.json}, \nolinkurl{run_status.json}, \nolinkurl{partition_diagnostics.csv}, \nolinkurl{residuals.csv}, \nolinkurl{forces.csv}, \nolinkurl{surface.csv}, \nolinkurl{field_final.vtu}, \nolinkurl{restart_final.bin}, and \nolinkurl{stdout.log}, plus requested transient \nolinkurl{field_t*.vtu} snapshots. The postprocessor regenerates \nolinkurl{report/figures/}, \nolinkurl{figure_manifest.csv}, \nolinkurl{result_statistics.json}, and \nolinkurl{sanity_checks.json} directly from those CSV/VTU files; this generator writes \nolinkurl{report.tex}, and \nolinkurl{tools/build_report.sh} creates the PDF when LaTeX is available.",
    ]
    sanity_map = mapping(sanity)
    all_passed = sanity_map.get("all_passed", "not reported")
    lines.append("The submitted aggregate sanity result is " + code(all_passed) + ". Per-case checks require finite history, field, and surface rows, positive density and pressure in field and surface data, nontrivial pressure/Mach ranges, final force/status/field alignment, positive cylinder drag, near-zero slip-wall normal speed or no-slip wall speed, and finite/nonzero skin friction where applicable. The figure manifest separately names every plotted variable and source file, preventing Mach/pressure filename substitution; individual failures remain visible in the audit below.")
    sanity_rows: list[list[str]] = []
    for entry in sequence(sanity_map.get("cases")):
        item = mapping(entry)
        checks = mapping(item.get("checks"))
        failed = [name for name, passed in checks.items() if passed is not True]
        sanity_rows.append([table_case(item.get("case_id", "unknown")),
                            tex(item.get("passed", "not reported")),
                            tex(", ".join(failed) if failed else "none reported")])
    lines += make_tabular(["case", "passed", "failed or unreported checks"], sanity_rows,
                          "p{0.27\\linewidth}p{0.12\\linewidth}p{0.52\\linewidth}")
    lines += [r"\section{Results}", r"\subsection{Summary Tables}"]
    status_rows = []
    force_rows = []
    for case_id in REQUIRED_CASES:
        stat = result_record(statistics, case_id)
        status = status_by_case.get(case_id, {})
        metadata = metadata_by_case.get(case_id, {})
        status_rows.append([table_case(case_id), integer(stat.get("mpi_ranks", status.get("mpi_ranks", metadata.get("mpi_ranks")))), integer(stat.get("final_step", status.get("final_step"))), number(stat.get("final_physical_time", status.get("final_physical_time"))), number(stat.get("residual_reduction_orders_measured", status.get("residual_reduction_orders"))), number(stat.get("wall_time_seconds", status.get("wall_time_seconds"))), compact_method(status_value(status, metadata))])
        force = force_summaries.get(case_id, {})
        force_rows.append([table_case(case_id), compact_method(force.get("basis", "not reported")), number(force.get("cd")), number(force.get("cl")), number(force.get("cmz")), number(force.get("pressure_drag")), number(force.get("viscous_drag")), compact_method(status_value(status, metadata))])
    lines += make_tabular(["case", "np", "steps", "$t$", "res. orders", "wall s", "status"], status_rows,
                          "p{0.20\\linewidth}r r r r r p{0.13\\linewidth}")
    lines += make_tabular(["case", "basis", "$C_D$", "$C_L$", "$C_m$", "$C_{D,p}$", "$C_{D,v}$", "status"], force_rows,
                          "p{0.17\\linewidth}p{0.08\\linewidth}r r r r r p{0.11\\linewidth}")
    lines += [
        r"The force table reads the final force row for steady cases and the post-60\% mean for Re200 directly from each submitted \texttt{forces.csv}; it exposes the pressure/viscous drag split. Values marked ``not reported'' were not fabricated. The raw history also retains pressure/viscous lift columns for traceability.",
        r"\subsection{NACA0012 Cases}",
        "Each NACA case below introduces all figure-manifest entries for its case. At zero angle of attack, symmetry is assessed only through the submitted force/sanity artifacts; no symmetry or shock conclusion is asserted where those artifacts are missing.",
    ]
    figures_by_case: dict[str, list[Figure]] = defaultdict(list)
    for figure in figures:
        figures_by_case[figure.case_id].append(figure)
    sanity_by_case = sanity_case_map(sanity)
    for case_id in REQUIRED_CASES[:6]:
        stat = result_record(statistics, case_id)
        config = configs.get(case_id, {})
        physics_mode = mapping(config.get("physics")).get("mode")
        mach = finite_number(mapping(config.get("freestream")).get("mach"))
        force = force_summaries.get(case_id, {})
        metadata = metadata_by_case.get(case_id, {})
        status = status_value(status_by_case.get(case_id, {}), metadata)
        sanity_entry = sanity_by_case.get(case_id, {})
        sanity_checks = mapping(sanity_entry.get("checks"))
        sanity_metrics = mapping(sanity_entry.get("metrics"))
        refs = [r"\cref{" + label(item.case_id, figure_variable_key(item), item.file) + r"}" for item in figures_by_case.get(case_id, [])]
        lines += [r"\subsubsection{" + code(case_id) + r"}",
                  "Submitted status: " + code(status) + "; final $C_D=" + number(stat.get("final_cd")) + "$ and $C_L=" + number(stat.get("final_cl")) + "$. " +
                  ("The submitted figure set is introduced in " + ", ".join(refs) + "." if refs else "No figure-manifest entries were supplied for this case.")]
        final_cl = finite_number(stat.get("final_cl"))
        if final_cl is None:
            lines.append("No finite final $C_L$ is present in the submitted statistics, so a force-based symmetry interpretation is not available for this case.")
        else:
            lines.append("Symmetry evidence at the supplied zero incidence is the final force coefficient $C_L=" +
                         number(final_cl) + "$; the submitted force-based near-symmetric-lift check is " +
                         code(sanity_checks.get("near_symmetric_lift", "not reported")) +
                         ". This is a numerical symmetry indicator, not a pointwise proof of upper/lower surface equality.")
        mach_span = finite_number(stat.get("field_mach_relative_span"))
        pressure_span = finite_number(stat.get("field_pressure_relative_span"))
        cp_range = finite_number(stat.get("surface_cp_range"))
        if mach_span is None or pressure_span is None or cp_range is None:
            lines.append("Pressure-distribution/compressibility interpretation is unavailable because the submitted statistics lack one or more surface-$C_p$, Mach-span, or pressure-span values. The figures remain named evidence, but no missing range is inferred from image appearance.")
        else:
            lines.append("The submitted chordwise surface-$C_p$ distribution has range " +
                         number(cp_range) + "; the final field has relative Mach span " +
                         number(mach_span) + " and relative pressure span " +
                         number(pressure_span) + ". The $C_p$ figure therefore documents the pressure-loading distribution and the contours document compressibility variation. The postprocessor does not extract upper/lower branch mismatch, extrema location, shock position, or pressure jump, so none of those quantities is asserted.")
        if mach is not None and mach <= 0.2:
            lines.append("At the supplied low-subsonic Mach number $M=" + number(mach) +
                         "$, the pressure and Mach plots are assessed for smooth, symmetric loading; the reported spans and $C_p$ range are numerical evidence, not an incompressible-reference comparison.")
        elif mach is not None and mach < 1.0:
            lines.append("At the supplied transonic Mach number $M=" + number(mach) +
                         "$ the near-body Mach, pressure, and $C_p$ plots are inspected for localized compression or shock-like gradients. Because no shock detector or jump metric is submitted, the report makes only this qualitative contour-based assessment and does not claim a shock location or strength.")
        elif mach is not None:
            lines.append("At the supplied supersonic Mach number $M=" + number(mach) +
                         "$ the near-body Mach, pressure, and $C_p$ plots provide qualitative compression/shock evidence. The absence of a fitted discontinuity or jump metric prevents a quantitative shock-location or robustness claim.")
        if physics_mode == "laminar":
            lines.append("The final-row drag level is $C_D=" + number(force.get("cd")) +
                         "$, split into pressure $C_{D,p}=" +
                         number(force.get("pressure_drag")) + "$ and viscous $C_{D,v}=" +
                         number(force.get("viscous_drag")) + "$. This exposes the wall-friction contribution but, without a grid/reference study, does not establish drag accuracy.")
            lines.append("Viscous-wall evidence from the submitted sanity metrics is maximum wall speed " +
                         number(sanity_metrics.get("max_wall_speed")) + " and maximum absolute skin-friction coefficient " +
                         number(sanity_metrics.get("max_abs_skin_friction_coefficient")) + "; the no-slip and finite/nonzero skin-friction checks are " +
                         code(sanity_checks.get("no_slip_wall_velocity", "not reported")) + "/" +
                         code(sanity_checks.get("skin_friction_finite", "not reported")) + "/" +
                         code(sanity_checks.get("nonzero_skin_friction_evidence", "not reported")) +
                         ". These values document boundary-output behavior without asserting a reference drag accuracy.")
        else:
            lines.append("With viscous flux disabled, the final-row drag level $C_D=" +
                         number(force.get("cd")) + "$ is the submitted pressure-drag value $C_{D,p}=" +
                         number(force.get("pressure_drag")) + "$ and $C_{D,v}=" +
                         number(force.get("viscous_drag")) + "$. Its magnitude may include physical compressibility/wave effects and Rusanov/mesh diffusion; no external reference or grid study is used to label it accurate.")
        for figure in figures_by_case.get(case_id, []):
            lines.extend(render_figure(figure, report_root, issues))
    lines += [r"\subsection{Cylinder Reynolds 20}"]
    cylinder20 = "cylinder_m010_laminar_re20"
    stat20 = result_record(statistics, cylinder20)
    sanity20 = sanity_by_case.get(cylinder20, {})
    sanity20_checks = mapping(sanity20.get("checks"))
    sanity20_metrics = mapping(sanity20.get("metrics"))
    status20 = status_value(status_by_case.get(cylinder20, {}), metadata_by_case.get(cylinder20, {}))
    lines.append("Submitted status: " + code(status20) + "; final $C_D=" + number(stat20.get("final_cd")) + "$ and tail mean $\\overline C_D=" + number(stat20.get("mean_cd_tail")) + "$. The submitted contour/window interpretation is in the figure captions.")
    re20_mach_span = finite_number(stat20.get("field_mach_relative_span"))
    re20_pressure_span = finite_number(stat20.get("field_pressure_relative_span"))
    re20_drag = finite_number(stat20.get("mean_cd_tail"))
    if re20_mach_span is None or re20_pressure_span is None or re20_drag is None:
        lines.append("Re20 wake interpretation is unavailable because the submitted statistics lack one or more field-span or tail-drag values; no recirculation length or separation location is claimed.")
    else:
        lines.append("The Re20 submitted field has relative Mach span " + number(re20_mach_span) +
                     " and relative pressure span " + number(re20_pressure_span) +
                     ", while its tail mean drag is " + number(re20_drag) +
                     ". Together with the required velocity-magnitude wake figure, these are evidence of a nonuniform downstream solution. No recirculation length or separation location is extracted, so neither is quantified here. The positive-drag and no-slip-wall checks are " +
                     code(sanity20_checks.get("positive_tail_mean_drag", "not reported")) + "/" +
                     code(sanity20_checks.get("no_slip_wall_velocity", "not reported")) +
                     " (maximum reported wall speed " + number(sanity20_metrics.get("max_wall_speed")) + ").")
        if status20 == "converged" and sanity20_checks.get("positive_tail_mean_drag") is True:
            lines.append("The converged submitted status and positive-tail-drag check support a steady, positive-drag wake under the solver's residual/force criteria. This is an internal plausibility assessment only; no external Re20 coefficient or grid-refinement result is available to establish quantitative drag accuracy.")
        else:
            lines.append("The submitted status/positive-drag evidence does not support calling this a settled steady wake; no steady-wake or drag-plausibility success claim is made.")
    for figure in figures_by_case.get(cylinder20, []):
        lines.extend(render_figure(figure, report_root, issues))
    lines += [r"\subsection{Cylinder Reynolds 200 Vortex Street}"]
    stat200 = result_record(statistics, TRANSIENT_CASE)
    status200 = status_value(status_by_case.get(TRANSIENT_CASE, {}), metadata_by_case.get(TRANSIENT_CASE, {}))
    lines.append("Submitted status: " + code(status200) + "; tail mean $\\overline C_D=" + number(stat200.get("mean_cd_tail")) + "$; lift amplitude $=" + number(stat200.get("lift_amplitude_tail")) + "$; dominant frequency $=" + number(stat200.get("dominant_frequency")) + "$; and $St=" + number(stat200.get("strouhal_number")) + "$. These quantities are reported only when the submitted statistics contain an accepted, resolved post-transient frequency estimate. A vorticity figure uses its manifest/source caption; the standard postprocessor applies a $[-5,5]$ nondimensional vorticity clip for Re200 when vorticity is available.")
    lines.append("The standard frequency estimator removes a least-squares linear trend from the tail lift signal, applies a Hann window, and computes a one-sided FFT. The submitted method field is " +
                 code(active_method(stat200, "frequency_estimation_method")) + "; validity/reason are " +
                 code(stat200.get("frequency_estimation_valid", "not reported")) + "/" +
                 code(stat200.get("frequency_estimation_reason", "not reported")) +
                 ". Eligible peaks must resolve at least three cycles in the tail; the accepted peak must contain at least 10\\% of eligible-band power and be at least five times the median lower-power eligible-bin background.")
    spectral_rows = [[
        code(stat200.get("frequency_estimation_valid", "not reported")),
        number(stat200.get("dominant_frequency")), number(stat200.get("frequency_peak_cycles")),
        number(stat200.get("frequency_peak_power_fraction")),
        number(stat200.get("frequency_peak_prominence")), number(stat200.get("strouhal_number")),
    ]]
    lines += make_tabular(
        ["valid", "$f$", "resolved cycles", "peak power fraction", "peak prominence", "$St$"],
        spectral_rows, "p{0.12\\linewidth}r r r r r")
    lines.append("The reported Strouhal conversion uses $St=fL_{ref}/U_\\infty$ with $L_{ref}=" +
                 number(stat200.get("strouhal_reference_length")) + "$ and $U_\\infty=" +
                 number(stat200.get("strouhal_freestream_velocity")) + "$; scale provenance is " +
                 code(active_method(stat200, "strouhal_reference_source")) +
                 ". Peak power fraction is relative to total eligible-band power, and peak prominence is relative to the median lower-power eligible-bin background.")
    re200_amplitude = finite_number(stat200.get("lift_amplitude_tail"))
    re200_frequency = finite_number(stat200.get("dominant_frequency"))
    re200_window = finite_number(stat200.get("tail_physical_time_span"))
    re200_cycles = finite_number(stat200.get("frequency_peak_cycles"))
    re200_peak_fraction = finite_number(stat200.get("frequency_peak_power_fraction"))
    re200_peak_prominence = finite_number(stat200.get("frequency_peak_prominence"))
    if (stat200.get("frequency_estimation_valid") is not True or
            re200_amplitude is None or re200_frequency is None or re200_window is None or
            re200_cycles is None or re200_peak_fraction is None or re200_peak_prominence is None):
        lines.append("Re200 shedding interpretation is unavailable because the submitted statistics do not contain a valid frequency estimate with finite lift amplitude, tail window, resolved-cycle count, peak-power fraction, and peak-prominence evidence; no periodic-shedding claim is made.")
    else:
        lines.append("The Re200 shedding interpretation is limited to submitted evidence: a post-transient lift amplitude of " +
                     number(re200_amplitude) + ", dominant lift frequency " + number(re200_frequency) +
                     ", " + number(re200_cycles) + " resolved cycles, peak power fraction " +
                     number(re200_peak_fraction) + ", peak prominence " + number(re200_peak_prominence) +
                     ", and tail sampling window " + number(re200_window) +
                     " provide a resolved force-history signature of periodic shedding when the status is statistically periodic. The vorticity figure supplies the spatial visualization; no independent vortex-identification metric is reported.")
    for figure in figures_by_case.get(TRANSIENT_CASE, []):
        lines.extend(render_figure(figure, report_root, issues))
    lines += [r"\clearpage", r"\section{Parallel Validation}"]
    comparison_rows: list[list[str]] = []
    comparison_consistency_rows: list[list[str]] = []
    comparison_partition_rows: list[list[str]] = []
    for case_id in ("naca0012_m015_inviscid", "cylinder_m010_laminar_re20"):
        evidence_rows = sorted(rank_comparisons.get(case_id, []), key=lambda item: int(item["ranks"]))
        reference = next((item for item in evidence_rows if int(item["ranks"]) == 8), None)
        reference_force = mapping(reference.get("force")) if reference else {}
        reference_status = mapping(reference.get("status")) if reference else {}
        reference_cd = finite_number(reference_force.get("cd"))
        reference_cl = finite_number(reference_force.get("cl"))
        reference_wall = finite_number(reference_status.get("wall_time_seconds"))
        reference_orders = residual_reduction_orders(reference.get("residuals", [])) if reference else None
        for evidence in evidence_rows:
            orders = residual_reduction_orders(evidence.get("residuals", []))
            force = mapping(evidence.get("force"))
            status = mapping(evidence.get("status"))
            partition_metrics = partition_communication_metrics(evidence.get("partitions", []))
            cd = finite_number(force.get("cd"))
            cl = finite_number(force.get("cl"))
            wall = finite_number(status.get("wall_time_seconds"))
            orders_difference = (abs(orders - reference_orders)
                                 if orders is not None and reference_orders is not None else None)
            cd_difference = abs(cd - reference_cd) if cd is not None and reference_cd is not None else None
            cl_difference = abs(cl - reference_cl) if cl is not None and reference_cl is not None else None
            wall_difference = wall - reference_wall if wall is not None and reference_wall is not None else None
            wall_ratio = wall / reference_wall if wall is not None and reference_wall is not None and reference_wall > 0.0 else None
            comparison_rows.append([
                table_case(case_id), integer(evidence.get("ranks")),
                number(status.get("wall_time_seconds")), number(orders),
                number(force.get("cd")), number(force.get("cl")),
                compact_method(status.get("convergence_status", "not reported")),
            ])
            comparison_consistency_rows.append([
                table_case(case_id), integer(evidence.get("ranks")),
                number(orders_difference), number(cd_difference), number(cl_difference),
            ])
            comparison_partition_rows.append([
                table_case(case_id), integer(evidence.get("ranks")),
                number(wall_difference), number(wall_ratio),
                number(partition_metrics.get("max_min_ratio")),
                number(partition_metrics.get("recv_owned_ratio")),
                number(partition_metrics.get("maximum_neighbors")),
            ])
    lines.append(r"Each row below is read from the manifest-named completed output directory: wall time and status are from \texttt{run\_status.json}, residual orders are recalculated from its \texttt{residuals.csv}, force values are the final steady \texttt{forces.csv} row, and partition counts are from \texttt{partition\_diagnostics.csv}. The $np=8$ row is the reference. Residual difference is the absolute difference in end-to-end $\log_{10}(R_0/R_f)$ reduction, not a pointwise comparison between histories of different length; force differences are absolute coefficient differences, $\Delta t$ is signed wall-time difference in seconds, and $t/t_8$ is the wall-time ratio.")
    lines += make_tabular(["case", "np", "wall s", "res. orders", "$C_D$", "$C_L$", "status"], comparison_rows,
                          "p{0.20\\linewidth}r r r r r p{0.13\\linewidth}")
    lines += make_tabular(["case", "np", r"$|\Delta\log_{10}(R_0/R_f)|_8$", r"$|\Delta C_D|_8$", r"$|\Delta C_L|_8$"], comparison_consistency_rows,
                          "p{0.27\\linewidth}r r r r")
    lines += make_tabular(["case", "np", r"$\Delta t_8$ s", "$t/t_8$", "owned max/min", "recv/owned", "max neighbours"], comparison_partition_rows,
                          "p{0.20\\linewidth}r r r r r r")
    for case_id in ("naca0012_m015_inviscid", "cylinder_m010_laminar_re20"):
        evidence_rows = sorted(rank_comparisons.get(case_id, []), key=lambda item: int(item["ranks"]))
        ranks = [int(item["ranks"]) for item in evidence_rows]
        reference = next((item for item in evidence_rows if int(item["ranks"]) == 8), None)
        reference_force = mapping(reference.get("force")) if reference else {}
        reference_status = mapping(reference.get("status")) if reference else {}
        reference_cd = finite_number(reference_force.get("cd"))
        reference_cl = finite_number(reference_force.get("cl"))
        reference_wall = finite_number(reference_status.get("wall_time_seconds"))
        reference_orders = residual_reduction_orders(reference.get("residuals", [])) if reference else None
        cd_differences = [abs(value - reference_cd) for item in evidence_rows
                          if reference_cd is not None
                          for value in [finite_number(mapping(item.get("force")).get("cd"))]
                          if value is not None]
        cl_differences = [abs(value - reference_cl) for item in evidence_rows
                          if reference_cl is not None
                          for value in [finite_number(mapping(item.get("force")).get("cl"))]
                          if value is not None]
        order_differences = [abs(value - reference_orders) for item in evidence_rows
                             if reference_orders is not None
                             for value in [residual_reduction_orders(item.get("residuals", []))]
                             if value is not None]
        timing_pairs = [(int(item["ranks"]), value) for item in evidence_rows
                        for value in [finite_number(mapping(item.get("status")).get("wall_time_seconds"))]
                        if value is not None]
        wall_times = [value for _, value in timing_pairs]
        fastest = min(timing_pairs, key=lambda item: item[1]) if timing_pairs else None
        eight_over_fastest = (reference_wall / fastest[1]
                              if reference_wall is not None and fastest and fastest[1] > 0.0 else None)
        partition_metrics = [partition_communication_metrics(item.get("partitions", []))
                             for item in evidence_rows]
        load_ratios = [value for item in partition_metrics
                       for value in [finite_number(item.get("max_min_ratio"))]
                       if value is not None]
        recv_owned_ratios = [value for item in partition_metrics
                             for value in [finite_number(item.get("recv_owned_ratio"))]
                             if value is not None]
        statuses = sorted({str(mapping(item.get("status")).get(
            "convergence_status", "not reported")) for item in evidence_rows})
        lines.append("Submitted rank-count evidence for " + code(case_id) + ": " +
                     (code(", ".join(str(rank) for rank in ranks)) if ranks else code("none")) +
                     ". Relative to the submitted $np=8$ reference, the maximum residual-reduction difference is $|\\Delta\\log_{10}(R_0/R_f)|=" +
                     number(max(order_differences) if order_differences else None) + "$ and the maximum absolute force differences are $|\\Delta C_D|=" +
                     number(max(cd_differences) if cd_differences else None) + "$ and $|\\Delta C_L|=" +
                     number(max(cl_differences) if cl_differences else None) + "$; wall times span " +
                     number(min(wall_times) if wall_times else None) + "--" + number(max(wall_times) if wall_times else None) +
                     " s. The fastest submitted row is $np=" + integer(fastest[0] if fastest else None) +
                     "$ and the $np=8$ time divided by that fastest time is " + number(eight_over_fastest) +
                     ". Owned-cell max/min ratios span " +
                     number(min(load_ratios) if load_ratios else None) + "--" +
                     number(max(load_ratios) if load_ratios else None) +
                     ", while summed receive-cell count divided by summed owned cells spans " +
                     number(min(recv_owned_ratios) if recv_owned_ratios else None) + "--" +
                     number(max(recv_owned_ratios) if recv_owned_ratios else None) +
                     ". Submitted statuses across ranks are " +
                     code(", ".join(statuses) if statuses else "none") +
                     ". These expose rank-dependent residual/force/status behavior and a count-based halo surface-to-volume proxy. Wall time still combines local work, halo traffic, global reductions, process placement, and system noise; no MPI profiler isolates communication overhead, so the measurements are run-specific consistency/timing observations rather than a hardware-independent scaling or communication-cost claim.")
    lines += [r"\section{Visualization and Plot Style}",
              "Figure captions are copied from the figure manifest, including source file and plotted variable. The expected post-processing figures are publication-style line histories and filled unstructured-field contours with labelled colorbars. The generator verifies that each manifest filename exists before including it; it does not rename a field figure or infer its variable from the filename.",
              r"\section{Limitations and Failure Analysis}",
              "Completed status and passing structural checks establish reproducible submitted results, not independent physical validation. The principal unresolved numerical and evidence limitations are:",
              r"\begin{itemize}",
              r"\item Rusanov/local Lax--Friedrichs is robust but diffusive. It can smear shocks, wake structures, and contact/shear features and can contribute to computed inviscid drag; no less-diffusive production flux or entropy-fix comparison is submitted.",
              r"\item Each geometry is evaluated on one supplied mesh. There is no grid-convergence, $y^+$, boundary-layer-resolution, or wall-gradient sensitivity study, so skin friction, viscous drag, separation, and shock thickness retain spatial-discretization uncertainty.",
        r"\item No external reference solution, experimental coefficient, or independently computed shock or separation metric is used. Surface-$C_p$ ranges and field contours support qualitative interpretation but do not validate shock location, jump strength, recirculation length, or drag accuracy.",
              r"\item Re200 permits occasional physical-step inner-target misses provided the aggregate settlement gate passes. Submitted metadata reports " +
              integer(re200_meta.get("inner_target_misses")) + " misses, converged-step fraction " +
              number(re200_meta.get("inner_target_converged_fraction")) +
              ", and last total-residual ratio " + number(re200_meta.get("last_inner_residual_ratio")) +
              "; these values are exposed rather than implying every inner solve met its target.",
              r"\item The Re200 vorticity rendering is clipped to $[-5,5]$ to expose the wake. Values outside that interval saturate in the visualization, so the image is unsuitable for reading peak vorticity magnitude.",
              r"\item MPI evidence covers two cases and reports end-to-end wall time, owned/ghost balance, and message-cell counts. It has no profiler-based compute/halo/reduction decomposition; communication overhead and hardware-independent scaling therefore remain unquantified.",
              r"\item Extension seams are modular, but the current state, geometry, gas closure, and model enum remain fixed to 2-D four-equation perfect-gas inviscid/laminar flow. Three-dimensional, general-EOS, RANS, and multispecies capability requires the interface generalizations described earlier.",
              r"\end{itemize}",
              "With additional time, the highest-value improvements would be a mesh-refinement/reference campaign, automated shock and wake metrics, boundary-layer resolution diagnostics, a less-diffusive shock-capable flux comparison, and MPI phase/halo timing instrumentation. Missing metadata, status records, diagnostics, statistics, figures, or sanity checks remain listed below and must be resolved before any final completion claim.",
              r"\section{Evidence Audit}",
              "The following items were missing or malformed while generating this report. An empty list means that the generator did not encounter an input-format or named-file problem; it does not by itself prove physical accuracy or that all numerical requirements passed."]
    if issues:
        lines.append(r"\begin{itemize}")
        lines.extend(r"\item " + tex(issue) for issue in sorted(set(issues)))
        lines.append(r"\end{itemize}")
    else:
        lines.append("No missing or malformed report inputs were detected by this generator.")
    lines += [r"\end{document}", ""]
    return "\n\n".join(lines)


def main() -> int:
    default_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--project-root", type=Path, default=default_root)
    parser.add_argument("--report", type=Path, default=Path("report"), help="report directory, relative to project root")
    parser.add_argument("--results", type=Path, default=Path("results"), help="results directory, relative to project root")
    parser.add_argument("--case-root", type=Path, default=Path("inputs/cases"), help="case JSON directory, relative to project root")
    parser.add_argument("--strict", action="store_true", help="return nonzero when required report evidence is absent")
    args = parser.parse_args()
    root = args.project_root.resolve()
    report_root = (root / args.report).resolve() if not args.report.is_absolute() else args.report.resolve()
    results_root = (root / args.results).resolve() if not args.results.is_absolute() else args.results.resolve()
    case_root = (root / args.case_root).resolve() if not args.case_root.is_absolute() else args.case_root.resolve()
    report_root.mkdir(parents=True, exist_ok=True)
    issues: list[str] = []
    configs = parse_case_configs(case_root, issues)
    for case_id in REQUIRED_CASES:
        if case_id not in configs:
            issues.append(f"missing usable case input for required case {case_id}")
    statistics_document = load_json(report_root / "result_statistics.json", issues, "result statistics")
    stats_rows = sequence(statistics_document)
    statistics: dict[str, Mapping[str, Any]] = {}
    for row in stats_rows:
        item = mapping(row)
        case_id = item.get("case_id")
        if isinstance(case_id, str):
            if case_id in statistics:
                issues.append(f"result statistics has duplicate case_id {case_id}")
            statistics[case_id] = item
        else:
            issues.append("result statistics row lacks string case_id")
    for case_id in REQUIRED_CASES:
        if case_id not in statistics:
            issues.append(f"missing result statistics for required case {case_id}")
    manifest = load_csv(report_root / "run_manifest.csv", issues, "run manifest")
    figure_rows = load_csv(report_root / "figure_manifest.csv", issues, "figure manifest")
    figures = parse_figures(figure_rows, issues)
    for figure in figures:
        if figure.case_id not in REQUIRED_CASES:
            issues.append(f"figure manifest references non-required/unknown case {figure.case_id}")
    sanity = load_json(report_root / "sanity_checks.json", issues, "sanity checks")
    sanity_map = mapping(sanity)
    if sanity_map.get("all_passed") is not True:
        issues.append("aggregate sanity_checks.json all_passed is not true")
    sanity_cases: dict[str, Mapping[str, Any]] = {}
    for entry in sequence(sanity_map.get("cases")):
        item = mapping(entry)
        case_id = item.get("case_id")
        if not isinstance(case_id, str):
            issues.append("sanity-check row lacks string case_id")
            continue
        if case_id in sanity_cases:
            issues.append(f"sanity checks has duplicate case_id {case_id}")
        sanity_cases[case_id] = item
        checks = mapping(item.get("checks"))
        if item.get("passed") is not True or not checks or any(value is not True for value in checks.values()):
            issues.append(f"sanity checks do not pass completely for {case_id}")
    for case_id in REQUIRED_CASES:
        if case_id not in sanity_cases:
            issues.append(f"missing sanity checks for required case {case_id}")
    manifest_dirs = manifest_case_dirs(root, results_root, manifest)
    primary_dirs = choose_primary_dirs(results_root, manifest_dirs, statistics)
    metadata_by_case: dict[str, Mapping[str, Any]] = {}
    status_by_case: dict[str, Mapping[str, Any]] = {}
    partitions: dict[str, list[dict[str, Any]]] = {}
    force_summaries: dict[str, Mapping[str, Any]] = {}
    for case_id, case_dir in primary_dirs.items():
        expected_dir = results_root / case_id
        if case_dir.resolve() != expected_dir.resolve():
            issues.append(f"primary output for {case_id} is {case_dir}, expected {expected_dir}")
        primary_manifest_rows = []
        for row in manifest:
            if row.get("case_id", "").strip() != case_id or positive_integer(row.get("mpi_ranks")) != 8:
                continue
            output = row.get("output_directory", "").strip()
            row_dir = Path(output) if output else expected_dir
            if not row_dir.is_absolute():
                row_dir = root / row_dir
            if row_dir.resolve() == expected_dir.resolve():
                primary_manifest_rows.append(row)
        if not primary_manifest_rows:
            issues.append(f"{case_id} lacks a primary np=8 manifest row for {expected_dir}")
        elif len(primary_manifest_rows) > 1:
            issues.append(f"{case_id} has duplicate primary np=8 manifest rows")
        metadata = load_json(case_dir / "metadata.json", issues, f"metadata for {case_id}")
        status = load_json(case_dir / "run_status.json", issues, f"run status for {case_id}")
        metadata_by_case[case_id] = mapping(metadata)
        status_by_case[case_id] = mapping(status)
        partitions[case_id] = read_partition_rows(case_dir, issues)
        force_summaries[case_id] = force_summary(case_dir, case_id, issues)
        issues.extend(output_evidence_issues(case_id, case_dir, 8, metadata_by_case[case_id],
                                             status_by_case[case_id], partitions[case_id]))
        if not partition_load_balance(partitions[case_id]):
            issues.append(f"{case_id} partition diagnostics lacks positive owned-cell values for load-balance evidence")
        if metadata_by_case[case_id].get("steady_recovery_activated") is True:
            recovery_step = positive_integer(metadata_by_case[case_id].get("steady_recovery_activation_step"))
            recovery_cfl_cap = finite_number(metadata_by_case[case_id].get("steady_recovery_cfl_cap"))
            recovery_relaxation_cap = finite_number(metadata_by_case[case_id].get("steady_recovery_relaxation_cap"))
            final_step_for_recovery = positive_integer(status_by_case[case_id].get("final_step"))
            control = mapping(configs.get(case_id, {}).get("run_control"))
            configured_cfl_max = finite_number(control.get("cfl_max"))
            if control.get("type") != "steady":
                issues.append(f"{case_id} marks steady recovery active for a non-steady run")
            if recovery_step is None or (final_step_for_recovery is not None and recovery_step > final_step_for_recovery):
                issues.append(f"{case_id} active steady recovery lacks an activation step within the completed run")
            if recovery_cfl_cap is None or recovery_cfl_cap <= 0.0:
                issues.append(f"{case_id} active steady recovery lacks a positive effective CFL cap")
            elif configured_cfl_max is None or configured_cfl_max <= 0.0 or recovery_cfl_cap > configured_cfl_max:
                issues.append(f"{case_id} active steady recovery CFL cap is not conservative relative to the supplied CFL maximum")
            if recovery_relaxation_cap is None or not (0.0 < recovery_relaxation_cap <= 1.0):
                issues.append(f"{case_id} active steady recovery lacks a relaxation cap in (0,1]")
        stat = statistics.get(case_id, {})
        if stat.get("convergence_status") != status_by_case[case_id].get("convergence_status"):
            issues.append(f"result statistics/status convergence_status disagree for {case_id}")
        if positive_integer(stat.get("mpi_ranks")) != 8:
            issues.append(f"result statistics for {case_id} are not from primary np=8 output")
        if case_id == TRANSIENT_CASE:
            if metadata_by_case[case_id].get("true_bdf2_inner_loop") is not True:
                issues.append("Re200 metadata does not affirm true frozen-history BDF2")
            if finite_number(status_by_case[case_id].get("final_physical_time")) is None or float(status_by_case[case_id].get("final_physical_time", 0.0)) < 300.0:
                issues.append("Re200 output does not reach physical time 300")
            if finite_number(stat.get("lift_amplitude_tail")) is None:
                issues.append("Re200 statistics lacks finite lift_amplitude_tail")
            if stat.get("frequency_estimation_method") != FREQUENCY_ESTIMATION_METHOD:
                issues.append(f"Re200 statistics does not report {FREQUENCY_ESTIMATION_METHOD} frequency estimation")
            if stat.get("frequency_estimation_valid") is not True:
                issues.append("Re200 frequency estimate is not valid (reason: " +
                              str(stat.get("frequency_estimation_reason", "not reported")) + ")")
            elif stat.get("frequency_estimation_reason") != "accepted":
                issues.append("Re200 valid frequency estimate does not carry the accepted reason")
            dominant_frequency = finite_number(stat.get("dominant_frequency"))
            strouhal = finite_number(stat.get("strouhal_number"))
            peak_cycles = finite_number(stat.get("frequency_peak_cycles"))
            minimum_cycles = finite_number(stat.get("frequency_min_cycles_required"))
            peak_power_fraction = finite_number(stat.get("frequency_peak_power_fraction"))
            peak_prominence = finite_number(stat.get("frequency_peak_prominence"))
            frequency_tail_duration = finite_number(stat.get("frequency_tail_duration"))
            reference_length = finite_number(stat.get("strouhal_reference_length"))
            reference_velocity = finite_number(stat.get("strouhal_freestream_velocity"))
            reference_source = stat.get("strouhal_reference_source")
            if dominant_frequency is None or dominant_frequency <= 0.0:
                issues.append("Re200 statistics lacks a positive dominant_frequency")
            if strouhal is None or strouhal <= 0.0:
                issues.append("Re200 statistics lacks a positive strouhal_number")
            if minimum_cycles is None or minimum_cycles < FREQUENCY_MIN_CYCLES:
                issues.append("Re200 frequency evidence lacks the required three-cycle resolution criterion")
            if peak_cycles is None or minimum_cycles is None or peak_cycles < minimum_cycles:
                issues.append("Re200 frequency peak does not resolve the reported minimum cycle count")
            if (peak_power_fraction is None or
                    not (FREQUENCY_MIN_PEAK_POWER_FRACTION <= peak_power_fraction <= 1.0)):
                issues.append("Re200 frequency peak lacks the required eligible-band power fraction")
            if peak_prominence is None or peak_prominence < FREQUENCY_MIN_PEAK_PROMINENCE:
                issues.append("Re200 frequency peak lacks the required prominence")
            if frequency_tail_duration is None or frequency_tail_duration <= 0.0:
                issues.append("Re200 frequency evidence lacks a positive tail duration")
            elif dominant_frequency is not None and peak_cycles is not None and not math.isclose(
                    peak_cycles, dominant_frequency * frequency_tail_duration,
                    rel_tol=1.0e-8, abs_tol=1.0e-10):
                issues.append("Re200 resolved-cycle count is inconsistent with frequency and tail duration")
            if reference_length is None or reference_length <= 0.0 or reference_velocity is None or reference_velocity <= 0.0:
                issues.append("Re200 Strouhal evidence lacks positive reference length/velocity scales")
            elif dominant_frequency is not None and strouhal is not None and not math.isclose(
                    strouhal, dominant_frequency * reference_length / reference_velocity,
                    rel_tol=1.0e-8, abs_tol=1.0e-10):
                issues.append("Re200 Strouhal number is inconsistent with its reported reference scales")
            if not isinstance(reference_source, str) or not reference_source.strip():
                issues.append("Re200 Strouhal reference-scale provenance is not reported")
            observed_min = positive_integer(metadata_by_case[case_id].get("observed_min_inner_iterations"))
            observed_max = positive_integer(metadata_by_case[case_id].get("observed_max_inner_iterations"))
            observed_mean = finite_number(metadata_by_case[case_id].get("observed_mean_inner_iterations"))
            target_misses = nonnegative_integer(metadata_by_case[case_id].get("inner_target_misses"))
            converged_fraction = unit_interval(metadata_by_case[case_id].get("inner_target_converged_fraction"))
            last_ratio = finite_number(metadata_by_case[case_id].get("last_inner_residual_ratio"))
            if observed_min is None or observed_max is None or observed_min > observed_max:
                issues.append("Re200 metadata lacks a valid observed inner-iteration min/max range")
            if observed_mean is None or observed_mean <= 0.0:
                issues.append("Re200 metadata lacks a positive observed mean inner-iteration count")
            elif observed_min is not None and observed_max is not None and not (observed_min <= observed_mean <= observed_max):
                issues.append("Re200 metadata observed mean inner iterations lies outside its reported range")
            if target_misses is None:
                issues.append("Re200 metadata lacks non-negative inner_target_misses evidence")
            if converged_fraction is None:
                issues.append("Re200 metadata lacks inner_target_converged_fraction in [0,1]")
            if last_ratio is None or last_ratio < 0.0:
                issues.append("Re200 metadata lacks a non-negative last_inner_residual_ratio")
        complete, missing = has_required_figures(case_id, configs.get(case_id, {}), figures)
        if not complete:
            issues.append(f"{case_id} lacks required figure variables: {', '.join(missing)}")
    rank_comparisons = rank_comparison_evidence(root, results_root, manifest, issues)
    document = report_text(root, report_root, results_root, configs, statistics, manifest, figures,
                           metadata_by_case, status_by_case, partitions, force_summaries, sanity,
                           rank_comparisons, issues)
    target = report_root / "report.tex"
    target.write_text(document, encoding="utf-8")
    print(f"wrote {target}")
    if issues:
        print(f"report evidence audit: {len(set(issues))} issue(s)", file=sys.stderr)
    return 1 if args.strict and issues else 0


if __name__ == "__main__":
    raise SystemExit(main())
