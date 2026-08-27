#!/bin/bash
# Production sweep: every required benchmark case at np=8, plus the MPI
# rank-count study and the parameter-verification runs.
#
#   scripts/run_all.sh [production|mpi|verify|all]
set -uo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
MODE=${1:-all}
R=results
S=studies
NP=${NP:-8}

# Options shared by every case.  The only documented deviation from the case
# files is for the Reynolds 200 dual-time run, which uses a larger pseudo-time
# CFL with a stricter inner residual target (see README.md and section 7 of the
# report).  Limiter freezing is automatic and uniform (three CFL-ramp lengths)
# and therefore needs no per-case flag.
COMMON="--progress-every 2000"
# The Re 200 dual-time run uses a larger pseudo-time CFL together with a
# *stricter* inner residual target than the supplied values; see report.
RE200_OPTS="--cfl-scale 30 --inner-target 1e-4"

run() { scripts/run_case.sh "$@" 2>&1 | grep -v "Authorization required" || true; }

if [ "$MODE" = steady ] || [ "$MODE" = production ] || [ "$MODE" = all ]; then
  run naca0012_m015_inviscid        $NP $R/naca0012_m015_inviscid        $COMMON
  run naca0012_m080_inviscid        $NP $R/naca0012_m080_inviscid        $COMMON
  run naca0012_m200_inviscid        $NP $R/naca0012_m200_inviscid        $COMMON
  run cylinder_m010_laminar_re20    $NP $R/cylinder_m010_laminar_re20    $COMMON
  run naca0012_m015_laminar_re5000  $NP $R/naca0012_m015_laminar_re5000  $COMMON
  run naca0012_m080_laminar_re5000  $NP $R/naca0012_m080_laminar_re5000  $COMMON
  run naca0012_m200_laminar_re5000  $NP $R/naca0012_m200_laminar_re5000  $COMMON
fi

if [ "$MODE" = transient ] || [ "$MODE" = production ] || [ "$MODE" = all ]; then
  run cylinder_m010_laminar_re200   $NP $R/cylinder_m010_laminar_re200   $COMMON $RE200_OPTS
fi

if [ "$MODE" = mpi ] || [ "$MODE" = all ]; then
  for np in 1 2 4 8; do
    run naca0012_m015_inviscid     $np $S/mpi/naca0012_m015_inviscid_np$np     $COMMON
    run cylinder_m010_laminar_re20 $np $S/mpi/cylinder_m010_laminar_re20_np$np $COMMON
  done
fi

if [ "$MODE" = verify ] || [ "$MODE" = all ]; then
  # Flux-scheme cross-check: Roe with Harten-Yee entropy fix vs HLLC.
  run naca0012_m015_inviscid     4 $S/verify/naca0012_m015_inviscid_roe  $COMMON --flux roe
  run cylinder_m010_laminar_re20 4 $S/verify/cylinder_m010_laminar_re20_roe $COMMON --flux roe
  # Mach 2 and Mach 0.8 without limiter freezing, to document the limit cycle.
  run naca0012_m200_inviscid     4 $S/verify/naca0012_m200_inviscid_nofreeze $COMMON \
      --max-steps 20000 --freeze-limiter-step 0
  run naca0012_m080_inviscid     4 $S/verify/naca0012_m080_inviscid_nofreeze $COMMON \
      --max-steps 20000 --freeze-limiter-step 0
  # First-order reference, to show what the linear reconstruction buys.
  run naca0012_m015_laminar_re5000 4 $S/verify/naca0012_m015_laminar_re5000_o1 \
      $COMMON --first-order --max-steps 20000
  # Re 200 with the literal supplied dual-time controls (pseudo-CFL 1, inner
  # target 1e-3) over a shorter horizon, to show the production settings agree.
  run cylinder_m010_laminar_re200 4 $S/verify/cylinder_m010_laminar_re200_suppliedcfl \
      $COMMON --final-time 20.0 --no-intermediate-fields
  run cylinder_m010_laminar_re200 4 $S/verify/cylinder_m010_laminar_re200_prodcfl20 \
      $COMMON --final-time 20.0 --no-intermediate-fields $RE200_OPTS
fi
echo "RUN_ALL_DONE mode=$MODE"
