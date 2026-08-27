#!/usr/bin/env python3
"""Generate a synthetic but realistic solver output directory for development.

The plotting tools in this directory must work before the C++ solver produces
any output, so this script fabricates a case directory that follows the output
contract exactly:

    residuals.csv                decaying residual history
    forces.csv                   oscillating cl/cd (vortex-shedding-like)
    surface.csv                  cylinder wall data with a STRING tag column
    field_final.vtu              genuine ASCII VTK UnstructuredGrid, mixed
                                 triangles and quads over an annulus
    metadata.json                contract fields
    run_status.json              contract fields
    partition_diagnostics.csv    per-rank diagnostics
    restart_final.bin            placeholder (existence is what is checked)
    stdout.log                   placeholder log

The mesh is an O-grid annulus around a unit-diameter cylinder.  Radial rings of
quadrilaterals are interrupted by two rings that are split into triangles, so
the reader and the triangulation helper both get exercised on genuinely mixed
cell types.  The fields are analytic but spatially structured: a potential-flow
style solution around the cylinder plus a travelling wake perturbation, which
gives Mach, pressure, velocity and vorticity plausible gradients rather than
constants.

This fixture is a development aid.  Keep it under tools/probe_out/ and never
under solver/results/, which must only contain real solver output.

Usage::

    python make_test_fixture.py --out-dir probe_out/fixture
    python make_test_fixture.py --out-dir probe_out/fixture_airfoil --body airfoil
"""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np

# ----------------------------------------------------------------------------
# Mesh construction
# ----------------------------------------------------------------------------


def build_annulus_mesh(
    n_theta: int = 48,
    n_radial: int = 10,
    r_inner: float = 0.5,
    r_outer: float = 6.0,
    triangulated_rings: Tuple[int, ...] = (2, 5),
) -> Tuple[np.ndarray, List[np.ndarray], np.ndarray]:
    """Build an O-grid annulus with a mix of quadrilateral and triangular cells.

    Parameters
    ----------
    n_theta:
        Number of cells around the circumference (the grid wraps, so node
        column n_theta is node column 0).
    n_radial:
        Number of cell rings in the radial direction.
    r_inner, r_outer:
        Inner (body) and outer (farfield) radii.
    triangulated_rings:
        Indices of rings whose quads are split into two triangles each, so the
        resulting mesh genuinely mixes VTK_TRIANGLE and VTK_QUAD.

    Returns
    -------
    (points, cells, cell_types) where points is (n_points, 2), cells is a list
    of node-index arrays and cell_types holds VTK type ids (5 or 9).
    """
    theta = np.linspace(0.0, 2.0 * np.pi, n_theta, endpoint=False)
    # Geometric radial stretching so cells near the wall are thin, as in a real
    # viscous mesh.
    s = np.linspace(0.0, 1.0, n_radial + 1)
    radii = r_inner * (r_outer / r_inner) ** s

    points = []
    for r in radii:
        for t in theta:
            points.append((r * np.cos(t), r * np.sin(t)))
    pts = np.array(points, dtype=np.float64)

    def node(ring: int, col: int) -> int:
        return ring * n_theta + (col % n_theta)

    cells: List[np.ndarray] = []
    types: List[int] = []
    for ring in range(n_radial):
        split = ring in triangulated_rings
        for col in range(n_theta):
            n0 = node(ring, col)
            n1 = node(ring, col + 1)
            n2 = node(ring + 1, col + 1)
            n3 = node(ring + 1, col)
            if split:
                cells.append(np.array([n0, n1, n2], dtype=np.int64))
                types.append(5)
                cells.append(np.array([n0, n2, n3], dtype=np.int64))
                types.append(5)
            else:
                # Counter-clockwise winding.
                cells.append(np.array([n0, n1, n2, n3], dtype=np.int64))
                types.append(9)
    return pts, cells, np.array(types, dtype=np.int64)


# ----------------------------------------------------------------------------
# Analytic flow field
# ----------------------------------------------------------------------------


