#!/usr/bin/env python3
"""Generate report/sanity_checks.json and report/run_manifest.md from results."""
import csv
import json
import math
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RESULTS = os.path.join(ROOT, "results")
REPORT = os.path.join(ROOT, "report")
os.makedirs(REPORT, exist_ok=True)

CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]


def read_csv(case, name):
    p = os.path.join(RESULTS, case, name)
    if not os.path.exists(p):
        return None
    with open(p, newline="") as f:
        return list(csv.DictReader(f))


def check_field_positivity(case):
    """Density and pressure positive in the final field (VTU cell data)."""
    p = os.path.join(RESULTS, case, "field_final.vtu")
    if not os.path.exists(p):
        return {"ok": False, "detail": "missing field_final.vtu"}
    sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
    from plot_field import read_vtu
    _, _, _, data = read_vtu(p)
    rho, pr = data["Density"], data["Pressure"]
    ok = bool((rho > 0).all() and (pr > 0).all())
    return {
        "ok": ok,
        "detail": f"rho_min={rho.min():.6e} p_min={pr.min():.6e}",
    }


def mean_last(rows, key, frac=0.2):
    vals = [float(r[key]) for r in rows]
    n = max(1, int(len(vals) * frac))
    return sum(vals[-n:]) / n


def main():
    checks = {}
    for case in CASES:
        entry = {"case_id": case, "checks": {}}
        forces = read_csv(case, "forces.csv")
        surface = read_csv(case, "surface.csv")
        status = None
        sp = os.path.join(RESULTS, case, "run_status.json")
        if os.path.exists(sp):
            status = json.load(open(sp))

        # 1. positive density/pressure in the final field
        entry["checks"]["positive_field_density_pressure"] = check_field_positivity(case)

        # 2. NACA near-zero lift by symmetry
        if case.startswith("naca") and forces:
            cl = abs(mean_last(forces, "cl"))
            entry["checks"]["naca_near_zero_lift"] = {
                "ok": bool(cl < 0.05),
                "detail": f"mean |cl| (last 20%) = {cl:.5f}",
            }
        # 3. cylinder positive mean drag
        if case.startswith("cylinder") and forces:
            cd = mean_last(forces, "cd")
            entry["checks"]["cylinder_positive_mean_drag"] = {
                "ok": bool(cd > 0.0),
                "detail": f"mean cd (last 20%) = {cd:.5f}",
            }
        # 4. Re200 unsteady lift variation
        if "re200" in case and forces:
            vals = [float(r["cl"]) for r in forces[len(forces) // 2:]]
            amp = (max(vals) - min(vals)) / 2
            entry["checks"]["re200_unsteady_lift"] = {
                "ok": bool(amp > 0.01),
                "detail": f"cl amplitude (last 50%) = {amp:.5f}",
            }
        # 5. surface cp varies along the body
        if surface:
            cp = [float(r["cp"]) for r in surface]
            entry["checks"]["surface_cp_varies"] = {
                "ok": bool(max(cp) - min(cp) > 0.1),
                "detail": f"cp range = {min(cp):.4f} .. {max(cp):.4f}",
            }
        # 6. no-slip wall rows: near-zero velocity, nonzero cf
        if surface and "laminar" in case and case != "cylinder_m010_laminar_re200":
            us = [abs(float(r["u"])) for r in surface]
            cf = [abs(float(r["cf"])) for r in surface]
            entry["checks"]["no_slip_wall_velocity"] = {
                "ok": bool(max(us) < 1e-6),
                "detail": f"max |u_wall| = {max(us):.3e}",
            }
            entry["checks"]["no_slip_wall_friction"] = {
                "ok": bool(max(cf) > 1e-6),
                "detail": f"max cf = {max(cf):.5f}",
            }
        # 7. inviscid slip-wall rows: near-zero normal velocity, zero viscous columns
        if surface and "inviscid" in case:
            vn = [abs(float(r["u"]) * float(r["nx"]) + float(r["v"]) * float(r["ny"]))
                  for r in surface]
            entry["checks"]["slip_wall_normal_velocity"] = {
                "ok": bool(max(vn) < 1e-4),
                "detail": f"max |vn_wall| = {max(vn):.3e}",
            }
            if forces:
                vd = abs(float(forces[-1]["viscous_drag"]))
                vl = abs(float(forces[-1]["viscous_lift"]))
                entry["checks"]["inviscid_zero_viscous_forces"] = {
                    "ok": bool(vd < 1e-8 and vl < 1e-8),
                    "detail": f"viscous_drag={vd:.2e} viscous_lift={vl:.2e}",
                }
        # 8. final step consistency
        if forces and status:
            last_step = int(float(forces[-1]["step"]))
            entry["checks"]["force_row_matches_status"] = {
                "ok": bool(last_step == int(status["final_step"])),
                "detail": f"forces last step {last_step} vs status {status['final_step']}",
            }
        entry["convergence_status"] = status["convergence_status"] if status else None
        entry["overall_ok"] = all(c["ok"] for c in entry["checks"].values())
        checks[case] = entry

    with open(os.path.join(REPORT, "sanity_checks.json"), "w") as f:
        json.dump(checks, f, indent=2)

    # run manifest
    rows = []
    for case in CASES:
        sp = os.path.join(RESULTS, case, "run_status.json")
        mp = os.path.join(RESULTS, case, "metadata.json")
        if not os.path.exists(sp):
            rows.append(f"| {case} | missing run_status |")
            continue
        s = json.load(open(sp))
        m = json.load(open(mp)) if os.path.exists(mp) else {}
        rows.append(
            f"| {case} | {s.get('mpi_ranks','?')} | {s.get('final_step','?')} "
            f"| {s.get('final_physical_time','?')} | {s.get('residual_reduction_orders','?')} "
            f"| {s.get('wall_time_seconds','?'):.0f} s | {s.get('convergence_status','?')} |"
        )
    header = (
        "# Run Manifest\n\n"
        "| case | ranks | final step | final t | residual reduction (orders) | wall time | status |\n"
        "|---|---|---|---|---|---|---|\n"
    )
    with open(os.path.join(REPORT, "run_manifest.md"), "w") as f:
        f.write(header + "\n".join(rows) + "\n")
    print("wrote sanity_checks.json and run_manifest.md")

    write_partition_table()


def write_partition_table():
    """Partition table for two representative np=8 production runs
    (report requirement: owned/ghost cells, neighbors, send/recv, load
    balance for representative runs)."""
    reps = ["naca0012_m015_inviscid", "cylinder_m010_laminar_re20"]
    tex = []
    for case in reps:
        p = os.path.join(RESULTS, case, "partition_diagnostics.csv")
        if not os.path.exists(p):
            continue
        rows = list(csv.DictReader(open(p)))
        owned = [int(r["num_cells_owned"]) for r in rows]
        tex.append(
            "\\emph{" + case.replace("_", "\\_") + " (np=8):}\n"
            "\\begin{center}\n\\small\n"
            "\\begin{tabular}{rrrrrrr}\n\\toprule\n"
            "Rank & Owned & Ghost & Bnd faces & Neighbors & Send & Recv \\\\\n"
            "\\midrule\n")
        for r in rows:
            tex.append(
                f"{r['rank']} & {r['num_cells_owned']} & {r['num_cells_ghost']} "
                f"& {r['num_boundary_faces']} & {r['num_neighbor_ranks']} "
                f"& {r['send_cells']} & {r['recv_cells']} \\\\")
        lb = max(owned) / min(owned)
        tex.append(
            "\\bottomrule\n\\end{tabular}\n\\end{center}\n"
            f"Load balance (max/min owned): {lb:.3f}.\n")
    with open(os.path.join(REPORT, "partition_table.tex"), "w") as f:
        f.write("\n".join(tex) + "\n")
    print("wrote partition_table.tex")


if __name__ == "__main__":
    main()
