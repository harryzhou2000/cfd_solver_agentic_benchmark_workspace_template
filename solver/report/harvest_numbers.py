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
import re
import subprocess


def transient_status(forces_path, start=None):
    """Parse tools/transient_status.py output into a dict.

    The shedding statistics quoted in the report come from that tool rather than
    from a second implementation here, so the report and the run-time diagnostics
    cannot disagree.
    """
    tool = REPORT_DIR.parent / "tools" / "transient_status.py"
    python = REPORT_DIR.parent / ".venv" / "bin" / "python"
    if not tool.exists() or not python.exists():
        return {}
    cmd = [str(python), str(tool), "--forces", str(forces_path)]
    if start is not None:
        cmd += ["--start", str(start)]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=180).stdout
    except Exception:
        return {}
    patterns = {
        "period": r"mean period\s*=\s*([0-9.eE+-]+)",
        "spread": r"spread\s+([0-9.eE+-]+)\s+over",
        "estimates": r"over\s+(\d+)\s+estimates",
        "strouhal": r"Strouhal St\s*=\s*([0-9.eE+-]+)",
        "cycles": r"completed cycles in window\s*=\s*([0-9.eE+-]+)",
        "cl_amp": r"C_L\s+amplitude\s*=\s*\+/-([0-9.eE+-]+)",
        "cl_rms": r"rms\s+([0-9.eE+-]+)\)",
        "cd_mean": r"C_D\s+mean\s*=\s*([0-9.eE+-]+)",
        "cd_ptp": r"peak-to-peak\s+([0-9.eE+-]+)",
        "t_end": r"record: t = [0-9.eE+-]+ \.\. ([0-9.eE+-]+)",
        "t_start": r"analysis window: t >=\s*([0-9.eE+-]+)",
    }
    stats = {}
    for key, pattern in patterns.items():
        match = re.search(pattern, out)
        if match:
            stats[key] = float(match.group(1))
    stats["saturated"] = "PASS" in out
    return stats

REPORT_DIR = Path(__file__).resolve().parent
RESULTS_DIR = REPORT_DIR.parent / "results"
#: Supplied case inputs, read only for values that originate there (the
#: freestream Mach number) rather than being recorded in metadata.json.
CASES_DIR = Path("/workspace/cfd_solver_agentic_benchmark/inputs/cases")

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
     "NACA $M{=}0.15$ inv."),
    ("naca0012_m080_inviscid", "NacaTraInv",
     "NACA $M{=}0.80$ inv."),
    ("naca0012_m200_inviscid", "NacaSupInv",
     "NACA $M{=}2.0$ inv."),
    ("naca0012_m015_laminar_re5000", "NacaSubLam",
     "NACA $M{=}0.15$ lam."),
    ("naca0012_m080_laminar_re5000", "NacaTraLam",
     "NACA $M{=}0.80$ lam."),
    ("naca0012_m200_laminar_re5000", "NacaSupLam",
     "NACA $M{=}2.0$ lam."),
    ("cylinder_m010_laminar_re20", "CylSteady",
     "Cyl.\\ $Re{=}20$"),
    ("cylinder_m010_laminar_re200", "CylShed",
     "Cyl.\\ $Re{=}200$"),
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


