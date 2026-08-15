#!/bin/bash
# Reproducible sequential run of all 8 required cases with the exact per-case
# numerical settings used for the submission (see report Limitations). Runs are
# sequential (not parallel) to avoid MPI oversubscription contention.
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:$LD_LIBRARY_PATH
cd /workspace/solver
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
SOLVER=./build/cfd2d
NP=4
run() {  # run <case> <maxsteps> <env>
  local c=$1 ms=$2 env="$3" out="results/$c"
  echo "[$(date +%T)] running $c (max-steps $ms)"
  eval "env $env mpirun --oversubscribe -np $NP $SOLVER solve --case $CASES/$c.json --output $out --max-steps $ms" > "$out/run.log" 2>&1
  echo "[$(date +%T)] done $c -> $(grep -o '"convergence_status": "[^"]*"' "$out/run_status.json" 2>/dev/null)"
}
# Inviscid subsonic: default CFL cap 2.0 / ramp 500 / Rusanov scale 2.0 / 2nd-order limcap 0.5
run naca0012_m015_inviscid        5000 ""
run naca0012_m080_inviscid        5000 ""
# Inviscid supersonic (Roe): low fixed CFL (diverges >0.5 near the bow shock)
run naca0012_m200_inviscid        8000 "CFD2D_CFL_CAP=0.3 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=4 CFD2D_SGS_SWEEPS=4"
# Laminar NACA Re5000 subsonic: 2nd-order (limcap 0.3), low fixed CFL 0.2
run naca0012_m015_laminar_re5000  6000 "CFD2D_LIMCAP=0.3 CFD2D_CFL_CAP=0.2 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=8 CFD2D_SGS_SWEEPS=4"
run naca0012_m080_laminar_re5000 12000 "CFD2D_LIMCAP=0.3 CFD2D_CFL_CAP=0.2 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=8 CFD2D_SGS_SWEEPS=4"
# Laminar NACA Re5000 supersonic: first-order (shock stability), low fixed CFL 0.2
run naca0012_m200_laminar_re5000 12000 "CFD2D_FIRSTORDER=1 CFD2D_CFL_CAP=0.2 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=8 CFD2D_SGS_SWEEPS=4"
# Cylinder Re20 steady: 2nd-order (limcap 0.3), low fixed CFL 0.2
run cylinder_m010_laminar_re20   12000 "CFD2D_LIMCAP=0.3 CFD2D_CFL_CAP=0.2 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=8 CFD2D_SGS_SWEEPS=4"
# Cylinder Re200 transient: BDF2 dual-time, pseudo-CFL 1.0, inner up to 1000.
# NOTE: the scalar point-implicit needs hundreds of SGS sweeps/physical step, so
# the full t=300 (30000-step) run is compute-bound; the solver checkpoints the
# output package every 500 physical steps, so a stopped run is still complete.
run cylinder_m010_laminar_re200   30000 "CFD2D_CFL_INIT=1.0 CFD2D_CFL_CAP=1.0 CFD2D_SGS_SWEEPS=4 CFD2D_MAX_INNER=1000"
echo "ALL CASES DONE $(date +%T)"
