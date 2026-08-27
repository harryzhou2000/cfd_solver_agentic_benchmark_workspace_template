"""Check whether a drag history is a bounded limit cycle or a developing drift.

A span that is constant across widely different trailing-window lengths, while the
window mean stays fixed, indicates an attracting cycle rather than a transient.
"""
import csv
import statistics
import sys

rows = [r for r in csv.DictReader(open(sys.argv[1])) if r.get("cd")]
cd = [float(r["cd"]) for r in rows]
print("rows:", len(cd))
for window in (500, 2000, 5000, 10000):
    if len(cd) >= window:
        seg = cd[-window:]
        mean = sum(seg) / len(seg)
        print("window %6d  mean %.8f  span %.4e  std %.3e"
              % (window, mean, max(seg) - min(seg), statistics.pstdev(seg)))

if len(cd) >= 10000:
    seg = cd[-10000:]
    first = seg[: len(seg) // 2]
    second = seg[len(seg) // 2 :]
    m1 = sum(first) / len(first)
    m2 = sum(second) / len(second)
    print("first-half mean  %.8f" % m1)
    print("second-half mean %.8f" % m2)
    print("drift %.3e absolute, %.3e relative" % (m2 - m1, abs(m2 - m1) / abs(m2)))
