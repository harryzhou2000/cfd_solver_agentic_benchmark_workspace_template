import csv, statistics

P = "/workspace/solver/results/cylinder_m010_laminar_re200/forces.csv"
rows = list(csv.DictReader(open(P)))
needed = ("physical_time", "cl", "cd")
rows = [r for r in rows if all(r.get(k) not in (None, "") for k in needed)]
t = [float(r["physical_time"]) for r in rows]
cl = [float(r["cl"]) for r in rows]
cd = [float(r["cd"]) for r in rows]
print("rows", len(rows), "t range", t[0], t[-1])
print("header", list(rows[0].keys()))


def stat(t0, tend=None):
    win = [(a, b, c) for a, b, c in zip(t, cl, cd)
           if a >= t0 and (tend is None or a <= tend)]
    if len(win) < 10:
        return None
    zc = []
    for i in range(1, len(win)):
        if win[i - 1][1] * win[i][1] < 0.0:
            ta, ca = win[i - 1][0], win[i - 1][1]
            tb, cb = win[i][0], win[i][1]
            zc.append(ta + (tb - ta) * (-ca) / (cb - ca))
    if len(zc) < 3:
        return None
    periods = [zc[i + 2] - zc[i] for i in range(len(zc) - 2)]
    period = statistics.mean(periods)
    spread = (max(periods) - min(periods)) if len(periods) > 1 else 0.0
    clw = [b for _, b, _ in win]
    cdw = [c for _, _, c in win]
    return dict(St=1.0 / period, period=period, spread=spread,
                ratio=spread / period, meanCD=statistics.mean(cdw),
                amp=0.5 * (max(clw) - min(clw)),
                cycles=(win[-1][0] - zc[0]) / period, n=len(win), nz=len(zc))


print("")
print("--- FINAL RECORD (t_end=%.4f) ---" % t[-1])
for t0 in (16.8, 30, 40, 60):
    s = stat(t0)
    print("t>=%-6s St=%.4f T=%.4f spread=%.4f ratio=%.2f%% meanCD=%.4f amp=%.4f cyc=%.1f"
          % (t0, s["St"], s["period"], s["spread"], 100 * s["ratio"],
             s["meanCD"], s["amp"], s["cycles"]))

claim = {16.8: (0.1666, 6.0023, 2.2324, 1.1173),
         30: (0.1796, 5.5667, 0.1947, 1.1928),
         40: (0.1816, 5.5078, 0.0360, 1.2276),
         60: (0.1830, 5.4652, 0.0022, 1.2468)}

print("")
print("--- report claim vs final-record recompute ---")
for t0 in (16.8, 30, 40, 60):
    s = stat(t0)
    c = claim[t0]
    got = (s["St"], s["period"], s["spread"], s["meanCD"])
    rel = [abs(g - w) / abs(w) for g, w in zip(got, c)]
    print("t>=%-6s claim St/T/spr/CD = %.4f %.4f %.4f %.4f | mine = %.4f %.4f %.4f %.4f | relerr = %s"
          % (t0, c[0], c[1], c[2], c[3], got[0], got[1], got[2], got[3],
             " ".join("%.1e" % x for x in rel)))

print("")
print("--- TRUNCATION SCAN: t_end that reproduces rows 1-3 to <0.1%% ---")
hits = []
tend = 40.0
while tend <= 300.0:
    ok = True
    for t0 in (16.8, 30, 40):
        s = stat(t0, tend)
        if s is None:
            ok = False
            break
        c = claim[t0]
        for g, w in ((s["St"], c[0]), (s["period"], c[1]),
                     (s["spread"], c[2]), (s["meanCD"], c[3])):
            if abs(g - w) / abs(w) > 0.001:
                ok = False
                break
        if not ok:
            break
    if ok:
        hits.append(tend)
    tend = round(tend + 0.05, 2)
if hits:
    print("REPRODUCES for t_end in [%.2f, %.2f]  (%d sample points)"
          % (min(hits), max(hits), len(hits)))
    tm = hits[len(hits) // 2]
    print("at t_end=%.2f:" % tm)
    for t0 in (16.8, 30, 40, 60):
        s = stat(t0, tm)
        if s:
            print("  t>=%-6s St=%.4f T=%.4f spread=%.4f ratio=%.2f%% meanCD=%.4f"
                  % (t0, s["St"], s["period"], s["spread"], 100 * s["ratio"], s["meanCD"]))
else:
    print("NO t_end reproduces all three rows to 0.1%")
