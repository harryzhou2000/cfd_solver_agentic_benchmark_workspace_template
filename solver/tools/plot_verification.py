#!/usr/bin/env python3
"""Plot the manufactured-solution convergence study."""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import matplotlib.pyplot as plt  # noqa: E402
from plot_style import apply_style, SERIES  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default="report/verification.json")
    ap.add_argument("--out", default="report/figures/mms_order.png")
    args = ap.parse_args()
    v = json.load(open(args.json))
    apply_style()
    fig, ax = plt.subplots(figsize=(6.4, 4.8))
    series = [("mms_first_order", "first order", SERIES[1], "s"),
              ("mms_second_order_limited", "second order, Venkatakrishnan", SERIES[2], "^"),
              ("mms_second_order_unlimited", "second order, unlimited", SERIES[0], "o")]
    for key, label, col, mk in series:
        lv = v.get(key, [])
        if not lv:
            continue
        h = np.array([l["mean_h"] for l in lv])
        e = np.array([l["err_l1"] for l in lv])
        p = np.polyfit(np.log(h), np.log(e), 1)[0]
        ax.loglog(h, e, marker=mk, color=col, label=f"{label} (slope {p:.2f})")
    h0 = np.array([l["mean_h"] for l in v["mms_second_order_unlimited"]])
    e0 = v["mms_second_order_unlimited"][0]["err_l1"]
    ax.loglog(h0, e0 * (h0 / h0[0]) ** 2, "k:", lw=1.0, label=r"$\mathcal{O}(h^2)$")
    ax.loglog(h0, e0 * (h0 / h0[0]), "k--", lw=1.0, label=r"$\mathcal{O}(h)$")
    ax.set_xlabel(r"mean cell size $h$")
    ax.set_ylabel(r"$L_1$ discretisation error of $\mathbf{W}$")
    ax.set_title("Manufactured-solution convergence")
    ax.legend(fontsize=9)
    fig.tight_layout()
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    fig.savefig(args.out)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
