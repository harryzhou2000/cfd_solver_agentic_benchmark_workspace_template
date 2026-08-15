#!/usr/bin/env python3
"""Generate run_manifest.csv, figure_manifest.csv, and sanity_checks.json
from the actual solver outputs under solver/results/.

Every sanity check is verified against the real data before it is recorded:
positivity from the field VTU, NACA symmetry from the force/surface data,
wall-condition checks from surface.csv, figure-variable checks from the
figure manifest itself.

Usage:  python3 tools/generate_manifests.py
"""

import csv
import json
import os
import sys
import xml.etree.ElementTree as ET

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from plot_results import read_field, compute_vorticity  # noqa: E402

WORKSPACE = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
RESULTS = os.path.join(WORKSPACE, "solver", "results")
REPORT = os.path.join(WORKSPACE, "solver", "report")
FIGURES = os.path.join(REPORT, "figures")

# case output dir, mpi ranks, exact command suffix used
CASES = [
    ("naca0012_m015_inviscid", 1,
     "--case cfd_solver_agentic_benchmark/inputs/cases/"
     "naca0012_m015_inviscid.json --output solver/results/"
     "naca0012_m015_inviscid"),
    ("naca0012_m015_inviscid_np2", 2,
     "--case cfd_solver_agentic_benchmark/inputs/cases/"
     "naca0012_m015_inviscid.json --output solver/results/"
     "naca0012_m015_inviscid_np2"),
    ("naca0012_m015_inviscid_np4", 4,
     "--case cfd_solver_agentic_benchmark/inputs/cases/"
     "naca0012_m015_inviscid.json --output solver/results/"
     "naca0012_m015_inviscid_np4"),
    ("naca0012_m015_inviscid_np8", 8,
     "--case cfd_solver_agentic_benchmark/inputs/cases/"
     "naca0012_m015_inviscid.json --output solver/results/"
     "naca0012_m015_inviscid_np8"),
    ("cylinder_m010_laminar_re20", 1,
     "--case cfd_solver_agentic_benchmark/inputs/cases/"
     "cylinder_m010_laminar_re20.json --output solver/results/"
     "cylinder_m010_laminar_re20"),
    ("cylinder_m010_laminar_re200", 2,
     "--case cfd_solver_agentic_benchmark/inputs/cases/"
     "cylinder_m010_laminar_re200.json --output solver/results/"
     "cylinder_m010_laminar_re200 --max-steps 100"),
]


