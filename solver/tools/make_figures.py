#!/usr/bin/env python3
"""Generate every report figure and the figure manifest.

Produces into report/figures/:
  <case>_residuals.png, <case>_forces.png, <case>_surface_cp.png,
  <case>_mach.png, <case>_pressure.png (field contours, near-body window),
  <case>_mach_full.png, <case>_pressure_full.png (whole-domain),
  cylinder_m010_laminar_re200_vorticity.png (wake, clipped range),
and report/figure_manifest.csv.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path

import plot_results as pr

NACA_WINDOW = (-0.4, 1.6, -0.9, 0.9)
CYL_WINDOW = (-1.5, 4.5, -2.5, 2.5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", type=Path, default=Path("results"))
    ap.add_argument("--report", type=Path, default=Path("report"))
    args = ap.parse_args()

    figdir = args.report / "figures"
    figdir.mkdir(parents=True, exist_ok=True)
    manifest_rows = []

    def add(fig, case_id, ftype, var, src, caption):
        manifest_rows.append({
            "figure_file": fig, "case_id": case_id, "figure_type": ftype,
            "variable": var, "source_file": src, "caption": caption,
        })

    for d in sorted(args.results.iterdir()):
        if not (d / "metadata.json").exists():
            continue
        cid = d.name
        is_cyl = cid.startswith("cylinder")
        window = CYL_WINDOW if is_cyl else NACA_WINDOW

        pr.plot_residuals(d, figdir / f"{cid}_residuals.png")
        add(f"{cid}_residuals.png", cid, "line", "residual_l2", str(d / "residuals.csv"),
            f"Residual history for {cid}")

        pr.plot_forces(d, figdir / f"{cid}_forces.png")
        add(f"{cid}_forces.png", cid, "line", "cl_cd_cmz", str(d / "forces.csv"),
            f"Force history for {cid}")

        if is_cyl:
            pr.plot_surface_cp_cylinder(d, figdir / f"{cid}_surface_cp.png")
            add(f"{cid}_surface_cp.png", cid, "line", "cp_cf", str(d / "surface.csv"),
                f"Cylinder wall pressure coefficient and skin friction for {cid}")
        else:
            pr.plot_surface_cp(d, figdir / f"{cid}_surface_cp.png")
            add(f"{cid}_surface_cp.png", cid, "line", "cp", str(d / "surface.csv"),
                f"Surface pressure coefficient for {cid}")

        pr.plot_field(d, figdir / f"{cid}_mach.png", "mach", "Mach number", window=window)
        add(f"{cid}_mach.png", cid, "contour", "mach", str(d / "field_final.vtu"),
            f"Mach number field for {cid} (near-body view)")
        pr.plot_field(d, figdir / f"{cid}_pressure.png", "pressure", "pressure", window=window)
        add(f"{cid}_pressure.png", cid, "contour", "pressure", str(d / "field_final.vtu"),
            f"Pressure field for {cid} (near-body view)")
        pr.plot_field(d, figdir / f"{cid}_mach_full.png", "mach", "Mach number")
        add(f"{cid}_mach_full.png", cid, "contour", "mach", str(d / "field_final.vtu"),
            f"Mach number field for {cid} (whole domain)")
        pr.plot_field(d, figdir / f"{cid}_pressure_full.png", "pressure", "pressure")
        add(f"{cid}_pressure_full.png", cid, "contour", "pressure", str(d / "field_final.vtu"),
            f"Pressure field for {cid} (whole domain)")

        if cid == "cylinder_m010_laminar_re200":
            # wake vorticity with documented clipped range
            pr.plot_field(d, figdir / f"{cid}_vorticity.png", "vorticity", "vorticity",
                          window=(-1.5, 6.0, -3.0, 3.0), clip=(-5.0, 5.0), cmap="RdBu_r")
            add(f"{cid}_vorticity.png", cid, "contour", "vorticity",
                str(d / "field_final.vtu"),
                f"Vorticity wake (clipped to [-5,5]) for {cid}, post-transient vortex street")
            pr.plot_field(d, figdir / f"{cid}_velocity_magnitude.png", "velocity_magnitude",
                          "velocity magnitude", window=(-1.5, 6.0, -3.0, 3.0))
            add(f"{cid}_velocity_magnitude.png", cid, "contour", "velocity",
                str(d / "field_final.vtu"),
                f"Velocity magnitude wake for {cid}")

    mf = args.report / "figure_manifest.csv"
    with mf.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["figure_file", "case_id", "figure_type",
                                          "variable", "source_file", "caption"])
        w.writeheader()
        w.writerows(manifest_rows)
    print(f"wrote {mf} with {len(manifest_rows)} figures")


if __name__ == "__main__":
    main()
