#!/usr/bin/env python3
from __future__ import annotations
import csv, json, math, subprocess, sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results"
REPORT = ROOT / "report"
FIGS = REPORT / "figures"
VALIDATOR = ROOT.parent / "cfd_solver_agentic_benchmark" / "examiner" / "validate_outputs.py"

CASES = [
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
    "naca0012_m015_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_inviscid",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_inviscid",
    "naca0012_m200_laminar_re5000",
]


def read_status(case):
    return json.loads((RESULTS / case / "run_status.json").read_text())


def read_meta(case):
    return json.loads((RESULTS / case / "metadata.json").read_text())


def read_csv(path: Path):
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def musigma(a):
    arr = np.asarray(a, dtype=float)
    return float(np.mean(arr)), float(np.std(arr))


def dominant_freq(t, y):
    dt = float(np.median(np.diff(t)))
    y0 = np.asarray(y, dtype=float) - float(np.mean(y))
    n = len(y0)
    win = np.hanning(n)
    spec = np.fft.rfft(y0 * win)
    freqs = np.fft.rfftfreq(n, d=dt)
    if len(freqs) < 3:
        return 0.0, 0.0
    power = np.abs(spec)
    power[0] = 0.0
    idx = int(np.argmax(power))
    amp = 4.0 * np.sqrt(np.mean(y0**2))
    return float(freqs[idx]), float(amp)


def ensure_figures():
    FIGS.mkdir(parents=True, exist_ok=True)
    for cid in CASES:
        cdir = RESULTS / cid
        if not (cdir / "field_final.vtu").exists():
            continue
        subprocess.run([sys.executable, str(ROOT/"tools"/"make_figures.py"), cid, str(cdir), "--out-dir", str(FIGS)], check=True)


def write_run_manifest(rows):
    with (REPORT/"run_manifest.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow([
            "case_id","mpi_ranks","final_step","final_physical_time","residual_reduction_orders","wall_time_seconds","convergence_status",
            "Cd","CL","Cmz","pressure_drag","viscous_drag"])
        for r in rows:
            w.writerow([r["case_id"], r["mpi_ranks"], r["final_step"], r["final_physical_time"], f"{r['residual_reduction_orders']:.3f}",
                        f"{r['wall_time_seconds']:.1f}", r["convergence_status"], f"{r['cd']:.6f}", f"{r['cl']:.6f}", f"{r['cmz']:.6f}",
                        f"{r['pressure_drag']:.6f}", f"{r['viscous_drag']:.6f}"])


def write_sanity(rows):
    checks = []
    for r in rows:
        cid = r["case_id"]
        last = r
        checks.extend([
            {"case_id": cid, "check": "residual_history_finite", "pass": bool(np.isfinite(r["residual_l2_last"]))},
            {"case_id": cid, "check": "force_history_finite", "pass": bool(np.isfinite(r["cd"]) and np.isfinite(r["cl"]))},
            {"case_id": cid, "check": "positive_mesh_cells", "pass": bool(r["num_cells_global"] > 0)},
            {"case_id": cid, "check": "final_step_positive", "pass": bool(r["final_step"] > 0)},
        ])
        if "inviscid" in cid:
            checks.append({"case_id": cid, "check": "inviscid_zero_viscous_forces", "pass": abs(r["viscous_drag"]) < 1e-8 and abs(r["viscous_lift"]) < 1e-8})
        if "laminar_re5000" in cid:
            checks.append({"case_id": cid, "check": "viscous_drag_positive", "pass": r["viscous_drag"] > 0.0})
        if "re200" in cid.lower():
            checks.extend([
                {"case_id": cid, "check": "final_physical_time_reached", "pass": r["final_physical_time"] >= 300.0},
                {"case_id": cid, "check": "bdf2_inner_fraction", "pass": r["inner_target_converged_fraction"] >= 0.95},
                {"case_id": cid, "check": "vortex_shedding_present", "pass": r["cl_amplitude"] > 1e-3},
                {"case_id": cid, "check": "strouhal_reasonable", "pass": 0.10 <= r["strouhal"] <= 0.23},
            ])
        if "cylinder_m010_laminar_re20" in cid:
            checks.append({"case_id": cid, "check": "steady_wake_drag_positive", "pass": r["cd"] > 0.5})
        if "m015" in cid or "re20" in cid.lower():
            checks.append({"case_id": cid, "check": "small_lift_near_symmetry", "pass": abs(r["cl"]) < 0.02})
    (REPORT/"sanity_checks.json").write_text(json.dumps(checks, indent=2))