def synthetic_fields(centroids: np.ndarray, mach_inf: float = 0.1) -> Dict[str, np.ndarray]:
    """Evaluate a plausible cylinder flow field at cell centres.

    The base state is incompressible potential flow past a cylinder of radius
    0.5, which gives the correct stagnation point, the correct suction peak on
    the shoulders and a sensible pressure coefficient.  On top of that a
    decaying sinusoidal wake perturbation is added downstream so that the
    velocity and vorticity fields contain a vortex-street-like structure and the
    contour plots have something interesting to show.

    Returns a dict of cell arrays matching the solver output contract:
    density, velocity (n,3), pressure, mach, temperature, total_energy,
    vorticity, rank.
    """
    x, y = centroids[:, 0], centroids[:, 1]
    r = np.hypot(x, y)
    r = np.maximum(r, 1.0e-9)
    a = 0.5  # cylinder radius
    gamma_gas = 1.4
    u_inf = 1.0
    rho_inf = 1.0
    # Freestream pressure consistent with rho=1, U=1 and the given Mach number:
    # a_inf = U / M, p = rho a^2 / gamma.
    p_inf = rho_inf * (u_inf / mach_inf) ** 2 / gamma_gas

    # Potential flow past a cylinder (doublet + uniform stream).
    inside = r < a
    r_eff = np.where(inside, a, r)
    cos_t = x / r_eff
    sin_t = y / r_eff
    u_r = u_inf * (1.0 - (a / r_eff) ** 2) * cos_t
    u_t = -u_inf * (1.0 + (a / r_eff) ** 2) * sin_t
    u = u_r * cos_t - u_t * sin_t
    v = u_r * sin_t + u_t * cos_t

    # Travelling wake: two rows of counter-rotating structures behind the body,
    # decaying with distance and confined near the centreline.
    wake_x = np.maximum(x - a, 0.0)
    envelope = np.exp(-wake_x / 4.0) * np.exp(-((y / 0.9) ** 2))
    downstream = (x > a).astype(np.float64)
    shed = 0.55 * envelope * downstream * np.sin(2.0 * np.pi * wake_x / 2.2)
    u = u - 0.45 * envelope * downstream  # momentum deficit
    v = v + shed

    # No-slip wall: kill the velocity in a thin layer next to the body.
    boundary_layer = np.exp(-((r - a) / 0.10) ** 2)
    wall_damp = 1.0 - 0.95 * boundary_layer
    u = u * wall_damp
    v = v * wall_damp

    speed = np.hypot(u, v)
    # Incompressible Bernoulli for the pressure, then an isentropic-ish state.
    cp = 1.0 - (speed / u_inf) ** 2
    q_inf = 0.5 * rho_inf * u_inf ** 2
    pressure = p_inf + cp * q_inf
    pressure = np.maximum(pressure, 0.35 * p_inf)  # keep positivity

    density = rho_inf * (pressure / p_inf) ** (1.0 / gamma_gas)
    sound_speed = np.sqrt(gamma_gas * pressure / density)
    mach = speed / sound_speed
    temperature = pressure / (density * 1.0)  # R = 1 in the case files
    total_energy = pressure / (gamma_gas - 1.0) + 0.5 * density * speed ** 2

    # Vorticity: analytic curl of the wake perturbation plus a wall shear layer.
    d_shed_dx = (
        0.55
        * downstream
        * np.exp(-((y / 0.9) ** 2))
        * (
            (2.0 * np.pi / 2.2) * np.exp(-wake_x / 4.0) * np.cos(2.0 * np.pi * wake_x / 2.2)
            - 0.25 * np.exp(-wake_x / 4.0) * np.sin(2.0 * np.pi * wake_x / 2.2)
        )
    )
    wall_shear = -18.0 * boundary_layer * np.where(y >= 0.0, 1.0, -1.0)
    vorticity = d_shed_dx + wall_shear + 0.9 * envelope * downstream * (y / 0.9)

    n = centroids.shape[0]
    # Fake a 4-rank decomposition by angular sector so the 'rank' array varies.
    rank = (np.floor((np.arctan2(y, x) + np.pi) / (2.0 * np.pi) * 4.0) % 4).astype(np.int64)

    velocity = np.zeros((n, 3), dtype=np.float64)
    velocity[:, 0] = u
    velocity[:, 1] = v

    return {
        "density": density,
        "velocity": velocity,
        "pressure": pressure,
        "mach": mach,
        "temperature": temperature,
        "total_energy": total_energy,
        "vorticity": vorticity,
        "rank": rank,
    }


