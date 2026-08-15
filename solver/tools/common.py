"""Shared helpers for report-infrastructure scripts.

Used by gen_manifests.py, gen_sanity.py and gen_report.py.  All helpers are
read-only and defensive: missing files / columns produce None/empty results
and a stderr warning rather than raising, so a partially-complete results tree
can still be processed.
"""
from __future__ import annotations

import csv
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from typing import Any, Dict, Iterable, List, Optional, Tuple

import numpy as np

# --- locations ---------------------------------------------------------------
_TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
SOLVER_ROOT = os.path.dirname(_TOOLS_DIR)
RESULTS_DIR = os.path.join(SOLVER_ROOT, "results")
REPORT_DIR = os.path.join(SOLVER_ROOT, "report")
FIGURES_DIR = os.path.join(REPORT_DIR, "figures")
BENCH_DIR = os.environ.get("CFD_BENCH_DIR", "/workspace/cfd_solver_agentic_benchmark")
CASES_DIR = os.path.join(BENCH_DIR, "inputs", "cases")


def warn(msg: str) -> None:
    print(f"[warn] {msg}", file=sys.stderr)


# --- JSON / CSV --------------------------------------------------------------
def load_json(path: str) -> Optional[dict]:
    try:
        with open(path) as f:
            return json.load(f)
    except FileNotFoundError:
        return None
    except Exception as exc:
        warn(f"could not parse JSON {path}: {exc}")
        return None


def _read_csv_rows(path: str) -> Tuple[List[str], List[Dict[str, str]]]:
    if not os.path.isfile(path):
        return [], []
    with open(path, newline="") as f:
        rdr = csv.DictReader(f)
        header = rdr.fieldnames or []
        rows = [r for r in rdr if r]
    return list(header), rows


def load_forces(path: str) -> Optional[Dict[str, np.ndarray]]:
    """Return forces.csv as a dict of float arrays (all columns numeric)."""
    header, rows = _read_csv_rows(path)
    if not header:
        warn(f"forces.csv missing/empty: {path}")
        return None
    out: Dict[str, np.ndarray] = {}
    for col in header:
        vals = []
        for r in rows:
            try:
                vals.append(float(r[col]))
            except (ValueError, TypeError, KeyError):
                vals.append(np.nan)
        out[col] = np.array(vals, dtype=float)
    return out


def load_surface(path: str) -> Optional[Dict[str, Any]]:
    """Return surface.csv as float arrays plus a 'tag' string list."""
    header, rows = _read_csv_rows(path)
    if not header:
        warn(f"surface.csv missing/empty: {path}")
        return None
    out: Dict[str, Any] = {}
    for col in header:
        if col == "tag":
            out[col] = [r.get(col, "") for r in rows]
        else:
            vals = []
            for r in rows:
                try:
                    vals.append(float(r[col]))
                except (ValueError, TypeError, KeyError):
                    vals.append(np.nan)
            out[col] = np.array(vals, dtype=float)
    out["_n"] = len(rows)
    return out


def final_force_row(path: str) -> Optional[Dict[str, float]]:
    """Last numeric row of forces.csv as floats."""
    fs = load_forces(path)
    if not fs or len(fs.get("step", [])) == 0:
        return None
    n = len(fs["step"])
    return {k: float(v[n - 1]) for k, v in fs.items()}


# --- VTU (ASCII XML unstructured grid) reader --------------------------------
def read_vtu_cell_data(path, names=("rho", "u", "v", "p", "mach", "T", "rank")):
    """Read named CellData arrays from an ASCII XML .vtu file.

    The solver writes ASCII-format VTK unstructured grids, so a plain
    ElementTree + whitespace split suffices and avoids a vtk/meshio dependency.
    """
    names = set(names)
    out: Dict[str, np.ndarray] = {}
    if not os.path.isfile(path):
        warn(f"field file missing: {path}")
        return out
    try:
        root = ET.parse(path).getroot()
    except Exception as exc:
        warn(f"could not parse VTU {path}: {exc}")
        return out
    celldata = root.find(".//CellData")
    if celldata is None:
        return out
    piece = root.find(".//Piece")
    ncells = int(piece.get("NumberOfCells")) if piece is not None else None
    for da in celldata.findall("DataArray"):
        nm = da.get("Name")
        if nm not in names:
            continue
        fmt = da.get("format", "ascii")
        if fmt != "ascii":
            warn(f"{path}: DataArray '{nm}' format '{fmt}' unsupported; skipping")
            continue
        txt = da.text or ""
        try:
            arr = np.array(txt.split(), dtype=float)
        except ValueError as exc:
            warn(f"{path}: could not convert '{nm}': {exc}")
            continue
        if ncells is not None and arr.size != ncells:
            warn(f"{path}: '{nm}' has {arr.size} values, expected {ncells}")
        out[nm] = arr
    return out


# --- result directory discovery ----------------------------------------------
def result_dir_files(rdir: str) -> List[str]:
    try:
        return sorted(os.listdir(rdir))
    except OSError:
        return []


