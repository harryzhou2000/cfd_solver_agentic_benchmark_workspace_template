"""Shared helpers for fv2d post-processing tools (plot_case, sanity_check,
make_figure_manifest)."""
import json
import os

import numpy as np

# Near-body zoom windows: (xmin, xmax, ymin, ymax)
ZOOM_WINDOWS = {
    "naca": (-0.5, 1.5, -1.0, 1.0),
    "cylinder": (-2.0, 6.0, -3.0, 3.0),
}

CSV_RESIDUALS = "residuals.csv"
CSV_FORCES = "forces.csv"
CSV_SURFACE = "surface.csv"
FIELD_FINAL = "field_final.vtk"


def load_csv(path):
    """Load a contract CSV with a header row into a dict of column arrays."""
    data = np.genfromtxt(path, delimiter=",", names=True, dtype=None,
                         encoding=None)
    if data.shape == ():
        data = np.array([data], dtype=data.dtype)
    out = {}
    for name in data.dtype.names:
        out[name] = data[name]
    return out


def read_metadata(case_dir):
    path = os.path.join(case_dir, "metadata.json")
    if os.path.isfile(path):
        with open(path) as f:
            return json.load(f)
    return {}


def case_id_of(case_dir, metadata=None):
    if metadata is None:
        metadata = read_metadata(case_dir)
    cid = metadata.get("case_id")
    if cid:
        return str(cid)
    return os.path.basename(os.path.normpath(case_dir))


def classify_case(case_dir):
    """Return dict(case_id, kind in {'cylinder','naca','unknown'}, viscous,
    transient) for a case result directory.

    Cylinder detection: surface.csv has a tag containing 'WALL' and the wall
    x-range is ~[-0.5, 0.5]; otherwise the case is treated as an airfoil.
    Falls back to case-id substrings when surface.csv is missing.
    """
    meta = read_metadata(case_dir)
    cid = case_id_of(case_dir, meta)
    kind = "unknown"
    low = cid.lower()
    surf_path = os.path.join(case_dir, CSV_SURFACE)
    surf_kind = "unknown"
    if os.path.isfile(surf_path):
        try:
            surf = load_csv(surf_path)
            x = np.asarray(surf["x"], dtype=float)
            y = np.asarray(surf["y"], dtype=float)
            if x.size:
                r = np.hypot(x, y)
                rmean = float(r.mean())
                # cylinder wall: points near origin on a ~constant-radius
                # circle of radius ~0.5; an airfoil chord has strongly
                # varying radius (sharp leading edge vs trailing edge)
                circular = (x.min() >= -0.6 and x.max() <= 0.6
                            and y.min() >= -0.6 and y.max() <= 0.6
                            and 0.3 <= rmean <= 0.7
                            and float(r.std()) < 0.2 * rmean)
                surf_kind = "cylinder" if circular else "naca"
        except Exception:
            pass
    # case-id hint only fills in when surface.csv is missing/unreadable
    if surf_kind != "unknown":
        kind = surf_kind
    elif "cyl" in low:
        kind = "cylinder"
    elif "naca" in low:
        kind = "naca"
    viscous = "none" not in str(meta.get("viscous_flux", "")).lower()
    transient = False
    res_path = os.path.join(case_dir, CSV_RESIDUALS)
    if os.path.isfile(res_path):
        try:
            res = load_csv(res_path)
            pt = np.asarray(res["physical_time"], dtype=float)
            transient = bool(pt.size and (pt.max() - pt.min()) > 1e-9)
        except Exception:
            pass
    return {"case_id": cid, "kind": kind, "viscous": viscous,
            "transient": transient, "metadata": meta}


def build_triangulation(vtk):
    """Build (matplotlib Triangulation, tri->cell index array) from a dict
    returned by fv_vtk.read_vtk. Quads are split into two triangles so
    cell-centered values can be repeated per sub-triangle."""
    from matplotlib.tri import Triangulation

    pts = vtk["points"]
    tris = []
    tri_cell = []
    for icell, conn in enumerate(vtk["cells"]):
        conn = list(conn)
        if len(conn) == 3:
            tris.append(conn)
            tri_cell.append(icell)
        elif len(conn) >= 4:
            # fan split (covers quads as (0,1,2),(0,2,3))
            for k in range(1, len(conn) - 1):
                tris.append([conn[0], conn[k], conn[k + 1]])
                tri_cell.append(icell)
    triang = Triangulation(pts[:, 0], pts[:, 1], np.asarray(tris, dtype=np.int64))
    return triang, np.asarray(tri_cell, dtype=np.int64)


def clipped_range(values, p_lo=1.0, p_hi=99.0):
    """Return (vmin, vmax, clipped). Uses percentile-clipped limits when the
    raw data range is dominated by outliers."""
    v = np.asarray(values, dtype=float)
    v = v[np.isfinite(v)]
    if v.size == 0:
        return 0.0, 1.0, False
    lo, hi = float(v.min()), float(v.max())
    if hi - lo <= 0.0:
        pad = abs(lo) * 1e-3 + 1e-12
        return lo - pad, hi + pad, False
    qlo = float(np.percentile(v, p_lo))
    qhi = float(np.percentile(v, p_hi))
    if qhi - qlo < 0.8 * (hi - lo):
        if qhi - qlo <= 0.0:
            qhi = qlo + abs(qlo) * 1e-3 + 1e-12
        return qlo, qhi, True
    return lo, hi, False


def list_field_snapshots(case_dir):
    """Return sorted list of intermediate transient field paths
    (fields/field_t*.vtk), oldest to newest (sorted by the time stamp
    encoded in the file name, not lexicographically)."""
    fdir = os.path.join(case_dir, "fields")
    if not os.path.isdir(fdir):
        return []
    names = [n for n in os.listdir(fdir)
             if n.startswith("field_t") and n.endswith(".vtk")]
    def _key(n):
        stamp = n[len("field_t"):-len(".vtk")]
        try:
            return (0, float(stamp))
        except ValueError:
            return (1, n)
    return [os.path.join(fdir, n) for n in sorted(names, key=_key)]


def pick_wake_field(case_dir):
    """Pick the field file for a post-transient wake visualization: prefer
    the latest intermediate field, else field_final.vtk. Returns
    (path or None, note)."""
    snaps = list_field_snapshots(case_dir)
    if snaps:
        return snaps[-1], os.path.basename(snaps[-1])
    final = os.path.join(case_dir, FIELD_FINAL)
    if os.path.isfile(final):
        return final, "source: field_final.vtk (no snapshots)"
    return None, "no field file found"
