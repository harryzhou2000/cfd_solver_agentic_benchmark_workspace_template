#!/usr/bin/env python3
"""Independent re-implementation of the "geometric tail" convergence diagnostic.

Written from scratch for the amp_verify audit. Reads ONLY
results/<case>/forces.csv and results/<case>/run_status.json.
No report/ or other scratch/ code was consulted.

Diagnostic: take last N=500 values of the cd column, split into 10 contiguous
sub-blocks of 50, take sub-block means m[0..9], decrements d[i]=m[i+1]-m[i]
(i=0..8, nine of them), ratios |d[i+1]|/|d[i]| (i=0..7, eight of them),
r = mean of the 8 ratios.  Remaining movement is |d[8]| times an amplification
factor: either r/(1-r) or 1/(1-r) -- both are reported so the convention the
solver used can be identified.
"""
import json
import os

RESULTS = "/workspace/solver/results"
CASES = [
    "cylinder_m010_laminar_re20",
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
]
CDCOL = 3
N = 500
NBLK = 10


def load(case):
    path = os.path.join(RESULTS, case, "forces.csv")
    steps, cd = [], []
    with open(path) as fh:
        header = fh.readline().strip().split(",")
        assert header[0] == "step", header
        assert header[CDCOL] == "cd", header
        for line in fh:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            steps.append(int(parts[0]))
            cd.append(float(parts[CDCOL]))
    return steps, cd


def status(case):
    with open(os.path.join(RESULTS, case, "run_status.json")) as fh:
        return json.load(fh)


def diag(vals):
    if len(vals) < N:
        return None
    w = vals[-N:]
    bs = N // NBLK
    means = [sum(w[i * bs:(i + 1) * bs]) / bs for i in range(NBLK)]
    d = [means[i + 1] - means[i] for i in range(NBLK - 1)]
    ratios = [abs(d[i + 1]) / abs(d[i]) for i in range(len(d) - 1)]
    r = sum(ratios) / len(ratios)
    mono = all(x > 0 for x in d) or all(x < 0 for x in d)
    res = {
        "d": d, "ratios": ratios, "r": r, "monotone": mono,
        "d_last": abs(d[-1]), "end_val": w[-1], "n_series": len(vals),
    }
    if r < 1.0:
        res["rem_r"] = res["d_last"] * r / (1.0 - r)
        res["rem_1"] = res["d_last"] * 1.0 / (1.0 - r)
    else:
        res["rem_r"] = float("nan")
        res["rem_1"] = float("nan")
    return res


def windows(case):
    steps, cd = load(case)
    fs = status(case)["final_step"]
    out = {}
    out["a_raw"] = (steps, cd)
    out["b_droplast"] = (steps[:-1], cd[:-1])
    trunc = [(s, v) for s, v in zip(steps, cd) if s <= fs]
    out["c_trunc"] = ([s for s, _ in trunc], [v for _, v in trunc])
    seen, dedup = set(), []
    for s, v in trunc:
        if s in seen:
            continue
        seen.add(s)
        dedup.append((s, v))
    out["d_trunc_dedup"] = ([s for s, _ in dedup], [v for _, v in dedup])
    return out, steps, cd, fs


def sec1():
    print("=" * 100)
    print("SECTION 1 -- file structure / fallback-append signature (CLAIM 1)")
    print("=" * 100)
    print("%-32s %6s %8s %8s %20s %8s %-11s %s" % (
        "case", "rows", "maxstep", "laststep", "last_row_cd", "json_fs",
        "status", "FALLBACK?"))
    for c in CASES:
        steps, cd = load(c)
        st = status(c)
        mx = max(steps)
        fb = steps[-1] < mx
        print("%-32s %6d %8d %8d %20.12e %8d %-11s %s" % (
            c, len(steps), mx, steps[-1], cd[-1], st["final_step"],
            st["convergence_status"], "YES" if fb else "no"))
    print()
    print("tail rows (step, cd) for each case:")
    for c in CASES:
        steps, cd = load(c)
        tail = ", ".join("(%d, %.12e)" % (s, v)
                         for s, v in zip(steps[-4:], cd[-4:]))
        print("  %-32s %s" % (c, tail))
        dup = [s for i, s in enumerate(steps[:-1]) if steps[i + 1] == s]
        print("      duplicated step numbers anywhere in file: %s" % (dup or "none"))


