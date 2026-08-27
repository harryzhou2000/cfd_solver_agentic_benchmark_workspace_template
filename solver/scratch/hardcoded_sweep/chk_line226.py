"""sec_results.tex:226 keeps a BARE '9 of 404' and a bare 47.1 % while the same
quantities two paragraphs later are macro-driven.  Verify both.
"""
import csv

PITOT = 1.6573003216492854
ISEN = 2.4373033779174153  # isentropic Cp0 at M=2.0, gamma=1.4

with open("/workspace/solver/results/naca0012_m200_inviscid/surface.csv", newline="") as fh:
    cps = [float(r["cp"]) for r in csv.DictReader(fh) if r["tag"] in ("WALL", "bc-4")]

print("wall faces                       %d   (numbers.tex \nacaWallEdges = 404)" % len(cps))
print("faces over pitot  %.4f           %d   (line 226 prints a bare 9)" % (PITOT, sum(1 for c in cps if c > PITOT)))
print("faces over isentropic %.4f       %d   (line 226 says zero)" % (ISEN, sum(1 for c in cps if c > ISEN)))
print("isentropic overstates pitot by   %.4f %%   (line 226 prints 47.1 %%)"
      % (100.0 * (ISEN / PITOT - 1.0)))