def reintegrate(surface, area_ref):
    """Re-integrate wall drag from the published surface.csv, independently of the solver.

    The face arc lengths are not published, so they are reconstructed from the ordered
    face midpoints and normals by a polygon-closure least-squares solve: consecutive
    vertices satisfy v_{i+1} = 2 m_i - v_i, and each vertex must lie in its face's
    plane.  That reconstruction is exact rather than approximate -- on the cylinder it
    recovers the analytic chord of the 100-sided polygon to 6e-13 -- which matters,
    because an earlier version of this check used half the spacing between neighbouring
    midpoints and its second-order error swamped the quantity being measured.

    Returns the drag components computed from cp and cf alone, for comparison against
    forces.csv.  Nothing here reads a geometric quantity from the solver.
    """
    try:
        mid = [(float(r["x"]), float(r["y"])) for r in surface]
        nrm = [(float(r["nx"]), float(r["ny"])) for r in surface]
        cp = [float(r["cp"]) for r in surface]
        cf = [float(r["cf"]) for r in surface]
    except (KeyError, TypeError, ValueError):
        return None
    n = len(mid)
    if n < 8 or not area_ref:
        return None
    # Vertices as an affine function of the unknown first vertex, then a 2x2
    # least-squares solve for that vertex against the in-plane conditions.
    a = [(0.0, 0.0)]
    for i in range(1, n):
        a.append((2.0 * mid[i - 1][0] - a[i - 1][0], 2.0 * mid[i - 1][1] - a[i - 1][1]))
    ata = [[0.0, 0.0], [0.0, 0.0]]
    atb = [0.0, 0.0]
    for i in range(n):
        sgn = -1.0 if i % 2 else 1.0
        row = (sgn * nrm[i][0], sgn * nrm[i][1])
        rhs = ((mid[i][0] - a[i][0]) * nrm[i][0] + (mid[i][1] - a[i][1]) * nrm[i][1])
        for p in range(2):
            for q in range(2):
                ata[p][q] += row[p] * row[q]
            atb[p] += row[p] * rhs
    det = ata[0][0] * ata[1][1] - ata[0][1] * ata[1][0]
    if abs(det) < 1.0e-30:
        return None
    v0 = ((atb[0] * ata[1][1] - ata[0][1] * atb[1]) / det,
          (ata[0][0] * atb[1] - atb[0] * ata[1][0]) / det)
    pressure = 0.0
    viscous = 0.0
    for i in range(n):
        sgn = -1.0 if i % 2 else 1.0
        vx = a[i][0] + sgn * v0[0]
        vy = a[i][1] + sgn * v0[1]
        length = 2.0 * math.hypot(mid[i][0] - vx, mid[i][1] - vy)
        pressure += cp[i] * nrm[i][0] * length
        viscous += cf[i] * (-nrm[i][1]) * length
    return {"pressure_drag": pressure / area_ref, "viscous_drag": viscous / area_ref}


def tail_diagnostic(forces, final_step, window=500, blocks=10):
    """Geometric-tail diagnostic on the drag window ENDING at the reported state.

    Three conventions matter here and getting them wrong changes the verdict, so
    they are spelled out.  (1) Rows are restricted to steps at or below
    ``final_step``: where the best-state fallback fired, the restored row is
    appended AFTER the rows of the march that superseded it, so a naive "last 500
    rows of the file" window splices the tail of the abandoned march onto one row
    from an earlier step and measures a trajectory that was thrown away.  (2) Only
    the FIRST row for a given step is kept, matching the solver, whose note quotes
    the first of the two values written at the final step.  (3) The remaining
    movement is the last decrement times r/(1-r), not 1/(1-r); the former
    reproduces the solver's own quoted figures and the latter is 15-27 % high.

    A geometric tail is only a model for a MONOTONE approach.  Where the window is
    non-monotone the drag is oscillating about its mean, so no tail is returned:
    r > 1 there means "wrong model", not "not converged".
    """
    if not forces or final_step is None:
        return None
    seen = set()
    series = []
    for row in forces:
        try:
            step = int(float(row["step"]))
            value = float(row["cd"])
        except (KeyError, TypeError, ValueError):
            continue
        if step > int(final_step) or step in seen:
            continue
        seen.add(step)
        series.append(value)
    if len(series) < window:
        return None
    segment = series[-window:]
    size = len(segment) // blocks
    means = [sum(segment[i * size:(i + 1) * size]) / size for i in range(blocks)]
    dec = [means[i + 1] - means[i] for i in range(blocks - 1)]
    ratios = [abs(dec[i + 1]) / abs(dec[i]) for i in range(len(dec) - 1)
              if abs(dec[i]) > 0.0]
    if not ratios:
        return None
    ratio = sum(ratios) / len(ratios)
    monotone = all(d > 0 for d in dec) or all(d < 0 for d in dec)
    out = {"ratio": ratio, "monotone": monotone, "cd": series[-1],
           "last_decrement": dec[-1]}
    if monotone and ratio < 1.0:
        amp = ratio / (1.0 - ratio)
        remaining = abs(dec[-1]) * amp
        out["amplification"] = amp
        out["remaining"] = remaining
        out["remaining_pct"] = 100.0 * remaining / abs(series[-1])
    return out


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