def is_complete(rdir: str) -> bool:
    files = set(result_dir_files(rdir))
    has_field = any(f.startswith("field_final.") for f in files)
    has_part = any(f.startswith("partition_diagnostics.") for f in files)
    return all(
        f in files for f in ("metadata.json", "run_status.json", "residuals.csv",
                             "forces.csv", "surface.csv", "stdout.log")
    ) and has_field and has_part


def load_result(rdir: str) -> Optional[Tuple[dict, dict]]:
    meta = load_json(os.path.join(rdir, "metadata.json"))
    stat = load_json(os.path.join(rdir, "run_status.json"))
    if not meta or not stat:
        return None
    return meta, stat


def discover_result_dirs(results_dir: str = RESULTS_DIR) -> List[str]:
    out: List[str] = []
    if not os.path.isdir(results_dir):
        return out
    for name in sorted(os.listdir(results_dir)):
        d = os.path.join(results_dir, name)
        if os.path.isdir(d) and os.path.isfile(os.path.join(d, "metadata.json")):
            out.append(d)
    return out


def best_dir_per_case(results_dir: str = RESULTS_DIR) -> Dict[str, str]:
    """Pick the most complete / longest result dir per case_id."""
    by: Dict[str, Tuple[Tuple, str]] = {}
    for d in discover_result_dirs(results_dir):
        loaded = load_result(d)
        if not loaded:
            continue
        meta, stat = loaded
        files = set(result_dir_files(d))
        has_field = any(f.startswith("field_final.") for f in files)
        has_part = any(f.startswith("partition_diagnostics.") for f in files)
        complete = is_complete(d)
        step = int(stat.get("final_step", 0) or 0)
        score = (complete, has_field and has_part, step)
        cid = meta.get("case_id", os.path.basename(d))
        prev = by.get(cid)
        if prev is None or score > prev[0]:
            by[cid] = (score, d)
    return {cid: v[1] for cid, v in by.items()}


# --- case parameters ---------------------------------------------------------
_CASE_CACHE: Dict[str, dict] = {}


def load_case_json(case_id: str) -> Optional[dict]:
    if case_id in _CASE_CACHE:
        return _CASE_CACHE[case_id]
    path = os.path.join(CASES_DIR, f"{case_id}.json")
    data = load_json(path)
    if data is None:
        warn(f"case json not found: {path}")
    _CASE_CACHE[case_id] = data
    return data


def case_kind(case_id: str) -> Dict[str, Any]:
    """Best-effort classification of a case from its id and case json."""
    cid = (case_id or "").lower()
    fam = "naca" if cid.startswith("naca") else ("cylinder" if cid.startswith("cylinder") else "other")
    mode = "inviscid" if "inviscid" in cid else ("laminar" if "laminar" in cid else "unknown")
    re_val = None
    m = re.search(r"re(\d+)", cid)
    if m:
        re_val = int(m.group(1))
    mach = None
    m2 = re.search(r"_m(\d+)", cid)
    if m2:
        digits = m2.group(1)
        mach = int(digits) / 100.0 if digits.startswith("0") else (2.0 if digits == "200" else int(digits) / 100.0)
    info: Dict[str, Any] = {
        "family": fam, "mode": mode, "reynolds": re_val, "mach": mach,
        "is_transient": "re200" in cid,
    }
    cj = load_case_json(case_id)
    if cj:
        info["case_json"] = cj
        fs = cj.get("freestream", {})
        if "mach" in fs:
            info["mach"] = fs["mach"]
        ph = cj.get("physics", {})
        if "reynolds" in ph:
            info["reynolds"] = ph["reynolds"]
        info["gamma"] = cj.get("gas", {}).get("gamma", 1.4)
        info["prandtl"] = cj.get("gas", {}).get("prandtl", 0.72)
        info["gas_R"] = cj.get("gas", {}).get("R", 1.0)
        info["aoa_deg"] = fs.get("aoa_degrees", 0.0)
        info["freestream"] = fs
        info["reference"] = cj.get("reference", {})
        info["run_control"] = cj.get("run_control", {})
        info["bcs"] = cj.get("boundary_conditions", {})
    return info


def late_indices(n: int, frac: float = 0.2, floor: int = 10) -> np.ndarray:
    """Indices covering the last ``frac`` of a series (at least ``floor``)."""
    if n <= 0:
        return np.array([], dtype=int)
    start = max(0, n - max(floor, int(round(n * frac))))
    return np.arange(start, n)


# --- LaTeX helpers -----------------------------------------------------------
def latex_escape(s: Any) -> str:
    """Escape a string for LaTeX text mode (also safe inside \\texttt)."""
    s = str(s)
    repl = {
        "\\": r"\textbackslash{}",
        "&": r"\&",
        "%": r"\%",
        "$": r"\$",
        "#": r"\#",
        "_": r"\_",
        "{": r"\{",
        "}": r"\}",
        "~": r"\textasciitilde{}",
        "^": r"\textasciicircum{}",
    }
    out = []
    for ch in s:
        out.append(repl.get(ch, ch))
    return "".join(out)


def tex_tt(s: Any) -> str:
    r"""Wrap an identifier in \texttt{} with proper escaping."""
    return r"\texttt{" + latex_escape(s) + "}"
