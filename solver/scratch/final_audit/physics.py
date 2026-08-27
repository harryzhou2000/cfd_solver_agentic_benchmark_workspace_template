import math, csv, json

print("### Blasius friction drag, both sides unit chord, Re=5000")
Re = 5000.0
print("  2*1.328/sqrt(Re) =", 2 * 1.328 / math.sqrt(Re))
print("  1.328/sqrt(Re)   =", 1.328 / math.sqrt(Re))
cdv = 3.666679267163e-02
print("  measured Cdv (m015 lam) =", cdv, " ratio to 2*1.328/sqrtRe =",
      cdv / (2 * 1.328 / math.sqrt(Re)))

print("")
print("### Isentropic stagnation Cp at M=0.15, 0.8, 2.0 (gamma=1.4)")
g = 1.4
for M in (0.15, 0.8, 2.0):
    cp0 = 2.0 / (g * M * M) * ((1 + 0.5 * (g - 1) * M * M) ** (g / (g - 1)) - 1)
    print(f"  M={M}: Cp0_isentropic = {cp0:.6f}")

print("")
print("### Pitot (normal-shock total pressure) Cp at M=2.0")
M = 2.0
# Rayleigh pitot formula: p02/p1
p02_p1 = ((g + 1) ** 2 * M * M / (4 * g * M * M - 2 * (g - 1))) ** (g / (g - 1)) \
    * ((1 - g + 2 * g * M * M) / (g + 1))
cp_pitot = (p02_p1 - 1) / (0.5 * g * M * M)
print(f"  p02/p1 = {p02_p1:.6f}   Cp_pitot = {cp_pitot:.6f}")

print("")
print("### Max Cp on m200 inviscid surface.csv")
rows = list(csv.DictReader(open("/workspace/solver/results/naca0012_m200_inviscid/surface.csv")))
cps = [float(r["cp"]) for r in rows]
print(f"  max cp = {max(cps):.6f}  n_exceed_isentropic(1.8) = ?")
cp_isen = 2.0 / (g * 4.0) * ((1 + 0.2 * 4.0) ** 3.5 - 1)
print(f"  isentropic Cp0 at M=2 = {cp_isen:.6f}")
print(f"  count cp > pitot({cp_pitot:.4f}) = {sum(1 for c in cps if c > cp_pitot)}")
print(f"  count cp > isentropic({cp_isen:.4f}) = {sum(1 for c in cps if c > cp_isen)}")

print("")
print("### Cylinder Re20 CD and Re200 stats")
rows = list(csv.DictReader(open("/workspace/solver/results/cylinder_m010_laminar_re20/forces.csv")))
rows = [r for r in rows if r.get("cd") not in (None, "")]
print("  Re20 final CD =", rows[-1]["cd"])