# ----------------------------------------------------------------------------
# VTU writer (mirrors what the C++ solver must emit)
# ----------------------------------------------------------------------------


def write_vtu(
    path: Path,
    points: np.ndarray,
    cells: List[np.ndarray],
    cell_types: np.ndarray,
    cell_data: Dict[str, np.ndarray],
    wrap: int = 6,
) -> None:
    """Write an ASCII VTK XML UnstructuredGrid file.

    Deliberately writes wrapped, irregularly indented data blocks so that the
    reader is exercised against line-wrapped ASCII rather than one number per
    line.  This is the exact format the C++ writer is expected to produce.
    """
    n_points = points.shape[0]
    n_cells = len(cells)
    connectivity = np.concatenate(cells) if n_cells else np.empty(0, dtype=np.int64)
    offsets = np.cumsum([len(c) for c in cells], dtype=np.int64)

    def fmt_block(values: np.ndarray, integer: bool, per_line: int) -> str:
        flat = np.asarray(values).ravel()
        out = []
        for start in range(0, flat.size, per_line):
            chunk = flat[start : start + per_line]
            if integer:
                out.append("          " + " ".join(str(int(v)) for v in chunk))
            else:
                out.append("          " + " ".join("%.10g" % float(v) for v in chunk))
        return "\n".join(out)

    lines: List[str] = []
    lines.append('<?xml version="1.0"?>')
    lines.append('<VTKFile type="UnstructuredGrid" version="0.1" byte_order="LittleEndian">')
    lines.append("  <UnstructuredGrid>")
    lines.append('    <Piece NumberOfPoints="%d" NumberOfCells="%d">' % (n_points, n_cells))

    # Points: always 3 components with z = 0 for this 2-D solver.
    xyz = np.zeros((n_points, 3), dtype=np.float64)
    xyz[:, :2] = points
    lines.append("      <Points>")
    lines.append('        <DataArray type="Float64" Name="Points" NumberOfComponents="3" format="ascii">')
    lines.append(fmt_block(xyz, False, wrap * 3))
    lines.append("        </DataArray>")
    lines.append("      </Points>")

    lines.append("      <Cells>")
    lines.append('        <DataArray type="Int64" Name="connectivity" format="ascii">')
    lines.append(fmt_block(connectivity, True, 12))
    lines.append("        </DataArray>")
    lines.append('        <DataArray type="Int64" Name="offsets" format="ascii">')
    lines.append(fmt_block(offsets, True, 12))
    lines.append("        </DataArray>")
    lines.append('        <DataArray type="UInt8" Name="types" format="ascii">')
    lines.append(fmt_block(cell_types, True, 20))
    lines.append("        </DataArray>")
    lines.append("      </Cells>")

    lines.append('      <CellData Scalars="density" Vectors="velocity">')
    for name, values in cell_data.items():
        arr = np.asarray(values)
        ncomp = 1 if arr.ndim == 1 else arr.shape[1]
        is_int = np.issubdtype(arr.dtype, np.integer)
        vtk_type = "Int32" if is_int else "Float64"
        comp_attr = ' NumberOfComponents="%d"' % ncomp if ncomp > 1 else ""
        lines.append(
            '        <DataArray type="%s" Name="%s"%s format="ascii">' % (vtk_type, name, comp_attr)
        )
        lines.append(fmt_block(arr, is_int, wrap * max(1, ncomp)))
        lines.append("        </DataArray>")
    lines.append("      </CellData>")

    lines.append("    </Piece>")
    lines.append("  </UnstructuredGrid>")
    lines.append("</VTKFile>")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n")


