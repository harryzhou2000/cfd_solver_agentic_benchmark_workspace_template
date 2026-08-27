#!/usr/bin/env python3
"""Harvest final numbers from results/ into LaTeX include files for report.tex.

Run with the project virtualenv from the report directory:

    ../.venv/bin/python harvest_numbers.py

It writes, next to report.tex:

    numbers_auto.tex   macro definitions for numbers cited inline in the prose
    tab_runstatus.tex  body rows of the run-status table
    tab_forces.tex     body rows of the force-coefficient table
    tab_partition.tex  body rows of the per-rank partition table
    tab_scaling.tex    body rows of the rank-count scaling table

Nothing here computes physics: every value is copied or reduced from a file the
solver itself wrote.  Cases that have not finished expand to a visible
"run in progress" marker rather than to a plausible-looking placeholder.
"""
from __future__ import annotations

import csv
import json
import math
from pathlib import Path
import os

REPORT_DIR = Path(__file__).resolve().parent
RESULTS_DIR = REPORT_DIR.parent / "results"

PENDING = "\\pending"

#: Results written before this epoch came from a build whose steady termination
#: test was defective: it normalised residuals by the impulsive-start transient
#: and could stop while the forces were still moving.  Such runs must never be
#: quoted as final results, so they are treated exactly like an unfinished run.
#: Set via the CNS_HARVEST_MIN_MTIME environment variable (a Unix timestamp).
MIN_MTIME = float(os.environ.get("CNS_HARVEST_MIN_MTIME", "0"))


def is_stale(path):
    """True when a results file predates the trusted-results cutoff."""
    try:
        return MIN_MTIME > 0.0 and path.stat().st_mtime < MIN_MTIME
    except OSError:
        return True


def pending_row(columns):
    """A table row of the given width, first cell holding the pending marker.

    A spanning multicolumn cannot be used here: these row files are pulled in
    with an input command from inside a tabular, and TeX will not accept
    multicolumn as the first token of an input file in that position.
    """
    return " & ".join([PENDING] + ["" for _ in range(columns - 1)]) + " \\\\"

#: case id, LaTeX macro suffix, human label for tables
CASES = [
    ("naca0012_m015_inviscid", "NacaSubInv",
     "NACA0012 $M_\\infty\\!=\\!0.15$ inviscid"),
    ("naca0012_m080_inviscid", "NacaTraInv",
     "NACA0012 $M_\\infty\\!=\\!0.80$ inviscid"),
    ("naca0012_m200_inviscid", "NacaSupInv",
     "NACA0012 $M_\\infty\\!=\\!2.0$ inviscid"),
    ("naca0012_m015_laminar_re5000", "NacaSubLam",
     "NACA0012 $M_\\infty\\!=\\!0.15$ laminar"),
    ("naca0012_m080_laminar_re5000", "NacaTraLam",
     "NACA0012 $M_\\infty\\!=\\!0.80$ laminar"),
    ("naca0012_m200_laminar_re5000", "NacaSupLam",
     "NACA0012 $M_\\infty\\!=\\!2.0$ laminar"),
    ("cylinder_m010_laminar_re20", "CylSteady",
     "Cylinder $M_\\infty\\!=\\!0.1$ $Re\\!=\\!20$"),
    ("cylinder_m010_laminar_re200", "CylShed",
     "Cylinder $M_\\infty\\!=\\!0.1$ $Re\\!=\\!200$"),
]


def read_json(path):
    try:
        with open(path) as handle:
            return json.load(handle)
    except Exception:
        return None


def read_rows(path):
    """Read a CSV, dropping any row that is short or unparseable.

    Production runs are polled while they are still writing, so the final line
    of a live CSV is frequently truncated.  Columns that are not numeric (for
    example the quoted neighbour-rank list in partition_diagnostics.csv) are
    carried through as strings instead of discarding the whole row.
    """
    try:
        with open(path, newline="") as handle:
            reader = csv.DictReader(handle)
            fields = reader.fieldnames or []
            rows = []
            for row in reader:
                if any(row.get(f) in (None, "") for f in fields):
                    continue
                parsed = {}
                for field in fields:
                    try:
                        parsed[field] = float(row[field])
                    except ValueError:
                        parsed[field] = row[field]
                rows.append(parsed)
            return rows
    except Exception:
        return []


