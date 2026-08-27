#!/usr/bin/env python3
"""MPI rank-count consistency & timing comparison for the fv2d solver.

Runs one case at several MPI rank counts for a fixed number of steps and
compares the force coefficients at a common step (rank-independence) and the
wall-clock time (scaling). Writes a CSV and a LaTeX fragment for the report.

Usage:
  run_rank_comparison.py --case <case.json> --tag <label> --steps 1000 \
      --ranks 1 2 4 8 --out-dir results_rankcmp/<label>
"""
import argparse, csv, json, os, subprocess, sys, time
from pathlib import Path

SOLVER_ROOT = Path(__file__).resolve().parent.parent
FV2D = os.environ.get("FV2D_BIN", str(SOLVER_ROOT / "bin" / "fv2d.v2"))


def make_variant(base_json, steps, out_path):
    d = json.load(open(base_json))
    mesh = d["mesh"]["file"]
    if not mesh.startswith("/"):
        d["mesh"]["file"] = str((Path(base_json).parent / mesh).resolve())
    d["run_control"]["max_steps"] = steps
    d["run_control"]["residual_reduction_target"] = 99.0  # never trip the gate
    d["case_id"] = d["case_id"] + "_rankcmp"
    json.dump(d, open(out_path, "w"), indent=2)
    return out_path


def run_one(variant_json, np_, out_dir, flux=None, venkat=False):
    os.makedirs(out_dir, exist_ok=True)
    cmd = ["mpirun", "-np", str(np_), FV2D, "solve", "--case", variant_json,
           "--output", out_dir]
    if flux:
        cmd += ["--flux", flux]
    env = dict(os.environ)
    if venkat:
        env["FV2D_VENKAT"] = "1.0"
    t0 = time.time()
    with open(os.path.join(out_dir, "rankcmp_stdout.log"), "w") as fh:
        subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT, check=True, env=env)
    return time.time() - t0


def force_at_step(forces_csv, step):
    rows = list(csv.DictReader(open(forces_csv)))
    best = None
    for r in rows:
        s = int(float(r["step"]))
        if s <= step:
            best = r
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--steps", type=int, default=1000)
    ap.add_argument("--ranks", type=int, nargs="+", default=[1, 2, 4, 8])
    ap.add_argument("--out-root", default=str(SOLVER_ROOT / "results_rankcmp"))
    ap.add_argument("--compare-step", type=int, default=None)
    ap.add_argument("--flux", default=None)
    ap.add_argument("--venkat", action="store_true")
    args = ap.parse_args()
    cmp_step = args.compare_step or args.steps
    root = Path(args.out_root) / args.tag
    root.mkdir(parents=True, exist_ok=True)
    variant = str(root / "variant.json")
    make_variant(args.case, max(args.steps, cmp_step), variant)
    rows = []
    for np_ in args.ranks:
        out_dir = str(root / f"np{np_}")
        wall = run_one(variant, np_, out_dir, flux=args.flux, venkat=args.venkat)
        fr = force_at_step(os.path.join(out_dir, "forces.csv"), cmp_step)
        rows.append({"np": np_, "wall_s": round(wall, 2),
                     "cl": float(fr["cl"]), "cd": float(fr["cd"]),
                     "cmz": float(fr["cmz"]), "step": int(float(fr["step"]))})
        print("np=%d wall=%.1fs cl=%.8f cd=%.8f" % (np_, wall, float(fr["cl"]), float(fr["cd"])))
    # reference = highest rank count
    ref = rows[-1]
    for r in rows:
        r["dcl"] = abs(r["cl"] - ref["cl"])
        r["dcd"] = abs(r["cd"] - ref["cd"])
    csv_path = root / "rank_comparison.csv"
    with open(csv_path, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["np", "wall_s", "step", "cl", "cd", "cmz", "dcl", "dcd"])
        w.writeheader()
        for r in rows:
            w.writerow(r)
    print("wrote", csv_path)


if __name__ == "__main__":
    main()