# ----------------------------------------------------------------------------
# CSV / JSON companions
# ----------------------------------------------------------------------------

RESIDUALS_HEADER = (
    "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf"
)
FORCES_HEADER = (
    "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift"
)
SURFACE_HEADER = "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag"
PARTITION_HEADER = (
    "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,"
    "neighbor_ranks,send_cells,recv_cells"
)


def write_residuals(
    path: Path, n_steps: int = 600, dt: float = 0.01, transient: bool = True, orders: float = 7.0
) -> None:
    """Write a residual history that decays by a realistic number of orders.

    The decay is expressed as a fixed number of orders of magnitude over the run
    plus a small stochastic plateau, so that a long history (the production Re200
    case runs 30000 steps) never underflows to exactly zero, which would leave
    nothing for a semilog-y plot to draw.
    """
    steps = np.arange(1, n_steps + 1)
    fraction = steps / float(max(n_steps, 1))
    rng = np.random.default_rng(1234)
    noise = 1.0 + 0.05 * rng.standard_normal(n_steps)
    start = 1.0e-1
    # Fast initial drop, then a slow tail, spanning 'orders' decades overall.
    decay = 10.0 ** (-orders * (1.0 - np.exp(-3.0 * fraction)) / (1.0 - np.exp(-3.0)))
    plateau = start * 10.0 ** (-orders) * 0.35
    base = start * decay * np.abs(noise) + plateau
    rows = [RESIDUALS_HEADER]
    for i, step in enumerate(steps):
        l2 = base[i]
        rows.append(
            "%d,%.6f,%d,%.4f,%.6g,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e"
            % (
                step,
                step * dt if transient else 0.0,
                int(6 + 4 * np.sin(step / 25.0)),
                min(100.0, 1.0 + 0.05 * step),
                dt,
                l2 * 1.0,
                l2 * 0.72,
                l2 * 0.51,
                l2 * 1.35,
                l2,
                l2 * 4.6,
            )
        )
    path.write_text("\n".join(rows) + "\n")


def write_forces(
    path: Path,
    n_steps: int = 600,
    dt: float = 0.01,
    strouhal: float = 0.195,
    diameter: float = 1.0,
    u_inf: float = 1.0,
    oscillating: bool = True,
) -> None:
    """Write a force history with a startup transient then periodic shedding.

    The shedding frequency is set from the requested Strouhal number so that the
    transient analysis tool can be checked against a known answer.
    """
    steps = np.arange(1, n_steps + 1)
    t = steps * dt
    freq = strouhal * u_inf / diameter
    growth = 1.0 - np.exp(-t / 1.2)  # startup transient
    cl = 0.62 * growth * np.sin(2.0 * np.pi * freq * t)
    cl = cl + 0.04 * growth * np.sin(2.0 * np.pi * 3.0 * freq * t)  # third harmonic
    cd = 1.34 - 0.28 * np.exp(-t / 0.9) + 0.045 * growth * np.sin(
        2.0 * np.pi * 2.0 * freq * t
    )
    if not oscillating:
        cl = 1.0e-6 * np.ones_like(t)
        cd = 1.34 - 0.28 * np.exp(-t / 0.9)
    cmz = 0.11 * cl
    viscous_frac = 0.22
    rows = [FORCES_HEADER]
    for i, step in enumerate(steps):
        rows.append(
            "%d,%.6f,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e"
            % (
                step,
                t[i],
                cl[i],
                cd[i],
                cmz[i],
                cd[i] * (1.0 - viscous_frac),
                cd[i] * viscous_frac,
                cl[i] * (1.0 - viscous_frac),
                cl[i] * viscous_frac,
            )
        )
    path.write_text("\n".join(rows) + "\n")