def num(value, digits=6):
    """siunitx-formatted number, or the pending marker for a missing value."""
    if value is None or not isinstance(value, (int, float)):
        return PENDING
    if not math.isfinite(value):
        return PENDING
    return "\\num{%.*g}" % (digits, value)


def drift(values, fraction=0.10):
    """Change in a quantity across the trailing fraction of its history.

    This is the stationarity evidence for a steady case: a converged steady run
    must have a drift far below its own magnitude.
    """
    if len(values) < 20:
        return None
    count = max(2, int(len(values) * fraction))
    segment = values[-count:]
    return segment[-1] - segment[0]


def shedding_stats(rows, half=0.5):
    """Mean/RMS forces and a Strouhal estimate from a transient force history.

    The frequency is taken from mean-crossing counts of the lift signal, which
    needs no spectral resolution assumptions; with D = U = 1 the Strouhal number
    equals the frequency.
    """
    if len(rows) < 50:
        return {}
    start = int(len(rows) * (1.0 - half))
    window = rows[start:]
    times = [r["physical_time"] for r in window]
    span = times[-1] - times[0]
    if span <= 0.0:
        return {}
    cl = [r["cl"] for r in window]
    cd = [r["cd"] for r in window]
    cl_mean = sum(cl) / len(cl)
    cd_mean = sum(cd) / len(cd)
    cl_rms = math.sqrt(sum((v - cl_mean) ** 2 for v in cl) / len(cl))
    cd_rms = math.sqrt(sum((v - cd_mean) ** 2 for v in cd) / len(cd))
    crossings = 0
    for a, b in zip(cl, cl[1:]):
        if (a - cl_mean) == 0.0:
            continue
        if (a - cl_mean) * (b - cl_mean) < 0.0:
            crossings += 1
    freq = crossings / (2.0 * span) if crossings else None
    return {
        "t_start": times[0],
        "t_end": times[-1],
        "cl_mean": cl_mean,
        "cd_mean": cd_mean,
        "cl_rms": cl_rms,
        "cd_rms": cd_rms,
        "cl_amp": 0.5 * (max(cl) - min(cl)),
        "cd_amp": 0.5 * (max(cd) - min(cd)),
        "freq": freq,
        "strouhal": freq,
    }


