import csv

S = "/workspace/solver/results/scaling"
base = "cylinder_m010_laminar_re20"
print("Hunting for cd values 2.12601 (np=1) and 2.12701 (np=8) in cylinder scaling runs")
for np_ in (1, 2, 4, 8):
    rows = list(csv.DictReader(open(f"{S}/{base}_np{np_}/forces.csv")))
    rows = [r for r in rows if r.get("cd") not in (None, "")]
    tgt = 2.12601 if np_ == 1 else (2.12701 if np_ == 8 else None)
    best = None
    for r in rows:
        cd = float(r["cd"])
        if tgt is not None:
            d = abs(cd - tgt)
            if best is None or d < best[0]:
                best = (d, r["step"], cd)
    last = float(rows[-1]["cd"])
    r1500 = [r for r in rows if int(float(r["step"])) == 1500]
    print(f" np={np_}: last={last:.8f} step1500={float(r1500[0]['cd']):.8f} " if r1500
          else f" np={np_}: last={last:.8f} (no step-1500 row)")
    if tgt:
        print(f"        closest to {tgt}: step={best[1]} cd={best[2]:.8f} |diff|={best[0]:.2e}")

print("")
print("5sf rounding of step-1500 cd:")
for np_ in (1, 2, 4, 8):
    rows = list(csv.DictReader(open(f"{S}/{base}_np{np_}/forces.csv")))
    rows = [r for r in rows if r.get("cd") not in (None, "")]
    r1500 = [r for r in rows if int(float(r["step"])) == 1500]
    if r1500:
        v = float(r1500[0]["cd"])
        print(f"  np={np_}: {v:.8f} -> {v:.5f}")