def write_surface_cylinder(
    path: Path, n: int = 160, viscous: bool = True, radius: float = 0.5
) -> None:
    """Write cylinder wall data, deliberately in a scrambled row order.

    The rows are shuffled so that the plotting tool must sort points along the
    body itself; a tool that simply plots file order would produce a scribble.
    The tag column is a STRING boundary-family name, per the contract.
    """
    rng = np.random.default_rng(7)
    theta = np.linspace(0.0, 2.0 * np.pi, n, endpoint=False)
    x = radius * np.cos(theta)
    y = radius * np.sin(theta)
    nx, ny = np.cos(theta), np.sin(theta)  # outward normals
    # Potential-flow surface cp, softened at the rear to mimic separation.
    cp = 1.0 - 4.0 * np.sin(theta) ** 2
    rear = 0.5 * (1.0 + np.cos(theta))
    cp = cp * (1.0 - 0.55 * rear) - 0.75 * rear
    p_inf = 1.0 * (1.0 / 0.1) ** 2 / 1.4
    pressure = p_inf + cp * 0.5
    # Skin friction: peaks on the shoulders, changes sign past separation.
    cf = 0.16 * np.sin(theta) * np.exp(-((np.cos(theta) - 0.25) ** 2) / 0.9) if viscous else np.zeros(n)
    order = rng.permutation(n)
    rows = [SURFACE_HEADER]
    for i in order:
        u_wall = 0.0 if viscous else -np.sin(theta[i]) * 2.0 * 0.0
        rows.append(
            "%.10g,%.10g,%.10g,%.10g,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%s"
            % (
                x[i],
                y[i],
                nx[i],
                ny[i],
                pressure[i],
                cp[i],
                cf[i],
                1.0 + 0.02 * cp[i],
                u_wall,
                0.0,
                0.0,
                "WALL",
            )
        )
    path.write_text("\n".join(rows) + "\n")


def write_surface_airfoil(path: Path, n_side: int = 90, viscous: bool = False) -> None:
    """Write NACA 0012 wall data for both surfaces, scrambled row order.

    Includes a trailing extra column to prove that the reader tolerates extra
    columns beyond the contract header.
    """
    rng = np.random.default_rng(11)
    beta = np.linspace(0.0, np.pi, n_side)
    xc = 0.5 * (1.0 - np.cos(beta))  # cosine clustering at the edges
    t = 0.12
    yt = (
        5.0
        * t
        * (
            0.2969 * np.sqrt(np.maximum(xc, 0.0))
            - 0.1260 * xc
            - 0.3516 * xc ** 2
            + 0.2843 * xc ** 3
            - 0.1015 * xc ** 4
        )
    )
    records = []
    for sign in (+1.0, -1.0):
        y = sign * yt
        # Surface slope for the normal direction.
        dy = np.gradient(y, xc)
        norm = np.hypot(1.0, dy)
        nx = -dy / norm * sign * 0.0 + (-dy) / norm
        ny = np.ones_like(nx) / norm
        nx, ny = nx * sign, ny * sign
        # A symmetric-airfoil-like cp: stagnation at the nose, suction peak,
        # recovery to near zero at the trailing edge.
        cp = 1.0 - 1.9 * np.sin(np.pi * np.clip(xc, 0.0, 1.0) ** 0.62) ** 2
        cp = cp + 0.28 * np.clip(xc, 0.0, 1.0) ** 2
        p_inf = 1.0 * (1.0 / 0.15) ** 2 / 1.4
        pressure = p_inf + cp * 0.5
        cf = (
            0.09 * np.exp(-xc / 0.35) * (1.0 - 0.6 * xc) + 0.004
            if viscous
            else np.zeros_like(xc)
        )
        speed = np.sqrt(np.maximum(1.0 - cp, 0.0))
        for i in range(n_side):
            records.append(
                (
                    xc[i],
                    y[i],
                    nx[i],
                    ny[i],
                    pressure[i],
                    cp[i],
                    cf[i],
                    1.0 + 0.02 * cp[i],
                    0.0 if viscous else speed[i],
                    0.0,
                    0.0 if viscous else speed[i] * 0.15,
                    "bc-4",
                )
            )
    rng.shuffle(records)
    rows = [SURFACE_HEADER + ",wall_distance"]
    for rec in records:
        rows.append(
            "%.10g,%.10g,%.10g,%.10g,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%s,%.6g"
            % (rec + (1.0e-4,))
        )
    path.write_text("\n".join(rows) + "\n")


