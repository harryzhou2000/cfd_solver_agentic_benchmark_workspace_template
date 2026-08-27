import csv, json

S = "/workspace/solver/results/scaling"
for base in ["cylinder_m010_laminar_re20", "naca0012_m015_laminar_re5000"]:
    print(f"== {base}  (LAST ROW = written/reported state)")
    ref = None
    for np_ in (1, 2, 4, 8):
        d = f"{S}/{base}_np{np_}"
        rows = list(csv.DictReader(open(f"{d}/forces.csv")))
        rows = [r for r in rows if r.get("cd") not in (None, "")]
        last = rows[-1]
        cd, cl = float(last["cd"]), float(last["cl"])
        cmz = float(last["cmz"]) if last.get("cmz") not in (None, "") else None
        if ref is None:
            ref = (cd, cl, cmz)
        rcd = abs(cd - ref[0]) / abs(ref[0])
        rcl = abs(cl - ref[1]) / abs(ref[1])
        rcm = abs(cmz - ref[2]) / abs(ref[2]) if cmz is not None and ref[2] else float("nan")
        print(f"  np={np_} step={last['step']} cd={cd:.10f} cl={cl:.3e} cmz={cmz!r}")
        print(f"        relCD={rcd:.2e}  relCL={rcl:.2e}  relCMZ={rcm:.2e}")
