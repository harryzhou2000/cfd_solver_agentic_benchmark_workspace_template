#!/bin/bash
# Additional cross-check runs that complete the verification study:
#   * the Mach 2 case with the Roe flux, to quantify its shock instability;
#   * two shock-free cases with the shock fix disabled, to show it is inactive;
#   * a restart round trip, to show that restart_final.* reproduces the state.
set -uo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
S=studies
COMMON="--progress-every 5000"
run() { scripts/run_case.sh "$@" 2>&1 | grep -v "Authorization required" || true; }

# The Roe run cannot converge on this case (see the report), so it is capped
# well past the point where its residual has plateaued.
run naca0012_m200_inviscid 4 $S/verify/naca0012_m200_inviscid_roe $COMMON --flux roe \
    --max-steps 15000

# Shock-fix invariance: two subsonic/transonic cases with the fix switched off.
# The sensor requires locally supersonic flow and a strong pressure gradient, so
# these must reproduce the production runs; the report cites them as evidence
# that the fix is inactive outside the strong-shock region.
run cylinder_m010_laminar_re20 4 $S/verify/cylinder_m010_laminar_re20_noshockfix \
    $COMMON --shock-fix 0
run naca0012_m015_laminar_re5000 4 $S/verify/naca0012_m015_laminar_re5000_noshockfix \
    $COMMON --shock-fix 0

# Restart round trip: continue the converged Mach 0.15 case from its restart
# file for a few steps and check that the forces are unchanged.  The run itself
# is a smoke test, not a benchmark deliverable, so it lives outside results/.
mkdir -p $S/restart
run naca0012_m015_inviscid 4 $S/restart/naca0012_m015_inviscid_restart $COMMON \
    --restart results/naca0012_m015_inviscid/restart_final.bin --max-steps 20
.venv/bin/python - <<'PY'
import csv, json, sys
a = list(csv.DictReader(open("results/naca0012_m015_inviscid/forces.csv")))[-1]
b = list(csv.DictReader(open("studies/restart/naca0012_m015_inviscid_restart/forces.csv")))[0]
out = {}
ok = True
for k in ("cl", "cd", "cmz", "pressure_drag"):
    d = abs(float(a[k]) - float(b[k]))
    out[k] = dict(original=float(a[k]), after_restart=float(b[k]), abs_difference=d)
    if d > 1e-10:
        ok = False
out["passed"] = ok
out["note"] = ("Forces recomputed from restart_final.bin at step 0 of the restarted run, "
               "compared with the final row of the original run's forces.csv. The restart "
               "file is written in global cell order, so it can be reloaded on any rank count.")
json.dump(out, open("studies/restart/restart_check.json", "w"), indent=2)
print("RESTART ROUND TRIP:", "PASS" if ok else "FAIL", out)
PY
echo VERIFY_EXTRA_DONE
