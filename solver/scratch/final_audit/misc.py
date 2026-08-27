import csv, math, json

R = "/workspace/solver/results"
print("### m080_inviscid residual near steps 2545-2548")
rows = list(csv.DictReader(open(f"{R}/naca0012_m080_inviscid/residuals.csv")))
for r in rows:
    s = int(float(r["step"]))
    if 2544 <= s <= 2549:
        print("   step", s, "res", r["residual_l2"])
print("   last row:", rows[-1]["step"], rows[-1]["residual_l2"])

print("")
print("### shortfall arithmetic")
print("   4.00 - 3.9158 =", 4.00 - 3.9157621899)
print("   3.00 - 2.8964 =", 3.00 - 2.896400890542)

print("")
print("### m080_inv tail stats after best step 2546")
vals = [(int(float(r["step"])), float(r["residual_l2"])) for r in rows]
tail = [v for s, v in vals if s > 2546]
print(f"   n_tail={len(tail)} mean={sum(tail)/len(tail):.4e} max={max(tail):.4e} min={min(tail):.4e}")
print("   steps after best:", max(s for s, _ in vals) - 2546)

print("")
print("### m200_inv tail after best 4854")
rows2 = list(csv.DictReader(open(f"{R}/naca0012_m200_inviscid/residuals.csv")))
vals2 = [(int(float(r["step"])), float(r["residual_l2"])) for r in rows2]
tail2 = [v for s, v in vals2 if s > 4854]
print(f"   n_tail={len(tail2)} mean={sum(tail2)/len(tail2):.4e} max={max(tail2):.4e}")

print("")
print("### Re200 residual orders: which norm")
rows3 = list(csv.DictReader(open(f"{R}/cylinder_m010_laminar_re200/residuals.csv")))
print("   header:", list(rows3[0].keys()))
for k in rows3[0].keys():
    if "res" in k.lower():
        v0 = float(rows3[0][k]); vl = float(rows3[-1][k])
        try:
            print(f"   {k}: init={v0:.6e} last={vl:.6e} orders={math.log10(v0/vl):.4f}")
        except Exception as e:
            print(f"   {k}: {e}")
