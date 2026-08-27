#!/usr/bin/env python3
"""Limiter limit-cycle figure: residual and drag history with and
without limiter freezing."""

from __future__ import annotations

import argparse
import json
import csv
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import matplotlib.pyplot as plt  # noqa: E402
from plot_style import apply_style, SERIES  # noqa: E402


def load(d):
    r = list(csv.DictReader(open(os.path.join(d, "residuals.csv"), newline="")))
    f = list(csv.DictReader(open(os.path.join(d, "forces.csv"), newline="")))
    return (np.array([float(x["step"]) for x in r]),
            np.array([float(x["residual_l2"]) for x in r]),
            np.array([float(x["cd"]) for x in f]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frozen", required=True, help="run with --freeze-limiter-step")
    ap.add_argument("--free", required=True, help="run without limiter freezing")
    ap.add_argument("--freeze-step", type=int, default=-1,
                    help="pseudo-time step at which the limiter was frozen; "
                         "read from the frozen run's metadata.json when omitted")
    ap.add_argument("--case-label", default="shock case")
    ap.add_argument("--out", default="report/figures/limiter_study.png")
    args = ap.parse_args()

    if args.freeze_step < 0:
        meta = json.load(open(os.path.join(args.frozen, "metadata.json")))
        args.freeze_step = int(meta.get("limiter_freeze_step", 0) or 0)

    apply_style()
    sf, rf, cf = load(args.frozen)
    sn, rn, cn = load(args.free)
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.2))
    axes[0].semilogy(sn, rn, color=SERIES[1], lw=1.3, label="limiter recomputed every step")
    axes[0].semilogy(sf, rf, color=SERIES[0], lw=1.3,
                     label=f"limiter frozen from step {args.freeze_step}")
    axes[0].axvline(args.freeze_step, color="0.4", ls="--", lw=1.2)
    axes[0].set_xlabel("pseudo-time step")
    axes[0].set_ylabel(r"total scaled $L_2$ residual")
    axes[0].set_title(f"{args.case_label}: residual limit cycle")
    axes[0].legend(fontsize=9)
    axes[1].plot(sn[:len(cn)], cn, color=SERIES[1], lw=1.3, label="limiter recomputed every step")
    axes[1].plot(sf[:len(cf)], cf, color=SERIES[0], lw=1.3,
                 label=f"limiter frozen from step {args.freeze_step}")
    axes[1].axvline(args.freeze_step, color="0.4", ls="--", lw=1.2)
    axes[1].set_xlim(left=max(1, 0.1 * sn[-1]))
    lo = np.percentile(cn[len(cn) // 4:], 1)
    hi = np.percentile(cn[len(cn) // 4:], 99)
    pad = 0.35 * (hi - lo) + 1e-6
    axes[1].set_ylim(lo - pad, hi + pad)
    axes[1].set_xlabel("pseudo-time step")
    axes[1].set_ylabel(r"drag coefficient $C_D$")
    axes[1].set_title(f"{args.case_label}: drag scatter")
    axes[1].legend(fontsize=9)
    fig.tight_layout()
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    fig.savefig(args.out)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
