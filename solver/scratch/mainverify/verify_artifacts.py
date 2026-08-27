#!/usr/bin/env python3
"""Independent re-verification of result artifacts against OUTPUT_CONTRACT.md.

Read-only. Parses existing outputs; runs no solver.
"""
from __future__ import annotations

import json
import math
import re
import sys
from pathlib import Path

ROOT = Path("/workspace/solver")
RESULTS = ROOT / "results"

STEADY = [
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
]

REQUIRED_FILES = [
    "metadata.json",
    "residuals.csv",
    "forces.csv",
    "surface.csv",
    "field_final.vtu",
    "stdout.log",
    "run_status.json",
]

META_KEYS = [
    "case_id", "solver_name", "solver_version", "git_revision", "mpi_ranks",
    "mesh_file", "num_cells_global", "num_faces_global", "num_cells_owned_local",
    "num_cells_ghost_local", "partitioner", "partition_edge_cut", "halo_exchange",
    "full_state_replication_during_iterations", "full_mesh_replication_during_iterations",
    "equation_set", "inviscid_flux", "entropy_fix", "viscous_flux", "time_integrator",
    "implicit_solver", "reconstruction", "limiter", "spatial_order_claimed",
    "positivity_preservation", "wall_boundary_output_semantics", "true_bdf2_inner_loop",
    "typical_inner_iterations", "min_inner_iterations", "max_inner_iterations",
    "observed_min_inner_iterations", "observed_max_inner_iterations",
    "inner_residual_reduction_target", "inner_target_misses",
    "inner_target_converged_fraction", "last_inner_residual_ratio",
    "start_time_utc", "end_time_utc", "completed", "convergence_status",
]

STATUS_KEYS = [
    "case_id", "command", "mpi_ranks", "wall_time_seconds", "final_step",
    "final_physical_time", "convergence_status", "residual_reduction_orders", "notes",
]

RESID_HEADER = "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf"
FORCES_HEADER = "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift"
SURFACE_HEADER = "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag"
PART_HEADER = "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells"

fails: list[str] = []


def check(ok: bool, msg: str) -> bool:
    if not ok:
        fails.append(msg)
    return ok


def finite_tokens(tokens):
    for t in tokens:
        t = t.strip()
        if not t:
            continue
        low = t.lower()
        if "nan" in low or "inf" in low:
            return False, t
        try:
            float(t)
        except ValueError:
            return True, None  # non-numeric column (e.g. tag / neighbor list)
    return True, None


def check_csv(path: Path, header: str, numeric_step_col: int | None):
    lines = path.read_text().splitlines()
    got = lines[0].strip()
    check(got == header, f"{path}: header mismatch\n    want {header}\n    got  {got}")
    ncol = len(header.split(","))
    rows = [l for l in lines[1:] if l.strip()]
    bad_ncol = []
    prev = None
    nonmono = 0
    for i, line in enumerate(rows, start=2):
        toks = line.split(",")
        if len(toks) != ncol:
            bad_ncol.append((i, len(toks)))
        ok, tok = finite_tokens(toks)
        if not ok:
            check(False, f"{path}:{i}: non-finite value {tok!r}")
            break
        if numeric_step_col is not None and len(toks) > numeric_step_col:
            try:
                v = float(toks[numeric_step_col])
            except ValueError:
                v = None
            if v is not None:
                if prev is not None and v <= prev:
                    nonmono += 1
                prev = v
    check(not bad_ncol, f"{path}: {len(bad_ncol)} rows with wrong column count, first {bad_ncol[:3]}")
    check(nonmono == 0, f"{path}: {nonmono} non-monotone step values")
    return len(rows), rows


