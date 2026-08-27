#!/usr/bin/env python3
"""Stagnation-region oscillation diagnostics for the inviscid NACA0012 case.

The Roe linear-wave floor exists to damp the entropy/shear waves whose eigenvalue
u.n vanishes on a stagnation streamline.  On a symmetric airfoil at zero incidence
that point sits at the leading edge, so this script reports the oscillation metrics
BOTH over the whole surface AND restricted to the leading-edge region, where the
carbuncle mode would have to appear if it appears at all.

Surface faces are ordered by signed arclength from the leading edge (negative on
the lower surface, positive on the upper) so that consecutive entries are
physically adjacent.  Unlike the cylinder this is an OPEN curve, so the
differences are taken along the chain without wrap-around.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import sys

import numpy as np


def read_surface(path):
    with open(path, newline="") as handle:
        reader = csv.DictReader(handle)
        rows = list(reader)
        names = reader.fieldnames or []
    if not rows:
        raise ValueError("no data rows")
    cols = {}
    for name in names:
        raw = [r[name] for r in rows]
        try:
            cols[name] = np.array([float(v) for v in raw])
        except (TypeError, ValueError):
            cols[name] = np.array(raw, dtype=object)
    return cols


def open_chain_metrics(values):
    """Smoothness metrics along an OPEN chain of adjacent faces (no wrap)."""
    v = np.asarray(values, dtype=float)
    n = v.size
    if n < 4:
        return {"n": int(n)}
    d = np.diff(v)                      # n-1 first differences
    d2 = v[2:] - 2.0 * v[1:-1] + v[:-2]  # n-2 second differences
    sign = np.sign(d)
    nz = sign[sign != 0.0]
    alternations = int(np.sum(nz[1:] != nz[:-1])) if nz.size > 1 else 0
    max_d = float(np.max(np.abs(d)))
    max_d2 = float(np.max(np.abs(d2)))
    return {
        "n": int(n),
        "min": float(np.min(v)),
        "max": float(np.max(v)),
        "max_abs_first_difference": max_d,
        "mean_abs_first_difference": float(np.mean(np.abs(d))),
        "max_abs_second_difference": max_d2,
        "mean_abs_second_difference": float(np.mean(np.abs(d2))),
        "oscillation_ratio": float(max_d2 / max_d) if max_d > 0.0 else float("nan"),
        "sign_alternations": alternations,
        "sign_alternation_fraction": float(alternations) / float(max(n - 1, 1)),
    }


def mirror_asymmetry(x, y, values):
    """Compare each face with the face nearest its reflection about y = 0.

    At zero incidence the flow is symmetric, so any upper/lower difference is
    numerical.  A carbuncle mode characteristically breaks this symmetry.
    """
    v = np.asarray(values, dtype=float)
    upper = np.where(y > 0)[0]
    lower = np.where(y < 0)[0]
    if upper.size == 0 or lower.size == 0:
        return {}
    diffs = []
    for i in upper:
        # nearest lower-surface face to the reflected point (x, -y)
        d2 = (x[lower] - x[i]) ** 2 + (y[lower] + y[i]) ** 2
        j = lower[int(np.argmin(d2))]
        diffs.append(abs(v[i] - v[j]))
    diffs = np.array(diffs)
    return {
        "max_asymmetry": float(np.max(diffs)),
        "rms_asymmetry": float(np.sqrt(np.mean(diffs ** 2))),
        "pairs": int(diffs.size),
    }


def signed_arclength_order(x, y):
    """Order faces along the airfoil by signed arclength from the leading edge.

    Lower surface gets negative arclength, upper surface positive, so the chain
    runs lower-trailing-edge -> leading edge -> upper-trailing-edge and the
    leading edge sits in the middle of the chain.
    """
    # Leading edge = minimum x.  Sort each surface by x, then chain them.
    lower = np.where(y <= 0)[0]
    upper = np.where(y > 0)[0]
    lower = lower[np.argsort(-x[lower])]  # from TE down to LE  (x decreasing)
    upper = upper[np.argsort(x[upper])]   # from LE up to TE    (x increasing)
    return np.concatenate([lower, upper])


def analyze(directory, le_cut=0.1):
    out = {"directory": directory,
           "name": os.path.basename(os.path.normpath(directory)),
           "le_cut": le_cut, "warnings": []}

    st_path = os.path.join(directory, "run_status.json")
    if os.path.isfile(st_path):
        with open(st_path) as handle:
            st = json.load(handle)
        out["run_status"] = {
            "convergence_status": st.get("convergence_status"),
            "final_step": st.get("final_step"),
            "residual_reduction_orders": st.get("residual_reduction_orders"),
            "wall_time_seconds": st.get("wall_time_seconds"),
            "notes": st.get("notes"),
        }
    else:
        out["warnings"].append("run_status.json missing")

    # Forces at the common final step.
    fpath = os.path.join(directory, "forces.csv")
    if os.path.isfile(fpath):
        with open(fpath, newline="") as handle:
            rows = list(csv.DictReader(handle))
        if rows:
            last = rows[-1]
            out["forces"] = {k: float(last[k]) for k in
                             ("step", "cd", "cl", "pressure_drag", "viscous_drag")
                             if k in last}
            out["forces"]["step"] = int(out["forces"]["step"])
            out["forces"]["rows"] = len(rows)
            # Drag drift over the trailing 500 steps: a stalling / oscillating run
            # shows up as a large span here.
            tail = [float(r["cd"]) for r in rows[-500:]]
            out["forces"]["cd_trailing500_span"] = max(tail) - min(tail)
    else:
        out["warnings"].append("forces.csv missing")

    # Convergence pathology indicators from residuals.csv.
    rpath = os.path.join(directory, "residuals.csv")
    if os.path.isfile(rpath):
        with open(rpath, newline="") as handle:
            rows = list(csv.DictReader(handle))
        if rows:
            res = np.array([float(r["residual_l2"]) for r in rows])
            inner = np.array([float(r["inner_iter"]) for r in rows])
            # Count steps where the residual went UP: oscillation / stall signature.
            increases = int(np.sum(np.diff(res) > 0.0))
            out["residual_health"] = {
                "steps": int(res.size),
                "initial": float(res[0]),
                "final": float(res[-1]),
                "orders": float(math.log10(res[0] / res[-1])) if res[-1] > 0 else None,
                "min": float(np.min(res)),
                "residual_increase_steps": increases,
                "residual_increase_fraction": increases / float(max(res.size - 1, 1)),
                "max_inner_iterations_used": float(np.max(inner)),
                "mean_inner_iterations": float(np.mean(inner)),
            }
    else:
        out["warnings"].append("residuals.csv missing")

    spath = os.path.join(directory, "surface.csv")
    if not os.path.isfile(spath):
        out["warnings"].append("surface.csv missing")
        return out, None
    cols = read_surface(spath)
    x, y, cp = cols["x"], cols["y"], cols["cp"]
    order = signed_arclength_order(x, y)
    xo, yo, cpo = x[order], y[order], cp[order]

    surf = {"num_faces": int(xo.size)}
    surf["whole_surface"] = open_chain_metrics(cpo)
    surf["whole_surface"].update(
        {"symmetry_" + k: v for k, v in mirror_asymmetry(x, y, cp).items()})

    # The NACA0012 section here has a BLUNT trailing edge (finite thickness at
    # x/c = 1.005), and the cp jump across those few faces dominates the
    # whole-surface first difference.  That is a geometric feature of the section,
    # present identically on upper and lower surfaces, not a numerical mode, so a
    # trailing-edge-excluded variant is reported to avoid masking real oscillation.
    te_mask = xo < 0.99
    if te_mask.sum() >= 4:
        surf["excluding_trailing_edge"] = open_chain_metrics(cpo[te_mask])
        tem = x < 0.99
        surf["excluding_trailing_edge"].update(
            {"symmetry_" + k: v for k, v in
             mirror_asymmetry(x[tem], y[tem], cp[tem]).items()})

    # Leading-edge region: the stagnation zone, where u.n -> 0.
    le_mask = xo < le_cut
    if le_mask.sum() >= 4:
        surf["leading_edge"] = open_chain_metrics(cpo[le_mask])
        lem = x < le_cut
        surf["leading_edge"].update(
            {"symmetry_" + k: v for k, v in
             mirror_asymmetry(x[lem], y[lem], cp[lem]).items()})
        surf["leading_edge"]["x_max_in_region"] = float(np.max(xo[le_mask]))
    else:
        out["warnings"].append("too few leading-edge faces")

    # The single stagnation face: minimum x.  cp there should be ~1.
    i_stag = int(np.argmin(x))
    surf["stagnation_face"] = {
        "x": float(x[i_stag]), "y": float(y[i_stag]),
        "cp": float(cp[i_stag]),
        "mach": float(cols["mach"][i_stag]) if "mach" in cols else None,
    }
    surf["cp_max_overall"] = float(np.max(cp))
    out["surface"] = surf
    return out, (xo, yo, cpo)


def report(out):
    print("=" * 78)
    print(out["name"])
    print("=" * 78)
    for w in out["warnings"]:
        print("  WARNING: " + w)
    st = out.get("run_status")
    if st:
        print("  run status: %s at step %s, %.2f orders, %.1f s" % (
            st["convergence_status"], st["final_step"],
            st["residual_reduction_orders"] or float("nan"),
            st["wall_time_seconds"] or float("nan")))
    f = out.get("forces")
    if f:
        print("  forces at step %d:  Cd = %.8f   Cl = %.8e" % (
            f["step"], f["cd"], f["cl"]))
        print("    pressure_drag = %.8f  viscous_drag = %.8f" % (
            f.get("pressure_drag", float("nan")), f.get("viscous_drag", float("nan"))))
        print("    Cd span over trailing 500 steps = %.3e" % f["cd_trailing500_span"])
    rh = out.get("residual_health")
    if rh:
        print("  residual health:")
        print("    %d steps, %.4e -> %.4e (%.2f orders)" % (
            rh["steps"], rh["initial"], rh["final"], rh["orders"]))
        print("    steps where residual increased: %d (%.1f%%)" % (
            rh["residual_increase_steps"], 100 * rh["residual_increase_fraction"]))
        print("    inner iterations: mean %.2f, max %.0f" % (
            rh["mean_inner_iterations"], rh["max_inner_iterations_used"]))
    s = out.get("surface")
    if not s:
        return
    print("  stagnation face: x=%.3e y=%.3e cp=%.6f mach=%.3e" % (
        s["stagnation_face"]["x"], s["stagnation_face"]["y"],
        s["stagnation_face"]["cp"], s["stagnation_face"]["mach"]))
    print("  cp max over surface = %.6f" % s["cp_max_overall"])
    for region in ("whole_surface", "excluding_trailing_edge", "leading_edge"):
        m = s.get(region)
        if not m:
            continue
        label = region.replace("_", " ")
        extra = ""
        if region == "leading_edge":
            extra = " (x/c < %g, %d faces)" % (out["le_cut"], m["n"])
        if region == "excluding_trailing_edge":
            extra = " (x/c < 0.99, %d faces)" % m["n"]
        print("  [%s]%s" % (label, extra))
        print("    cp range               = %.6f .. %.6f" % (m["min"], m["max"]))
        print("    max |d1| adjacent      = %.5e" % m["max_abs_first_difference"])
        print("    mean |d1|              = %.5e" % m["mean_abs_first_difference"])
        print("    max |d2|               = %.5e" % m["max_abs_second_difference"])
        print("    oscillation ratio      = %.3f" % m["oscillation_ratio"])
        print("    sign alternations      = %d of %d (%.3f)" % (
            m["sign_alternations"], m["n"] - 1, m["sign_alternation_fraction"]))
        if "symmetry_max_asymmetry" in m:
            print("    max mirror asymmetry   = %.4e" % m["symmetry_max_asymmetry"])
            print("    rms mirror asymmetry   = %.4e" % m["symmetry_rms_asymmetry"])


def make_plot(curves, out_path, le_cut):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    try:
        sys.path.insert(0, "/workspace/solver/tools")
        import plot_style
        plot_style.apply_style()
    except Exception:
        pass
    fig, axes = plt.subplots(2, 2, figsize=(14.0, 9.0))
    for label in sorted(curves):
        xo, yo, cpo = curves[label]
        s = np.arange(cpo.size)
        axes[0][0].plot(xo, cpo, marker="o", markersize=2.5, linewidth=1.0, label=label)
        m = xo < le_cut
        axes[0][1].plot(xo[m], cpo[m], marker="o", markersize=4.0, linewidth=1.0, label=label)
        axes[1][0].plot(s[:-1], np.diff(cpo), marker="o", markersize=2.5,
                        linewidth=1.0, label=label)
        idx = np.where(m)[0]
        axes[1][1].plot(idx[:-1], np.diff(cpo[m]), marker="o", markersize=4.0,
                        linewidth=1.0, label=label)
    axes[0][0].set_xlabel("x/c"); axes[0][0].set_ylabel("cp")
    axes[0][0].set_title("Whole surface"); axes[0][0].invert_yaxis()
    axes[0][1].set_xlabel("x/c"); axes[0][1].set_ylabel("cp")
    axes[0][1].set_title("Leading-edge / stagnation region (x/c < %g)" % le_cut)
    axes[0][1].invert_yaxis()
    axes[1][0].set_xlabel("face index along surface"); axes[1][0].set_ylabel("d(cp)")
    axes[1][0].set_title("Face-to-face difference, whole surface")
    axes[1][1].set_xlabel("face index along surface"); axes[1][1].set_ylabel("d(cp)")
    axes[1][1].set_title("Face-to-face difference, LE region (saw-tooth = carbuncle)")
    for row in axes:
        for ax in row:
            ax.grid(True, alpha=0.4)
            ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out_path, dpi=200, bbox_inches="tight")
    print("wrote " + out_path)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--le-cut", type=float, default=0.1)
    ap.add_argument("--json", dest="json_out")
    ap.add_argument("--plot", dest="plot_out")
    args = ap.parse_args(argv)
    payload, curves = {}, {}
    for d in args.dirs:
        if not os.path.isdir(d):
            print("MISSING DIRECTORY: " + d)
            continue
        out, curve = analyze(d, args.le_cut)
        report(out)
        payload[out["name"]] = out
        if curve is not None:
            curves[out["name"]] = curve
    if args.json_out:
        with open(args.json_out, "w") as h:
            json.dump(payload, h, indent=2, sort_keys=True)
        print("wrote " + args.json_out)
    if args.plot_out and curves:
        make_plot(curves, args.plot_out, args.le_cut)
    return 0


if __name__ == "__main__":
    sys.exit(main())
