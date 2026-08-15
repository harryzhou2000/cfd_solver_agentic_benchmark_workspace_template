#!/bin/bash
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:$LD_LIBRARY_PATH
cd /workspace/solver
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
SOLVER=./build/cfd2d
run() {  # run <case> <maxsteps> <env>
  local c=$1; local ms=$2; local env="$3"; local out="results/$c"
  for a in 1 2 3; do
    eval "env $env mpirun --oversubscribe -np 4 $SOLVER solve --case $CASES/$c.json --output $out --max-steps $ms" > "$out/run.log" 2>&1
    rc=$?
    if [ $rc -eq 0 ] && grep -q '"completed": true' "$out/metadata.json" 2>/dev/null; then break; fi
    echo "retry $a $c rc=$rc $(date +%T)" >> "$out/run.log"; sleep 2
  done
  echo "DONE $c -> $(grep -o '"convergence_status": "[^"]*"' "$out/run_status.json" 2>/dev/null) finalstep=$(grep -o '"final_step": [0-9]*' "$out/run_status.json" 2>/dev/null) $(date +%T)"
}
# inviscid subsonic: default CFL cap 2.0, ramp 500, Rusanov 2.0, 2nd-order limcap 0.5
run naca0012_m015_inviscid  5000 ""
run naca0012_m080_inviscid  5000 ""
# inviscid supersonic (Roe): low fixed CFL (diverges >0.5)
run naca0012_m200_inviscid  8000 "CFD2D_CFL_CAP=0.3 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=4 CFD2D_SGS_SWEEPS=4"
# laminar NACA re5000 subsonic: 2nd-order, low fixed CFL
run naca0012_m015_laminar_re5000 6000 "CFD2D_LIMCAP=0.3 CFD2D_CFL_CAP=0.2 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=8 CFD2D_SGS_SWEEPS=4"
run naca0012_m080_laminar_re5000 6000 "CFD2D_LIMCAP=0.3 CFD2D_CFL_CAP=0.2 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=8 CFD2D_SGS_SWEEPS=4"
# laminar NACA re5000 supersonic: first-order (shock stability), low fixed CFL
run naca0012_m200_laminar_re5000 6000 "CFD2D_FIRSTORDER=1 CFD2D_CFL_CAP=0.2 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=8 CFD2D_SGS_SWEEPS=4"
# cylinder re20 steady: 2nd-order, low fixed CFL
run cylinder_m010_laminar_re20 8000 "CFD2D_LIMCAP=0.3 CFD2D_CFL_CAP=0.2 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=8 CFD2D_SGS_SWEEPS=4"
echo "STEADY2 DONE $(date +%T)"