def check_vtu(path: Path):
    txt = path.read_text()
    npiece = txt.count("<Piece")
    check(npiece == 1, f"{path}: {npiece} <Piece> elements, want 1")
    check('format="ascii"' in txt, f"{path}: no ascii DataArray format")
    check(txt.count("<UnstructuredGrid>") == 1, f"{path}: not a single UnstructuredGrid")
    m = re.search(r'NumberOfPoints="(\d+)"\s+NumberOfCells="(\d+)"', txt)
    if not m:
        m = re.search(r'NumberOfCells="(\d+)"\s+NumberOfPoints="(\d+)"', txt)
        npts, ncells = (int(m.group(2)), int(m.group(1))) if m else (0, 0)
    else:
        npts, ncells = int(m.group(1)), int(m.group(2))
    check(npts > 0 and ncells > 0, f"{path}: bad point/cell count {npts}/{ncells}")

    def array(name, section=None):
        pat = re.compile(r'<DataArray[^>]*Name="' + re.escape(name) + r'"[^>]*>(.*?)</DataArray>', re.S)
        mm = pat.search(txt)
        return mm.group(1).split() if mm else None

    types = array("types")
    check(types is not None, f"{path}: no types array")
    if types:
        tset = sorted(set(int(t) for t in types))
        check(all(t in (5, 9) for t in tset), f"{path}: cell types {tset} outside {{5,9}}")
        check(len(types) == ncells, f"{path}: {len(types)} types for {ncells} cells")
    offsets = array("offsets")
    conn = array("connectivity")
    if offsets and conn:
        offs = [int(o) for o in offsets]
        check(len(offs) == ncells, f"{path}: {len(offs)} offsets for {ncells} cells")
        check(all(offs[i] < offs[i + 1] for i in range(len(offs) - 1)),
              f"{path}: offsets not strictly increasing")
        check(offs[-1] == len(conn),
              f"{path}: final offset {offs[-1]} != connectivity length {len(conn)}")
        expect = sum(3 if int(t) == 5 else 4 for t in types)
        check(offs[-1] == expect,
              f"{path}: final offset {offs[-1]} != sum of per-type sizes {expect}")
        check(max(int(c) for c in conn) == npts - 1,
              f"{path}: connectivity max index {max(int(c) for c in conn)} != npts-1 {npts-1}")
    pts = array("Points") or array("points")
    if pts is None:
        mm = re.search(r'<Points>(.*?)</Points>', txt, re.S)
        if mm:
            inner = re.search(r'>([^<]*)</DataArray>', mm.group(1), re.S)
            pts = inner.group(1).split() if inner else None
    if pts:
        check(len(pts) == 3 * npts, f"{path}: {len(pts)} point coords for {npts} points")
        zs = pts[2::3]
        nz = [z for z in zs if float(z) != 0.0]
        check(not nz, f"{path}: {len(nz)} nonzero z coordinates, first {nz[:3]}")
    names = re.findall(r'<DataArray[^>]*Name="([^"]+)"', txt)
    cd = re.search(r'<CellData[^>]*>(.*?)</CellData>', txt, re.S)
    cd_names = re.findall(r'Name="([^"]+)"', cd.group(1)) if cd else []
    for arr in cd_names:
        vals = array(arr)
        if vals is None:
            continue
        bad = [v for v in vals if "nan" in v.lower() or "inf" in v.lower()]
        check(not bad, f"{path}: array {arr} has non-finite entries {bad[:3]}")
    return npts, ncells, cd_names


