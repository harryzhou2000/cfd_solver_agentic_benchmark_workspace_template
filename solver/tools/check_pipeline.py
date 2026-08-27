#!/usr/bin/env python3
"""End-to-end self-check for the post-processing toolchain.

Builds a multi-case synthetic fixture tree, runs the whole pipeline over it and
asserts that the results are usable rather than merely present:

*   the .vtu round-trips through vtu_reader (points, mixed cell types, every
    cell array, sane total area, area-weighted cell-to-point averaging bounded
    by the cell extrema);
*   every expected figure exists and is a non-trivial PNG (valid signature,
    plausible size, more than a handful of distinct colours);
*   the figure manifest has exactly the required header, references only files
    that exist, and satisfies the examiner's filename-vs-variable rules;
*   every case has both a 'mach' and a 'pressure' manifest entry, and the Re200
     case additionally has a 'vorticity' or 'velocity' entry;
*   the transient analysis recovers the Strouhal number that was synthesised
    into the fixture, to within the FFT frequency resolution;
*   the inviscid case produces no skin-friction figure, and viscous cases do.

Finally it invokes the benchmark's own examiner validate_report() on the
generated report artefacts, so the manifest is checked by the real grader rather
than by a local re-implementation.

Usage::

    python check_pipeline.py [--keep] [--fixture-root probe_out/selftest]

Exit status is 0 only when every check passes.
"""

from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import shutil
import struct
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np

TOOLS_DIR = Path(__file__).resolve().parent
EXAMINER = Path("/workspace/cfd_solver_agentic_benchmark/examiner/validate_outputs.py")

#: Cases synthesised for the self-check: (case_id, body, steps, steady, inviscid)
FIXTURE_CASES = [
    ("cylinder_m010_laminar_re200", "cylinder", 30000, False, False),
    ("cylinder_m010_laminar_re20", "cylinder", 3000, True, False),
    ("naca0012_m080_inviscid", "airfoil", 3000, True, True),
    ("naca0012_m015_laminar_re5000", "airfoil", 4000, True, False),
]

#: Strouhal number written into the fixture force history by make_test_fixture.
FIXTURE_STROUHAL = 0.195

REQUIRED_MANIFEST_HEADER = [
    "figure_file",
    "case_id",
    "figure_type",
    "variable",
    "source_file",
    "caption",
]


class CheckFailure(AssertionError):
    """Raised when a self-check assertion fails."""


_failures: List[str] = []
_checks = 0


def check(condition: bool, message: str) -> bool:
    """Record a check; returns the condition so callers can branch on it."""
    global _checks
    _checks += 1
    if not condition:
        _failures.append(message)
        print("  FAIL  %s" % message)
    return bool(condition)


def png_dimensions(path: Path) -> Tuple[int, int]:
    """Read (width, height) straight from the PNG IHDR chunk."""
    data = path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise CheckFailure("%s is not a PNG" % path)
    if data[12:16] != b"IHDR":
        raise CheckFailure("%s has no IHDR chunk where one is required" % path)
    width, height = struct.unpack(">II", data[16:24])
    return (int(width), int(height))


