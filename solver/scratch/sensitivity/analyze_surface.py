#!/usr/bin/env python3
"""Quantify surface-pressure smoothness for the Roe dissipation-floor study.

The question this script answers: when the artificial dissipation floor on the
Roe linear (entropy/shear) waves is reduced or removed, does the cylinder
surface pressure develop the classic point-to-point checkerboard / carbuncle
oscillation?

The diagnostics are deliberately local (adjacent-face differences) because the
checkerboard mode is a mesh-frequency mode: it is nearly invisible in an
integrated quantity such as Cd but obvious in cp between neighbouring faces.
"""

from __future__ import annotations

import argparse
import csv
import datetime as _dt
import json
import math
import os
import sys

import numpy as np


def _read_surface(path):
    """Return a dict of column name -> numpy array for a surface.csv file."""
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


def _loop_metrics(values):
    """Smoothness metrics for a sequence sampled around a closed loop.

    The input must already be ordered so that consecutive entries are
    physically adjacent faces, with the last entry adjacent to the first.
    """
    v = np.asarray(values, dtype=float)
    n = v.size
    if n < 4:
        return {"n": int(n)}
    # First difference around the closed loop: d[i] = v[i+1] - v[i].
    d = np.roll(v, -1) - v
    # Second difference around the closed loop; large compared with d means a
    # mesh-frequency (saw-tooth) mode rather than a resolved gradient.
    d2 = np.roll(v, -1) - 2.0 * v + np.roll(v, 1)
    # Sign alternations of the first difference.  A smooth cylinder cp has one
    # maximum and one minimum, hence about two alternations.
    sign = np.sign(d)
    nonzero = sign != 0.0
    alternations = 0
    if nonzero.any():
        s = sign[nonzero]
        alternations = int(np.sum(s != np.roll(s, 1)))
    max_abs_d = float(np.max(np.abs(d)))
    max_abs_d2 = float(np.max(np.abs(d2)))
    ratio = float(max_abs_d2 / max_abs_d) if max_abs_d > 0.0 else float("nan")
    return {
        "n": int(n),
        "min": float(np.min(v)),
        "max": float(np.max(v)),
        "max_abs_first_difference": max_abs_d,
        "mean_abs_first_difference": float(np.mean(np.abs(d))),
        "max_abs_second_difference": max_abs_d2,
        "mean_abs_second_difference": float(np.mean(np.abs(d2))),
        "oscillation_ratio": ratio,
        "sign_alternations": alternations,
        "sign_alternation_fraction": float(alternations) / float(n),
    }


def _symmetry(theta, values):
    """Max/rms difference between each face and its mirror image about y = 0."""
    v = np.asarray(values, dtype=float)
    th = np.asarray(theta, dtype=float)
    diffs = []
    for i in range(th.size):
        target = -th[i]
        delta = ((th - target + math.pi) % (2.0 * math.pi)) - math.pi
        j = int(np.argmin(np.abs(delta)))
        diffs.append(abs(v[i] - v[j]))
    diffs = np.array(diffs)
    return {
        "max_asymmetry": float(np.max(diffs)),
        "rms_asymmetry": float(np.sqrt(np.mean(diffs ** 2))),
    }


def _read_forces(path):
    with open(path, newline="") as handle:
        reader = csv.DictReader(handle)
        last = None
        count = 0
        for row in reader:
            last = row
            count += 1
    if last is None:
        return {"rows": 0}
    out = {"rows": count}
    for key in ("step", "cd", "cl", "pressure_drag", "viscous_drag", "cmz"):
        if key in last:
            try:
                out[key] = float(last[key])
            except (TypeError, ValueError):
                out[key] = last[key]
    if isinstance(out.get("step"), float):
        out["step"] = int(out["step"])
    return out


def _parse_utc(stamp):
    if not stamp:
        return None
    text = str(stamp).strip()
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    try:
        return _dt.datetime.fromisoformat(text)
    except ValueError:
        return None


