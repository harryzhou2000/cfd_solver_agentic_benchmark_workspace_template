import csv, json

R = "/workspace/solver/results"
CASES = ["naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid",
         "naca0012_m015_laminar_re5000", "naca0012_m080_laminar_re5000",
         "naca0012_m200_laminar_re5000", "cylinder_m010_laminar_re20",
         "cylinder_m010_laminar_re200"]

for c in CASES:
    j = json.load(open(f"{R}/{c}/metadata.json"))
    fams = j.get("run_details", {}).get("boundary_families", [])
    rows = list(csv.DictReader(open(f"{R}/{c}/surface.csv")))
    hdr = list(rows[0].keys())
    print(f"-- {c}  fams={[f['type'] for f in fams]}  nrows={len(rows)}")
    if c == CASES[0]:
        print("   surface.csv header:", hdr)
    # find velocity-ish columns
    def col(*names):
        for n in names:
            if n in hdr:
                return n
        return None
    for name in hdr:
        if any(k in name.lower() for k in ("speed", "mach", "vn", "normal_vel", "vt", "tang")):
            vals = [abs(float(r[name])) for r in rows if r[name] not in (None, "")]
            if vals:
                print(f"     max|{name}| = {max(vals):.6e}")
