import csv, json, os, statistics

R = "/workspace/solver/results"
CASES = ["naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid",
         "naca0012_m015_laminar_re5000", "naca0012_m080_laminar_re5000",
         "naca0012_m200_laminar_re5000", "cylinder_m010_laminar_re20",
         "cylinder_m010_laminar_re200"]

print("############ FORCES: last row vs max-step row ############")
for c in CASES:
    rows = list(csv.DictReader(open(f"{R}/{c}/forces.csv")))
    rows = [r for r in rows if r.get("cd") not in (None, "")]
    last = rows[-1]
    mx = max(rows, key=lambda r: int(float(r["step"])))
    print(f"-- {c}")
    print("   LAST  step=%s cd=%s cl=%s cmz=%s pd=%s vd=%s" % (
        last["step"], last["cd"], last["cl"], last.get("cmz"),
        last.get("pressure_drag"), last.get("viscous_drag")))
    print("   MAXST step=%s cd=%s cl=%s" % (mx["step"], mx["cd"], mx["cl"]))
    print("   nrows=%d  last!=max: %s" % (len(rows), last["step"] != mx["step"]))

print("")
print("############ RUN_STATUS.JSON ############")
for c in CASES:
    p = f"{R}/{c}/run_status.json"
    j = json.load(open(p))
    print(f"-- {c}: {json.dumps(j, sort_keys=True)[:600]}")

print("")
print("############ METADATA.JSON (keys of interest) ############")
for c in CASES:
    j = json.load(open(f"{R}/{c}/metadata.json"))
    keep = {k: v for k, v in j.items() if not isinstance(v, (list, dict))}
    print(f"-- {c}: {json.dumps(keep, sort_keys=True)[:900]}")
    for k, v in j.items():
        if isinstance(v, dict):
            print(f"     [{k}] {json.dumps(v, sort_keys=True)[:500]}")
