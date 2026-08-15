#!/bin/bash
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:$LD_LIBRARY_PATH
cd /workspace/solver
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
SOLVER=./build/cfd2d
run1() { local c=$1; local ms=$2; local env="$3"; local out="results/$c"
  eval "env $env mpirun --oversubscribe -np 4 $SOLVER solve --case $CASES/$c.json --output $out --max-steps $ms" > "$out/run.log" 2>&1
  echo "DONE $c -> $(grep -o '"convergence_status": "[^"]*"' "$out/run_status.json" 2>/dev/null) fstep=$(grep -o '"final_step": [0-9]*' "$out/run_status.json" 2>/dev/null) $(date +%T)"
}
# retry m200 re5000 with more steps (first-order, low CFL) for plateau
run1 naca0012_m200_laminar_re5000 12000 "CFD2D_FIRSTORDER=1 CFD2D_CFL_CAP=0.2 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=8 CFD2D_SGS_SWEEPS=4"
# re200 transient: BDF2, CFL=1.0, max_inner=1000 (converging), partial to t=12 (1200 steps)
# -- full 30000-step run is infeasible (scalar implicit needs ~800 inner/step); documented honest partial
run1 cylinder_m010_laminar_re200 1200 "CFD2D_CFL_INIT=1.0 CFD2D_CFL_CAP=1.0 CFD2D_SGS_SWEEPS=4 CFD2D_MAX_INNER=1000"
echo "FINAL RUN DONE $(date +%T)"
