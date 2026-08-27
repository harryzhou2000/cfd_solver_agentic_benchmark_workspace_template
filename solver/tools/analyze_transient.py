#!/usr/bin/env python3
"""Post-transient analysis of the cylinder vortex-shedding force history.

Usage::

    python analyze_transient.py --case-dir <results/cylinder_m010_laminar_re200> \
                                --output <report/cylinder_re200_transient.json> \
                                [--figure-dir <figdir>] [--window-fraction 0.4] \
                                [--diameter 1.0] [--velocity 1.0]

Reads forces.csv, selects a post-transient window (by default the last 40 % of
the physical-time history) and reports:

*   mean drag coefficient and mean lift coefficient over the window;
*   lift amplitude as half the peak-to-peak range and as the RMS of the
    mean-removed signal;
*   the dominant shedding frequency from an FFT of the mean-removed lift, using
    the physical_time column to establish the sample interval;
*   the Strouhal number St = f D / U.

Everything is written to a JSON file, a readable summary is printed, and a
spectrum figure (<case_id>_spectrum.png) is produced with the dominant peak
marked.

Conventions chosen here:

*   The window is defined on physical time, not on row count, so a run with a
    variable write interval is still handled correctly.
*   The sample interval is the median difference of the physical_time column.
    If the sampling is materially non-uniform the signal is linearly resampled
    onto a uniform grid before the FFT, and the JSON records that this happened.
*   A Hann window is applied before the FFT to suppress spectral leakage from
    the finite record, and the amplitude spectrum is corrected for the window's
    coherent gain.  The peak frequency is refined by parabolic interpolation on
    the three points around the discrete maximum, which removes most of the
    bias from the finite frequency resolution.
*   The zero-frequency bin is excluded when searching for the dominant peak.
*   A zero-crossing count of the mean-removed lift is reported as an independent
    cross-check of the FFT frequency.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Dict, Optional

import numpy as np

import plot_style as ps
from plot_results import read_table

__all__ = ["analyze_forces", "write_spectrum_figure"]


def _dominant_frequency(
    freqs: np.ndarray, amplitude: np.ndarray
) -> tuple[float, int, float]:
    """Locate the dominant spectral peak, refined by parabolic interpolation.

    Returns (frequency, index_of_discrete_maximum, amplitude_at_peak).  The
    zero-frequency bin is skipped so that a non-zero mean cannot masquerade as
    the shedding peak.
    """
    if amplitude.size < 2:
        return (float("nan"), 0, float("nan"))
    search = amplitude.copy()
    search[0] = 0.0  # ignore DC
    index = int(np.argmax(search))
    freq = float(freqs[index])
    peak_amp = float(amplitude[index])

    # Parabolic (three-point) refinement in the frequency direction.
    if 0 < index < amplitude.size - 1:
        y0, y1, y2 = (
            float(amplitude[index - 1]),
            float(amplitude[index]),
            float(amplitude[index + 1]),
        )
        denominator = y0 - 2.0 * y1 + y2
        if abs(denominator) > 1.0e-300:
            delta = 0.5 * (y0 - y2) / denominator
            if abs(delta) <= 1.0:
                spacing = float(freqs[1] - freqs[0])
                freq = float(freqs[index]) + delta * spacing
                peak_amp = y1 - 0.25 * (y0 - y2) * delta
    return (freq, index, peak_amp)


def _zero_crossing_frequency(time: np.ndarray, signal: np.ndarray) -> float:
    """Estimate frequency from the mean-removed signal's zero crossings.

    Two crossings make one period, so f = (crossings - 1) / (2 * elapsed).
    Returns NaN when there are too few crossings to be meaningful.
    """
    sign = np.sign(signal)
    sign[sign == 0.0] = 1.0
    crossings = np.flatnonzero(np.diff(sign) != 0.0)
    if crossings.size < 3:
        return float("nan")
    # Linearly interpolate each crossing instant for a sharper estimate.
    times = []
    for i in crossings:
        y0, y1 = signal[i], signal[i + 1]
        if y1 == y0:
            times.append(time[i])
        else:
            frac = -y0 / (y1 - y0)
            times.append(time[i] + frac * (time[i + 1] - time[i]))
    times = np.asarray(times, dtype=np.float64)
    half_periods = np.diff(times)
    mean_half = float(np.mean(half_periods))
    if mean_half <= 0.0:
        return float("nan")
    return 0.5 / mean_half


def analyze_forces(
    case_dir,
    window_fraction: float = 0.4,
    diameter: float = 1.0,
    velocity: float = 1.0,
    case_id: Optional[str] = None,
) -> Dict[str, object]:
    """Analyse forces.csv and return a dictionary of transient statistics.

    Parameters
    ----------
    case_dir:
        Solver output directory containing forces.csv.
    window_fraction:
        Fraction of the physical-time record, measured from the end, treated as
        post-transient.  0.4 keeps the last 40 %.
    diameter, velocity:
        Reference length D and freestream speed U used to form the Strouhal
        number St = f D / U.
    case_id:
        Case identifier for labelling; taken from metadata.json when omitted.
    """
    case_dir = Path(case_dir)
    forces = read_table(case_dir / "forces.csv")
    if len(forces) < 8:
        raise ValueError(
            "forces.csv in %s has only %d rows; too short for transient analysis"
            % (case_dir, len(forces))
        )
    if not (0.0 < window_fraction <= 1.0):
        raise ValueError("--window-fraction must lie in (0, 1], got %r" % window_fraction)

    if case_id is None:
        metadata_path = case_dir / "metadata.json"
        if metadata_path.exists():
            try:
                case_id = str(json.loads(metadata_path.read_text()).get("case_id") or case_dir.name)
            except json.JSONDecodeError:
                case_id = case_dir.name
        else:
            case_id = case_dir.name

    steps = forces.column("step", np.arange(1, len(forces) + 1, dtype=np.float64))
    has_time = forces.varies("physical_time")
    if has_time:
        time = forces.column("physical_time")
    else:
        # Steady-style history: fall back to the step index as a pseudo time so
        # the tool still produces a result, and record that in the output.
        time = steps.astype(np.float64)
    cl_all = forces.column("cl")
    cd_all = forces.column("cd")

    finite = np.isfinite(time) & np.isfinite(cl_all) & np.isfinite(cd_all)
    time, cl_all, cd_all, steps = time[finite], cl_all[finite], cd_all[finite], steps[finite]
    order = np.argsort(time, kind="stable")
    time, cl_all, cd_all, steps = time[order], cl_all[order], cd_all[order], steps[order]

    t_start_full, t_end = float(time[0]), float(time[-1])
    span = t_end - t_start_full
    window_start = t_end - window_fraction * span
    mask = time >= window_start
    if int(np.count_nonzero(mask)) < 8:
        # Guarantee a usable window even for a very short record.
        keep = max(8, int(0.25 * time.size))
        mask = np.zeros(time.size, dtype=bool)
        mask[-keep:] = True
        window_start = float(time[mask][0])

    t_win = time[mask]
    cl = cl_all[mask]
    cd = cd_all[mask]

    mean_cl = float(np.mean(cl))
    mean_cd = float(np.mean(cd))
    cl_fluct = cl - mean_cl

    cl_amplitude_half_pp = 0.5 * float(np.max(cl) - np.min(cl))
    cl_amplitude_rms = float(np.sqrt(np.mean(cl_fluct ** 2)))
    cd_amplitude_half_pp = 0.5 * float(np.max(cd) - np.min(cd))

    # --- sample interval and (if necessary) uniform resampling --------------
    diffs = np.diff(t_win)
    positive = diffs[diffs > 0.0]
    dt = float(np.median(positive)) if positive.size else float("nan")
    resampled = False
    signal = cl_fluct
    t_uniform = t_win
    if positive.size and np.isfinite(dt) and dt > 0.0:
        # More than 1 % scatter in the interval means the FFT would be biased.
        if float(np.max(positive) - np.min(positive)) > 0.01 * dt:
            n_uniform = int(np.floor((t_win[-1] - t_win[0]) / dt)) + 1
            if n_uniform >= 8:
                t_uniform = t_win[0] + dt * np.arange(n_uniform)
                signal = np.interp(t_uniform, t_win, cl_fluct)
                signal = signal - float(np.mean(signal))
                resampled = True

    # --- FFT of the mean-removed lift --------------------------------------
    n = int(signal.size)
    frequency = float("nan")
    strouhal = float("nan")
    freqs = np.empty(0)
    amplitude = np.empty(0)
    peak_index = 0
    peak_amplitude = float("nan")
    zero_crossing_frequency = float("nan")

    if n >= 8 and np.isfinite(dt) and dt > 0.0:
        window_taper = np.hanning(n)
        coherent_gain = float(np.mean(window_taper))
        spectrum = np.fft.rfft(signal * window_taper)
        freqs = np.fft.rfftfreq(n, d=dt)
        # Single-sided amplitude spectrum, corrected for the taper's gain.
        amplitude = np.abs(spectrum) * (2.0 / n) / max(coherent_gain, 1.0e-300)
        if amplitude.size:
            amplitude[0] *= 0.5  # DC bin is not doubled
        frequency, peak_index, peak_amplitude = _dominant_frequency(freqs, amplitude)
        if np.isfinite(frequency) and velocity > 0.0:
            strouhal = frequency * diameter / velocity
        zero_crossing_frequency = _zero_crossing_frequency(t_uniform, signal)

    shedding_period = 1.0 / frequency if np.isfinite(frequency) and frequency > 0.0 else float("nan")
    cycles = span_window = float(t_win[-1] - t_win[0])
    n_cycles = (
        span_window / shedding_period if np.isfinite(shedding_period) and shedding_period > 0 else float("nan")
    )

    result: Dict[str, object] = {
        "case_id": case_id,
        "case_dir": str(case_dir),
        "source_file": "forces.csv",
        "samples_total": int(time.size),
        "samples_in_window": int(t_win.size),
        "time_column_used": "physical_time" if has_time else "step_index_fallback",
        "record_start_time": t_start_full,
        "record_end_time": t_end,
        "window_fraction": float(window_fraction),
        "window_start_time": float(t_win[0]),
        "window_end_time": float(t_win[-1]),
        "window_start_step": float(steps[mask][0]),
        "window_end_step": float(steps[mask][-1]),
        "window_duration": span_window,
        "sample_interval_dt": dt,
        "uniformly_resampled_for_fft": bool(resampled),
        "mean_cd": mean_cd,
        "mean_cl": mean_cl,
        "cl_amplitude_half_peak_to_peak": cl_amplitude_half_pp,
        "cl_amplitude_rms": cl_amplitude_rms,
        "cd_amplitude_half_peak_to_peak": cd_amplitude_half_pp,
        "cl_min": float(np.min(cl)),
        "cl_max": float(np.max(cl)),
        "cd_min": float(np.min(cd)),
        "cd_max": float(np.max(cd)),
        "shedding_frequency": frequency,
        "shedding_period": shedding_period,
        "shedding_frequency_zero_crossing_check": zero_crossing_frequency,
        "strouhal_number": strouhal,
        "reference_diameter": float(diameter),
        "reference_velocity": float(velocity),
        "cycles_in_window": n_cycles,
        "frequency_resolution": float(freqs[1] - freqs[0]) if freqs.size > 1 else float("nan"),
        "peak_spectral_amplitude": peak_amplitude,
        "fft_window": "hann",
    }
    # Keep the spectrum out of the JSON but hand it to the figure writer.
    result["_spectrum"] = {
        "freqs": freqs,
        "amplitude": amplitude,
        "peak_index": peak_index,
        "signal_time": t_uniform,
        "signal": signal,
    }
    return result


def write_spectrum_figure(result: Dict[str, object], figure_dir) -> Optional[Path]:
    """Write the lift spectrum figure with the dominant peak marked.

    Two stacked panels: the mean-removed lift over the analysis window on top,
    and its single-sided amplitude spectrum below with the shedding peak and the
    Strouhal number annotated.
    """
    import matplotlib.pyplot as plt

    spectrum = result.get("_spectrum") or {}
    freqs = np.asarray(spectrum.get("freqs", []), dtype=np.float64)
    amplitude = np.asarray(spectrum.get("amplitude", []), dtype=np.float64)
    if freqs.size < 2 or amplitude.size < 2:
        return None

    case_id = str(result.get("case_id", "case"))
    frequency = float(result.get("shedding_frequency", float("nan")))
    strouhal = float(result.get("strouhal_number", float("nan")))

    ps.apply_style()
    fig, (ax_signal, ax_spec) = plt.subplots(2, 1, figsize=(7.0, 6.0))

    t_sig = np.asarray(spectrum.get("signal_time", []), dtype=np.float64)
    signal = np.asarray(spectrum.get("signal", []), dtype=np.float64)
    if t_sig.size and signal.size:
        ax_signal.plot(t_sig, signal, color="#0072B2", label=r"$C_L - \overline{C_L}$")
        ax_signal.axhline(0.0, color="0.5", linewidth=0.8, linestyle=":")
    ps.label_axes(ax_signal, "physical time $t$", r"lift fluctuation $C_L'$")
    ax_signal.set_title("%s  |  post-transient lift fluctuation and spectrum" % case_id)
    ps.finalize_line_axes(ax_signal)
    ps.annotate_corner(
        ax_signal,
        "analysis window $t \\in [%.3g, %.3g]$ (last %.0f%% of the record)"
        % (
            float(result.get("window_start_time", 0.0)),
            float(result.get("window_end_time", 0.0)),
            100.0 * float(result.get("window_fraction", 0.0)),
        ),
        loc="lower right",
    )

    ax_spec.plot(freqs, amplitude, color="#D55E00", label="lift amplitude spectrum")
    if np.isfinite(frequency) and frequency > 0.0:
        peak_amp = float(result.get("peak_spectral_amplitude", np.max(amplitude)))
        ax_spec.plot(
            [frequency],
            [peak_amp],
            marker="v",
            markersize=9.0,
            color="#111111",
            linestyle="none",
            label="dominant peak",
            zorder=6,
        )
        ax_spec.axvline(frequency, color="0.4", linewidth=0.9, linestyle="--")
        ax_spec.annotate(
            "$f = %.4f$,  $St = %.4f$" % (frequency, strouhal),
            xy=(frequency, peak_amp),
            xytext=(12.0, -6.0),
            textcoords="offset points",
            fontsize=10,
            bbox=dict(
                boxstyle="round,pad=0.3",
                facecolor="white",
                alpha=0.88,
                edgecolor="0.7",
                linewidth=0.6,
            ),
        )
        # Show a handful of harmonics rather than the whole Nyquist range.
        ax_spec.set_xlim(0.0, min(float(freqs[-1]), 8.0 * frequency))
    ps.label_axes(ax_spec, "frequency $f$", "amplitude of $C_L'$")
    ps.finalize_line_axes(ax_spec)

    figure_dir = Path(figure_dir)
    return ps.save_figure(fig, figure_dir / ("%s_spectrum.png" % case_id))


def print_summary(result: Dict[str, object]) -> None:
    """Print a readable summary of the transient statistics."""
    def value(key: str, default=float("nan")):
        return result.get(key, default)

    print("transient analysis: %s" % result.get("case_id"))
    print("  source                   : %s" % (Path(str(result.get("case_dir"))) / "forces.csv"))
    print(
        "  samples                  : %s in window of %s total"
        % (value("samples_in_window"), value("samples_total"))
    )
    print(
        "  analysis window          : t = [%.4f, %.4f]  (last %.0f%% of the record)"
        % (value("window_start_time"), value("window_end_time"), 100.0 * float(value("window_fraction")))
    )
    print("  steps in window          : %.0f to %.0f" % (value("window_start_step"), value("window_end_step")))
    print("  sample interval dt       : %.6g%s" % (
        float(value("sample_interval_dt")),
        "  (resampled uniformly for the FFT)" if value("uniformly_resampled_for_fft") else "",
    ))
    print("  mean cd                  : %.6f" % float(value("mean_cd")))
    print("  mean cl                  : %.6e" % float(value("mean_cl")))
    print("  cl amplitude (half p-p)  : %.6f" % float(value("cl_amplitude_half_peak_to_peak")))
    print("  cl amplitude (rms)       : %.6f" % float(value("cl_amplitude_rms")))
    print("  cd amplitude (half p-p)  : %.6f" % float(value("cd_amplitude_half_peak_to_peak")))
    print("  shedding frequency f     : %.6f  (period %.4f)" % (
        float(value("shedding_frequency")), float(value("shedding_period"))
    ))
    print("  zero-crossing cross-check: %.6f" % float(value("shedding_frequency_zero_crossing_check")))
    print("  frequency resolution     : %.6g" % float(value("frequency_resolution")))
    print("  cycles in window         : %.2f" % float(value("cycles_in_window")))
    print("  Strouhal St = f D / U    : %.6f   (D = %.4g, U = %.4g)" % (
        float(value("strouhal_number")),
        float(value("reference_diameter")),
        float(value("reference_velocity")),
    ))


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Analyse post-transient vortex shedding from a forces.csv history."
    )
    parser.add_argument("--case-dir", required=True, type=Path, help="solver output directory")
    parser.add_argument("--output", required=True, type=Path, help="JSON file to write")
    parser.add_argument(
        "--figure-dir",
        type=Path,
        default=None,
        help="directory for the spectrum figure (defaults to the JSON file's directory)",
    )
    parser.add_argument(
        "--window-fraction",
        type=float,
        default=0.4,
        help="trailing fraction of the record treated as post-transient (default 0.4)",
    )
    parser.add_argument("--diameter", type=float, default=1.0, help="reference diameter D (default 1)")
    parser.add_argument("--velocity", type=float, default=1.0, help="freestream speed U (default 1)")
    parser.add_argument("--case-id", default=None, help="override the case id used for labelling")
    parser.add_argument("--no-figure", action="store_true", help="skip the spectrum figure")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    result = analyze_forces(
        case_dir=args.case_dir,
        window_fraction=args.window_fraction,
        diameter=args.diameter,
        velocity=args.velocity,
        case_id=args.case_id,
    )

    figure_path = None
    if not args.no_figure:
        figure_dir = args.figure_dir or args.output.parent
        figure_path = write_spectrum_figure(result, figure_dir)

    serialisable = {k: v for k, v in result.items() if not k.startswith("_")}
    serialisable["spectrum_figure"] = str(figure_path) if figure_path else None
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(serialisable, indent=2, sort_keys=True) + "\n")

    print_summary(result)
    print("  json written             : %s" % args.output)
    if figure_path:
        print("  spectrum figure          : %s" % figure_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
