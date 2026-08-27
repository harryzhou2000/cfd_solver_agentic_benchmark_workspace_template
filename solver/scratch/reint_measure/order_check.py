"""Check whether published surface.csv row order traverses the body contiguously."""
import numpy as np

ROOT = "/workspace/solver/results"


def stats(tag, mid, nrm):
    d = np.hypot(*(np.roll(mid, -1, axis=0) - mid).T)
    # closure of the normal field weighted by crude lengths
    Lc = 0.5 * (d + np.roll(d, 1))
    clos = np.einsum("i,ij->j", Lc, nrm)
    # signed area from midpoints (shoelace)
    vx, vy = mid[:, 0], mid[:, 1]
    area = 0.5 * np.sum(vx * np.roll(vy, -1) - np.roll(vx, -1) * vy)
    # local smoothness: angle turned between consecutive normals
    dot = np.clip(np.einsum("ij,ij->i", nrm, np.roll(nrm, -1, axis=0)), -1, 1)
    turn = np.degrees(np.arccos(dot))
    print("  %-14s gap max %.4e sum %.6f | signed area %+.6f | turn max %.2f deg sum %.1f | closure (%.2e,%.2e)"
          % (tag, d.max(), d.sum(), area, turn.max(), turn.sum(), clos[0], clos[1]))


for case in ["cylinder_m010_laminar_re20", "naca0012_m015_inviscid"]:
    s = np.genfromtxt(f"{ROOT}/{case}/surface.csv", delimiter=",", names=True,
                      dtype=None, encoding="utf-8")
    x, y, nx, ny = s["x"], s["y"], s["nx"], s["ny"]
    mid = np.stack([x, y], axis=1)
    nrm = np.stack([nx, ny], axis=1)
    print("=" * 100)
    print(case, len(x), "faces")
    stats("published", mid, nrm)
    cx = 0.0 if "cylinder" in case else 0.3
    o = np.argsort(np.arctan2(y, x - cx))
    stats("atan2-sorted", mid[o], nrm[o])
    if "naca" in case:
        print("  x range [%.6f, %.6f]  y range [%.6f, %.6f]" % (x.min(), x.max(), y.min(), y.max()))
        print("  first 5 (x,y):", np.round(mid[:5], 5).tolist())
        print("  last 5  (x,y):", np.round(mid[-5:], 5).tolist())
        # where are the biggest published-order jumps?
        d = np.hypot(*(np.roll(mid, -1, axis=0) - mid).T)
        j = np.argsort(d)[-5:]
        print("  largest published gaps at rows", j.tolist(), "gaps", np.round(d[j], 6).tolist())