def png_info(path: Path) -> Tuple[int, int, int]:
    """Return (width, height, distinct_colour_estimate) for a PNG file.

    Decoding goes through matplotlib's own PNG reader, which is already a
    dependency and is orders of magnitude faster than unfiltering scanlines in
    Python.  The colour count is what distinguishes a real plot from a blank
    canvas: an axes frame plus data typically yields hundreds of distinct
    colours, whereas an empty figure yields a handful.
    """
    width, height = png_dimensions(path)
    try:
        import matplotlib.image as mpimg

        pixels = mpimg.imread(str(path))
    except Exception:  # noqa: BLE001 - fall back to the header-only answer
        return (width, height, -1)

    array = np.asarray(pixels)
    if array.ndim == 2:
        array = array[:, :, None]
    if array.dtype.kind == "f":
        array = np.clip(array * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)
    rgb = array[:, :, :3] if array.shape[2] >= 3 else np.repeat(array[:, :, :1], 3, axis=2)
    flat = rgb.reshape(-1, 3)
    if flat.shape[0] > 200000:
        flat = flat[:: flat.shape[0] // 200000]
    # Pack the three channels into one integer so np.unique stays cheap.
    packed = (
        flat[:, 0].astype(np.uint32) << 16
        | flat[:, 1].astype(np.uint32) << 8
        | flat[:, 2].astype(np.uint32)
    )
    return (width, height, int(np.unique(packed).size))


def build_fixtures(fixture_root: Path) -> None:
    """Generate the synthetic case tree."""
    import make_test_fixture

    print("building fixture tree in %s" % fixture_root)
    for case_id, body, steps, steady, inviscid in FIXTURE_CASES:
        make_test_fixture.make_fixture(
            out_dir=fixture_root / case_id,
            case_id=case_id,
            body=body,
            n_steps=steps,
            viscous=not inviscid,
            transient=not steady,
        )


def check_vtu_roundtrip(fixture_root: Path) -> None:
    """Verify the reader reproduces the mesh and data written by the fixture."""
    from vtu_reader import read_vtu

    print("checking .vtu round-trip")
    path = fixture_root / FIXTURE_CASES[0][0] / "field_final.vtu"
    mesh = read_vtu(path)

    check(mesh.n_points == 528, "expected 528 points, got %d" % mesh.n_points)
    check(mesh.n_cells == 576, "expected 576 cells, got %d" % mesh.n_cells)
    n_tri = int(np.sum(mesh.cell_types == 5))
    n_quad = int(np.sum(mesh.cell_types == 9))
    check(n_tri == 192 and n_quad == 384, "expected 192 tri / 384 quad, got %d / %d" % (n_tri, n_quad))
    check(mesh.points.shape[1] == 2, "points must be (N,2), got %s" % (mesh.points.shape,))

    for name in (
        "density",
        "velocity",
        "pressure",
        "mach",
        "temperature",
        "total_energy",
        "vorticity",
        "rank",
    ):
        check(mesh.has_cell_array(name), "missing cell array '%s'" % name)
    velocity = mesh.cell_array("velocity")
    check(velocity.ndim == 2 and velocity.shape[1] == 3, "velocity must be (N,3), got %s" % (velocity.shape,))

    # Analytic annulus area: pi (R^2 - r^2) = pi (36 - 0.25).  The polygonal mesh
    # under-resolves the circles, so allow a couple of percent.
    exact = np.pi * (6.0 ** 2 - 0.5 ** 2)
    area = float(mesh.cell_areas().sum())
    check(
        abs(area - exact) / exact < 0.03,
        "total area %.4f differs from analytic %.4f by more than 3%%" % (area, exact),
    )

    tri, tri_to_cell = mesh.triangulation()
    check(
        tri.triangles.shape[0] == n_tri + 2 * n_quad,
        "expected %d triangles, got %d" % (n_tri + 2 * n_quad, tri.triangles.shape[0]),
    )
    check(tri_to_cell.shape[0] == tri.triangles.shape[0], "tri_to_cell length mismatch")
    tri_area = 0.0
    x, y = tri.x, tri.y
    t = tri.triangles
    tri_area = float(
        np.sum(
            0.5
            * np.abs(
                (x[t[:, 1]] - x[t[:, 0]]) * (y[t[:, 2]] - y[t[:, 0]])
                - (x[t[:, 2]] - x[t[:, 0]]) * (y[t[:, 1]] - y[t[:, 0]])
            )
        )
    )
    check(
        abs(tri_area - area) / area < 1.0e-9,
        "triangulated area %.6f does not match cell area %.6f" % (tri_area, area),
    )

    # The cell-to-point interpolation must stay inside the cell-data range.
    for name in ("mach", "pressure", "vorticity"):
        cell_values = mesh.cell_array(name)
        nodal = mesh.cell_to_point(cell_values)
        check(nodal.shape[0] == mesh.n_points, "%s nodal array has the wrong length" % name)
        check(np.all(np.isfinite(nodal)), "%s nodal array contains non-finite values" % name)
        lo, hi = float(np.min(cell_values)), float(np.max(cell_values))
        span = max(hi - lo, 1.0e-30)
        check(
            float(np.min(nodal)) >= lo - 1.0e-9 * span and float(np.max(nodal)) <= hi + 1.0e-9 * span,
            "%s nodal range [%.6g, %.6g] escapes the cell range [%.6g, %.6g]"
            % (name, float(np.min(nodal)), float(np.max(nodal)), lo, hi),
        )
    # A constant field must average to that constant exactly (partition of unity).
    constant = np.full(mesh.n_cells, 3.25)
    nodal_constant = mesh.cell_to_point(constant)
    check(
        float(np.max(np.abs(nodal_constant - 3.25))) < 1.0e-12,
        "area-weighted averaging is not exact for a constant field",
    )

    # Physical sanity of the synthesised state.
    check(float(np.min(mesh.cell_array("density"))) > 0.0, "fixture density must be positive")
    check(float(np.min(mesh.cell_array("pressure"))) > 0.0, "fixture pressure must be positive")
    mach = mesh.cell_array("mach")
    check(float(np.ptp(mach)) > 1.0e-3, "fixture Mach field is essentially constant")
    check(float(np.ptp(mesh.cell_array("vorticity"))) > 1.0, "fixture vorticity field is too flat")


def check_reader_robustness(fixture_root: Path, work_dir: Path) -> None:
    """Confirm the reader tolerates reformatted whitespace and rejects binary."""
    from vtu_reader import VtuFormatError, read_vtu

    print("checking reader robustness")
    source = fixture_root / FIXTURE_CASES[0][0] / "field_final.vtu"
    original = read_vtu(source)
    text = source.read_text()
    work_dir.mkdir(parents=True, exist_ok=True)

    # 1. Collapse every data block onto one line, and re-indent chaotically.
    one_line = work_dir / "one_line.vtu"
    squashed: List[str] = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("<"):
            squashed.append(line)
        else:
            squashed.append("\t  " + " ".join(stripped.split()) + "   ")
    one_line.write_text("\n".join(squashed) + "\n")
    reformatted = read_vtu(one_line)
    check(
        reformatted.n_points == original.n_points and reformatted.n_cells == original.n_cells,
        "reader changed size after whitespace reformatting",
    )
    check(
        np.allclose(reformatted.cell_array("mach"), original.cell_array("mach")),
        "reader changed Mach values after whitespace reformatting",
    )

    # 2. One number per line, the other extreme of line wrapping.
    per_line = work_dir / "per_line.vtu"
    exploded: List[str] = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("<"):
            exploded.append(line)
        else:
            exploded.extend("  " + token for token in stripped.split())
    per_line.write_text("\n".join(exploded) + "\n")
    wrapped = read_vtu(per_line)
    check(
        np.allclose(wrapped.cell_array("pressure"), original.cell_array("pressure")),
        "reader changed pressure values with one number per line",
    )

    # 3. offsets written with a leading zero (n_cells + 1 values).
    leading_zero = work_dir / "leading_zero_offsets.vtu"
    offsets = np.cumsum([len(c) for c in original.cells])
    old_block = " ".join(str(int(v)) for v in offsets)
    new_block = "0 " + old_block
    marker = '<DataArray type="Int64" Name="offsets" format="ascii">'
    start = text.index(marker) + len(marker)
    end = text.index("</DataArray>", start)
    rebuilt = text[:start] + "\n          " + new_block + "\n        " + text[end:]
    leading_zero.write_text(rebuilt)
    with_zero = read_vtu(leading_zero)
    check(
        with_zero.n_cells == original.n_cells,
        "reader mishandled offsets written with a leading zero (%d vs %d cells)"
        % (with_zero.n_cells, original.n_cells),
    )

    # 4. A binary/appended file must be rejected with a clear error.
    binary = work_dir / "binary.vtu"
    binary.write_text(text.replace('format="ascii"', 'format="appended"', 1) + "\n<AppendedData/>\n")
    try:
        read_vtu(binary)
        check(False, "reader accepted a non-ASCII .vtu instead of raising")
    except VtuFormatError:
        check(True, "")
    except Exception as exc:  # noqa: BLE001 - any other error is the wrong one
        check(False, "reader raised %s instead of VtuFormatError for binary input" % type(exc).__name__)

    # 5. Truncated connectivity must be caught, not silently mis-meshed.
    torn = work_dir / "torn.vtu"
    conn_marker = '<DataArray type="Int64" Name="connectivity" format="ascii">'
    cstart = text.index(conn_marker) + len(conn_marker)
    cend = text.index("</DataArray>", cstart)
    tokens = text[cstart:cend].split()
    torn.write_text(text[:cstart] + "\n " + " ".join(tokens[:-4]) + "\n" + text[cend:])
    try:
        read_vtu(torn)
        check(False, "reader accepted a truncated connectivity block")
    except VtuFormatError:
        check(True, "")
    except Exception as exc:  # noqa: BLE001
        check(False, "reader raised %s instead of VtuFormatError for torn input" % type(exc).__name__)


def check_figures(figures_dir: Path) -> Dict[str, Path]:
    """Assert every expected figure exists and looks like a real plot."""
    print("checking figures in %s" % figures_dir)
    found = {p.name: p for p in figures_dir.glob("*.png")}

    expected: List[str] = []
    for case_id, _body, _steps, _steady, inviscid in FIXTURE_CASES:
        expected += [
            "%s_residuals.png" % case_id,
            "%s_forces.png" % case_id,
            "%s_cp.png" % case_id,
            "%s_mach.png" % case_id,
            "%s_mach_farfield.png" % case_id,
            "%s_pressure.png" % case_id,
            "%s_velocity.png" % case_id,
            "%s_vorticity.png" % case_id,
        ]
        if inviscid:
            # An inviscid case must NOT produce a skin-friction figure.
            check(
                "%s_cf.png" % case_id not in found,
                "inviscid case %s should not have a cf figure" % case_id,
            )
        else:
            expected.append("%s_cf.png" % case_id)
    expected.append("cylinder_m010_laminar_re200_spectrum.png")

    for name in expected:
        if not check(name in found, "missing figure %s" % name):
            continue
        path = found[name]
        size = path.stat().st_size
        check(size > 12000, "figure %s is suspiciously small (%d bytes)" % (name, size))
        width, height, distinct = png_info(path)
        check(width > 400 and height > 300, "figure %s is too small: %dx%d" % (name, width, height))
        if distinct >= 0:
            check(distinct > 24, "figure %s has only %d distinct colours; likely blank" % (name, distinct))
    return found


def check_manifest(manifest_path: Path, figures_dir: Path) -> List[Dict[str, str]]:
    """Apply the examiner's manifest rules locally, with clearer messages."""
    print("checking manifest %s" % manifest_path)
    if not check(manifest_path.exists(), "manifest %s does not exist" % manifest_path):
        return []
    with manifest_path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        header = reader.fieldnames or []
        rows = list(reader)

    check(
        header == REQUIRED_MANIFEST_HEADER,
        "manifest header %s does not equal the required %s" % (header, REQUIRED_MANIFEST_HEADER),
    )
    check(len(rows) > 0, "manifest has no rows")

    by_case: Dict[str, set] = {}
    for row in rows:
        name = row.get("figure_file", "")
        check("/" not in name, "figure_file must be a basename, got %r" % name)
        check((figures_dir / name).exists(), "manifest references a missing figure: %s" % name)
        variable = (row.get("variable") or "").lower()
        lowered = name.lower()
        # The examiner's filename-vs-variable cross-checks.
        if "mach" in lowered:
            check("mach" in variable, "%s: filename says mach but variable is %r" % (name, variable))
        if "pressure" in lowered:
            check(
                "pressure" in variable,
                "%s: filename says pressure but variable is %r" % (name, variable),
            )
        check(bool(row.get("case_id")), "%s: empty case_id" % name)
        check(bool(row.get("figure_type")), "%s: empty figure_type" % name)
        check(bool(row.get("source_file")), "%s: empty source_file" % name)
        caption = row.get("caption") or ""
        check(len(caption) > 20, "%s: caption is too short: %r" % (name, caption))
        check(
            (row.get("case_id") or "") in caption,
            "%s: caption does not name the case" % name,
        )
        by_case.setdefault(row.get("case_id", ""), set()).add(variable)

    for case_id, _body, _steps, _steady, _inviscid in FIXTURE_CASES:
        variables = by_case.get(case_id, set())
        # The examiner tests exact set membership, not a substring search.
        check("mach" in variables, "case %s has no manifest entry with variable exactly 'mach'" % case_id)
        check(
            "pressure" in variables,
            "case %s has no manifest entry with variable exactly 'pressure'" % case_id,
        )
        if "re200" in case_id.lower():
            check(
                any("vorticity" in v or "velocity" in v for v in variables),
                "Re200 case %s has no vorticity/velocity wake entry" % case_id,
            )
    return rows


def check_transient(json_path: Path) -> None:
    """Verify the shedding analysis recovers the synthesised Strouhal number."""
    print("checking transient analysis %s" % json_path)
    if not check(json_path.exists(), "transient JSON %s does not exist" % json_path):
        return
    data = json.loads(json_path.read_text())

    for key in (
        "mean_cd",
        "mean_cl",
        "cl_amplitude_half_peak_to_peak",
        "cl_amplitude_rms",
        "shedding_frequency",
        "strouhal_number",
        "window_start_time",
        "window_end_time",
        "window_fraction",
        "samples_in_window",
    ):
        check(key in data, "transient JSON is missing '%s'" % key)

    strouhal = float(data.get("strouhal_number", float("nan")))
    resolution = float(data.get("frequency_resolution", 0.0))
    tolerance = max(3.0 * resolution, 0.01)
    check(
        abs(strouhal - FIXTURE_STROUHAL) < tolerance,
        "recovered St %.5f differs from the synthesised %.5f by more than %.5f"
        % (strouhal, FIXTURE_STROUHAL, tolerance),
    )
    zero_crossing = float(data.get("shedding_frequency_zero_crossing_check", float("nan")))
    check(
        np.isfinite(zero_crossing) and abs(zero_crossing - FIXTURE_STROUHAL) < 0.02,
        "zero-crossing cross-check %.5f disagrees with the synthesised frequency" % zero_crossing,
    )
    check(float(data.get("mean_cd", 0.0)) > 0.5, "mean cd should be positive and O(1)")
    check(
        float(data.get("cl_amplitude_half_peak_to_peak", 0.0)) > 0.05,
        "cl amplitude should be clearly nonzero for the shedding case",
    )
    check(
        float(data.get("window_end_time", 0.0)) > float(data.get("window_start_time", 0.0)),
        "transient window bounds are not ordered",
    )
    check(
        float(data.get("cycles_in_window", 0.0)) > 5.0,
        "analysis window holds too few shedding cycles to be meaningful",
    )


def run_examiner(report_dir: Path, case_dirs: List[Path]) -> None:
    """Run the benchmark examiner's validate_report() on the generated report.

    This is the authoritative check: it uses the grader's own code rather than a
    local re-implementation of the rules.
    """
    print("running the benchmark examiner on the generated report")
    if not EXAMINER.exists():
        print("  note: examiner not found at %s; skipping" % EXAMINER)
        return
    spec = importlib.util.spec_from_file_location("benchmark_examiner", EXAMINER)
    if spec is None or spec.loader is None:
        check(False, "could not load the examiner module")
        return
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    case_ids = [c.name for c in case_dirs]
    try:
        module.validate_report(report_dir, case_ids)
    except AssertionError as exc:
        check(False, "examiner validate_report rejected the report: %s" % exc)
    else:
        check(True, "")
        print("  examiner validate_report: OK")


def stage_report(figures_dir: Path, manifest_path: Path, report_dir: Path) -> None:
    """Assemble the minimal report directory the examiner expects.

    Only the artefacts the figure-manifest check needs are staged here; the real
    report.tex and sanity_checks.json are produced elsewhere in the submission.
    """
    report_dir.mkdir(parents=True, exist_ok=True)
    target_figures = report_dir / "figures"
    target_figures.mkdir(parents=True, exist_ok=True)
    for png in figures_dir.glob("*.png"):
        shutil.copy2(png, target_figures / png.name)
    shutil.copy2(manifest_path, report_dir / "figure_manifest.csv")
    (report_dir / "report.tex").write_text(
        "% placeholder staged by check_pipeline.py for the examiner manifest check\n"
    )
    (report_dir / "run_manifest.csv").write_text("case_id,command\nselftest,placeholder\n")
    (report_dir / "sanity_checks.json").write_text(json.dumps({"selftest": True}, indent=2) + "\n")


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--fixture-root",
        type=Path,
        default=TOOLS_DIR / "probe_out" / "selftest",
        help="working directory for the fixture, figures and staged report",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="reuse an existing fixture tree instead of regenerating it",
    )
    args = parser.parse_args(argv)

    root = args.fixture_root
    fixture_tree = root / "results"
    figures_dir = root / "figures"
    manifest_path = root / "figure_manifest.csv"
    report_dir = root / "report"

    sys.path.insert(0, str(TOOLS_DIR))
    import make_figures

    if not args.skip_build:
        build_fixtures(fixture_tree)

    check_vtu_roundtrip(fixture_tree)
    check_reader_robustness(fixture_tree, root / "reader_variants")

    print("running the full figure pipeline")
    summary = make_figures.build_all(
        results_root=fixture_tree,
        out_dir=figures_dir,
        manifest_path=manifest_path,
        transient_json=root / "cylinder_m010_laminar_re200_transient.json",
    )
    check(
        len(summary["cases"]) == len(FIXTURE_CASES),
        "pipeline plotted %d cases, expected %d" % (len(summary["cases"]), len(FIXTURE_CASES)),
    )

    check_figures(figures_dir)
    check_manifest(manifest_path, figures_dir)
    check_transient(root / "cylinder_m010_laminar_re200_transient.json")

    stage_report(figures_dir, manifest_path, report_dir)
    run_examiner(report_dir, sorted(p for p in fixture_tree.iterdir() if p.is_dir()))

    print("")
    if _failures:
        print("SELF-CHECK FAILED: %d of %d checks failed" % (len(_failures), _checks))
        for message in _failures:
            print("  - %s" % message)
        return 1
    print("SELF-CHECK PASSED: all %d checks green" % _checks)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