def write_partition_diagnostics(path: Path, n_ranks: int = 4, n_cells: int = 1000) -> None:
    """Write per-rank partition diagnostics matching the contract header."""
    rng = np.random.default_rng(3)
    owned = np.full(n_ranks, n_cells // n_ranks)
    owned[: n_cells % n_ranks] += 1
    rows = [PARTITION_HEADER]
    for rank in range(n_ranks):
        neighbours = [r for r in range(n_ranks) if r != rank][:2]
        ghost = int(20 + rng.integers(0, 12))
        rows.append(
            "%d,%d,%d,%d,%d,%s,%d,%d"
            % (
                rank,
                int(owned[rank]),
                ghost,
                int(30 + rng.integers(0, 10)),
                len(neighbours),
                " ".join(str(r) for r in neighbours),
                ghost,
                ghost,
            )
        )
    path.write_text("\n".join(rows) + "\n")


def write_metadata(path: Path, case_id: str, n_cells: int, transient: bool) -> None:
    """Write a metadata.json holding every field the validator requires."""
    start = datetime(2026, 8, 27, 4, 0, 0, tzinfo=timezone.utc)
    meta = {
        "case_id": case_id,
        "solver_name": "fixture-generator",
        "solver_version": "0.0.0-dev-fixture",
        "git_revision": None,
        "mpi_ranks": 4,
        "mesh_file": "CylinderB1.cgns",
        "num_cells_global": n_cells,
        "num_faces_global": int(n_cells * 2.1),
        "num_cells_owned_local": n_cells // 4,
        "num_cells_ghost_local": 24,
        "partitioner": "metis_kway",
        "partition_edge_cut": 412,
        "halo_exchange": "neighbor_isend_irecv",
        "full_state_replication_during_iterations": False,
        "full_mesh_replication_during_iterations": False,
        "equation_set": "compressible_navier_stokes_2d",
        "inviscid_flux": "roe_with_entropy_fix",
        "entropy_fix": "harten_hyman",
        "viscous_flux": "central_gradient",
        "time_integrator": "bdf2" if transient else "implicit_euler_pseudo_time",
        "implicit_solver": "gmres_ilu0",
        "reconstruction": "least_squares_gradient",
        "limiter": "venkatakrishnan",
        "spatial_order_claimed": 2,
        "positivity_preservation": "pressure_density_floor",
        "wall_boundary_output_semantics": "boundary_value",
        "true_bdf2_inner_loop": bool(transient),
        "typical_inner_iterations": 8,
        "min_inner_iterations": 5,
        "max_inner_iterations": 1000,
        "observed_min_inner_iterations": 5,
        "observed_max_inner_iterations": 21,
        "inner_residual_reduction_target": 0.001,
        "inner_target_misses": 0,
        "inner_target_converged_fraction": 1.0,
        "last_inner_residual_ratio": 0.00042,
        "start_time_utc": start.isoformat(),
        "end_time_utc": (start + timedelta(minutes=37)).isoformat(),
        "completed": True,
        "convergence_status": "statistically_periodic" if transient else "converged",
    }
    path.write_text(json.dumps(meta, indent=2) + "\n")


def write_run_status(path: Path, case_id: str, n_steps: int, dt: float, transient: bool) -> None:
    """Write a run_status.json holding every field the validator requires."""
    status = {
        "case_id": case_id,
        "command": "mpirun -np 4 ./cfd_solver solve --case %s.json --output out" % case_id,
        "mpi_ranks": 4,
        "wall_time_seconds": 2231.4,
        "final_step": n_steps,
        "final_physical_time": n_steps * dt if transient else 0.0,
        "convergence_status": "statistically_periodic" if transient else "converged",
        "residual_reduction_orders": 6.4,
        "notes": "synthetic development fixture, not a real solver run",
    }
    path.write_text(json.dumps(status, indent=2) + "\n")


# ----------------------------------------------------------------------------
# Driver
# ----------------------------------------------------------------------------


def make_fixture(
    out_dir: Path,
    case_id: str = "cylinder_m010_laminar_re200",
    body: str = "cylinder",
    n_theta: int = 48,
    n_radial: int = 10,
    n_steps: int = 30000,
    dt: float = 0.01,
    viscous: bool = True,
    transient: bool = True,
) -> Path:
    """Create a complete synthetic case directory and return its path."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    points, cells, cell_types = build_annulus_mesh(n_theta=n_theta, n_radial=n_radial)
    centroids = np.array([points[c].mean(axis=0) for c in cells], dtype=np.float64)
    mach_inf = 0.1 if body == "cylinder" else 0.15
    fields = synthetic_fields(centroids, mach_inf=mach_inf)

    write_vtu(out_dir / "field_final.vtu", points, cells, cell_types, fields)
    write_residuals(out_dir / "residuals.csv", n_steps=n_steps, dt=dt, transient=transient)
    write_forces(
        out_dir / "forces.csv", n_steps=n_steps, dt=dt, oscillating=transient
    )
    if body == "cylinder":
        write_surface_cylinder(out_dir / "surface.csv", viscous=viscous)
    else:
        write_surface_airfoil(out_dir / "surface.csv", viscous=viscous)
    write_partition_diagnostics(out_dir / "partition_diagnostics.csv", n_cells=len(cells))
    write_metadata(out_dir / "metadata.json", case_id, len(cells), transient)
    write_run_status(out_dir / "run_status.json", case_id, n_steps, dt, transient)
    (out_dir / "restart_final.bin").write_bytes(b"synthetic-restart-placeholder\n")
    (out_dir / "stdout.log").write_text(
        "synthetic fixture log: %d cells, %d steps\n" % (len(cells), n_steps)
    )

    print("fixture written to %s" % out_dir)
    print("  cells      : %d (%d tri, %d quad)" % (
        len(cells),
        int(np.sum(cell_types == 5)),
        int(np.sum(cell_types == 9)),
    ))
    print("  points     : %d" % points.shape[0])
    for name in ("mach", "pressure", "vorticity"):
        arr = fields[name]
        print("  %-10s : min=%.4g max=%.4g" % (name, arr.min(), arr.max()))
    return out_dir


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--out-dir",
        default="probe_out/fixture",
        help="directory to create (keep under tools/probe_out/)",
    )
    parser.add_argument("--case-id", default="cylinder_m010_laminar_re200")
    parser.add_argument("--body", choices=("cylinder", "airfoil"), default="cylinder")
    parser.add_argument("--n-theta", type=int, default=48, help="circumferential cells")
    parser.add_argument("--n-radial", type=int, default=10, help="radial cell rings")
    parser.add_argument(
        "--n-steps",
        type=int,
        default=30000,
        help="history length; the default matches the production Re200 run (dt=0.01, t_f=300)",
    )
    parser.add_argument("--dt", type=float, default=0.01)
    parser.add_argument(
        "--steady",
        action="store_true",
        help="write a steady-style history (no cl oscillation, zero physical time)",
    )
    parser.add_argument(
        "--inviscid",
        action="store_true",
        help="write surface cf identically zero, as an inviscid case would",
    )
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    make_fixture(
        Path(args.out_dir),
        case_id=args.case_id,
        body=args.body,
        n_theta=args.n_theta,
        n_radial=args.n_radial,
        n_steps=args.n_steps,
        dt=args.dt,
        viscous=not args.inviscid,
        transient=not args.steady,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
