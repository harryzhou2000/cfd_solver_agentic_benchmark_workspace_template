#!/bin/bash
# Parallel-consistency regression test.
#
# Runs the same short steady case on 1, 2 and 4 MPI ranks and checks that
#   * the partition is complete (owned cells sum to the global cell count),
#   * the global residual and the force coefficients agree across rank counts,
#   * the halo exchange really moves data (non-zero ghost/send/recv counts),
#   * field_final.vtu is rank-independent: identical point and connectivity
#     blocks, cells in global id order, and no non-finite value anywhere.
# This covers the parts of the MPI layer that the serial doctest suite cannot.
set -uo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
CASE=${1:-$CASES/naca0012_m015_inviscid.json}
OUT=$(mktemp -d ./mpi_test_XXXXXX)
trap 'rm -rf "$OUT"' EXIT
STEPS=${STEPS:-400}

# The runs are deliberately cut off at a few hundred steps, long before the
# case converges, because this test compares rank counts and not convergence.
# `cfd2d solve` reports that with exit code 2 (ran to the step cap without
# meeting the criterion), which is expected here; anything else is a real
# failure.
for np in 1 2 4; do
  mpirun -np $np $MPIRUN_FLAGS ./build/cfd2d solve --case "$CASE" \
      --output "$OUT/np$np" --max-steps $STEPS --progress-every 100000 \
      >/dev/null 2>&1
  st=$?
  if [ $st -ne 0 ] && [ $st -ne 2 ]; then
    echo "FAIL: np=$np run exited $st"; exit 1
  fi
  if [ ! -f "$OUT/np$np/field_final.vtu" ]; then
    echo "FAIL: np=$np produced no field_final.vtu"; exit 1
  fi
done

.venv/bin/python - "$OUT" <<'PY'
import json, os, sys, csv, math
sys.path.insert(0, "tools")
import numpy as np
from vtu_reader import read_vtu
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
        # The LU-SGS sweeps are lagged across rank boundaries, so a *fixed step*
        # of an unconverged march is not the same iterate at every rank count.
        # The residual, a global norm, is insensitive to that and is held to
        # 1%; the forces, which are surface integrals of a still-moving
        # solution, are held to 5%.  Agreement of the *converged* answers is a
        # much tighter 1e-5 and is measured by the rank study in studies/mpi/,
        # not here.
        for name, a, b, tol in zip(("residual", "cd", "cl"), cur, ref, (1e-2, 5e-2, 5e-2)):
            scale = max(abs(b), 1e-3)
            if abs(a - b) / scale > tol:
                print(f"FAIL np={np_}: {name} {a:.6g} vs {b:.6g} (np=1), "
                      f"relative {abs(a - b) / scale:.2e} > {tol:g}"); ok = False
    print(f"np={np_}: owned {owned} ghost {ghost} halo {sent} "
          f"res {cur[0]:.4e} cd {cur[1]:+.6f} cl {cur[2]:+.6f}")
# The field file must be reproducible across rank counts: same points, same
# connectivity, cells in global id order, and no uninitialised values.  A bug in
# the rank-0 gather is invisible in the CSV outputs and shows up only here.
meshes = {np_: read_vtu(f"{root}/np{np_}/field_final.vtu") for np_ in (1, 2, 4)}
base = meshes[1]
for np_, mesh in meshes.items():
    gid = mesh.cell_data["CellGlobalId"]
    if not np.all(np.diff(gid) > 0):
        print(f"FAIL np={np_}: field_final.vtu cells are not in global id order"); ok = False
    for name, arr in mesh.cell_data.items():
        if not np.all(np.isfinite(arr)):
            print(f"FAIL np={np_}: field '{name}' has non-finite values"); ok = False
    nodal = mesh.cell_to_point(mesh.cell_data["Mach"])
    if not np.all(np.isfinite(nodal)):
        print(f"FAIL np={np_}: {int((~np.isfinite(nodal)).sum())} mesh points carry no cell "
              f"(stale or duplicated points in the gathered point list)"); ok = False
    if np_ != 1:
        if len(mesh.points) != len(base.points) or not np.array_equal(mesh.points, base.points):
            print(f"FAIL np={np_}: point block differs from np=1 "
                  f"({len(mesh.points)} vs {len(base.points)} points)"); ok = False
        if not np.array_equal(mesh.connectivity, base.connectivity):
            print(f"FAIL np={np_}: connectivity differs from np=1"); ok = False
    print(f"np={np_}: vtu {mesh.n_cells} cells, {len(mesh.points)} points, global order ok")

print("MPI CONSISTENCY: " + ("PASS" if ok else "FAIL"))
sys.exit(0 if ok else 1)
PY