def read_csv_rows(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def figure_variable(fname):
    """Map a figure file name to (case_id, variable, source_file, type)."""
    # Longest case id first: naca0012_m015_inviscid_np2 must match before
    # naca0012_m015_inviscid.
    for case, _, _ in sorted(CASES, key=lambda c: len(c[0]), reverse=True):
        if fname.startswith(case + "_"):
            rest = fname[len(case) + 1:]
            var = rest.split(".png")[0]
            if var.endswith("_zoom"):
                var = var[: -len("_zoom")]
            sources = {
                "residuals": ("results/{}/residuals.csv", "residual_l2",
                              "line"),
                "forces": ("results/{}/forces.csv", "cd", "line"),
                "cp": ("results/{}/surface.csv", "cp", "line"),
                "cf": ("results/{}/surface.csv", "cf", "line"),
                "mach": ("results/{}/field_final.pvtu", "mach", "contour"),
                "pressure": ("results/{}/field_final.pvtu", "pressure",
                             "contour"),
                "vorticity": ("results/{}/field_final.pvtu", "vorticity",
                              "contour"),
            }
            if var in sources:
                src_fmt, var_name, ftype = sources[var]
                return case, var_name, src_fmt.format(case), ftype
            raise ValueError(f"unrecognized figure type: {fname}")
    return None, None, None, None


def captions():
    return {
        "residuals": "Residual history (per-variable and aggregate L2 norms)",
        "forces": "Force coefficient history (Cd, Cl)",
        "cp": "Surface pressure coefficient Cp",
        "cf": "Surface skin-friction coefficient Cf",
        "mach": "Mach number field",
        "mach_zoom": "Mach number field, near-body zoom",
        "pressure": "Pressure field",
        "pressure_zoom": "Pressure field, near-body zoom",
        "vorticity": "Vorticity field (clip [-5,5] for the wake)",
    }


def main():
    # ------------------------------------------------------------------
    # run_manifest.csv
    # ------------------------------------------------------------------
    with open(os.path.join(REPORT, "run_manifest.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["case_id", "command", "mpi_ranks", "wall_time_seconds",
                    "steps", "residual_orders", "status"])
        for case, np_, suffix in CASES:
            rs = json.load(open(os.path.join(RESULTS, case,
                                             "run_status.json")))
            md = json.load(open(os.path.join(RESULTS, case,
                                             "metadata.json")))
            cmd = (f"mpirun -np {np_} solver/build/cfd_solver solve {suffix}")
            w.writerow([case, cmd, np_, f"{rs['wall_time_seconds']:.1f}",
                        md["steps_run"],
                        f"{rs['residual_reduction_orders']:.3f}",
                        rs["convergence_status"]])
    print("wrote run_manifest.csv")

    # ------------------------------------------------------------------
    # figure_manifest.csv (only for figures that exist on disk)
    # ------------------------------------------------------------------
    caps = captions()
    entries = []
    for fname in sorted(os.listdir(FIGURES)):
        if not fname.endswith(".png"):
            continue
        case, var, source, ftype = figure_variable(fname)
        if case is None:
            raise ValueError(f"figure {fname} does not match any case")
        rest = fname[len(case) + 1:].split(".png")[0]
        caption = caps.get(rest, rest.replace("_", " "))
        entries.append({
            "figure_file": fname, "case_id": case, "figure_type": ftype,
            "variable": var, "source_file": source, "caption": caption,
        })
    with open(os.path.join(REPORT, "figure_manifest.csv"), "w",
              newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(entries[0].keys()))
        w.writeheader()
        w.writerows(entries)
    print(f"wrote figure_manifest.csv ({len(entries)} figures)")

    # ------------------------------------------------------------------
    # sanity checks, each verified against the data
    # ------------------------------------------------------------------
    checks = {}

    # 1. positive density/pressure in the final fields of every case
    checks["positive_density_pressure"] = True
    for case, _, _ in CASES:
        field = read_field(os.path.join(RESULTS, case))
        rho = field["data"]["density"]
        p = field["data"]["pressure"]
        ok = bool(np.all(rho > 0) and np.all(p > 0))
        if not ok:
            checks["positive_density_pressure"] = False
            print(f"  FAIL: positivity in {case}")

    # 2. NACA zero-lift symmetry: |Cl| small and upper/lower Cp agree at
    #    the same x stations (surfaces are x-monotonic along the airfoil)
    frows = read_csv_rows(os.path.join(
        RESULTS, "naca0012_m015_inviscid", "forces.csv"))
    cl = abs(float(frows[-1]["cl"]))
    srows = read_csv_rows(os.path.join(
        RESULTS, "naca0012_m015_inviscid", "surface.csv"))
    upper_x = np.array([float(r["x"]) for r in srows if float(r["y"]) > 0])
    upper_cp = np.array([float(r["cp"]) for r in srows if float(r["y"]) > 0])
    lower_x = np.array([float(r["x"]) for r in srows if float(r["y"]) <= 0])
    lower_cp = np.array([float(r["cp"]) for r in srows if float(r["y"]) <= 0])
    order_u = np.argsort(upper_x)
    order_l = np.argsort(lower_x)
    upper_x, upper_cp = upper_x[order_u], upper_cp[order_u]
    lower_x, lower_cp = lower_x[order_l], lower_cp[order_l]
    x0 = max(upper_x.min(), lower_x.min())
    x1 = min(upper_x.max(), lower_x.max())
    xg = np.linspace(x0, x1, 201)
    cp_u = np.interp(xg, upper_x, upper_cp)
    cp_l = np.interp(xg, lower_x, lower_cp)
    cp_diff = float(np.max(np.abs(cp_u - cp_l)))
    checks["naca_zero_lift_symmetry"] = bool(cl < 1e-4 and cp_diff < 0.05)
    print(f"  naca symmetry: |Cl|={cl:.2e}, max upper/lower Cp diff={cp_diff:.4f}")

    # 3. cylinder positive drag (Re20)
    f20 = read_csv_rows(os.path.join(
        RESULTS, "cylinder_m010_laminar_re20", "forces.csv"))
    cd20 = float(f20[-1]["cd"])
    checks["cylinder_positive_drag"] = bool(cd20 > 1.0)
    print(f"  re20 drag: Cd={cd20:.4f}")

    # 4. Re200 unsteady lift: not yet statistically periodic (100 steps)
    f200 = read_csv_rows(os.path.join(
        RESULTS, "cylinder_m010_laminar_re200", "forces.csv"))
    cl200 = np.array([float(r["cl"]) for r in f200])
    checks["cylinder_re200_unsteady_lift"] = bool(
        np.std(cl200) > 0.01 and len(cl200) >= 30000)
    print(f"  re200: 100-step run, lift std={np.std(cl200):.4f} "
          "(periodicity not reached -> false)")

    # 5. surface Cp varies
    cp_range = float(np.max(np.abs(
        np.array([float(r["cp"]) for r in srows]))))
    checks["surface_cp_varies"] = bool(cp_range > 0.5)

    # 6. no-slip wall velocity near zero (re20 + re200)
    checks["no_slip_wall_velocity"] = True
    for case in ("cylinder_m010_laminar_re20", "cylinder_m010_laminar_re200"):
        ws = read_csv_rows(os.path.join(RESULTS, case, "surface.csv"))
        walls = [r for r in ws if r["tag"].upper() == "WALL"]
        if not walls:
            walls = ws
        vmax = max(max(abs(float(r["u"])), abs(float(r["v"]))) for r in walls)
        if vmax > 1e-6:
            checks["no_slip_wall_velocity"] = False
            print(f"  FAIL: wall velocity {vmax} in {case}")

    # 7. slip-wall normal velocity near zero (naca)
    naca_walls = [r for r in srows if r["tag"].lower() == "bc-4"]
    vn = max(abs(float(r["u"]) * float(r["nx"]) +
                 float(r["v"]) * float(r["ny"])) for r in naca_walls)
    checks["slip_wall_normal_velocity"] = bool(vn < 1e-6)
    print(f"  slip wall: max |vn| = {vn:.2e}")

    # 8. mach and pressure in separate figures with matching names
    mach_figs = [e for e in entries if "mach" in e["figure_file"]]
    press_figs = [e for e in entries if "pressure" in e["figure_file"]]
    checks["mach_and_pressure_figures_separate"] = bool(
        mach_figs and press_figs
        and all("mach" in e["variable"] for e in mach_figs)
        and all("pressure" in e["variable"] for e in press_figs))

    # 9. figure manifest complete: every entry exists and every case has
    #    mach + pressure
    checks["figure_manifest_complete"] = True
    for e in entries:
        if not os.path.exists(os.path.join(FIGURES, e["figure_file"])):
            checks["figure_manifest_complete"] = False
            print(f"  FAIL: manifest entry missing on disk: {e['figure_file']}")
    for case, _, _ in CASES:
        vs = {e["variable"] for e in entries if e["case_id"] == case}
        if "mach" not in vs or "pressure" not in vs:
            checks["figure_manifest_complete"] = False
            print(f"  FAIL: {case} missing mach/pressure manifest entries")

    with open(os.path.join(REPORT, "sanity_checks.json"), "w") as f:
        json.dump(checks, f, indent=2)
        f.write("\n")
    print("wrote sanity_checks.json:")
    for k, v in checks.items():
        print(f"  {k}: {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