def show(tag, r):
    if r is None:
        print("  %-16s INSUFFICIENT DATA" % tag)
        return
    print("  %-16s r=%.6f  monotone=%-5s |d8|=%.6e  rem[r/(1-r)]=%.6e  "
          "rem[1/(1-r)]=%.6e  end_cd=%.12e  %%ofcd=%.4f%%" % (
              tag, r["r"], r["monotone"], r["d_last"], r["rem_r"], r["rem_1"],
              r["end_val"], 100.0 * r["rem_r"] / abs(r["end_val"])))


def sec2():
    case = "naca0012_m080_inviscid"
    print()
    print("=" * 100)
    print("SECTION 2 -- %s under three window conventions (CLAIM 2)" % case)
    print("=" * 100)
    w, steps, cd, fs = windows(case)
    for tag in ("a_raw", "b_droplast", "c_trunc", "d_trunc_dedup"):
        s, v = w[tag]
        r = diag(v)
        print("[%s] series_len=%d  window steps %d..%d" % (
            tag, len(v), s[-N:][0], s[-1]))
        show(tag, r)
        if r:
            print("     9 decrements:")
            for i, dv in enumerate(r["d"]):
                print("        d[%d] = %+.6e" % (i, dv))
            print("     8 ratios: %s" % " ".join("%.4f" % x for x in r["ratios"]))
        print()


def sec3():
    print()
    print("=" * 100)
    print("SECTION 3 -- all 7 cases, convention (c) trunc and (d) trunc+dedup (CLAIM 3)")
    print("=" * 100)
    rows = []
    for c in CASES:
        w, steps, cd, fs = windows(c)
        print(c)
        for tag in ("c_trunc", "d_trunc_dedup"):
            s, v = w[tag]
            r = diag(v)
            show(tag, r)
            if tag == "d_trunc_dedup" and r:
                rows.append((c, r))
        print()
    print("ranking by remaining movement, r/(1-r) convention, window (d), DESC:")
    for c, r in sorted(rows, key=lambda t: -t[1]["rem_r"]):
        print("  %-32s rem=%.6e  (%.4f%% of cd=%.12e)  r=%.6f  mono=%s" % (
            c, r["rem_r"], 100.0 * r["rem_r"] / abs(r["end_val"]),
            r["end_val"], r["r"], r["monotone"]))


def sec4():
    quoted = {
        "cylinder_m010_laminar_re20": (1.03e-03, 0.736),
        "naca0012_m200_laminar_re5000": (1.45e-05, 0.919),
        "naca0012_m015_laminar_re5000": (1.25e-04, 0.839),
    }
    print()
    print("=" * 100)
    print("SECTION 4 -- which amplification factor reproduces the solver note (CLAIM 4)")
    print("=" * 100)
    for c, (qrem, qr) in quoted.items():
        print("%s  solver note: remaining=%.3e, ratio=%.3f" % (c, qrem, qr))
        w, steps, cd, fs = windows(c)
        for tag in ("a_raw", "b_droplast", "c_trunc", "d_trunc_dedup"):
            s, v = w[tag]
            r = diag(v)
            if r is None:
                continue
            print("  %-16s r=%.6f (quoted %.3f, dr=%+.4f)  |d8|=%.6e" % (
                tag, r["r"], qr, r["r"] - qr, r["d_last"]))
            print("      r/(1-r)=%.4f -> rem=%.6e   quoted/computed=%.4f" % (
                r["r"] / (1 - r["r"]), r["rem_r"], qrem / r["rem_r"]))
            print("      1/(1-r)=%.4f -> rem=%.6e   quoted/computed=%.4f" % (
                1 / (1 - r["r"]), r["rem_1"], qrem / r["rem_1"]))
        print()


if __name__ == "__main__":
    sec1()
    sec2()
    sec3()
    sec4()
