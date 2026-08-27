import re, os

R = "/workspace/solver/results"
CASES = ["naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid",
         "naca0012_m015_laminar_re5000", "naca0012_m080_laminar_re5000",
         "naca0012_m200_laminar_re5000", "cylinder_m010_laminar_re20",
         "cylinder_m010_laminar_re200"]
pat = re.compile(
    r"face-closure error ([0-9.e+-]+), volume-closure error ([0-9.e+-]+), total area "
    r"([0-9.e+-]+) \(boundary integral ([0-9.e+-]+), mismatch ([0-9.e+-]+)\), "
    r"linear-gradient error ([0-9.e+-]+), conservation defect ([0-9.e+-]+), "
    r"uniform-flow residual ([0-9.e+-]+)")

hdr = ["case", "faceclos", "volclos", "areamis", "lsqgrad", "consdef", "unifres"]
print(" | ".join(f"{h:>26}" if h != "case" else f"{h:<30}" for h in hdr))
rowsout = []
for c in CASES:
    txt = open(f"{R}/{c}/stdout.log", errors="replace").read()
    m = pat.search(txt)
    if not m:
        print(f"{c:<30} NO MATCH")
        continue
    fc, vc, ta, bi, am, lg, cd, ur = m.groups()
    rowsout.append((c, fc, vc, am, lg, cd, ur))
    print(f"{c:<30} | {fc:>26} | {vc:>26} | {am:>26} | {lg:>26} | {cd:>26} | {ur:>26}")

print("")
print("### numbers.tex hand-maintained claims vs observed set")
claims = {
    "vFaceClosure 1.0e-16": [r[1] for r in rowsout],
    "vVolumeClosure 1.1e-10": [r[2] for r in rowsout],
    "vAreaLineIntegral 1.4e-14": [r[3] for r in rowsout],
    "vLsqExactness 3.1e-13": [r[4] for r in rowsout],
    "vConservation 9.2e-17": [r[5] for r in rowsout],
}
for k, v in claims.items():
    print(f"  {k:<28} observed distinct: {sorted(set(v))}")

print("")
print("### does 9.2e-17 or 1.4e-14 appear in ANY production stdout.log?")
for needle in ("9.2", "9.19", "9.20", "1.4e-14", "1.40e-14"):
    hit = []
    for c in CASES:
        txt = open(f"{R}/{c}/stdout.log", errors="replace").read()
        for line in txt.splitlines():
            if "mesh verification:" in line and needle in line:
                hit.append(c)
    print(f"  '{needle}' in a mesh-verification line: {hit}")