def collect_rows():
    rows = []
    for cid in CASES:
        s = read_status(cid)
        m = read_meta(cid)
        fr = read_csv(RESULTS / cid / "forces.csv")
        rr = read_csv(RESULTS / cid / "residuals.csv")
        last = fr[-1]
        t = np.asarray([float(x["physical_time"]) for x in fr])
        cl = np.asarray([float(x["cl"]) for x in fr])
        cd = np.asarray([float(x["cd"]) for x in fr])
        extra = {"residual_l2_last": float(rr[-1]["residual_l2"]), "num_cells_global": m["num_cells_global"]}
        row = dict(case_id=cid, mpi_ranks=s["mpi_ranks"], final_step=int(float(s["final_step"])),
                   final_physical_time=float(s["final_physical_time"]),
                   residual_reduction_orders=float(s["residual_reduction_orders"]), wall_time_seconds=float(s["wall_time_seconds"]),
                   convergence_status=s["convergence_status"], cd=float(last["cd"]), cl=float(last["cl"]), cmz=float(last["cmz"]),
                   pressure_drag=float(last["pressure_drag"]), viscous_drag=float(last["viscous_drag"]),
                   pressure_lift=float(last["pressure_lift"]), viscous_lift=float(last["viscous_lift"]),
                   inner_target_converged_fraction=float(m.get("inner_target_converged_fraction", 1.0)),
                   **extra)
        if "re200" in cid.lower():
            n = max(10, int(0.5*len(t)))
            f, amp = dominant_freq(t[-n:], cl[-n:])
            row["strouhal"] = f
            row["cl_amplitude"] = amp
            row["mean_cd"] = float(np.mean(cd[-n:]))
            row["std_cd"] = float(np.std(cd[-n:]))
        rows.append(row)
    return rows


def rank_status_table(path):
    rows = []
    for rank_dir in sorted(path.glob("*_np*")):
        rs = rank_dir / "run_status.json"
        if not rs.exists():
            continue
        s = json.loads(rs.read_text())
        rows.append((rank_dir.name, s))
    return rows


def main():
    REPORT.mkdir(parents=True, exist_ok=True)
    rows = collect_rows()
    write_run_manifest(rows)
    write_sanity(rows)

    with (REPORT/"run_manifest.md").open("w") as f:
        f.write("# Run Manifest\n\n")
        f.write("| Case | np | Steps | Time | Orders | Wall (s) | Status | Cd | CL |\n")
        f.write("|---|---|---:|---:|---:|---:|---|---:|---:|\n")
        for r in rows:
            f.write("| {} | {} | {} | {:.2f} | {:.2f} | {:.0f} | {} | {:.5f} | {:.5f} |\n".format(
                r["case_id"], r["mpi_ranks"], r["final_step"], r["final_physical_time"],
                r["residual_reduction_orders"], r["wall_time_seconds"],
                r["convergence_status"], r["cd"], r["cl"]))

    re200 = [r for r in rows if r["case_id"] == "cylinder_m010_laminar_re200"]
    if re200:
        r = re200[0]
        (REPORT/"re200_stats.json").write_text(json.dumps({
            "mean_cd": r["mean_cd"], "std_cd": r["std_cd"],
            "cl_amplitude": r["cl_amplitude"], "strouhal": r["strouhal"],
            "dominant_frequency": r["strouhal"],  # dimensional U=D=1
        }, indent=2))

    # Simple rank comparison table from existing MPI validation runs.
    rank_rows = []
    naca_rank = ROOT/"rankcount"
    if naca_rank.exists():
        for d in sorted(naca_rank.glob("*_np*")):
            rs = d / "run_status.json"
            if rs.exists():
                s = json.loads(rs.read_text())
                frcsv = d / "forces.csv"
                cd = float(list(csv.DictReader(frcsv.open()))[-1]["cd"]) if frcsv.exists() else None
                rank_rows.append((d.name, s.get("mpi_ranks"), s.get("final_step"), s.get("wall_time_seconds"), cd))
    with (REPORT/"mpi_rank_summary.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["case","np","steps","wall_time_seconds","Cd"])
        for rr in rank_rows:
            w.writerow(rr)


if __name__ == "__main__":
    main()