#: Stationarity tolerances, matching src/solve/steady_driver.cpp exactly so this
#: table cannot disagree with the status the solver recorded:
#:   kCdRelTol = 1e-2, kCdAbsTol = 1e-5, kCdDriftRelTol = 1e-3.
CD_REL_TOL = 1.0e-2
CD_ABS_TOL = 1.0e-5
CD_DRIFT_REL_TOL = 1.0e-3


def stationarity(values, window=500):
    """Classify a force history using the solver's own two-branch criterion.

    The solver accepts two distinct routes to a stationary force: a small span over
    the trailing window (a fixed point), or a small drift of the window mean against
    the preceding window (a limit cycle).  Both tolerances and the absolute floor
    are taken from the solver source, because a table that classified these runs by
    a different rule than the solver used would contradict the status recorded in
    run_status.json.
    """
    if len(values) < 2 * window:
        return None
    last = values[-window:]
    prev = values[-2 * window : -window]
    span = max(last) - min(last)
    mean_last = sum(last) / len(last)
    mean_prev = sum(prev) / len(prev)
    scale = max(abs(mean_last), 1e-9)
    span_tol = max(CD_REL_TOL * max(abs(mean_last), 1.0e-4), CD_ABS_TOL)
    drift = abs(mean_last - mean_prev)
    drift_tol = max(CD_DRIFT_REL_TOL * abs(mean_last), CD_ABS_TOL)
    return {
        "span": span,
        "span_rel": span / scale,
        "span_ok": span <= span_tol,
        "mean_drift": mean_last - mean_prev,
        "mean_drift_rel": drift / scale,
        "drift_ok": drift <= drift_tol,
    }


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
    saturation_rows = []

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
    for extra in ("Period", "Spread", "Estimates", "CdPtp", "SpreadPct"):
        define("Shed" + extra, PENDING)
    for _case_id, key, _label in CASES:
        define("PhysTime" + key, PENDING)
    for _case_id, key, _label in CASES:
        define("SpanRel" + key, PENDING)
        define("MeanDriftRel" + key, PENDING)
    for _case_id, key, _label in CASES:
        for field, _digits in META_FIELDS:
            define(camel(field) + key, PENDING)

    for case_id, key, label in CASES:
        case_dir = RESULTS_DIR / case_id
        status = read_json(case_dir / "run_status.json") or {}
        meta = read_json(case_dir / "metadata.json") or {}
        forces = read_rows(case_dir / "forces.csv")
        surface = read_rows(case_dir / "surface.csv")
        last = forces[-1] if forces else {}

        # A run is usable only if it completed AND its output postdates the
        # trusted cutoff.  Stale output is reported as pending, never as a result.
        stale = is_stale(case_dir / "run_status.json")
        finished = bool(status) and not stale

        # Stagnation-point diagnostic, harvested rather than hand-copied so it cannot
        # go stale when a case is re-run.  For a COMPRESSIBLE flow the stagnation
        # reference is not Cp = 1: the isentropic stagnation pressure coefficient is
        #   Cp0 = (p0/p_inf - 1) / (gamma/2 M^2),  p0/p = (1 + (g-1)/2 M^2)^(g/(g-1))
        # which exceeds 1 and tends to 1 as M -> 0.  Comparing a wall value against 1
        # rather than against Cp0 inverts the SIGN of the discrepancy, which is how a
        # stale figure in this report once acquired a backwards physical explanation.
        if surface and finished:
            # The freestream Mach number is an input and lives in the supplied case
            # file; it is not recorded in metadata.json.
            mach_inf = None
            case_json = read_json(CASES_DIR / (case_id + ".json"))
            if case_json:
                mach_inf = (case_json.get("freestream") or {}).get("mach")
            try:
                nose = min(surface, key=lambda r: float(r["x"]))
                nose_cp = float(nose["cp"])
            except (KeyError, TypeError, ValueError):
                nose_cp = None
            if nose_cp is not None:
                define("NoseCp" + key, num(nose_cp, 7))
                if isinstance(mach_inf, (int, float)) and mach_inf > 0:
                    g = 1.4
                    m2 = float(mach_inf) ** 2
                    cp0 = ((1.0 + 0.5 * (g - 1.0) * m2) ** (g / (g - 1.0)) - 1.0) / (0.5 * g * m2)
                    define("StagCp" + key, num(cp0, 7))
                    define("NoseCpDeficitPct" + key,
                           num(100.0 * (cp0 - nose_cp) / cp0, 3))

        # Tail-estimate figures parsed from the solver's OWN termination note, so the
        # report cannot disagree with the note it attributes them to.  Re-deriving them
        # here would create a second authority for one quantity; four stale hand-written
        # values during preparation are the argument for parsing rather than retyping.
        if finished:
            note_text = str(status.get("notes", ""))
            m = re.search(r"decaying at a ratio of ([0-9.]+) per sub-block", note_text)
            if m:
                define("DecayRatio" + key, num(float(m.group(1)), 4))
                r_ = float(m.group(1))
                if 0.0 < r_ < 1.0:
                    define("DecayAmp" + key, num(r_ / (1.0 - r_), 3))
            m = re.search(r"estimated ([0-9.eE+-]+) of C_D movement remaining \(([0-9.]+)",
                          note_text)
            if m:
                define("TailRemaining" + key, "\\num{%s}" % m.group(1))
                define("TailRemainingPct" + key, num(float(m.group(2)), 3))
            m = re.search(r"asymptote near ([0-9]+\.[0-9]+)", note_text)
            if m:
                define("TailAsymptote" + key, num(float(m.group(1)), 7))

            # The same diagnostic recomputed from forces.csv alone, for every case
            # rather than only the ones whose note happens to quote it.  This is the
            # discriminator the convergence argument turns on, and a hand-written
            # version of it inverted a conclusion once already, so the whole ladder
            # is machine-extracted.  Independent of the note-parsing above, which
            # makes the two a cross-check rather than one authority copied twice.
            td = tail_diagnostic(forces, status.get("final_step"))
            if td:
                define("MeasRatio" + key, num(td["ratio"], 4))
                define("MeasMonotone" + key,
                       "monotone" if td["monotone"] else "non-monotone")
                if "amplification" in td:
                    define("MeasAmp" + key, num(td["amplification"], 3))
                    define("MeasRemaining" + key, num(td["remaining"], 3))
                    define("MeasRemainingPct" + key, num(td["remaining_pct"], 3))

            # Independent re-integration of the wall forces from surface.csv, compared
            # against forces.csv.  Harvested rather than hand-tabulated because the
            # previous hand-written version of this table was measured on a superseded
            # run AND with a cruder arc-length estimate, which together understated the
            # agreement by nine orders of magnitude.
            area_ref = 1.0
            case_json_r = read_json(CASES_DIR / (case_id + ".json")) or {}
            ref_block = case_json_r.get("reference") or {}
            if isinstance(ref_block.get("area"), (int, float)):
                area_ref = float(ref_block["area"])
            ri = reintegrate(surface, area_ref) if surface else None
            if ri and last:
                for field, mac in (("pressure_drag", "ReintPd"),
                                   ("viscous_drag", "ReintVd")):
                    got = ri[field]
                    ref = last.get(field)
                    if not isinstance(ref, (int, float)):
                        continue
                    define(mac + key, num(got, 10))
                    define(mac + "Ref" + key, num(ref, 10))
                    define(mac + "Abs" + key, num(abs(got - ref), 3))
                    if abs(ref) > 0.0:
                        define(mac + "Rel" + key, num(abs(got - ref) / abs(ref), 3))

            # Pointwise wall diagnostics.  These are harvested rather than written by
            # hand because a superseded hand-written value survived into prose four
            # times during preparation, twice inverting the conclusion it supported.
            # For a supersonic case the attainable ceiling is the PITOT value behind a
            # normal shock, not the isentropic stagnation value, which is 47 % higher at
            # M = 2 and would mask a real bound violation.
            try:
                cps = [(float(r["x"]), float(r["y"]), float(r["cp"])) for r in surface]
            except (KeyError, TypeError, ValueError):
                cps = []
            if cps:
                define("MaxWallCp" + key, num(max(c for _, _, c in cps), 7))
                # Upper/lower asymmetry on an x-matched pair, exact for a symmetric body.
                up = sorted((x, c) for x, y, c in cps if y > 0.0)
                lo_ = sorted((x, c) for x, y, c in cps if y < 0.0)
                worst_pair = 0.0
                for xu, cu in up:
                    if not lo_:
                        break
                    xl, cl_ = min(lo_, key=lambda t: abs(t[0] - xu))
                    if abs(xl - xu) < 1.0e-9:
                        worst_pair = max(worst_pair, abs(cu - cl_))
                if worst_pair > 0.0:
                    define("WorstPairAsym" + key, num(worst_pair, 6))
            if cps and isinstance(mach_inf, (int, float)) and float(mach_inf) > 1.0:
                g = 1.4
                m2 = float(mach_inf) ** 2
                a = ((g + 1.0) ** 2 * m2 / (4.0 * g * m2 - 2.0 * (g - 1.0))) ** (g / (g - 1.0))
                b = (1.0 - g + 2.0 * g * m2) / (g + 1.0)
                pitot = (a * b - 1.0) / (0.5 * g * m2)
                over = [(x, y, c) for x, y, c in cps if c > pitot]
                define("PitotCp" + key, num(pitot, 7))
                define("FacesOverPitot" + key, num(len(over), 4))
                define("NumWallFaces" + key, num(len(cps), 5))
                if over:
                    define("OverPitotXMin" + key, num(min(x for x, _, _ in over), 6))
                    define("OverPitotXMax" + key, num(max(x for x, _, _ in over), 6))
                    define("OverPitotUpper" + key,
                           num(sum(1 for _, y, _ in over if y > 0.0), 3))
                # Faces satisfying the bound, and the margin by which the isentropic
                # reference would overstate the post-shock ceiling.  Both were hand
                # typed and both are measurements of a run, so both are harvested:
                # the count moves if the case is re-run, and quoting the isentropic
                # value as the ceiling would hide the violation entirely.
                define("FacesUnderPitot" + key, num(len(cps) - len(over), 5))
                cp0 = ((1.0 + 0.5 * (g - 1.0) * m2) ** (g / (g - 1.0)) - 1.0) / (0.5 * g * m2)
                define("IsenOverstatePct" + key, num(100.0 * (cp0 - pitot) / pitot, 3))
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
            # Fill the remaining cells with an em dash rather than leaving them
            # blank.  A row of empty cells reads as missing data; an explicit dash
            # beside the pending marker reads as "not yet measured", which is what
            # is meant.  A spanning multicolumn cannot be used: these row files are
            # pulled in with \input from inside a tabular, and TeX rejects
            # \multicolumn as the first token of an input file in that position.
            filler = " & ".join([PENDING] + ["---" for _ in range(5)])
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

        # Stationarity note derived from the drag history using the same two
        # branches the solver's own criterion uses, so a converged limit cycle is
        # not mislabelled as a drifting run.
        note = "--"
        st = stationarity([r["cd"] for r in forces]) if forces else None
        # The solver's own verdict is authoritative: it is what gated the run, and
        # it was evaluated on the exact trailing window the run terminated on.
        # Re-deriving a verdict here from the final CSV cannot reproduce that window
        # exactly (the driver appends extra final-state rows after the test), so a
        # locally computed classification can disagree marginally with the recorded
        # status.  The note text is therefore parsed for the branch the solver took,
        # and the local computation is used only as a fallback.
        notes = str(status.get("notes", ""))
        if key == "CylShed":
            # The transient is periodic BY DESIGN, so the steady stationarity tests
            # do not apply to it: a vortex street has no fixed point and its drag is
            # supposed to oscillate.  Classifying it with the steady drift test
            # labels a correct result "drifting", which is wrong and misleading.
            # The meaningful statement is the solver's own transient verdict plus the
            # measured periodicity, reported in the shedding table.
            note = "periodic, see \\cref{tab:saturation}"
        elif "bounded oscillation" in notes:
            note = "limit cycle"
            if st is not None:
                # A peak-to-peak expressed as a percentage of a near-zero mean is
                # meaningless (it can exceed 100 %), so for those cases the
                # amplitude is quoted in absolute terms instead.
                if st["span_rel"] <= 0.5:
                    note = "cycle, %.1f\\%% p--p" % (100.0 * st["span_rel"])
                else:
                    note = "cycle, p--p \\num{%.0e}" % st["span"]
        elif "asymptotic approach" in notes:
            # The solver distinguishes a fixed point already reached from a monotone
            # asymptotic approach whose remaining movement it estimates from the
            # decay of the sub-block decrements.  Those are different claims and the
            # table must not collapse them: quoting "fixed point" for a window that
            # is still monotone would assert more than the run established.  The
            # remaining movement is the honest precision of the coefficient, so it
            # is what the table reports.
            note = "asymptotic"
            m = re.search(r"estimated ([0-9.eE+-]+) of C_D movement remaining", notes)
            if m:
                note = "asymptotic, $\\pm$\\num{%s}" % m.group(1)
        elif "stationary to" in notes:
            note = "fixed point"
        elif "still moving" in notes or "not_converged" in notes:
            note = "\\textbf{not stationary}"
        elif st is not None:
            if st["span_ok"]:
                note = "fixed point"
            elif st["drift_ok"]:
                note = "limit cycle (%.1f\\%% p--p)" % (100.0 * st["span_rel"])
            else:
                note = "\\textbf{drifting} (%.1f\\%%)" % (100.0 * st["mean_drift_rel"])
            define("SpanRel" + key, num(100.0 * st["span_rel"], 3))
            define("MeanDriftRel" + key, num(st["mean_drift_rel"], 3))
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
            # Shedding statistics come from tools/transient_status.py so the report
            # and the run-time diagnostics cannot disagree; the local
            # shedding_stats() is kept only for the mean/RMS force values.
            stats = shedding_stats(forces)
            for field, digits in SHED_FIELDS:
                define("Shed" + camel(field), num(stats.get(field), digits))
            # The analysis window is stated in the prose as the second half of the
            # record, so it is passed explicitly rather than left to the tool's own
            # default of the last 60 %.  Deriving it from the recorded physical time
            # keeps the two in step if the record length ever changes; hard-coding the
            # start time would be a hand-written number that could silently drift from
            # the sentence describing it.
            half_time = None
            if isinstance(status.get("final_physical_time"), (int, float)):
                half_time = 0.5 * float(status["final_physical_time"])
            tool = transient_status(case_dir / "forces.csv", start=half_time)
            if tool:
                define("ShedStrouhal", num(tool.get("strouhal"), 4))
                define("ShedPeriod", num(tool.get("period"), 5))
                define("ShedSpread", num(tool.get("spread"), 3))
                define("ShedEstimates", num(tool.get("estimates"), 4))
                define("ShedCycles", num(tool.get("cycles"), 3))
                define("ShedClAmp", num(tool.get("cl_amp"), 4))
                define("ShedClRms", num(tool.get("cl_rms"), 4))
                define("ShedCdMean", num(tool.get("cd_mean"), 5))
                define("ShedCdPtp", num(tool.get("cd_ptp"), 4))
                define("ShedTStart", num(tool.get("t_start"), 5))
                define("ShedTEnd", num(tool.get("t_end"), 5))
                define("ShedSaturated", "PASS" if tool.get("saturated") else "FAIL")
                # Saturation-evidence table: the same measurement at a sequence of
                # window starts, emitted as complete rows.  These were hand-typed
                # from a mid-run snapshot at t=54.8 and three of the four rows went
                # stale when the run completed to t=300, so the whole table is now
                # regenerated from the final record.  The argument rests on the
                # collapse of the period spread, which is why the spread and its
                # percentage are both emitted.
                sat_rows = []
                for start in (16.8, 30.0, 40.0, 60.0):
                    w = transient_status(case_dir / "forces.csv", start=start)
                    if not w:
                        continue
                    per = w.get("period")
                    spr = w.get("spread")
                    pct = (100.0 * spr / per) if (per and spr is not None) else None
                    sat_rows.append(" & ".join([
                        "$t\\ge %g$" % start,
                        num(w.get("strouhal"), 4),
                        num(per, 5),
                        num(spr, 3),
                        (num(pct, 2) + "\\,\\%") if pct is not None else "---",
                        num(w.get("cd_mean"), 5),
                    ]) + " \\\\")
                if sat_rows:
                    saturation_rows.extend(sat_rows)
                # Relative spread of the individual period estimates: the sharpest
                # available indicator that the signal is a genuine limit cycle
                # rather than a signal still passing through linear growth.
                period = tool.get("period")
                spread = tool.get("spread")
                if period and spread is not None:
                    define("ShedSpreadPct", num(100.0 * spread / period, 3))

        for field, digits in META_FIELDS:
            define(camel(field) + key, num(meta.get(field), digits))

    # ---- per-rank partition table ----------------------------------------
    #
    # Prefer the highest rank count available, because that is where load
    # imbalance and the neighbour structure actually show: a 2-way split of a
    # balanced mesh is trivially perfect and demonstrates nothing.  The rank-count
    # study writes np=1/2/4/8 into results/scaling/<case>_np<N>/, so those are
    # searched first and the production directories used only as a fallback.
    partition_rows = []
    scaling_dirs = sorted(
        (p for p in (RESULTS_DIR / "scaling").glob("*_np*")
         if p.is_dir() and p.name.rsplit("_np", 1)[-1].isdigit()),
        key=lambda p: int(p.name.rsplit("_np", 1)[-1]),
        reverse=True,
    )
    for path_dir in scaling_dirs:
        rows = read_rows(path_dir / "partition_diagnostics.csv")
        if not rows or len(rows) < 4:
            continue
        case_stem = path_dir.name.rsplit("_np", 1)[0]
        nranks = path_dir.name.rsplit("_np", 1)[-1]
        label = next((lb for cid, _k, lb in CASES if cid == case_stem), case_stem)
        owned = [r["num_cells_owned"] for r in rows]
        mean = sum(owned) / len(owned)
        partition_rows.append(("%s, $np=%s$" % (label, nranks), rows, mean))
        break
    if not partition_rows:
        for case_id, _key, label in CASES:
            rows = read_rows(RESULTS_DIR / case_id / "partition_diagnostics.csv")
            if not rows:
                continue
            owned = [r["num_cells_owned"] for r in rows]
            if not owned:
                continue
            mean = sum(owned) / len(owned)
            partition_rows.append((label, rows, sum(owned) / len(owned)))
            break

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
            base_all = {}
            dev = {"cd": 0.0, "cl": 0.0, "cmz": 0.0}
            worst_cd_rel = 0.0
            worst_cd_ranks = None
            cds = []
            cuts = []
            for ranks, entry in runs:
                status = read_json(entry) or {}
                meta = read_json(entry.parent / "metadata.json") or {}
                forces = read_rows(entry.parent / "forces.csv")
                cd = forces[-1].get("cd") if forces else None
                if baseline is None and cd is not None:
                    baseline = cd
                # Rank-consistency spread, harvested for all three coefficients.
                # C_L and C_mz are near zero by symmetry on these cases, so their
                # RELATIVE deviations are large (up to 0.4) for a reason that has
                # nothing to do with the halo exchange; the meaningful measure for
                # them is the absolute deviation normalised by the drag, which is
                # the one O(1) coefficient available.  Quoting a single relative
                # bound for "force coefficients" is false for two of the three.
                if forces:
                    for field in dev:
                        val = forces[-1].get(field)
                        if not isinstance(val, (int, float)):
                            continue
                        if field not in base_all:
                            base_all[field] = val
                            continue
                        dev[field] = max(dev[field], abs(val - base_all[field]))
                if cd is not None and baseline and abs(cd - baseline) / abs(baseline) > worst_cd_rel:
                    worst_cd_rel = abs(cd - baseline) / abs(baseline)
                    worst_cd_ranks = ranks
                if cd is not None:
                    cds.append(cd)
                cut = meta.get("partition_edge_cut")
                if isinstance(cut, (int, float)):
                    cuts.append(int(cut))
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
            skey = "CylScal" if base.startswith("cylinder") else "NacaScal"
            if baseline:
                define("ScalCdWorstRel" + skey, num(worst_cd_rel, 3))
                if worst_cd_ranks is not None:
                    define("ScalCdWorstRanks" + skey, num(worst_cd_ranks, 3))
                for field, mac in (("cd", "Cd"), ("cl", "Cl"), ("cmz", "Cmz")):
                    define("ScalAbsDev" + mac + skey, num(dev[field], 3))
                    define("ScalDevOverCd" + mac + skey,
                           num(dev[field] / abs(baseline), 3))
                    if base_all.get(field):
                        define("ScalRelDev" + mac + skey,
                               num(dev[field] / abs(base_all[field]), 3))
            if cds:
                define("ScalCdMin" + skey, num(min(cds), 9))
                define("ScalCdMax" + skey, num(max(cds), 9))
            if cuts:
                define("ScalCutMax" + skey, num(max(cuts), 6))
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
        ("tab_saturation.tex", "lrrrrr", saturation_rows or [pending_row(6)],
         ["Window", "$St$", "Period", "Spread", "Spread/period", "Mean $C_D$"]),
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
