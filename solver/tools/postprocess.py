#!/usr/bin/env python3
"""Generate traceable publication-style figures and CFD sanity checks.

This script intentionally uses only NumPy, Matplotlib, and the Python standard
library so it is reproducible in the submission-local virtual environment.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import dataclass
from pathlib import Path
import xml.etree.ElementTree as ET

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
import numpy as np


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

# These checks deliberately examine the files used to make the report rather
# than trusting the status JSON.  They are a final guard against a solver
# stopping successfully while writing a stale, incomplete, or physically
# trivial final snapshot.
REQUIRED_FIELD_DATA = (
    "density",
    "velocity",
    "pressure",
    "mach",
    "temperature",
    "total_energy",
    "owner_rank",
)
RANK_COMPARISON_CASES = (
    "naca0012_m015_inviscid",
    "cylinder_m010_laminar_re20",
)
STEADY_TERMINAL_FORCE_SAMPLES = 200

# A reported shedding frequency must be supported by several cycles in the
# post-transient tail.  These are signal-quality criteria, not a prior on the
# expected Strouhal number: the eligible frequency band is derived solely from
# the available sampling interval and tail duration.
FREQUENCY_MIN_SAMPLES = 16
FREQUENCY_MIN_CYCLES = 3.0
FREQUENCY_MIN_RELATIVE_DETRENDED_RMS = 1.0e-6
FREQUENCY_MIN_PEAK_POWER_FRACTION = 0.10
FREQUENCY_MIN_PEAK_PROMINENCE = 5.0


@dataclass
class Field:
    points: np.ndarray
    cells: list[np.ndarray]
    data: dict[str, np.ndarray]


def configure_style() -> None:
    plt.rcParams.update(
        {
            "font.family": "serif",
            "font.size": 9.5,
            "axes.labelsize": 10,
            "axes.titlesize": 10.5,
            "legend.fontsize": 8.5,
            "lines.linewidth": 1.35,
            "axes.grid": True,
            "grid.alpha": 0.25,
            "figure.dpi": 130,
            "savefig.dpi": 220,
            "savefig.bbox": "tight",
        }
    )


def structured_csv(path: Path) -> np.ndarray:
    data = np.genfromtxt(path, delimiter=",", names=True, dtype=None, encoding="utf-8")
    if data.size == 0:
        raise ValueError(f"empty CSV: {path}")
    return np.atleast_1d(data)


def force_tail_slice(case_id: str, sample_count: int) -> slice:
    """Select the force-history window appropriate to the case type.

    Steady histories can be long pseudo-time continuations, so their reported
    terminal statistics must exclude the early continuation.  Re200 remains a
    physical-time statistical case and retains its established final-40%
    window for shedding analysis.
    """
    if "re200" in case_id.lower():
        return slice(max(0, int(0.6 * sample_count)), None)
    return slice(max(0, sample_count - STEADY_TERMINAL_FORCE_SAMPLES), None)


def force_history_caption(case_id: str, sample_count: int) -> str:
    """Describe the force-history visualization without hiding startup data."""
    if "re200" in case_id.lower():
        return "Physical-time lift and drag coefficient history."
    terminal_samples = sample_count - force_tail_slice(case_id, sample_count).start
    return (
        "Full lift and drag coefficient history (left) and the terminal "
        f"{terminal_samples}-sample convergence window (right)."
    )


def positive_number(value: object) -> float | None:
    """Return a finite, strictly positive scalar or ``None``."""
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) and number > 0.0 else None


def strouhal_reference_scales(case_id: str, metadata: dict[str, object]) -> tuple[float, float, str]:
    """Get the reference length and freestream speed used for St = f L / U.

    Production metadata is deliberately compact, so read the immutable input
    case file when it is available.  Direct or nested metadata values remain a
    useful fallback for externally supplied result directories.  The final
    unit fallback is explicit in the returned provenance rather than silently
    assuming that frequency and Strouhal number are interchangeable.
    """
    candidate_sources: list[tuple[object, object, str]] = []
    candidate_sources.append((metadata.get("reference_length"),
                              metadata.get("freestream_velocity"), "metadata"))
    reference_metadata = metadata.get("reference")
    freestream_metadata = metadata.get("freestream")
    if isinstance(reference_metadata, dict) and isinstance(freestream_metadata, dict):
        candidate_sources.append((reference_metadata.get("length"),
                                  freestream_metadata.get("velocity_magnitude"),
                                  "metadata"))

    case_file = Path(__file__).resolve().parents[1] / "inputs" / "cases" / f"{case_id}.json"
    if case_file.is_file():
        try:
            config = json.loads(case_file.read_text())
            reference = config.get("reference", {})
            freestream = config.get("freestream", {})
            if isinstance(reference, dict) and isinstance(freestream, dict):
                candidate_sources.append((reference.get("length"),
                                          freestream.get("velocity_magnitude"),
                                          "input_case_config"))
        except (OSError, json.JSONDecodeError):
            pass

    for length_value, velocity_value, source in candidate_sources:
        length = positive_number(length_value)
        velocity = positive_number(velocity_value)
        if length is not None and velocity is not None:
            return length, velocity, source
    return 1.0, 1.0, "unit_fallback"


def dominant_frequency_estimate(times: np.ndarray, values: np.ndarray,
                                *, reference_length: float = 1.0,
                                freestream_velocity: float = 1.0) -> dict[str, object]:
    """Estimate a resolved dominant frequency from a uniformly sampled tail.

    The estimator removes a least-squares linear trend, applies a Hann window,
    and searches only frequencies with at least ``FREQUENCY_MIN_CYCLES`` in
    the supplied time interval.  Returned diagnostics make a rejected or weak
    peak auditable in the result statistics rather than converting every
    nonzero FFT bin into a claimed shedding frequency.
    """
    times = np.asarray(times, dtype=float)
    values = np.asarray(values, dtype=float)
    result: dict[str, object] = {
        "frequency_estimation_method": "linear_detrend_hann_window_fft",
        "frequency_estimation_valid": False,
        "frequency_sample_count": int(times.size),
        "frequency_min_cycles_required": FREQUENCY_MIN_CYCLES,
    }
    if times.ndim != 1 or values.ndim != 1 or times.size != values.size:
        result["frequency_estimation_reason"] = "time_value_shape_mismatch"
        return result
    if times.size < FREQUENCY_MIN_SAMPLES:
        result["frequency_estimation_reason"] = "insufficient_samples"
        return result
    if not np.all(np.isfinite(times)) or not np.all(np.isfinite(values)):
        result["frequency_estimation_reason"] = "nonfinite_samples"
        return result

    differences = np.diff(times)
    spacing = float(np.median(differences))
    duration = float(times[-1] - times[0])
    if not (spacing > 0.0 and duration > 0.0) or not np.allclose(
            differences, spacing, rtol=1.0e-3, atol=1.0e-10):
        result["frequency_estimation_reason"] = "nonuniform_or_nonincreasing_sampling"
        return result

    # Centering and scaling time protects the least-squares fit from loss of
    # precision for long physical-time histories.
    normalized_time = (times - float(np.mean(times))) / duration
    trend_matrix = np.column_stack((np.ones(times.size), normalized_time))
    trend_coefficients, _, _, _ = np.linalg.lstsq(trend_matrix, values, rcond=None)
    detrended = values - trend_matrix @ trend_coefficients
    detrended_rms = float(np.sqrt(np.mean(detrended * detrended)))
    raw_scale = max(float(np.ptp(values)), float(np.std(values)),
                    abs(float(np.mean(values))), np.finfo(float).tiny)
    relative_detrended_rms = detrended_rms / raw_scale
    result.update({
        "frequency_tail_duration": duration,
        "frequency_sample_spacing": spacing,
        "frequency_nyquist": 0.5 / spacing,
        "frequency_detrended_rms": detrended_rms,
        "frequency_relative_detrended_rms": relative_detrended_rms,
        "frequency_linear_trend_intercept": float(trend_coefficients[0]),
        "frequency_linear_trend_slope_per_time": float(trend_coefficients[1] / duration),
    })
    if relative_detrended_rms < FREQUENCY_MIN_RELATIVE_DETRENDED_RMS:
        result["frequency_estimation_reason"] = "linear_trend_or_negligible_fluctuation"
        return result

    window = np.hanning(times.size)
    coherent_gain = float(np.sum(window))
    if not coherent_gain > 0.0:  # Defensive: impossible once the sample check passes.
        result["frequency_estimation_reason"] = "invalid_window"
        return result
    amplitudes = np.abs(np.fft.rfft(detrended * window)) / coherent_gain
    if amplitudes.size > 2:
        amplitudes[1:-1] *= 2.0
    frequencies = np.fft.rfftfreq(times.size, d=spacing)
    power = amplitudes * amplitudes
    positive = frequencies > 0.0
    unrestricted_peak = int(np.argmax(np.where(positive, power, -np.inf)))
    unrestricted_cycles = float(frequencies[unrestricted_peak] * duration)
    minimum_frequency = FREQUENCY_MIN_CYCLES / duration
    eligible = positive & (frequencies >= minimum_frequency)
    result.update({
        "frequency_minimum_eligible": minimum_frequency,
        "frequency_eligible_bin_count": int(np.count_nonzero(eligible)),
        "frequency_unrestricted_peak": float(frequencies[unrestricted_peak]),
        "frequency_unrestricted_peak_cycles": unrestricted_cycles,
    })
    if not np.any(eligible):
        result["frequency_estimation_reason"] = "no_frequency_with_required_cycles"
        return result
    if unrestricted_cycles < FREQUENCY_MIN_CYCLES:
        result["frequency_estimation_reason"] = "dominant_signal_has_insufficient_cycles"
        return result

    peak = int(np.argmax(np.where(eligible, power, -np.inf)))
    eligible_power = power[eligible]
    peak_power = float(power[peak])
    peak_power_fraction = peak_power / max(float(np.sum(eligible_power)), np.finfo(float).tiny)
    background = eligible_power[eligible_power < peak_power]
    background_median = float(np.median(background)) if background.size else 0.0
    peak_prominence = peak_power / max(background_median, np.finfo(float).tiny)
    frequency = float(frequencies[peak])
    cycles = frequency * duration
    result.update({
        "dominant_frequency": frequency,
        "frequency_peak_cycles": cycles,
        "frequency_peak_amplitude": float(amplitudes[peak]),
        "frequency_peak_power_fraction": peak_power_fraction,
        "frequency_peak_prominence": peak_prominence,
        "frequency_peak_power": peak_power,
    })
    if peak_power_fraction < FREQUENCY_MIN_PEAK_POWER_FRACTION:
        result["frequency_estimation_reason"] = "diffuse_spectrum"
        return result
    if peak_prominence < FREQUENCY_MIN_PEAK_PROMINENCE:
        result["frequency_estimation_reason"] = "insufficient_peak_prominence"
        return result

    length = positive_number(reference_length)
    velocity = positive_number(freestream_velocity)
    if length is None or velocity is None:
        result["frequency_estimation_reason"] = "invalid_strouhal_reference_scale"
        return result
    result.update({
        "frequency_estimation_valid": True,
        "frequency_estimation_reason": "accepted",
        "strouhal_number": frequency * length / velocity,
        "strouhal_reference_length": length,
        "strouhal_freestream_velocity": velocity,
    })
    return result


def parse_ascii_vtu(path: Path) -> Field:
    root = ET.parse(path).getroot()
    piece = root.find(".//Piece")
    if piece is None:
        raise ValueError(f"VTU contains no Piece: {path}")

    point_array = piece.find("./Points/DataArray")
    if point_array is None or not point_array.text:
        raise ValueError(f"VTU contains no ASCII points: {path}")
    points = np.fromstring(point_array.text, sep=" ", dtype=float).reshape((-1, 3))[:, :2]

    arrays: dict[str, np.ndarray] = {}
    cells_node = piece.find("./Cells")
    if cells_node is None:
        raise ValueError(f"VTU contains no Cells: {path}")
    for element in cells_node.findall("DataArray"):
        name = element.attrib.get("Name", "")
        arrays[name] = np.fromstring(element.text or "", sep=" ", dtype=np.int64)
    connectivity = arrays["connectivity"]
    offsets = arrays["offsets"]
    cells: list[np.ndarray] = []
    begin = 0
    for end in offsets:
        cells.append(connectivity[begin:int(end)].copy())
        begin = int(end)

    data: dict[str, np.ndarray] = {}
    cell_data = piece.find("./CellData")
    if cell_data is None:
        raise ValueError(f"VTU contains no CellData: {path}")
    for element in cell_data.findall("DataArray"):
        name = element.attrib.get("Name", "")
        components = int(element.attrib.get("NumberOfComponents", "1"))
        values = np.fromstring(element.text or "", sep=" ", dtype=float)
        if components > 1:
            values = values.reshape((-1, components))
        data[name] = values
    return Field(points=points, cells=cells, data=data)


def triangles_and_parent(cells: list[np.ndarray]) -> tuple[np.ndarray, np.ndarray]:
    triangles: list[list[int]] = []
    parents: list[int] = []
    for cell_id, nodes in enumerate(cells):
        if len(nodes) == 3:
            triangles.append([int(nodes[0]), int(nodes[1]), int(nodes[2])])
            parents.append(cell_id)
        elif len(nodes) == 4:
            triangles.append([int(nodes[0]), int(nodes[1]), int(nodes[2])])
            triangles.append([int(nodes[0]), int(nodes[2]), int(nodes[3])])
            parents.extend((cell_id, cell_id))
        else:
            raise ValueError(f"unsupported output polygon with {len(nodes)} nodes")
    return np.asarray(triangles, dtype=np.int64), np.asarray(parents, dtype=np.int64)


def body_view(surface: np.ndarray) -> tuple[tuple[float, float], tuple[float, float], bool]:
    x = np.asarray(surface["x"], dtype=float)
    y = np.asarray(surface["y"], dtype=float)
    xmin, xmax = float(x.min()), float(x.max())
    ymin, ymax = float(y.min()), float(y.max())
    width = max(xmax - xmin, 1.0e-8)
    height = max(ymax - ymin, 1.0e-8)
    slender = width / height > 2.2
    if slender:
        return ((xmin - 0.18 * width, xmax + 0.25 * width),
                (-0.55 * width, 0.55 * width), True)
    diameter = max(width, height)
    return ((xmin - 1.0 * diameter, xmax + 6.0 * diameter),
            (-2.2 * diameter, 2.2 * diameter), False)


def field_centers(field: Field) -> np.ndarray:
    return np.asarray([field.points[nodes].mean(axis=0) for nodes in field.cells])


def robust_limits(values: np.ndarray, mask: np.ndarray | None = None) -> tuple[float, float]:
    selected = values[mask] if mask is not None and np.any(mask) else values
    selected = selected[np.isfinite(selected)]
    if selected.size == 0:
        return (0.0, 1.0)
    lower, upper = np.percentile(selected, (1.0, 99.0))
    if not upper > lower:
        scale = max(abs(float(lower)), 1.0)
        lower, upper = float(lower) - 1.0e-6 * scale, float(upper) + 1.0e-6 * scale
    return float(lower), float(upper)


def relative_span(values: np.ndarray) -> float:
    """Return a scale-aware range that is meaningful for nondimensional data."""
    values = np.asarray(values, dtype=float)
    if values.size == 0 or not np.all(np.isfinite(values)):
        return float("nan")
    return float(np.ptp(values) / max(1.0, float(np.max(np.abs(values)))))


def nearly_equal(left: float, right: float, *, relative: float = 1.0e-8) -> bool:
    return abs(left - right) <= relative * max(1.0, abs(left), abs(right))


def nondecreasing(values: np.ndarray, *, tolerance: float = 1.0e-12) -> bool:
    values = np.asarray(values, dtype=float)
    return bool(values.size > 0 and np.all(np.isfinite(values)) and
                np.all(np.diff(values) >= -tolerance))


def add_manifest(entries: list[dict[str, str]], figure: Path, case_id: str,
                 figure_type: str, variable: str, source: Path, caption: str) -> None:
    entries.append(
        {
            "figure_file": figure.name,
            "case_id": case_id,
            "figure_type": figure_type,
            "variable": variable,
            "source_file": str(source),
            "caption": caption,
        }
    )


def save_line_figures(case_dir: Path, figure_dir: Path, case_id: str,
                      entries: list[dict[str, str]]) -> None:
    residuals = structured_csv(case_dir / "residuals.csv")
    forces = structured_csv(case_dir / "forces.csv")
    surface = structured_csv(case_dir / "surface.csv")

    x_res = np.asarray(residuals["physical_time"], float)
    xlabel_res = r"Physical time, $tU_\infty/L$"
    if np.ptp(x_res) <= 1.0e-14:
        x_res = np.asarray(residuals["step"], float)
        xlabel_res = "Pseudo-time step"
    fig, ax = plt.subplots(figsize=(5.6, 3.35))
    residual_floor = 1.0e-300
    ax.semilogy(x_res, np.maximum(np.asarray(residuals["residual_l2"], float), residual_floor),
                color="#173f5f", label=r"total $L_2$")
    for name, label, color in (("rho", r"$\rho$", "#3caea3"),
                               ("rhou", r"$\rho u$", "#f6d55c"),
                               ("rhov", r"$\rho v$", "#ed553b"),
                               ("rhoE", r"$\rho E$", "#7b2cbf")):
        ax.semilogy(x_res, np.maximum(np.asarray(residuals[name], float), residual_floor),
                    color=color, alpha=0.8, label=label)
    ax.set(xlabel=xlabel_res, ylabel="Global residual norm", title=case_id.replace("_", " "))
    ax.legend(ncol=3, frameon=False)
    fig_path = figure_dir / f"{case_id}_residual_history.png"
    fig.savefig(fig_path)
    plt.close(fig)
    add_manifest(entries, fig_path, case_id, "line_history", "residual_l2",
                 case_dir / "residuals.csv", "Global MPI-reduced conservative residual history.")

    x_force = np.asarray(forces["physical_time"], float)
    xlabel_force = r"Physical time, $tU_\infty/L$"
    if np.ptp(x_force) <= 1.0e-14:
        x_force = np.asarray(forces["step"], float)
        xlabel_force = "Pseudo-time step"
    cd = np.asarray(forces["cd"], float)
    cl = np.asarray(forces["cl"], float)
    if "re200" in case_id.lower():
        # The Re200 trace is a physical-time statistical observable, not a
        # pseudo-time convergence history, so retain its single full-history
        # view for shedding analysis.
        fig, ax = plt.subplots(figsize=(5.6, 3.35))
        ax.plot(x_force, cd, color="#d1495b", label=r"$C_D$")
        ax.plot(x_force, cl, color="#00798c", label=r"$C_L$")
        ax.set(xlabel=xlabel_force, ylabel="Force coefficient",
               title=case_id.replace("_", " "))
        ax.legend(frameon=False)
    else:
        # Retain the complete startup trace as evidence, but also resolve the
        # terminal force window.  In particular, an initial viscous-drag
        # overshoot can be hundreds of times larger than the converged value.
        tail = force_tail_slice(case_id, len(forces))
        tail_count = len(cd[tail])
        fig, axes = plt.subplots(1, 2, figsize=(8.5, 3.35), constrained_layout=True)
        full_ax, terminal_ax = axes
        full_ax.plot(x_force, cd, color="#d1495b", label=r"$C_D$")
        full_ax.plot(x_force, cl, color="#00798c", label=r"$C_L$")
        full_ax.set(xlabel=xlabel_force, ylabel="Force coefficient")
        full_ax.legend(frameon=False)
        full_ax.set_title("Full history")

        # The dual terminal ordinates make both coefficients readable when a
        # small symmetric-airfoil lift accompanies an order-of-magnitude
        # larger drag coefficient.  The complete, shared-axis trace remains
        # at left, so no startup behavior is suppressed.
        cd_line, = terminal_ax.plot(x_force[tail], cd[tail], color="#d1495b", label=r"$C_D$")
        terminal_ax.set(xlabel=xlabel_force, ylabel=r"Drag coefficient, $C_D$")
        terminal_ax.tick_params(axis="y", colors="#d1495b")
        terminal_ax.yaxis.label.set_color("#d1495b")
        terminal_cl_ax = terminal_ax.twinx()
        cl_line, = terminal_cl_ax.plot(x_force[tail], cl[tail], color="#00798c", label=r"$C_L$")
        terminal_cl_ax.set_ylabel(r"Lift coefficient, $C_L$")
        terminal_cl_ax.tick_params(axis="y", colors="#00798c")
        terminal_cl_ax.yaxis.label.set_color("#00798c")
        terminal_ax.legend((cd_line, cl_line), (r"$C_D$", r"$C_L$"), frameon=False)
        terminal_ax.set_title(f"Terminal convergence ({tail_count} samples)")
        fig.suptitle(case_id.replace("_", " "))
    fig_path = figure_dir / f"{case_id}_force_history.png"
    fig.savefig(fig_path)
    plt.close(fig)
    add_manifest(entries, fig_path, case_id, "line_history", "cl, cd",
                 case_dir / "forces.csv", force_history_caption(case_id, len(forces)))

    x = np.asarray(surface["x"], float)
    y = np.asarray(surface["y"], float)
    cp = np.asarray(surface["cp"], float)
    view_x, view_y, slender = body_view(surface)
    fig, ax = plt.subplots(figsize=(5.6, 3.35))
    if slender:
        median_y = float(np.median(y))
        upper = y >= median_y
        lower = ~upper
        for selection, label, color in ((upper, "upper", "#00798c"),
                                        (lower, "lower", "#d1495b")):
            order = np.argsort(x[selection])
            ax.plot(x[selection][order], cp[selection][order], color=color, label=label)
        ax.set_xlabel(r"$x/L$")
        ax.invert_yaxis()
    else:
        theta = np.unwrap(np.arctan2(y - 0.5 * (view_y[0] + view_y[1]),
                                     x - 0.5 * (x.min() + x.max())))
        order = np.argsort(theta)
        ax.plot(np.degrees(theta[order]), cp[order], color="#00798c")
        ax.set_xlabel(r"Surface angle, $\theta$ [deg]")
    ax.set_ylabel(r"Pressure coefficient, $C_p$")
    ax.set_title(case_id.replace("_", " "))
    if slender:
        ax.legend(frameon=False)
    fig_path = figure_dir / f"{case_id}_surface_cp.png"
    fig.savefig(fig_path)
    plt.close(fig)
    add_manifest(entries, fig_path, case_id, "surface_distribution", "cp",
                 case_dir / "surface.csv", "Computed wall pressure-coefficient distribution.")

    if "inviscid" not in case_id:
        cf = np.asarray(surface["cf"], float)
        fig, ax = plt.subplots(figsize=(5.6, 3.35))
        if slender:
            median_y = float(np.median(y))
            upper = y >= median_y
            lower = ~upper
            for selection, label, color in ((upper, "upper", "#00798c"),
                                            (lower, "lower", "#d1495b")):
                order = np.argsort(x[selection])
                ax.plot(x[selection][order], cf[selection][order],
                        color=color, label=label)
            ax.set_xlabel(r"$x/L$")
            ax.legend(frameon=False)
        else:
            theta = np.unwrap(np.arctan2(y - 0.5 * (view_y[0] + view_y[1]),
                                         x - 0.5 * (x.min() + x.max())))
            order = np.argsort(theta)
            ax.plot(np.degrees(theta[order]), cf[order], color="#00798c")
            ax.set_xlabel(r"Surface angle, $\theta$ [deg]")
        ax.set_ylabel(r"Skin-friction coefficient, $C_f$")
        ax.set_title(case_id.replace("_", " "))
        fig_path = figure_dir / f"{case_id}_surface_cf.png"
        fig.savefig(fig_path)
        plt.close(fig)
        add_manifest(entries, fig_path, case_id, "surface_distribution", "cf",
                     case_dir / "surface.csv",
                     "Computed tangential wall skin-friction-coefficient distribution.")


def save_field_figure(field: Field, surface: np.ndarray, case_dir: Path, figure_dir: Path,
                      case_id: str, variable: str, label: str,
                      entries: list[dict[str, str]], fixed_limits: tuple[float, float] | None = None) -> None:
    if variable not in field.data:
        raise KeyError(f"field {case_dir} lacks {variable}")
    values = np.asarray(field.data[variable], dtype=float)
    if values.ndim != 1:
        raise ValueError(f"field variable {variable} is not scalar")
    triangles, parents = triangles_and_parent(field.cells)
    triangulation = mtri.Triangulation(field.points[:, 0], field.points[:, 1], triangles)
    tri_values = values[parents]
    xlim, ylim, _ = body_view(surface)
    centers = field_centers(field)
    in_view = ((centers[:, 0] >= xlim[0]) & (centers[:, 0] <= xlim[1]) &
               (centers[:, 1] >= ylim[0]) & (centers[:, 1] <= ylim[1]))
    vmin, vmax = fixed_limits if fixed_limits is not None else robust_limits(values, in_view)
    clipped = np.clip(tri_values, vmin, vmax)

    fig, ax = plt.subplots(figsize=(7.1, 3.7))
    collection = ax.tripcolor(triangulation, facecolors=clipped, shading="flat",
                              cmap="turbo", vmin=vmin, vmax=vmax, rasterized=True)
    ax.set(xlim=xlim, ylim=ylim, xlabel=r"$x/L$", ylabel=r"$y/L$",
           title=case_id.replace("_", " "))
    ax.set_aspect("equal", adjustable="box")
    colorbar = fig.colorbar(collection, ax=ax, pad=0.02)
    colorbar.set_label(label)
    fig_path = figure_dir / f"{case_id}_{variable}.png"
    fig.savefig(fig_path)
    plt.close(fig)
    caption = f"Near-body computed {label}; displayed range [{vmin:.5g}, {vmax:.5g}]."
    add_manifest(entries, fig_path, case_id, "filled_unstructured_field", variable,
                 case_dir / "field_final.vtu", caption)


def case_statistics(case_dir: Path, field: Field, surface: np.ndarray) -> dict[str, object]:
    metadata = json.loads((case_dir / "metadata.json").read_text())
    status = json.loads((case_dir / "run_status.json").read_text())
    residuals = structured_csv(case_dir / "residuals.csv")
    forces = structured_csv(case_dir / "forces.csv")
    rho = np.asarray(field.data["density"], float)
    pressure = np.asarray(field.data["pressure"], float)
    mach = np.asarray(field.data["mach"], float)
    cl = np.asarray(forces["cl"], float)
    cd = np.asarray(forces["cd"], float)
    times = np.asarray(forces["physical_time"], float)
    tail = force_tail_slice(str(metadata["case_id"]), len(forces))
    first_residual = max(float(np.asarray(residuals["residual_l2"], float)[0]), 1.0e-300)
    last_residual = max(float(np.asarray(residuals["residual_l2"], float)[-1]), 1.0e-300)
    result: dict[str, object] = {
        "case_id": metadata["case_id"],
        "convergence_status": status["convergence_status"],
        "mpi_ranks": int(metadata["mpi_ranks"]),
        "final_step": int(status["final_step"]),
        "final_physical_time": float(status["final_physical_time"]),
        "wall_time_seconds": float(status["wall_time_seconds"]),
        "residual_reduction_orders_measured": math.log10(first_residual / last_residual),
        "final_cl": float(cl[-1]),
        "final_cd": float(cd[-1]),
        "mean_cl_tail": float(np.mean(cl[tail])),
        "mean_cd_tail": float(np.mean(cd[tail])),
        "lift_amplitude_tail": 0.5 * float(np.ptp(cl[tail])),
        "min_density": float(np.min(rho)),
        "min_pressure": float(np.min(pressure)),
        "surface_cp_range": float(np.ptp(np.asarray(surface["cp"], float))),
        "field_pressure_relative_span": relative_span(pressure),
        "field_mach_relative_span": relative_span(mach),
        "tail_force_samples": int(len(forces[tail])),
        "tail_physical_time_span": float(times[tail][-1] - times[tail][0]),
    }
    if "re200" in metadata["case_id"].lower():
        length, velocity, source = strouhal_reference_scales(str(metadata["case_id"]), metadata)
        frequency_statistics = dominant_frequency_estimate(
            times[tail], cl[tail], reference_length=length, freestream_velocity=velocity
        )
        frequency_statistics["strouhal_reference_source"] = source
        result.update(frequency_statistics)
    return result


def sanity_for_case(case_dir: Path, field: Field, surface: np.ndarray,
                    statistics: dict[str, object]) -> dict[str, object]:
    metadata = json.loads((case_dir / "metadata.json").read_text())
    status = json.loads((case_dir / "run_status.json").read_text())
    residuals = structured_csv(case_dir / "residuals.csv")
    forces = structured_csv(case_dir / "forces.csv")
    case_id = str(metadata["case_id"])
    is_inviscid = "inviscid" in case_id
    is_re200 = "re200" in case_id
    surface_u = np.asarray(surface["u"], float)
    surface_v = np.asarray(surface["v"], float)
    surface_nx = np.asarray(surface["nx"], float)
    surface_ny = np.asarray(surface["ny"], float)
    velocity = np.hypot(surface_u, surface_v)
    normal_velocity = surface_u * surface_nx + surface_v * surface_ny
    tangential_velocity = -surface_u * surface_ny + surface_v * surface_nx
    required_fields_present = all(name in field.data for name in REQUIRED_FIELD_DATA)
    field_finite = required_fields_present and all(
        np.all(np.isfinite(np.asarray(field.data[name], dtype=float)))
        for name in REQUIRED_FIELD_DATA
    )
    field_size_consistent = required_fields_present and all(
        len(np.asarray(field.data[name])) == len(field.cells) for name in REQUIRED_FIELD_DATA
    )
    force_steps = np.asarray(forces["step"], dtype=float)
    residual_steps = np.asarray(residuals["step"], dtype=float)
    force_times = np.asarray(forces["physical_time"], dtype=float)
    residual_times = np.asarray(residuals["physical_time"], dtype=float)
    final_step = int(status.get("final_step", -1))
    final_time = float(status.get("final_physical_time", float("nan")))
    final_force_matches_status = (
        int(force_steps[-1]) == final_step and nearly_equal(float(force_times[-1]), final_time)
    )
    residual_does_not_exceed_status = int(residual_steps[-1]) <= final_step
    residual_time_does_not_exceed_status = float(residual_times[-1]) <= final_time + 1.0e-8 * max(1.0, abs(final_time))
    field_density = np.asarray(field.data["density"], dtype=float) if "density" in field.data else np.array([])
    field_pressure = np.asarray(field.data["pressure"], dtype=float) if "pressure" in field.data else np.array([])
    cf = np.asarray(surface["cf"], float)
    finite_residual_history = all(
        np.all(np.isfinite(np.asarray(residuals[name], dtype=float))) for name in residuals.dtype.names or ()
    )
    finite_force_history = all(
        np.all(np.isfinite(np.asarray(forces[name], dtype=float))) for name in forces.dtype.names or ()
    )
    finite_surface = all(
        np.all(np.isfinite(np.asarray(surface[name], dtype=float)))
        for name in surface.dtype.names or () if name != "tag"
    )
    checks: dict[str, bool] = {
        "required_field_data": required_fields_present,
        "finite_field_data": bool(field_finite),
        "field_cell_data_size_consistent": bool(field_size_consistent),
        "positive_density": bool(field_density.size and np.all(field_density > 0.0)),
        "positive_pressure": bool(field_pressure.size and np.all(field_pressure > 0.0)),
        "positive_surface_density_pressure": bool(
            np.all(np.asarray(surface["rho"], dtype=float) > 0.0) and
            np.all(np.asarray(surface["pressure"], dtype=float) > 0.0)
        ),
        "finite_residual_history": bool(finite_residual_history),
        "finite_force_history": bool(finite_force_history),
        "finite_surface_data": bool(finite_surface),
        "nontrivial_surface_cp": bool(float(statistics["surface_cp_range"]) > 1.0e-7),
        "nontrivial_field_pressure": bool(float(statistics["field_pressure_relative_span"]) > 1.0e-8),
        "nontrivial_field_mach": bool(float(statistics["field_mach_relative_span"]) > 1.0e-8),
        "completed_status": metadata.get("completed") is True and
                            metadata.get("convergence_status") in {"converged", "statistically_periodic"},
        "metadata_status_alignment": (
            metadata.get("case_id") == status.get("case_id") == case_id and
            metadata.get("convergence_status") == status.get("convergence_status") and
            int(metadata.get("mpi_ranks", -1)) == int(status.get("mpi_ranks", -2))
        ),
        "history_steps_nondecreasing": nondecreasing(force_steps) and nondecreasing(residual_steps),
        "history_times_nondecreasing": nondecreasing(force_times) and nondecreasing(residual_times),
        "final_force_status_alignment": final_force_matches_status,
        "residual_not_after_final_status": residual_does_not_exceed_status,
        "residual_time_not_after_final_status": residual_time_does_not_exceed_status,
    }
    if "naca" in case_id:
        checks["near_symmetric_lift"] = abs(float(statistics["final_cl"])) < 0.08
        checks["nontrivial_drag"] = abs(float(statistics["final_cd"])) > 1.0e-8
    if "cylinder" in case_id:
        checks["positive_tail_mean_drag"] = float(statistics["mean_cd_tail"]) > 0.0
    if is_re200:
        checks["unsteady_tail_lift"] = float(statistics["lift_amplitude_tail"]) > 1.0e-5
        checks["statistically_periodic_status"] = metadata.get("convergence_status") == "statistically_periodic"
        checks["production_time_horizon"] = final_time >= 300.0 - 1.0e-8 and final_step >= 30000
        checks["physical_time_force_sampling"] = bool(np.any(np.diff(force_times) > 0.0))
        checks["post_transient_sampling_depth"] = (
            int(statistics["tail_force_samples"]) >= 100 and
            float(statistics["tail_physical_time_span"]) >= 50.0
        )
        checks["dominant_lift_frequency"] = (
            statistics.get("frequency_estimation_valid") is True and
            float(statistics.get("dominant_frequency", 0.0)) > 0.0 and
            float(statistics.get("strouhal_number", 0.0)) > 0.0
        )
        checks["resolved_shedding_cycles"] = (
            float(statistics.get("frequency_peak_cycles", 0.0)) >= FREQUENCY_MIN_CYCLES
        )
        checks["dominant_frequency_peak_quality"] = (
            float(statistics.get("frequency_peak_power_fraction", 0.0)) >=
            FREQUENCY_MIN_PEAK_POWER_FRACTION and
            float(statistics.get("frequency_peak_prominence", 0.0)) >=
            FREQUENCY_MIN_PEAK_PROMINENCE
        )
        checks["true_bdf2_inner_loop"] = metadata.get("true_bdf2_inner_loop") is True
        checks["inner_target_attainment"] = (
            float(metadata.get("inner_residual_reduction_target", float("inf"))) <= 1.0e-3 and
            float(metadata.get("inner_target_converged_fraction", 0.0)) >= 0.95 and
            int(metadata.get("observed_min_inner_iterations", 0)) >= 5 and
            int(metadata.get("observed_max_inner_iterations", 1001)) <= 1000
        )
    if is_inviscid:
        checks["slip_normal_velocity"] = float(np.max(np.abs(normal_velocity))) < 2.0e-5
        checks["slip_tangential_velocity_nontrivial"] = float(np.max(np.abs(tangential_velocity))) > 1.0e-5
        checks["zero_viscous_forces"] = (
            float(np.max(np.abs(np.asarray(forces["viscous_drag"], float)))) < 1.0e-8 and
            float(np.max(np.abs(np.asarray(forces["viscous_lift"], float)))) < 1.0e-8
        )
    else:
        checks["no_slip_wall_velocity"] = float(np.max(velocity)) < 2.0e-5
        checks["skin_friction_finite"] = bool(np.all(np.isfinite(cf)))
        checks["nonzero_skin_friction_evidence"] = float(np.max(np.abs(cf))) > 1.0e-10
    return {
        "case_id": case_id,
        "checks": checks,
        "passed": all(checks.values()),
        "metrics": {
            "max_wall_speed": float(np.max(velocity)),
            "max_wall_normal_speed": float(np.max(np.abs(normal_velocity))),
            "max_wall_tangential_speed": float(np.max(np.abs(tangential_velocity))),
            "max_abs_skin_friction_coefficient": float(np.max(np.abs(cf))),
            "field_cell_count": int(len(field.cells)),
            "final_force_step": int(force_steps[-1]),
            "final_residual_step": int(residual_steps[-1]),
            "final_force_time": float(force_times[-1]),
            "final_residual_time": float(residual_times[-1]),
            **statistics,
        },
    }


def process_case(case_dir: Path, figure_dir: Path,
                 entries: list[dict[str, str]]) -> tuple[dict[str, object], dict[str, object]]:
    metadata = json.loads((case_dir / "metadata.json").read_text())
    case_id = str(metadata["case_id"])
    if case_dir.name != case_id:
        raise ValueError(f"case directory/name mismatch: {case_dir.name} != {case_id}")
    surface = structured_csv(case_dir / "surface.csv")
    field = parse_ascii_vtu(case_dir / "field_final.vtu")
    save_line_figures(case_dir, figure_dir, case_id, entries)
    save_field_figure(field, surface, case_dir, figure_dir, case_id,
                      "mach", r"Mach number, $M$", entries)
    save_field_figure(field, surface, case_dir, figure_dir, case_id,
                      "pressure", r"Nondimensional pressure, $p$", entries)
    if "cylinder" in case_id:
        save_field_figure(field, surface, case_dir, figure_dir, case_id,
                          "velocity_magnitude", r"Velocity magnitude, $|\mathbf{u}|/U_\infty$", entries)
        if "vorticity" in field.data:
            fixed = (-5.0, 5.0) if "re200" in case_id else None
            save_field_figure(field, surface, case_dir, figure_dir, case_id,
                              "vorticity", r"Nondimensional vorticity, $\omega_z L/U_\infty$",
                              entries, fixed_limits=fixed)
    statistics = case_statistics(case_dir, field, surface)
    return statistics, sanity_for_case(case_dir, field, surface, statistics)


def comparison_record(case_dir: Path, expected_case_id: str) -> dict[str, object]:
    """Read only the traceable terminal quantities needed for MPI consistency."""
    metadata = json.loads((case_dir / "metadata.json").read_text())
    status = json.loads((case_dir / "run_status.json").read_text())
    forces = structured_csv(case_dir / "forces.csv")
    residuals = structured_csv(case_dir / "residuals.csv")
    first = max(float(np.asarray(residuals["residual_l2"], float)[0]), 1.0e-300)
    last = max(float(np.asarray(residuals["residual_l2"], float)[-1]), 1.0e-300)
    final_step = int(status.get("final_step", -1))
    return {
        "output_directory": str(case_dir),
        "mpi_ranks": int(metadata.get("mpi_ranks", -1)),
        "case_id_matches": metadata.get("case_id") == status.get("case_id") == expected_case_id,
        "completed": metadata.get("completed") is True and
                     metadata.get("convergence_status") in {"converged", "statistically_periodic"} and
                     status.get("convergence_status") == metadata.get("convergence_status"),
        "final_force_matches_status": (
            int(float(forces["step"][-1])) == final_step and
            nearly_equal(float(forces["physical_time"][-1]), float(status.get("final_physical_time", float("nan"))))
        ),
        "final_cd": float(forces["cd"][-1]),
        "final_cl": float(forces["cl"][-1]),
        "residual_reduction_orders": math.log10(first / last),
        "wall_time_seconds": float(status.get("wall_time_seconds", float("nan"))),
    }


def rank_comparison_evidence(results_root: Path, selected_cases: list[str],
                             required: bool) -> dict[str, object]:
    """Collect rank-count reproducibility evidence without inventing absent data."""
    requested = [case_id for case_id in RANK_COMPARISON_CASES if case_id in selected_cases]
    records: list[dict[str, object]] = []
    for case_id in requested:
        candidates = [results_root / case_id]
        comparison_root = results_root / "rank_comparisons"
        candidates.extend(sorted(comparison_root.glob(f"{case_id}_np*")))
        runs: list[dict[str, object]] = []
        read_errors: list[str] = []
        for candidate in candidates:
            if not candidate.is_dir() or not (candidate / "metadata.json").exists():
                continue
            try:
                runs.append(comparison_record(candidate, case_id))
            except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
                read_errors.append(f"{candidate}: {error}")
        # A duplicate rank is not additional reproducibility evidence, but it
        # is retained in the JSON for auditability.
        rank_counts = sorted({int(run["mpi_ranks"]) for run in runs if int(run["mpi_ranks"]) > 0})
        cds = np.asarray([float(run["final_cd"]) for run in runs], dtype=float)
        cls = np.asarray([float(run["final_cl"]) for run in runs], dtype=float)
        cd_spread = relative_span(cds)
        cl_spread = relative_span(cls)
        checks = {
            "two_or_more_rank_counts": len(rank_counts) >= 2,
            "all_case_ids_align": bool(runs) and all(bool(run["case_id_matches"]) for run in runs),
            "all_runs_completed": bool(runs) and all(bool(run["completed"]) for run in runs),
            "all_final_forces_align_with_status": bool(runs) and all(
                bool(run["final_force_matches_status"]) for run in runs
            ),
            # This tolerance deliberately allows normal nonlinear stopping
            # differences while rejecting order-one rank-dependent forces.
            "force_consistency": bool(runs) and np.isfinite(cd_spread) and np.isfinite(cl_spread) and
                                 cd_spread <= 5.0e-2 and cl_spread <= 5.0e-2,
            "no_read_errors": not read_errors,
        }
        records.append(
            {
                "case_id": case_id,
                "runs": runs,
                "read_errors": read_errors,
                "checks": checks,
                "passed": all(checks.values()),
                "metrics": {
                    "rank_counts": rank_counts,
                    "relative_cd_spread": cd_spread,
                    "relative_cl_spread": cl_spread,
                },
            }
        )
    return {
        "required": required,
        "available_for_cases": requested,
        "cases": records,
        "passed": (bool(records) and
                   all(bool(record["passed"]) for record in records))
                  if required else True,
    }


def write_manifest(path: Path, entries: list[dict[str, str]]) -> None:
    columns = ("figure_file", "case_id", "figure_type", "variable", "source_file", "caption")
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        writer.writerows(entries)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=Path("results"))
    parser.add_argument("--report", type=Path, default=Path("report"))
    parser.add_argument("--cases", nargs="*", default=list(REQUIRED_CASES))
    parser.add_argument("--require-rank-comparisons", action="store_true",
                        help="fail when the selected final campaign lacks NACA and cylinder rank comparisons")
    args = parser.parse_args()

    configure_style()
    figure_dir = args.report / "figures"
    figure_dir.mkdir(parents=True, exist_ok=True)
    entries: list[dict[str, str]] = []
    statistics: list[dict[str, object]] = []
    sanity: list[dict[str, object]] = []
    for case_id in args.cases:
        case_dir = args.results / case_id
        case_statistics_entry, case_sanity = process_case(case_dir, figure_dir, entries)
        statistics.append(case_statistics_entry)
        sanity.append(case_sanity)
        print(f"processed {case_id}: sanity={'PASS' if case_sanity['passed'] else 'FAIL'}")

    write_manifest(args.report / "figure_manifest.csv", entries)
    (args.report / "result_statistics.json").write_text(json.dumps(statistics, indent=2) + "\n")
    full_campaign = set(REQUIRED_CASES).issubset(set(args.cases))
    rank_evidence = rank_comparison_evidence(
        args.results, args.cases, required=args.require_rank_comparisons or full_campaign
    )
    sanity_document = {
        "all_passed": all(item["passed"] for item in sanity) and bool(rank_evidence["passed"]),
        "cases": sanity,
        "rank_comparisons": rank_evidence,
    }
    (args.report / "sanity_checks.json").write_text(json.dumps(sanity_document, indent=2) + "\n")
    if not sanity_document["all_passed"]:
        raise SystemExit("one or more physics sanity checks failed")


if __name__ == "__main__":
    main()
