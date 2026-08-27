#!/usr/bin/env python3
"""Vortex-shedding analysis of the transient cylinder case.

Estimates the shedding frequency from the post-transient lift history using
both a zero-crossing count and the peak of the windowed FFT, and reports the
Strouhal number, mean drag and lift amplitude.  Also produces the figure used
in the report.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import matplotlib.pyplot as plt  # noqa: E402
from plot_style import apply_style, SERIES  # noqa: E402


def load_forces(path):
    rows = list(csv.DictReader(open(path, newline="")))
    t = np.array([float(r["physical_time"]) for r in rows])
    cl = np.array([float(r["cl"]) for r in rows])
    cd = np.array([float(r["cd"]) for r in rows])
    return t, cl, cd


def zero_crossing_frequency(t, y):
    """Frequency from up-crossings of the mean, using a linear interpolation."""
    ym = y - y.mean()
    idx = np.where((ym[:-1] < 0) & (ym[1:] >= 0))[0]
    if len(idx) < 3:
        return None, 0
    frac = -ym[idx] / (ym[idx + 1] - ym[idx])
    tc = t[idx] + frac * (t[idx + 1] - t[idx])
    periods = np.diff(tc)
    return 1.0 / periods.mean(), len(periods)


def spectral_frequency(t, y):
    dt = np.mean(np.diff(t))
    ym = (y - y.mean()) * np.hanning(len(y))
    spec = np.abs(np.fft.rfft(ym))
    freq = np.fft.rfftfreq(len(y), dt)
    k = int(np.argmax(spec[1:]) + 1)
    # Parabolic interpolation of the spectral peak for sub-bin accuracy.
    if 1 <= k < len(spec) - 1:
        a, b, c = spec[k - 1], spec[k], spec[k + 1]
        denom = (a - 2 * b + c)
        delta = 0.5 * (a - c) / denom if denom != 0 else 0.0
        return float(freq[k] + delta * (freq[1] - freq[0])), freq, spec
    return float(freq[k]), freq, spec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case-dir", required=True)
    ap.add_argument("--out-figure", default=None)
    ap.add_argument("--out-json", default=None)
    ap.add_argument("--start-fraction", type=float, default=0.5,
                    help="fraction of the physical-time record discarded as initial transient")
    ap.add_argument("--case-json", default=None,
                    help="case file, used for the reference length (defaults to 1)")
    args = ap.parse_args()

    t, cl, cd = load_forces(os.path.join(args.case_dir, "forces.csv"))
    meta = json.load(open(os.path.join(args.case_dir, "metadata.json")))
    # Window by physical time, not by sample index, so the analysis is correct
    # even if the force history is ever written at a coarser cadence.
    t0 = t[0] + args.start_fraction * (t[-1] - t[0])
    i0 = int(np.searchsorted(t, t0))
    ts, cls, cds = t[i0:], cl[i0:], cd[i0:]

    f_zc, n_periods = zero_crossing_frequency(ts, cls)
    f_fft, freq, spec = spectral_frequency(ts, cls)
    uref, lref = 1.0, 1.0
    if args.case_json and os.path.exists(args.case_json):
        cj = json.load(open(args.case_json))
        lref = float(cj["reference"].get("length", 1.0))
        uref = float(cj["freestream"].get("velocity_magnitude", 1.0))
    st_zc = f_zc * lref / uref if f_zc else None
    st_fft = f_fft * lref / uref

    cl_amp = 0.5 * (np.percentile(cls, 99.5) - np.percentile(cls, 0.5))
    result = dict(
        case_id=meta["case_id"],
        analysis_window=[float(ts[0]), float(ts[-1])],
        num_samples=int(len(ts)),
        shedding_frequency_zero_crossing=float(f_zc) if f_zc else None,
        shedding_frequency_fft=float(f_fft),
        num_periods_detected=int(n_periods),
        strouhal_zero_crossing=float(st_zc) if st_zc else None,
        strouhal_fft=float(st_fft),
        mean_cd=float(cds.mean()),
        cd_amplitude=float(0.5 * (np.percentile(cds, 99.5) - np.percentile(cds, 0.5))),
        mean_cl=float(cls.mean()),
        cl_rms=float(cls.std()),
        cl_amplitude=float(cl_amp),
        mean_pressure_drag=None,
        reference_length=lref,
        reference_velocity=uref,
        note="Strouhal number St = f * L_ref / U_inf.",
    )
    fr = list(csv.DictReader(open(os.path.join(args.case_dir, "forces.csv"), newline="")))
    pdg = np.array([float(r["pressure_drag"]) for r in fr])[i0:]
    vdg = np.array([float(r["viscous_drag"]) for r in fr])[i0:]
    result["mean_pressure_drag"] = float(pdg.mean())
    result["mean_viscous_drag"] = float(vdg.mean())

    if args.out_json:
        os.makedirs(os.path.dirname(os.path.abspath(args.out_json)), exist_ok=True)
        json.dump(result, open(args.out_json, "w"), indent=2)
    print(json.dumps(result, indent=2))

    if args.out_figure:
        apply_style()
        fig, axes = plt.subplots(1, 3, figsize=(15, 4.0))
        w = (ts >= ts[-1] - 40.0)
        axes[0].plot(ts[w], cls[w], color=SERIES[0], label=r"$C_L$")
        axes[0].set_xlabel(r"$t\,U_\infty/D$")
        axes[0].set_ylabel(r"$C_L$")
        axes[0].set_title("post-transient lift oscillation")
        axes[0].legend()
        axes[1].plot(ts[w], cds[w], color=SERIES[1], label=r"$C_D$")
        axes[1].axhline(result["mean_cd"], color="k", ls="--", lw=0.9,
                        label=fr"mean $C_D={result['mean_cd']:.4f}$")
        axes[1].set_xlabel(r"$t\,U_\infty/D$")
        axes[1].set_ylabel(r"$C_D$")
        axes[1].set_title("post-transient drag oscillation")
        axes[1].legend()
        sel = (freq > 0) & (freq < 1.0)
        axes[2].semilogy(freq[sel], spec[sel] / spec[sel].max(), color=SERIES[2])
        axes[2].axvline(f_fft, color="k", ls="--", lw=0.9,
                        label=fr"$f={f_fft:.4f}$, $St={st_fft:.4f}$")
        axes[2].set_xlabel(r"frequency $f\,D/U_\infty$")
        axes[2].set_ylabel("normalised $|\\hat{C_L}|$")
        axes[2].set_title("lift spectrum (Hann window)")
        axes[2].legend()
        fig.tight_layout()
        os.makedirs(os.path.dirname(os.path.abspath(args.out_figure)), exist_ok=True)
        fig.savefig(args.out_figure)
        plt.close(fig)
    return 0


if __name__ == "__main__":
    sys.exit(main())
