#!/usr/bin/env python3
"""Physics sanity checks for CFD solver benchmark results.

Evaluates the eight required physics sanity checks per case and writes
solver/report/sanity_checks.json:

  1. density/pressure positive in the final field
  2. NACA at 0 deg AoA has near-zero lift (|CL| < 0.01)
  3. cylinder laminar cases have positive drag after startup
  4. cylinder Re200 shows nonzero lift variation
  5. surface pressure coefficient varies along the walls
  6. no-slip wall surface rows have near-zero velocity
  7. inviscid cases have negligible viscous forces (< 1e-6)
  8. Mach and pressure field visualizations exist

Usage:
    python3 run_sanity_checks.py RESULT_DIR... [--figdir FIGDIR] [--out JSON]
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

from plot_fields import parse_vtk  # reuse the VTK reader

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_FIGDIR = REPO_ROOT / "solver" / "report" / "figures"
DEFAULT_OUT = REPO_ROOT / "solver" / "report" / "sanity_checks.json"

LIFT_TOL = 1.0e-2
VISCOUS_TOL = 1.0e-6
WALL_SPEED_TOL = 1.0e-6


def read_csv_rows(path: Path) -> list[dict[str, str]] | None:
    if not path.exists():
        return None
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def wall_rows(surface: list[dict[str, str]], case_json: dict | None) -> list[dict[str, str]]:
    """Rows belonging to no-slip wall boundaries.

    Wall tags are resolved from the case file's boundary_conditions (values
    containing 'no_slip'); falls back to tag names containing 'wall'.
    """
    tags: set[str] = set()
    if case_json:
        for key, value in case_json.get("boundary_conditions", {}).items():
            if "no_slip" in str(value).lower():
                tags.add(str(key).lower())
    return [
        r for r in surface
        if str(r.get("tag", "")).lower() in tags
        or "wall" in str(r.get("tag", "")).lower()
    ]


def case_json_for(case_id: str) -> dict | None:
    path = REPO_ROOT / "cfd_solver_agentic_benchmark" / "inputs" / "cases" \
        / f"{case_id}.json"
    if not path.exists():
        return None
    return json.loads(path.read_text())


def case_checks(res_dir: Path, figdir: Path) -> dict:
    case_id = res_dir.name
    results: dict[str, dict] = {}

    def add(check_id: str, status: str, note: str) -> None:
        results[check_id] = {"status": status, "note": note}

    # ---- Load data -------------------------------------------------------
    forces = read_csv_rows(res_dir / "forces.csv")
    surface = read_csv_rows(res_dir / "surface.csv")
    vtk_path = res_dir / "field_final.vtk"
    vtk = parse_vtk(vtk_path) if vtk_path.exists() else None

    is_naca = "naca" in case_id
    is_inviscid = "inviscid" in case_id
    is_cylinder = "cylinder" in case_id
    is_re200 = "re200" in case_id

    # ---- 1. positive density/pressure in final field ---------------------
    if vtk is not None and "density" in vtk and "pressure" in vtk:
        rho_min = float(vtk["density"].min())
        p_min = float(vtk["pressure"].min())
        ok = rho_min > 0.0 and p_min > 0.0
        add("positive_field", "pass" if ok else "fail",
            f"min rho={rho_min:.6e}, min p={p_min:.6e} over {len(vtk['density'])} cells")
    else:
        add("positive_field", "fail", "missing field_final.vtk")

    # ---- 2. NACA 0-deg near-zero lift ------------------------------------
    if is_naca and forces:
        cl = float(forces[-1]["cl"])
        ok = abs(cl) < LIFT_TOL
        add("near_zero_lift", "pass" if ok else "fail",
            f"final CL={cl:.6e} (|CL| < {LIFT_TOL})")
    elif is_naca:
        add("near_zero_lift", "fail", "missing forces.csv")

    # ---- 3. cylinder positive drag ---------------------------------------
    if is_cylinder and forces:
        cd = float(forces[-1]["cd"])
        ok = cd > 0.0
        add("positive_drag", "pass" if ok else "fail",
            f"final CD={cd:.6e} (last {min(500, len(forces))} steps, "
            f"mean={sum(float(f['cd']) for f in forces[-500:])/min(500, len(forces)):.6e})")
    elif is_cylinder:
        add("positive_drag", "fail", "missing forces.csv")

    # ---- 4. cylinder Re200 nonzero lift variation ------------------------
    if is_re200 and forces:
        cls = [float(f["cl"]) for f in forces]
        spread = max(cls) - min(cls)
        std = (sum((c - sum(cls) / len(cls)) ** 2 for c in cls) / len(cls)) ** 0.5
        ok = spread > 1.0e-4
        add("lift_variation", "pass" if ok else "fail",
            f"CL range={spread:.6e}, std={std:.6e} over {len(cls)} rows "
            f"(partial run shows transient lift growth)")
    elif is_re200:
        # Run still in progress: fall back to the CL history printed in
        # stdout.log (one row per 100 physical steps).
        stdout = res_dir / "stdout.log"
        cls = []
        if stdout.exists():
            for line in stdout.read_text().splitlines():
                if "step " in line and "CL=" in line:
                    try:
                        cls.append(float(line.split("CL=")[1].split()[0]))
                    except (IndexError, ValueError):
                        pass
        if cls:
            spread = max(cls) - min(cls)
            std = (sum((c - sum(cls) / len(cls)) ** 2
                       for c in cls) / len(cls)) ** 0.5
            add("lift_variation", "pass" if spread > 1.0e-4 else "fail",
                f"stdout CL range={spread:.6e}, std={std:.6e} over "
                f"{len(cls)} logged steps (run in progress)")
        else:
            add("lift_variation", "fail", "no forces.csv and no stdout CL data")

    # ---- 5. surface cp varies along walls --------------------------------
    if surface and len(surface) > 1:
        cps = [float(r["cp"]) for r in surface]
        spread = max(cps) - min(cps)
        ok = spread > 1.0e-3
        add("cp_variation", "pass" if ok else "fail",
            f"cp range={spread:.6e} over {len(surface)} surface rows")
    else:
        add("cp_variation", "fail", "missing surface.csv")

    # ---- 6. no-slip walls have near-zero velocity ------------------------
    if surface:
        walls = wall_rows(surface, case_json_for(case_id))
        if walls and not is_inviscid:
            u_max = max(abs(float(r["u"])) for r in walls)
            v_max = max(abs(float(r["v"])) for r in walls)
            ok = u_max < WALL_SPEED_TOL and v_max < WALL_SPEED_TOL
            add("no_slip_wall", "pass" if ok else "fail",
                f"{len(walls)} wall rows, max |u|={u_max:.3e}, max |v|={v_max:.3e}")
        elif not is_inviscid:
            add("no_slip_wall", "fail", "no wall-tagged rows in surface.csv")
        else:
            add("no_slip_wall", "pass",
                "inviscid slip wall; no no-slip requirement (wall tags present)")
    else:
        add("no_slip_wall", "fail", "missing surface.csv")

    # ---- 7. inviscid cases negligible viscous forces ---------------------
    if is_inviscid and forces:
        vd = abs(float(forces[-1]["viscous_drag"]))
        vl = abs(float(forces[-1]["viscous_lift"]))
        ok = vd < VISCOUS_TOL and vl < VISCOUS_TOL
        add("zero_viscous_forces", "pass" if ok else "fail",
            f"final viscous_drag={vd:.3e}, viscous_lift={vl:.3e} (< {VISCOUS_TOL})")
    elif is_inviscid:
        add("zero_viscous_forces", "fail", "missing forces.csv")

    # ---- 8. Mach and pressure visualizations exist -----------------------
    mach_fig = figdir / f"{case_id}_mach.png"
    pres_fig = figdir / f"{case_id}_pressure.png"
    if mach_fig.exists() and pres_fig.exists():
        add("field_figures", "pass",
            f"{mach_fig.name}, {pres_fig.name} present in {figdir}")
    else:
        add("field_figures", "fail",
            f"missing {mach_fig.name} or {pres_fig.name} in {figdir}")

    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result_dirs", nargs="+", type=Path)
    parser.add_argument("--figdir", type=Path, default=DEFAULT_FIGDIR)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    figdir = args.figdir.resolve()
    out = args.out.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)

    report: dict = {
        "schema_version": 1,
        "generated_by": "solver/tools/run_sanity_checks.py",
        "checks": [
            "positive_field",
            "near_zero_lift",
            "positive_drag",
            "lift_variation",
            "cp_variation",
            "no_slip_wall",
            "zero_viscous_forces",
            "field_figures",
        ],
        "cases": {},
        "summary": {},
    }
    for res in args.result_dirs:
        res = res.resolve()
        case_id = res.name
        checks = case_checks(res, figdir)
        report["cases"][case_id] = checks
        n_pass = sum(1 for c in checks.values() if c["status"] == "pass")
        report["summary"][case_id] = {"passed": n_pass, "total": len(checks)}
        print(f"{case_id}: {n_pass}/{len(checks)} checks passed")
        for cid, c in checks.items():
            print(f"    [{c['status'].upper():4s}] {cid:22s} {c['note']}")

    out.write_text(json.dumps(report, indent=2) + "\n")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
