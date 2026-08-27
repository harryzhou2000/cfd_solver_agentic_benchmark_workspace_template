"""Is "more than 99.8 % of the surface" defensible under ANY reasonable measure?

Candidates: face-count fraction, arclength fraction, chordwise-extent fraction.
The claim sits in a sentence about "the remaining 395 faces, covering more than
99.8 % of the surface", so it is a statement about the 395 clean faces.
"""
import csv
import math

PITOT = 1.6573003216492854

with open("/workspace/solver/results/naca0012_m200_inviscid/surface.csv", newline="") as fh:
    rows = [r for r in csv.DictReader(fh) if r["tag"] in ("WALL", "bc-4")]
pts = [(float(r["x"]), float(r["y"]), float(r["cp"])) for r in rows]

over = {(x, y) for x, y, c in pts if c > PITOT}
n = len(pts)
nover = len(over)
print("faces total %d   over %d   clean %d" % (n, nover, n - nover))
print()
print("1) face-count fraction of clean faces      : %.4f %%" % (100.0 * (n - nover) / n))

# 2) arclength fraction: order along the section, sum segment lengths
lower = sorted([p for p in pts if p[1] <= 0], key=lambda t: -t[0])
upper = sorted([p for p in pts if p[1] > 0], key=lambda t: t[0])
chain = lower + upper
seg = []
for i in range(len(chain) - 1):
    x0, y0, _ = chain[i]
    x1, y1, _ = chain[i + 1]
    seg.append(math.hypot(x1 - x0, y1 - y0))
total_arc = sum(seg)

# attribute half of each adjacent segment to a face (midpoint rule)
face_arc = []
for i, p in enumerate(chain):
    a = seg[i - 1] / 2.0 if i > 0 else 0.0
    b = seg[i] / 2.0 if i < len(seg) else 0.0
    face_arc.append((p, a + b))
clean_arc = sum(w for p, w in face_arc if (p[0], p[1]) not in over)
print("2) arclength fraction of clean faces       : %.4f %%"
      % (100.0 * clean_arc / total_arc))

# 3) chordwise extent
xs_over = [x for x, y, c in pts if c > PITOT]
xmax = max(x for x, _, _ in pts)
print("3) chordwise extent NOT in cluster         : %.4f %%"
      % (100.0 * (xmax - (max(xs_over) - min(xs_over))) / xmax))
print()
print("report sentence: 'The remaining 395 faces, covering more than 99.8 %% of the surface'")
print("=> 395/404 is %.4f %%, which is 97.77 %%, not >99.8 %%." % (100.0 * 395 / 404))
print("   The 99.8 %% figure is the CHORDWISE complement of the 0.18 %% cluster width,")
print("   i.e. it describes extent, while '395 faces' describes count. 8 of the 9")
print("   offending faces are stacked at nearly the same x, so the two measures differ.")