summary = []
for case in STEADY:
    d = RESULTS / case
    print(f"\n=== {case}")
    for f in REQUIRED_FILES:
        p = d / f
        check(p.is_file() and p.stat().st_size > 0, f"{case}: missing/empty {f}")
    has_part = (d / "partition_diagnostics.csv").is_file() or (d / "partition_diagnostics.json").is_file()
    check(has_part, f"{case}: no partition_diagnostics.{{csv,json}}")
    restarts = list(d.glob("restart_final.*"))
    check(bool(restarts), f"{case}: no restart_final.*")

    meta = json.loads((d / "metadata.json").read_text())
    missing = [k for k in META_KEYS if k not in meta]
    check(not missing, f"{case}: metadata.json missing keys {missing}")
    st = json.loads((d / "run_status.json").read_text())
    missing = [k for k in STATUS_KEYS if k not in st]
    check(not missing, f"{case}: run_status.json missing keys {missing}")
    check(meta.get("completed") is True, f"{case}: completed={meta.get('completed')!r}")
    check(meta.get("git_revision") == "cc42ab4facfd",
          f"{case}: git_revision={meta.get('git_revision')!r} != cc42ab4facfd")
    check(meta.get("mpi_ranks") == 4, f"{case}: mpi_ranks={meta.get('mpi_ranks')!r} != 4")
    check(st.get("mpi_ranks") == 4, f"{case}: run_status mpi_ranks={st.get('mpi_ranks')!r}")
    check(meta.get("convergence_status") == st.get("convergence_status"),
          f"{case}: convergence_status mismatch metadata={meta.get('convergence_status')!r} status={st.get('convergence_status')!r}")
    check("metis" in str(meta.get("partitioner", "")).lower(),
          f"{case}: partitioner={meta.get('partitioner')!r} does not name METIS")
    check("isend" in str(meta.get("halo_exchange", "")).lower() or "neighbor" in str(meta.get("halo_exchange", "")).lower(),
          f"{case}: halo_exchange={meta.get('halo_exchange')!r}")
    check(meta.get("full_state_replication_during_iterations") is False,
          f"{case}: full_state_replication is not False")
    check(meta.get("equation_set") == "compressible_navier_stokes_2d",
          f"{case}: equation_set={meta.get('equation_set')!r}")

    nres, _ = check_csv(d / "residuals.csv", RESID_HEADER, 0)
    nfor, frows = check_csv(d / "forces.csv", FORCES_HEADER, 0)
    nsur, srows = check_csv(d / "surface.csv", SURFACE_HEADER, None)
    if (d / "partition_diagnostics.csv").is_file():
        check_csv(d / "partition_diagnostics.csv", PART_HEADER, None)

    npts, ncells, cd_names = check_vtu(d / "field_final.vtu")
    check(ncells == meta.get("num_cells_global"),
          f"{case}: vtu cells {ncells} != num_cells_global {meta.get('num_cells_global')}")

    last = frows[-1].split(",")
    fstep = int(float(last[0]))
    check(fstep == st.get("final_step"),
          f"{case}: forces.csv last step {fstep} != run_status final_step {st.get('final_step')}")
    cl, cd_, cmz = float(last[2]), float(last[3]), float(last[4])
    pd_, vd_, pl_, vl_ = (float(x) for x in last[5:9])
    check(abs((pd_ + vd_) - cd_) <= 1e-9 * max(1.0, abs(cd_)),
          f"{case}: pressure_drag+viscous_drag {pd_+vd_!r} != cd {cd_!r}")
    check(abs((pl_ + vl_) - cl) <= 1e-9 * max(1.0, abs(cl)),
          f"{case}: pressure_lift+viscous_lift {pl_+vl_!r} != cl {cl!r}")
    if "inviscid" in case:
        check(abs(vd_) <= 1e-12 and abs(vl_) <= 1e-12,
              f"{case}: inviscid viscous columns not negligible vd={vd_!r} vl={vl_!r}")
    else:
        check(abs(vd_) > 0.0, f"{case}: laminar case has zero viscous drag")
    cps = [float(r.split(",")[5]) for r in srows]
    check(max(cps) - min(cps) > 0.05, f"{case}: cp range on wall only {max(cps)-min(cps)!r}")
    summary.append((case, meta.get("convergence_status"), st.get("final_step"),
                    round(st.get("residual_reduction_orders", 0.0), 2), cd_, cl,
                    nres, nfor, nsur, npts, ncells, len(cd_names)))

print("\n=== SUMMARY")
hdr = ("case", "status", "steps", "orders", "cd", "cl", "nres", "nfor", "nsur", "pts", "cells", "arrays")
print(" | ".join(hdr))
for row in summary:
    print(" | ".join(str(x) for x in row))

print(f"\n=== {len(fails)} FAILURES")
for f in fails:
    print(" FAIL " + f)
sys.exit(1 if fails else 0)
