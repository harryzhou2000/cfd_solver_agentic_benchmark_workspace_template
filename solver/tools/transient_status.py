#!/usr/bin/env python3
"""Report the shedding state of the Re 200 transient from its force history.

Measures, rather than asserts: the Strouhal number comes from zero crossings of
the lift signal over a trailing analysis window, the amplitude from that same
window, and the number of completed cycles is reported so that a frequency
estimated from too few cycles can be recognised as meaningless.
"""
import argparse
import csv
import statistics


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--forces", default="results/cylinder_m010_laminar_re200/forces.csv")
    ap.add_argument("--start", type=float, default=None,
                    help="physical time to start the analysis window (default: last 60%% of record)")
    args = ap.parse_args()

    with open(args.forces, newline="") as fh:
        rows = list(csv.DictReader(fh))
    # The file may be read while the solver is mid-write, leaving a short final
    # row with missing fields; drop any row that is not fully populated.
    needed = ("physical_time", "cl", "cd")
    rows = [r for r in rows if all(r.get(k) not in (None, "") for k in needed)]
    if len(rows) < 10:
        print("too few rows")
        return 1

    t = [float(r["physical_time"]) for r in rows]
    cl = [float(r["cl"]) for r in rows]
    cd = [float(r["cd"]) for r in rows]

    t_end = t[-1]
    t0 = args.start if args.start is not None else 0.4 * t_end
    win = [(a, b, c) for a, b, c in zip(t, cl, cd) if a >= t0]
    if len(win) < 10:
        print("analysis window too short")
        return 1

    # Zero crossings of lift, linearly interpolated.
    zc = []
    for i in range(1, len(win)):
        if win[i - 1][1] * win[i][1] < 0.0:
            ta, ca = win[i - 1][0], win[i - 1][1]
            tb, cb = win[i][0], win[i][1]
            zc.append(ta + (tb - ta) * (-ca) / (cb - ca))

    print(f"record: t = 0 .. {t_end:.2f} ({len(rows)} force rows)")
    print(f"analysis window: t >= {t0:.2f} ({len(win)} rows)")
    print(f"lift zero crossings in window: {len(zc)}")

    if len(zc) >= 3:
        # A full period spans two zero crossings of the same sense.
        periods = [zc[i + 2] - zc[i] for i in range(len(zc) - 2)]
        period = statistics.mean(periods)
        spread = (max(periods) - min(periods)) if len(periods) > 1 else 0.0
        strouhal = 1.0 / period
        cycles = (win[-1][0] - zc[0]) / period
        print(f"mean period    = {period:.4f} (spread {spread:.4f} over {len(periods)} estimates)")
        print(f"Strouhal St    = {strouhal:.4f}   [f*D/U with D = U = 1]")
        print(f"completed cycles in window = {cycles:.1f}")
        print(f"saturation gate (>= 5 cycles): {'PASS' if cycles >= 5.0 else 'FAIL'}")
    else:
        print("not enough zero crossings to estimate a frequency")

    cl_w = [b for _, b, _ in win]
    cd_w = [c for _, _, c in win]
    print(f"C_L  amplitude = +/-{0.5 * (max(cl_w) - min(cl_w)):.4f}  (rms {statistics.pstdev(cl_w):.4f})")
    print(f"C_D  mean      = {statistics.mean(cd_w):.4f}  "
          f"peak-to-peak {max(cd_w) - min(cd_w):.4f}")
    print("literature for Re 200: St ~ 0.19-0.20, mean C_D ~ 1.3-1.4")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
