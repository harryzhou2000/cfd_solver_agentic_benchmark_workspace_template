#!/usr/bin/env python
"""Ground-truth derivation helpers for the hardcoded-number sweep.

READ-ONLY over /workspace/solver/results/.  Import this from a scratch script
or use the CLI:

    python gt.py cases                 # list cases + key run_status fields
    python gt.py surf <case>           # surface.csv summary stats
    python gt.py forces <case>         # last row + tail window stats of forces.csv
    python gt.py meta <case> <key>     # dotted lookup into metadata.json
    python gt.py scaling               # scaling study table
    python gt.py cp_exceed <case> <b>  # count faces with cp > b
"""
import csv
import json
import math
import sys
from pathlib import Path

RESULTS = Path("/workspace/solver/results")

CASES = [
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
]

#: cylinder_m010_laminar_re200 is mid-run: metadata/run_status/surface are stale.
STEADY = [c for c in CASES if c != "cylinder_m010_laminar_re200"]


def _dir(case):
    p = RESULTS / case
    if not p.is_dir():
        p = RESULTS / "scaling" / case
    if not p.is_dir():
        raise SystemExit("no such case dir: %s" % case)
    return p


def load_json(case, name):
    return json.loads((_dir(case) / name).read_text())


def load_csv(case, name):
    """Return (fieldnames, rows) with all numeric-looking cells cast to float."""
    with open(_dir(case) / name, newline="") as handle:
        reader = csv.DictReader(handle)
        fields = list(reader.fieldnames or [])
        rows = []
        for raw in reader:
            row = {}
            for key, val in raw.items():
                if val is None:
                    row[key] = None
                    continue
                try:
                    row[key] = float(val)
                except (TypeError, ValueError):
                    row[key] = val
            rows.append(row)
    return fields, rows


def surface(case):
    return load_csv(case, "surface.csv")[1]


def wall_rows(case):
    """Surface rows on the solid wall only (tag WALL for cylinder, bc-4 for NACA)."""
    rows = surface(case)
    tags = {}
    for r in rows:
        tags.setdefault(r.get("tag"), 0)
        tags[r["tag"]] += 1
    # the wall family is the one the case's metadata marks slip_wall / viscous wall
    walls = [t for t in tags if t == "WALL" or t == "bc-4"]
    if not walls:
        return rows, tags
    return [r for r in rows if r["tag"] in walls], tags


def col(rows, key):
    return [r[key] for r in rows if isinstance(r.get(key), float)]


def summarize(vals):
    if not vals:
        return {}
    s = sorted(vals)
    n = len(s)
    mean = sum(s) / n
    return {
        "n": n,
        "min": s[0],
        "max": s[-1],
        "mean": mean,
        "median": s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2]),
        "absmax": max(abs(v) for v in s),
        "rms": math.sqrt(sum(v * v for v in s) / n),
    }


def _p(obj):
    print(json.dumps(obj, indent=2, sort_keys=True, default=str))


def main(argv):
    if not argv:
        print(__doc__)
        return 0
    cmd = argv[0]
    if cmd == "cases":
        out = {}
        for case in CASES:
            try:
                st = load_json(case, "run_status.json")
            except Exception as exc:  # noqa: BLE001
                out[case] = {"error": str(exc)}
                continue
            out[case] = {
                k: st.get(k)
                for k in (
                    "convergence_status", "final_step", "final_physical_time",
                    "mpi_ranks", "residual_reduction_orders", "wall_time_seconds",
                )
            }
        _p(out)
    elif cmd == "surf":
        case = argv[1]
        rows, tags = wall_rows(case)
        out = {"tag_counts": tags, "n_wall_rows": len(rows)}
        for key in ("cp", "cf", "mach", "pressure", "rho", "u", "v"):
            vals = col(rows, key)
            if vals:
                out[key] = summarize(vals)
        vel = [math.hypot(r["u"], r["v"]) for r in rows
               if isinstance(r.get("u"), float) and isinstance(r.get("v"), float)]
        if vel:
            out["speed"] = summarize(vel)
        _p(out)
    elif cmd == "forces":
        case = argv[1]
        win = int(argv[2]) if len(argv) > 2 else 500
        _, rows = load_csv(case, "forces.csv")
        out = {"n_rows": len(rows), "last": rows[-1] if rows else None}
        tail = rows[-win:]
        for key in ("cl", "cd", "cmz", "pressure_drag", "viscous_drag",
                    "pressure_lift", "viscous_lift"):
            vals = col(tail, key)
            if vals:
                out["tail%d_%s" % (win, key)] = summarize(vals)
        _p(out)
    elif cmd == "meta":
        case = argv[1]
        node = load_json(case, "metadata.json")
        for part in argv[2:]:
            node = node[part]
        _p(node)
    elif cmd == "status":
        _p(load_json(argv[1], "run_status.json"))
    elif cmd == "part":
        _p(load_json(argv[1], "partition_diagnostics.json"))
    elif cmd == "scaling":
        out = {}
        for base in ("cylinder_m010_laminar_re20", "naca0012_m015_laminar_re5000"):
            for np_ in (1, 2, 4, 8):
                case = "%s_np%d" % (base, np_)
                try:
                    st = load_json(case, "run_status.json")
                    pd = load_json(case, "partition_diagnostics.json")
                    _, fr = load_csv(case, "forces.csv")
                except Exception as exc:  # noqa: BLE001
                    out[case] = {"error": str(exc)}
                    continue
                at1500 = None
                for row in fr:
                    if row.get("step") == 1500.0:
                        at1500 = row["cd"]
                out[case] = {
                    "final_step": st.get("final_step"),
                    "wall_time_seconds": st.get("wall_time_seconds"),
                    "convergence_status": st.get("convergence_status"),
                    "orders": st.get("residual_reduction_orders"),
                    "edge_cut": pd.get("edge_cut"),
                    "load_balance_ratio": pd.get("load_balance_ratio"),
                    "cd_last": fr[-1]["cd"] if fr else None,
                    "cd_last_step": fr[-1]["step"] if fr else None,
                    "cd_at_step_1500": at1500,
                }
        _p(out)
    elif cmd == "cp_exceed":
        case = argv[1]
        bound = float(argv[2])
        rows, _ = wall_rows(case)
        over = [r for r in rows if isinstance(r.get("cp"), float) and r["cp"] > bound]
        _p({
            "case": case, "bound": bound, "n_wall_rows": len(rows),
            "n_exceeding": len(over),
            "max_cp": max((r["cp"] for r in rows if isinstance(r.get("cp"), float)),
                          default=None),
            "worst": sorted((r["cp"] for r in over), reverse=True)[:12],
        })
    else:
        raise SystemExit("unknown command %r" % cmd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