def main():
    macros = []
    status_rows = []
    force_rows = []

    def define(name, value):
        macros.append("\\def\\cns%s{%s}" % (name, value))

    #: Macros the prose always cites.  They are pre-defined to the pending marker
    #: so the document compiles before the corresponding run has finished; real
    #: values overwrite them below via a later \def.
    SHED_FIELDS = (("cd_mean", 4), ("cd_rms", 3), ("cl_mean", 3), ("cl_rms", 3),
                   ("cl_amp", 3), ("cd_amp", 3), ("strouhal", 3), ("freq", 4),
                   ("t_start", 5), ("t_end", 5))
    META_FIELDS = (("typical_inner_iterations", 4),
                   ("observed_min_inner_iterations", 4),
                   ("observed_max_inner_iterations", 4),
                   ("inner_target_converged_fraction", 4),
                   ("inner_target_misses", 8),
                   ("partition_edge_cut", 8),
                   ("num_cells_global", 8),
                   ("num_faces_global", 8))

    def camel(field):
        return "".join(part.capitalize() for part in field.split("_"))

    for field, _digits in SHED_FIELDS:
        define("Shed" + camel(field), PENDING)
    define("ShedSaturated", PENDING)
    define("ShedCycles", PENDING)
    for _case_id, key, _label in CASES:
        define("PhysTime" + key, PENDING)
    for _case_id, key, _label in CASES:
        for field, _digits in META_FIELDS:
            define(camel(field) + key, PENDING)

    for case_id, key, label in CASES:
        case_dir = RESULTS_DIR / case_id
        status = read_json(case_dir / "run_status.json") or {}
        meta = read_json(case_dir / "metadata.json") or {}
        forces = read_rows(case_dir / "forces.csv")
        last = forces[-1] if forces else {}

        # A run is usable only if it completed AND its output postdates the
        # trusted cutoff.  Stale output is reported as pending, never as a result.
        stale = is_stale(case_dir / "run_status.json")
        finished = bool(status) and not stale
        if stale and status:
            print("  SKIPPING STALE: %s (predates trusted cutoff)" % case_id)
        cd = last.get("cd")
        cl = last.get("cl")
        cd_drift = drift([r["cd"] for r in forces]) if forces else None

        define("Status" + key,
               status.get("convergence_status", PENDING).replace("_", "\\_")
               if finished else PENDING)
        define("Steps" + key, num(status.get("final_step"), 8) if finished else PENDING)
        define("PhysTime" + key,
               num(status.get("final_physical_time"), 5) if finished else PENDING)
        define("Orders" + key,
               num(status.get("residual_reduction_orders"), 3) if finished else PENDING)
        define("Wall" + key, num(status.get("wall_time_seconds"), 4) if finished else PENDING)
        define("Ranks" + key, num(status.get("mpi_ranks"), 3) if finished else PENDING)
        define("Cd" + key, num(cd, 4) if finished else PENDING)
        define("Cl" + key, num(cl, 3) if finished else PENDING)
        define("Cmz" + key, num(last.get("cmz"), 3) if finished else PENDING)
        define("Pd" + key, num(last.get("pressure_drag"), 4) if finished else PENDING)
        define("Vd" + key, num(last.get("viscous_drag"), 4) if finished else PENDING)
        define("CdDrift" + key, num(cd_drift, 2) if finished else PENDING)

        if not finished:
            filler = " & ".join([PENDING] + ["" for _ in range(5)])
            status_rows.append("%s & %s \\\\" % (label, filler))
            force_rows.append("%s & %s \\\\" % (label, filler))
            continue

        status_rows.append(" & ".join([
            label,
            num(status.get("mpi_ranks"), 3),
            num(status.get("final_step"), 8),
            num(status.get("final_physical_time"), 5),
            num(status.get("residual_reduction_orders"), 3),
            num(status.get("wall_time_seconds"), 4),
            status.get("convergence_status", "?").replace("_", "\\_"),
        ]) + " \\\\")

        # Honest stationarity note derived from the drag history itself.
        note = "--"
        if cd_drift is not None and cd is not None:
            scale = max(abs(cd), 1e-9)
            rel = abs(cd_drift) / scale
            if rel > 1e-2:
                note = "\\textbf{drifting} (%.0f\\%%)" % (100.0 * rel)
            elif rel > 1e-3:
                note = "near-stationary"
            else:
                note = "stationary"
        force_rows.append(" & ".join([
            label,
            num(cd, 4),
            num(cl, 3),
            num(last.get("cmz"), 3),
            num(last.get("pressure_drag"), 4),
            num(last.get("viscous_drag"), 4),
            note,
        ]) + " \\\\")

        if key == "CylShed":
            stats = shedding_stats(forces)
            for field, digits in SHED_FIELDS:
                define("Shed" + camel(field), num(stats.get(field), digits))
            # A frequency estimate is only meaningful if the analysis window
            # actually contains several completed shedding cycles.  Below a few
            # cycles the "frequency" is an artefact of the window length.
            freq = stats.get("freq")
            span = ((stats.get("t_end") or 0.0) - (stats.get("t_start") or 0.0))
            cycles = freq * span if (freq and span > 0.0) else 0.0
            define("ShedCycles", num(cycles, 3) if cycles else PENDING)
            define("ShedSaturated", "yes" if cycles >= 5.0 else "no")

        for field, digits in META_FIELDS:
            define(camel(field) + key, num(meta.get(field), digits))

    # ---- per-rank partition table, from the np=8 production runs ----------
    partition_rows = []
    for case_id, _key, label in CASES:
        path = RESULTS_DIR / case_id / "partition_diagnostics.csv"
        rows = read_rows(path)
        if not rows:
            continue
        owned = [r["num_cells_owned"] for r in rows]
        if not owned:
            continue
        mean = sum(owned) / len(owned)
        partition_rows.append((label, rows, mean))

    partition_body = []
    if partition_rows:
        label, rows, mean = partition_rows[0]
        for row in rows:
            partition_body.append(" & ".join([
                num(row["rank"], 3),
                num(row["num_cells_owned"], 6),
                num(row["num_cells_ghost"], 6),
                num(row["num_boundary_faces"], 6),
                num(row["num_neighbor_ranks"], 3),
                num(row["send_cells"], 6),
                num(row["recv_cells"], 6),
            ]) + " \\\\")
        define("PartitionCase", label)
        define("PartitionBalance",
               num(max(r["num_cells_owned"] for r in rows) / mean, 4) if mean else PENDING)
    else:
        partition_body.append(pending_row(7))
        define("PartitionCase", PENDING)
        define("PartitionBalance", PENDING)

    # ---- rank-count scaling study ----------------------------------------
    scaling_body = []
    scaling_dir = RESULTS_DIR / "scaling"
    entries = sorted(scaling_dir.glob("*/run_status.json")) if scaling_dir.is_dir() else []
    if entries:
        # Group by base case so each case's rank sweep reads as one block, and
        # report the deviation of each rank count from its own np=1 baseline: that
        # is the consistency result the study exists to establish.
        grouped = {}
        for entry in entries:
            name = entry.parent.name
            base, _, tail = name.rpartition("_np")
            try:
                ranks = int(tail)
            except ValueError:
                base, ranks = name, 0
            grouped.setdefault(base, []).append((ranks, entry))
        for base in sorted(grouped):
            runs = sorted(grouped[base])
            baseline = None
            for ranks, entry in runs:
                status = read_json(entry) or {}
                meta = read_json(entry.parent / "metadata.json") or {}
                forces = read_rows(entry.parent / "forces.csv")
                cd = forces[-1].get("cd") if forces else None
                if baseline is None and cd is not None:
                    baseline = cd
                if cd is not None and baseline:
                    delta = "\\num{%.1e}" % (abs(cd - baseline) / abs(baseline))
                else:
                    delta = "--"
                scaling_body.append(" & ".join([
                    base.replace("_", "\\_"),
                    num(ranks, 3),
                    num(meta.get("partition_edge_cut"), 8),
                    "%.8f" % cd if cd is not None else PENDING,
                    delta,
                    num(status.get("wall_time_seconds"), 4),
                ]) + " \\\\")
    else:
        scaling_body.append(pending_row(6))

    (REPORT_DIR / "numbers_auto.tex").write_text("\n".join(macros) + "\n")

    # Each table file is written as a COMPLETE tabular environment, not as a set
    # of bare rows.  Pulling bare rows into a surrounding tabular with \input
    # does not work reliably: the last \\ in the included file scans ahead for an
    # optional argument, expands the following \bottomrule during that scan, and
    # fails with a misplaced \noalign.  Emitting the whole environment keeps the
    # alignment entirely within one file and compiles cleanly.
    tables = (
        ("tab_runstatus.tex", "lrrrrrl", status_rows,
         ["Case", "Ranks", "Steps", "$t_{\\mathrm{phys}}$", "Orders",
          "Wall (s)", "Status"]),
        ("tab_forces.tex", "lrrrrrl", force_rows,
         ["Case", "$C_D$", "$C_L$", "$C_{m,z}$", "$C_{D,p}$", "$C_{D,v}$",
          "Stationarity"]),
        ("tab_partition.tex", "rrrrrrr", partition_body,
         ["Rank", "Owned", "Ghost", "Boundary faces", "Neighbours", "Send",
          "Recv"]),
        ("tab_scaling.tex", "lrrrrr", scaling_body,
         ["Case", "Ranks", "Edge cut", "$C_D$ at step 1500",
          "rel.\\ dev.\\ from $np{=}1$", "Wall (s)$^\\ddagger$"]),
    )
    for name, spec, body, header in tables:
        lines = ["\\begin{tabular}{%s}" % spec, "\\toprule",
                 " & ".join(header) + " \\\\", "\\midrule"]
        lines.extend(body)
        lines.extend(["\\bottomrule", "\\end{tabular}"])
        (REPORT_DIR / name).write_text("\n".join(lines) + "\n")
    print("harvested %d macro definitions" % len(macros))
    print("run-status rows: %d, force rows: %d" % (len(status_rows), len(force_rows)))


if __name__ == "__main__":
    main()
