#!/bin/bash
# Parallel-consistency regression test.
#
# Runs the same short steady case on 1, 2 and 4 MPI ranks and checks that
#   * the partition is complete (owned cells sum to the global cell count),
#   * the global residual and the force coefficients agree across rank counts,
#   * the halo exchange really moves data (non-zero ghost/send/recv counts).
# This covers the parts of the MPI layer that the serial doctest suite cannot.
set -uo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
CASE=${1:-$CASES/naca0012_m015_inviscid.json}
OUT=$(mktemp -d ./mpi_test_XXXXXX)
trap 'rm -rf "$OUT"' EXIT
STEPS=${STEPS:-400}

for np in 1 2 4; do
  mpirun -np $np $MPIRUN_FLAGS ./build/cfd2d solve --case "$CASE" \
      --output "$OUT/np$np" --max-steps $STEPS --progress-every 100000 \
      >/dev/null 2>&1 || { echo "FAIL: np=$np run failed"; exit 1; }
done

.venv/bin/python - "$OUT" <<'PY'
import json, sys, csv, math
root = sys.argv[1]
ref = None
ok = True
for np_ in (1, 2, 4):
    m = json.load(open(f"{root}/np{np_}/metadata.json"))
    p = json.load(open(f"{root}/np{np_}/partition_diagnostics.json"))
    r = list(csv.DictReader(open(f"{root}/np{np_}/residuals.csv")))[-1]
    f = list(csv.DictReader(open(f"{root}/np{np_}/forces.csv")))[-1]
    owned = sum(x["num_cells_owned"] for x in p["ranks"])
    ghost = sum(x["num_cells_ghost"] for x in p["ranks"])
    sent = sum(x["send_cells"] for x in p["ranks"])
    recv = sum(x["recv_cells"] for x in p["ranks"])
    if owned != m["num_cells_global"]:
        print(f"FAIL np={np_}: owned {owned} != global {m['num_cells_global']}"); ok = False
    if np_ > 1 and (ghost == 0 or sent == 0 or sent != recv):
        print(f"FAIL np={np_}: halo empty or asymmetric (ghost {ghost}, send {sent}, recv {recv})")
        ok = False
    if np_ == 1 and (ghost != 0 or sent != 0):
        print(f"FAIL np=1: unexpected halo (ghost {ghost}, send {sent})"); ok = False
    cur = (float(r["residual_l2"]), float(f["cd"]), float(f["cl"]))
    if ref is None:
        ref = cur
    else:
        # The LU-SGS sweeps are lagged across rank boundaries, so the iterate at
        # a fixed step differs slightly; require agreement to 1%.
        for name, a, b in zip(("residual", "cd", "cl"), cur, ref):
            scale = max(abs(b), 1e-3)
            if abs(a - b) / scale > 1e-2:
                print(f"FAIL np={np_}: {name} {a:.6g} vs {b:.6g} (np=1)"); ok = False
    print(f"np={np_}: owned {owned} ghost {ghost} halo {sent} "
          f"res {cur[0]:.4e} cd {cur[1]:+.6f} cl {cur[2]:+.6f}")
print("MPI CONSISTENCY: " + ("PASS" if ok else "FAIL"))
sys.exit(0 if ok else 1)
PY