def _read_metadata(path):
    with open(path) as handle:
        meta = json.load(handle)
    details = meta.get("run_details", {}) or {}
    out = {
        "convergence_status": meta.get("convergence_status"),
        "completed": meta.get("completed"),
        "mpi_ranks": meta.get("mpi_ranks"),
        "residual_reduction_orders": details.get("residual_reduction_orders"),
        "final_residual_l2": details.get("final_residual_l2"),
        "initial_residual_l2": details.get("initial_residual_l2"),
        "spatial_order_claimed": meta.get("spatial_order_claimed"),
        "limiter": meta.get("limiter"),
        "mean_inner_iterations": meta.get("mean_inner_iterations"),
    }
    start = _parse_utc(meta.get("start_time_utc"))
    end = _parse_utc(meta.get("end_time_utc"))
    if start is not None and end is not None:
        out["wall_time_seconds"] = (end - start).total_seconds()
    return out


def analyze(directory):
    """Analyze one results directory.  Missing pieces are reported, not fatal."""
    result = {
        "directory": directory,
        "name": os.path.basename(os.path.normpath(directory)),
        "warnings": [],
    }
    if not os.path.isdir(directory):
        result["warnings"].append("directory does not exist")
        return result, None

    meta_path = os.path.join(directory, "metadata.json")
    if os.path.isfile(meta_path):
        try:
            result["convergence"] = _read_metadata(meta_path)
        except Exception as exc:
            result["warnings"].append("metadata.json unreadable: " + str(exc))
    else:
        result["warnings"].append("metadata.json missing (run may be incomplete)")

    forces_path = os.path.join(directory, "forces.csv")
    if os.path.isfile(forces_path):
        try:
            result["forces"] = _read_forces(forces_path)
        except Exception as exc:
            result["warnings"].append("forces.csv unreadable: " + str(exc))
    else:
        result["warnings"].append("forces.csv missing")

    curve = None
    surf_path = os.path.join(directory, "surface.csv")
    if os.path.isfile(surf_path):
        try:
            cols = _read_surface(surf_path)
            if "tag" in cols and cols["tag"].dtype == object:
                tags = np.array([str(t) for t in cols["tag"]])
                uniq = sorted(set(tags))
                wall = "WALL" if "WALL" in uniq else uniq[0]
                mask = tags == wall
                result["wall_tag"] = wall
                result["wall_tags_present"] = uniq
            else:
                mask = np.ones(cols["x"].size, dtype=bool)
            x = cols["x"][mask]
            y = cols["y"][mask]
            theta = np.arctan2(y, x)
            order = np.argsort(theta)
            theta_s = theta[order]
            metrics = {"num_wall_faces": int(theta_s.size)}
            for field in ("cp", "pressure"):
                if field in cols:
                    vals = cols[field][mask][order]
                    metrics[field] = _loop_metrics(vals)
                    for key, value in _symmetry(theta_s, vals).items():
                        metrics[field]["symmetry_" + key] = value
            if "cp" in cols:
                cp_s = cols["cp"][mask][order]
                metrics["cp"]["theta_at_max_deg"] = float(
                    np.degrees(theta_s[int(np.argmax(cp_s))]))
                metrics["cp"]["theta_at_min_deg"] = float(
                    np.degrees(theta_s[int(np.argmin(cp_s))]))
                curve = (np.degrees(theta_s), cp_s)
            result["surface"] = metrics
        except Exception as exc:
            result["warnings"].append("surface.csv unreadable: " + str(exc))
    else:
        result["warnings"].append("surface.csv missing")
    return result, curve


def _fmt(value, spec="{:.6f}"):
    if value is None:
        return "n/a"
    if isinstance(value, float) and (math.isnan(value) or math.isinf(value)):
        return "n/a"
    if isinstance(value, float):
        return spec.format(value)
    return str(value)


