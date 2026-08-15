#!/bin/bash
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:$LD_LIBRARY_PATH
cd /workspace/solver
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
SOLVER=./build/cfd2d
run1() {  # run1 <case> <maxsteps> <env>  (single attempt, no retry)
  local c=$1; local ms=$2; local env="$3"; local out="results/$c"
  eval "env $env mpirun --oversubscribe -np 4 $SOLVER solve --case $CASES/$c.json --output $out --max-steps $ms" > "$out/run.log" 2>&1
  echo "DONE $c -> $(grep -o '"convergence_status": "[^"]*"' "$out/run_status.json" 2>/dev/null) fstep=$(grep -o '"final_step": [0-9]*' "$out/run_status.json" 2>/dev/null) $(date +%T)"
}
run1 naca0012_m080_laminar_re5000 12000 "CFD2D_LIMCAP=0.3 CFD2D_CFL_CAP=0.2 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=8 CFD2D_SGS_SWEEPS=4"
run1 naca0012_m200_laminar_re5000  6000 "CFD2D_FIRSTORDER=1 CFD2D_CFL_CAP=0.2 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=8 CFD2D_SGS_SWEEPS=4"
run1 cylinder_m010_laminar_re20   12000 "CFD2D_LIMCAP=0.3 CFD2D_CFL_CAP=0.2 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=8 CFD2D_SGS_SWEEPS=4"
echo "REMAINING DONE $(date +%T)"
