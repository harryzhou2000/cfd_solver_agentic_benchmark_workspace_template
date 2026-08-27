#!/bin/bash
# End-to-end submission check: runs the benchmark's own validator and then a
# set of stricter self-checks on completeness, honesty and reproducibility.
set -uo pipefail
cd "$(dirname "$0")/.."
PY=.venv/bin/python
BM=../cfd_solver_agentic_benchmark
fail=0
note() { printf '%-58s %s\n' "$1" "$2"; }

echo "=== benchmark validator ==="
$PY $BM/examiner/validate_outputs.py results/* --report report || fail=1

echo
echo "=== submission self-check ==="
$PY - <<'PY' || fail=1
import csv, glob, json, os, re, sys

ok = True
def check(name, cond, detail=""):
    global ok
    print(f"{name:<58s} {'PASS' if cond else 'FAIL'} {detail}")
    if not cond:
        ok = False

required = ["naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid",
            "naca0012_m015_laminar_re5000", "naca0012_m080_laminar_re5000",
            "naca0012_m200_laminar_re5000", "cylinder_m010_laminar_re20",
            "cylinder_m010_laminar_re200"]
have = sorted(os.path.basename(d) for d in glob.glob("results/*") if os.path.isdir(d))
check("all eight required case directories present", have == sorted(required), str(have))
check("no extra directories under results/", have == sorted(required))

for cid in required:
    d = os.path.join("results", cid)
    if not os.path.isdir(d):
        continue
    files = ["metadata.json", "run_status.json", "residuals.csv", "forces.csv",
             "surface.csv", "field_final.vtu", "restart_final.bin", "stdout.log"]
    miss = [f for f in files if not os.path.exists(os.path.join(d, f))]
    check(f"{cid}: contract files", not miss, ",".join(miss))
    if os.path.exists(os.path.join(d, "metadata.json")):
        m = json.load(open(os.path.join(d, "metadata.json")))
        s = json.load(open(os.path.join(d, "run_status.json")))
        check(f"{cid}: completed and not failed",
              m.get("completed") is True and
              s["convergence_status"] in ("converged", "statistically_periodic"),
              s["convergence_status"])
        check(f"{cid}: numerics_required satisfied",
              m.get("numerics_required_satisfied", True) is True)

# Every submitted case must come from one build of the solver: a mixed set,
# where some cases predate a numerics change, would be silently inconsistent.
builds = {}
for cid in required:
    f = os.path.join("results", cid, "metadata.json")
    if os.path.exists(f):
        m = json.load(open(f))
        builds[cid] = (m.get("solver_version"), m.get("git_revision"),
                       m.get("inviscid_flux"), m.get("shock_fix"),
                       m.get("shock_fix_strength"), m.get("reconstruction"),
                       m.get("limiter"))
check("all cases produced by the same solver build and numerics",
      len(set(builds.values())) <= 1,
      "; ".join(f"{k}={v}" for k, v in sorted(builds.items())) if len(set(builds.values())) > 1 else "")

for p in ["report/report.tex", "report/report.pdf", "report/figures",
          "report/figure_manifest.csv", "report/sanity_checks.json",
          "report/run_manifest.csv", "report/run_manifest.md",
          "report/verification.json", "report/mpi_study.csv",
          "report/shedding_analysis.json", "report/mesh_facts.json",
          "report/rank_independence.json", "report/unit_tests.log"]:
    check(f"report artefact {p}", os.path.exists(p))

if os.path.exists("report/sanity_checks.json"):
    sc = json.load(open("report/sanity_checks.json"))
    check("physics sanity gate: no failed checks", sc["num_failed_checks"] == 0,
          f"{sc['num_failed_checks']} failures")
    check("physics sanity gate: status consistent with checks",
          sc.get("all_status_consistent", False))

if os.path.exists("report/generated_macros.tex"):
    txt = open("report/generated_macros.tex").read()
    unfilled = re.findall(r"\\providecommand\{\\([A-Za-z]+)\}", txt)
    check("every report macro populated from data", not unfilled,
          ",".join(unfilled[:5]))
    bad = [l for l in txt.splitlines()
           if l.startswith("\\newcommand") and re.search(r"\\newcommand\{\\[A-Za-z]*[0-9]", l)]
    check("generated macro names are alphabetic", not bad)

for t in glob.glob("report/tables/tab_*.tex"):
    body = open(t).read()
    check(f"table {os.path.basename(t)} has data", "data not available" not in body)

if os.path.exists("report/figure_manifest.csv"):
    rows = list(csv.DictReader(open("report/figure_manifest.csv")))
    missing = [r["figure_file"] for r in rows
               if not os.path.exists(os.path.join("report/figures", r["figure_file"]))]
    check("every manifest figure exists", not missing, ",".join(missing[:3]))
    used = set(re.findall(r"\\fig(?:\[[^\]]*\])?\{([^}]+)\}",
                          open("report/report.tex").read()))
    listed = {r["figure_file"] for r in rows}
    check("every figure used by the report is in the manifest",
          used <= listed, ",".join(sorted(used - listed))[:120])

if os.path.exists("report/rank_independence.json"):
    ri = json.load(open("report/rank_independence.json"))
    bad = [k for k, v in ri.items() if not all(v.values())]
    check("rank-independent surface output", not bad, ",".join(bad[:3]))

if os.path.exists("studies/restart/restart_check.json"):
    rc = json.load(open("studies/restart/restart_check.json"))
    check("restart round trip reproduces the forces", rc.get("passed", False))

sys.exit(0 if ok else 1)
PY

echo
if [ $fail -eq 0 ]; then echo "SUBMISSION CHECK: PASS"; else echo "SUBMISSION CHECK: FAIL"; fi
exit $fail