def _print_report(result):
    print("=" * 78)
    print(result["name"])
    print("=" * 78)
    for warning in result["warnings"]:
        print("  WARNING: " + warning)
    forces = result.get("forces")
    if forces and forces.get("rows"):
        print("  forces (last row):")
        print("    step            = " + _fmt(forces.get("step")))
        print("    cd              = " + _fmt(forces.get("cd")))
        print("      pressure_drag = " + _fmt(forces.get("pressure_drag")))
        print("      viscous_drag  = " + _fmt(forces.get("viscous_drag")))
        print("    cl              = " + _fmt(forces.get("cl"), "{:.4e}"))
    conv = result.get("convergence")
    if conv:
        print("  convergence:")
        print("    status          = " + str(conv.get("convergence_status")))
        print("    residual orders = " + _fmt(conv.get("residual_reduction_orders"), "{:.3f}"))
        print("    final res L2    = " + _fmt(conv.get("final_residual_l2"), "{:.4e}"))
        print("    wall time [s]   = " + _fmt(conv.get("wall_time_seconds"), "{:.1f}"))
        print("    mpi ranks       = " + str(conv.get("mpi_ranks")))
    surf = result.get("surface")
    if surf:
        print("  surface smoothness (" + str(surf.get("num_wall_faces"))
              + " wall faces, tag=" + str(result.get("wall_tag")) + "):")
        for field in ("cp", "pressure"):
            m = surf.get(field)
            if not m:
                continue
            print("    [" + field + "]")
            print("      range                   = " + _fmt(m.get("min"), "{:.6f}")
                  + " .. " + _fmt(m.get("max"), "{:.6f}"))
            print("      max |d1| adjacent faces = " + _fmt(m.get("max_abs_first_difference"), "{:.5e}"))
            print("      mean |d1|               = " + _fmt(m.get("mean_abs_first_difference"), "{:.5e}"))
            print("      max |d2| checkerboard   = " + _fmt(m.get("max_abs_second_difference"), "{:.5e}"))
            print("      oscillation ratio d2/d1 = " + _fmt(m.get("oscillation_ratio"), "{:.3f}"))
            print("      sign alternations       = " + str(m.get("sign_alternations"))
                  + " of " + str(m.get("n"))
                  + " (" + _fmt(m.get("sign_alternation_fraction"), "{:.3f}") + ")")
            print("      max mirror asymmetry    = " + _fmt(m.get("symmetry_max_asymmetry"), "{:.3e}"))
            print("      rms mirror asymmetry    = " + _fmt(m.get("symmetry_rms_asymmetry"), "{:.3e}"))
        cp = surf.get("cp")
        if cp and "theta_at_max_deg" in cp:
            print("      theta at cp max [deg]   = " + _fmt(cp["theta_at_max_deg"], "{:.2f}"))
            print("      theta at cp min [deg]   = " + _fmt(cp["theta_at_min_deg"], "{:.2f}"))


def _make_plot(curves, out_path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    try:
        sys.path.insert(0, "/workspace/solver/tools")
        import plot_style
        plot_style.apply_style()
    except Exception:
        pass
    fig, axes = plt.subplots(2, 1, figsize=(9.0, 9.0), sharex=True)
    for label in sorted(curves):
        theta_deg, cp = curves[label]
        axes[0].plot(theta_deg, cp, marker="o", markersize=3.0, linewidth=1.1, label=label)
        d = np.roll(cp, -1) - cp
        axes[1].plot(theta_deg, d, marker="o", markersize=3.0, linewidth=1.1, label=label)
    axes[0].set_ylabel("cp")
    axes[0].set_title("Cylinder surface pressure (markers = individual faces)")
    axes[0].grid(True, alpha=0.4)
    axes[0].legend(fontsize=8)
    axes[1].set_xlabel("theta [deg]  (180 deg = upstream stagnation)")
    axes[1].set_ylabel("cp[i+1] - cp[i]")
    axes[1].set_title("Face-to-face difference: a saw-tooth here is the checkerboard mode")
    axes[1].grid(True, alpha=0.4)
    axes[1].legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out_path, dpi=200, bbox_inches="tight")
    print("wrote " + out_path)


def main(argv=None):
    parser = argparse.ArgumentParser(description="surface smoothness diagnostics")
    parser.add_argument("dirs", nargs="+", help="results directories to analyze")
    parser.add_argument("--json", dest="json_out", default=None)
    parser.add_argument("--plot", dest="plot_out", default=None)
    args = parser.parse_args(argv)

    payload = {}
    curves = {}
    missing = 0
    for directory in args.dirs:
        result, curve = analyze(directory)
        _print_report(result)
        payload[result["name"]] = result
        if curve is not None:
            curves[result["name"]] = curve
        if "directory does not exist" in result["warnings"]:
            missing += 1

    if args.json_out:
        with open(args.json_out, "w") as handle:
            json.dump(payload, handle, indent=2, sort_keys=True)
        print("wrote " + args.json_out)
    if args.plot_out and curves:
        _make_plot(curves, args.plot_out)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())

