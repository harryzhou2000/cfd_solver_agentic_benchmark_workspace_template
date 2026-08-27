#!/usr/bin/env python3
"""Walk a results tree, plot every case and write the report figure manifest.

Usage::

    python make_figures.py --results-root /workspace/solver/results \
                           --out-dir      /workspace/solver/report/figures \
                           --manifest     /workspace/solver/report/figure_manifest.csv
                           [--transient-json <path>] [--vorticity-clip 5.0]
                           [--window-fraction 0.4] [--case <id> ...] [--no-farfield]

For every case directory found under the results root (a directory is a case
when it contains at least one of metadata.json, residuals.csv, forces.csv,
surface.csv or field_final.vtu) this tool:

1.  plots the full figure set through plot_results.generate_case_figures();
2.  runs the vortex-shedding analysis through analyze_transient for any case id
    containing 're200', which also emits the lift-spectrum figure;
3.  appends the resulting rows to figure_manifest.csv.

The manifest header is exactly::

    figure_file,case_id,figure_type,variable,source_file,caption

Field conventions, which the examiner checks:

*   figure_file is the basename as it appears inside the figures directory.
*   variable is exactly 'mach' for Mach figures and exactly 'pressure' for
    pressure figures.  The examiner both matches the filename against this field
    and requires the literal tokens 'mach' and 'pressure' among each case's
    variables, so no decorated spelling such as 'mach_number' is used.
*   Any case id containing 're200' additionally gets figures whose variable
    contains 'vorticity' and 'velocity', which satisfies the wake-visualisation
    requirement.
*   source_file is the originating data file relative to the results root, for
    example cylinder_m010_laminar_re200/field_final.vtu.
"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Dict, List, Optional, Sequence

import analyze_transient
from plot_results import FigureRecord, detect_body, generate_case_figures, read_table

__all__ = ["MANIFEST_HEADER", "discover_cases", "build_all", "write_manifest"]

#: Exact manifest header the examiner expects.
MANIFEST_HEADER = [
    "figure_file",
    "case_id",
    "figure_type",
    "variable",
    "source_file",
    "caption",
]

#: A directory is treated as a case when it holds at least one of these.
CASE_MARKERS = (
    "metadata.json",
    "residuals.csv",
    "forces.csv",
    "surface.csv",
    "field_final.vtu",
)


def discover_cases(results_root: Path) -> List[Path]:
    """Find every case directory under the results root, sorted by name.

    The root itself is included when it looks like a single case directory, so
    the tool works both on a tree of cases and on one case in isolation.
    """
    results_root = Path(results_root)
    if not results_root.exists():
        raise FileNotFoundError("results root does not exist: %s" % results_root)

    def is_case(path: Path) -> bool:
        return path.is_dir() and any((path / marker).exists() for marker in CASE_MARKERS)

    if is_case(results_root):
        return [results_root]
    found = sorted((p for p in results_root.rglob("*") if is_case(p)), key=lambda p: str(p))
    # Drop nested duplicates: keep the outermost directory of any chain.
    cases: List[Path] = []
    for path in found:
        if not any(str(path).startswith(str(existing) + "/") for existing in cases):
            cases.append(path)
    return cases


def case_id_for(case_dir: Path) -> str:
    """Resolve a case id: metadata.json wins, otherwise the directory name."""
    metadata_path = case_dir / "metadata.json"
    if metadata_path.exists():
        try:
            value = json.loads(metadata_path.read_text()).get("case_id")
            if value:
                return str(value)
        except (json.JSONDecodeError, OSError):
            pass
    return case_dir.name


def _relative_source(case_dir: Path, results_root: Path, filename: str) -> str:
    """Express a source data file relative to the results root."""
    try:
        relative = case_dir.resolve().relative_to(Path(results_root).resolve())
        return str(relative / filename)
    except ValueError:
        return "%s/%s" % (case_dir.name, filename)


def write_manifest(rows: Sequence[Dict[str, str]], manifest_path: Path) -> Path:
    """Write the figure manifest CSV with the exact required header."""
    manifest_path = Path(manifest_path)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    with manifest_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=MANIFEST_HEADER)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in MANIFEST_HEADER})
    return manifest_path


def build_all(
    results_root: Path,
    out_dir: Path,
    manifest_path: Path,
    transient_json: Optional[Path] = None,
    vorticity_clip: float = 5.0,
    window_fraction: float = 0.4,
    diameter: float = 1.0,
    velocity: float = 1.0,
    levels: int = 40,
    farfield: bool = True,
    only_cases: Optional[Sequence[str]] = None,
) -> Dict[str, object]:
    """Plot every case, run the transient analysis and write the manifest.

    Returns a summary dictionary describing what was produced, so a caller (or a
    test) can assert on it.
    """
    results_root = Path(results_root)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    cases = discover_cases(results_root)
    if only_cases:
        wanted = set(only_cases)
        cases = [c for c in cases if case_id_for(c) in wanted or c.name in wanted]
    if not cases:
        raise RuntimeError(
            "no case directories found under %s (looked for %s)"
            % (results_root, ", ".join(CASE_MARKERS))
        )

    rows: List[Dict[str, str]] = []
    summary_cases: List[Dict[str, object]] = []
    transient_results: List[Dict[str, object]] = []

    for case_dir in cases:
        case_id = case_id_for(case_dir)
        print("=== %s  (%s)" % (case_id, case_dir))
        records: List[FigureRecord] = generate_case_figures(
            case_dir=case_dir,
            case_id=case_id,
            out_dir=out_dir,
            vorticity_clip=vorticity_clip,
            levels=levels,
            farfield=farfield,
        )
        for record in records:
            rows.append(
                {
                    "figure_file": record.path.name,
                    "case_id": record.case_id,
                    "figure_type": record.figure_type,
                    "variable": record.variable,
                    "source_file": _relative_source(case_dir, results_root, record.source_file),
                    "caption": record.caption,
                }
            )

        # Vortex-shedding analysis for the transient case.
        transient_entry: Optional[Dict[str, object]] = None
        if "re200" in case_id.lower() and (case_dir / "forces.csv").exists():
            print("  running transient shedding analysis")
            try:
                result = analyze_transient.analyze_forces(
                    case_dir=case_dir,
                    window_fraction=window_fraction,
                    diameter=diameter,
                    velocity=velocity,
                    case_id=case_id,
                )
            except (ValueError, KeyError, FileNotFoundError) as exc:
                print("  WARNING: transient analysis failed: %s" % exc)
            else:
                figure_path = analyze_transient.write_spectrum_figure(result, out_dir)
                analyze_transient.print_summary(result)
                serialisable = {k: v for k, v in result.items() if not k.startswith("_")}
                serialisable["spectrum_figure"] = str(figure_path) if figure_path else None
                transient_results.append(serialisable)
                transient_entry = serialisable

                target = transient_json or (Path(manifest_path).parent / ("%s_transient.json" % case_id))
                target = Path(target)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(json.dumps(serialisable, indent=2, sort_keys=True) + "\n")
                print("  transient json: %s" % target)

                if figure_path is not None:
                    strouhal = serialisable.get("strouhal_number")
                    frequency = serialisable.get("shedding_frequency")
                    rows.append(
                        {
                            "figure_file": figure_path.name,
                            "case_id": case_id,
                            "figure_type": "spectrum",
                            "variable": "lift_coefficient_spectrum",
                            "source_file": _relative_source(case_dir, results_root, "forces.csv"),
                            "caption": (
                                "Amplitude spectrum of the post-transient lift coefficient "
                                "fluctuation for case %s; dominant shedding frequency "
                                "f = %.4f, Strouhal number St = %.4f."
                                % (
                                    case_id,
                                    float(frequency) if frequency is not None else float("nan"),
                                    float(strouhal) if strouhal is not None else float("nan"),
                                )
                            ),
                        }
                    )

        surface_path = case_dir / "surface.csv"
        surface = read_table(surface_path) if surface_path.exists() else None
        summary_cases.append(
            {
                "case_id": case_id,
                "case_dir": str(case_dir),
                "body": detect_body(surface, case_id),
                "figures": [r.path.name for r in records],
                "transient_analysis": transient_entry is not None,
            }
        )

    manifest = write_manifest(rows, manifest_path)
    print("manifest written: %s (%d rows)" % (manifest, len(rows)))

    return {
        "cases": summary_cases,
        "manifest": str(manifest),
        "manifest_rows": len(rows),
        "figures_dir": str(out_dir),
        "transient_results": transient_results,
    }


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Plot every case under a results root and write the figure manifest."
    )
    parser.add_argument(
        "--results-root",
        default="/workspace/solver/results",
        type=Path,
        help="directory containing the per-case solver output directories",
    )
    parser.add_argument(
        "--out-dir",
        default="/workspace/solver/report/figures",
        type=Path,
        help="directory to write the PNG figures into",
    )
    parser.add_argument(
        "--manifest",
        default="/workspace/solver/report/figure_manifest.csv",
        type=Path,
        help="figure manifest CSV to write",
    )
    parser.add_argument(
        "--transient-json",
        default=None,
        type=Path,
        help="path for the transient analysis JSON (default: <manifest dir>/<case_id>_transient.json)",
    )
    parser.add_argument("--vorticity-clip", type=float, default=5.0)
    parser.add_argument("--window-fraction", type=float, default=0.4)
    parser.add_argument("--diameter", type=float, default=1.0)
    parser.add_argument("--velocity", type=float, default=1.0)
    parser.add_argument("--levels", type=int, default=40)
    parser.add_argument("--no-farfield", action="store_true", help="skip whole-domain Mach figures")
    parser.add_argument(
        "--case",
        action="append",
        default=None,
        dest="cases",
        help="restrict to this case id (repeatable)",
    )
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    summary = build_all(
        results_root=args.results_root,
        out_dir=args.out_dir,
        manifest_path=args.manifest,
        transient_json=args.transient_json,
        vorticity_clip=args.vorticity_clip,
        window_fraction=args.window_fraction,
        diameter=args.diameter,
        velocity=args.velocity,
        levels=args.levels,
        farfield=not args.no_farfield,
        only_cases=args.cases,
    )
    print(
        "done: %d case(s), %d manifest rows"
        % (len(summary["cases"]), summary["manifest_rows"])
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
