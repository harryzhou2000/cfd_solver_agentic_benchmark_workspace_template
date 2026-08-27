#!/usr/bin/env python3
"""Mesh and flow facts quoted in the report prose.

Everything the report says about cell sizes, aspect ratios, the concentration of
the unweighted residual in the trailing-edge slivers, boundary-layer thickness
and the skin-friction comparison with Blasius is computed here and written to
report/mesh_facts.json, so that no such number has to be typed by hand.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vtu_reader import read_vtu  # noqa: E402

GAMMA = 1.4


def cell_edge_lengths(mesh, i):
    p = mesh.points[mesh.cell_nodes(i)]
    d = np.roll(p, -1, axis=0) - p
    return np.hypot(d[:, 0], d[:, 1])


def aspect_ratios(mesh):
    """Longest edge over shortest edge, the usual measure for a thin cell."""
    out = np.empty(mesh.n_cells)
    for i in range(mesh.n_cells):
        e = cell_edge_lengths(mesh, i)
        out[i] = e.max() / max(e.min(), 1e-300)
    return out


def geometry_facts(mesh):
    a = mesh.cell_areas()
    ar = aspect_ratios(mesh)
    return dict(num_cells=int(mesh.n_cells),
                min_cell_area=float(a.min()), max_cell_area=float(a.max()),
                median_cell_area=float(np.median(a)),
                max_aspect_ratio=float(ar.max()),
                median_aspect_ratio=float(np.median(ar)),
                cells_above_aspect_ratio_1e3=int((ar > 1e3).sum()),
                cells_above_aspect_ratio_1e4=int((ar > 1e4).sum()))


def mirror_symmetry(mesh, xlo, xhi):
    """How nearly mirror-symmetric the mesh is about y=0 over [xlo, xhi].

    The benchmark cases are symmetric sections at zero incidence, so any
    asymmetry in the computed solution is bounded below by the asymmetry of the
    mesh itself.  Each cell above the axis is matched to the nearest cell below
    it after reflection, and the offset is reported in units of the local cell
    size.
    """
    from scipy.spatial import cKDTree

    xy = mesh.cell_centers()
    a = mesh.cell_areas()
    sel = np.where((xy[:, 0] > xlo) & (xy[:, 0] < xhi))[0]
    pos, neg = sel[xy[sel, 1] > 0], sel[xy[sel, 1] < 0]
    if pos.size == 0 or neg.size == 0:
        return None
    tree = cKDTree(np.c_[xy[neg, 0], -xy[neg, 1]])
    d, _ = tree.query(np.c_[xy[pos, 0], xy[pos, 1]])
    rel = d / np.sqrt(a[pos])
    return dict(window=[xlo, xhi], num_upper=int(pos.size), num_lower=int(neg.size),
                median_offset_over_cell_size=float(np.median(rel)),
                p95_offset_over_cell_size=float(np.percentile(rel, 95)),
                fraction_without_close_mirror=float((rel > 0.1).mean()))


def sliver_concentration(mesh, frac=0.90):
    """How few cells carry `frac` of an *unweighted* sum of squared residuals.

    This is the measurement that motivates the volume-weighted residual norm.
    """
    r = mesh.cell_data["ScaledResidual"]
    a = mesh.cell_areas()
    s = np.sort(r**2)[::-1]
    order = np.argsort(r**2)[::-1]
    csum = np.cumsum(s)
    n = int(np.searchsorted(csum, frac * csum[-1]) + 1)
    sel = order[:n]
    return dict(fraction=frac, num_cells=n,
                mean_cell_area=float(a[sel].mean()),
                median_cell_area_all=float(np.median(a)),
                area_fraction=float(a[sel].sum() / a.sum()))


def boundary_layer(mesh, x_over_c, chord, half_thickness, search=0.25):
    """delta_99 on a wall-normal ray at x/c, walking outward from the wall.

    The edge velocity is the maximum along the ray inside `search`, not the
    freestream: at Mach 2 the flow above the section has already been through an
    oblique shock and an expansion, so u_infinity is not the edge value.
    """
    xy = mesh.cell_centers()
    u = mesh.cell_data["VelocityX"]
    x0 = x_over_c * chord
    band = np.abs(xy[:, 0] - x0) < 0.01 * chord
    if not band.any():
        return None
    y, uu = xy[band, 1], u[band]
    up = y > half_thickness * 0.5
    if not up.any():
        return None
    o = np.argsort(y[up])
    yy, ue = y[up][o] - half_thickness, uu[up][o]
    inside = yy < search
    if inside.sum() < 5:
        return None
    u_edge = float(ue[inside].max())
    reach = np.where(ue >= 0.99 * u_edge)[0]
    if reach.size == 0:
        return None
    d99 = float(yy[int(reach[0])])
    return dict(x_over_c=x_over_c, delta99=d99, edge_velocity=u_edge,
                local_half_thickness=half_thickness,
                delta99_over_half_thickness=d99 / half_thickness)


def naca_half_thickness(xc, t=0.12):
    return 5.0 * t * (0.2969 * np.sqrt(xc) - 0.1260 * xc - 0.3516 * xc**2
                      + 0.2843 * xc**3 - 0.1015 * xc**4)


def blasius_check(case_dir, x_over_c, re_ref):
    rows = list(csv.DictReader(open(os.path.join(case_dir, "surface.csv"), newline="")))
    x = np.array([float(r["x"] ) for r in rows])
    cf = np.array([float(r["cf"]) for r in rows])
    ny = np.array([float(r["ny"]) for r in rows])
    chord = x.max() - x.min()
    xc = (x - x.min()) / chord
    up = ny < 0.0
    o = np.argsort(xc[up])
    computed = float(np.interp(x_over_c, xc[up][o], cf[up][o]))
    blasius = 0.664 / np.sqrt(re_ref * x_over_c)
    return dict(x_over_c=x_over_c, computed_cf=computed, blasius_cf=float(blasius),
                relative_difference=float(abs(computed - blasius) / blasius))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default="results")
    ap.add_argument("--out", default="report/mesh_facts.json")
    args = ap.parse_args()

    out = dict(generated_by="tools/mesh_facts.py", meshes={}, cases={})

    # One representative case per mesh carries the geometry.
    for tag, cid in (("naca", "naca0012_m200_inviscid"),
                     ("cylinder", "cylinder_m010_laminar_re20")):
        f = os.path.join(args.results, cid, "field_final.vtu")
        if not os.path.exists(f):
            continue
        m = read_vtu(f)
        out["meshes"][tag] = geometry_facts(m)
        out["meshes"][tag]["source_case"] = cid
        if tag == "naca":
            # The concentration depends on the solution as well as the mesh, so
            # it is reported across every case that shares this mesh rather than
            # from one case presented as a property of the grid.
            conc = {}
            for other in sorted(os.listdir(args.results)):
                f2 = os.path.join(args.results, other, "field_final.vtu")
                if other.startswith("naca") and os.path.exists(f2):
                    conc[other] = sliver_concentration(read_vtu(f2))
            out["meshes"][tag]["sliver_residual_concentration"] = conc.get(cid)
            out["meshes"][tag]["sliver_residual_concentration_by_case"] = conc
            if conc:
                counts = [v["num_cells"] for v in conc.values()]
                out["meshes"][tag]["sliver_cells_min"] = min(counts)
                out["meshes"][tag]["sliver_cells_max"] = max(counts)
            out["meshes"][tag]["leading_edge_mirror_symmetry"] = mirror_symmetry(m, -0.02, 0.02)
            out["meshes"][tag]["body_mirror_symmetry"] = mirror_symmetry(m, -0.5, 1.5)

    # L-infinity residual plateau across the aerofoil cases.
    linf = {}
    for cid in sorted(os.listdir(args.results)):
        p = os.path.join(args.results, cid, "residuals.csv")
        if not os.path.exists(p) or not cid.startswith("naca"):
            continue
        rows = list(csv.DictReader(open(p, newline="")))
        linf[cid] = float(rows[-1]["residual_linf"])
    if linf:
        out["aerofoil_final_residual_linf"] = dict(
            values=linf, min=min(linf.values()), max=max(linf.values()))

    # Boundary layer on the Mach 2 laminar aerofoil at mid-chord.
    f = os.path.join(args.results, "naca0012_m200_laminar_re5000", "field_final.vtu")
    if os.path.exists(f):
        m = read_vtu(f)
        # The section runs from x = 0 to x = 1.005 (see the mesh table), so the
        # nominal unit chord places the station within half a percent of
        # mid-chord.  Deriving a chord from the cell centres would give the
        # farfield extent, not the body.
        bl = boundary_layer(m, 0.5, 1.0, float(naca_half_thickness(0.5)))
        if bl:
            out["cases"]["naca0012_m200_laminar_re5000"] = dict(boundary_layer=bl)

    d = os.path.join(args.results, "naca0012_m015_laminar_re5000")
    if os.path.exists(os.path.join(d, "surface.csv")):
        out["cases"].setdefault("naca0012_m015_laminar_re5000", {})["blasius"] = \
            blasius_check(d, 0.3, 5000.0)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    json.dump(out, open(args.out, "w"), indent=2)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
