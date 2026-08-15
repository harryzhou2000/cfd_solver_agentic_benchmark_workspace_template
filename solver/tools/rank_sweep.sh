#!/bin/bash
# Rank-count comparison: 1 NACA (m015 inviscid) + 1 cylinder (re20) at np=1/2/4/8.
# Short runs (max-steps 200) to measure timing + consistency + np=8 stability.
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:$LD_LIBRARY_PATH
cd /workspace/solver
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
SOLVER=./build/cfd2d
OUT=results/rank_sweep
mkdir -p "$OUT"
echo "case,np,wall_s,final_step,status,cd,cl,rl2_final,note" > "$OUT/rank_comparison.csv"
run() {  # run <case> <np> <env>
  local c=$1; local np=$2; local env="$3"
  local d="$OUT/${c}_np${np}"
  rm -rf "$d"; mkdir -p "$d"
  eval "env $env mpirun --oversubscribe -np $np $SOLVER solve --case $CASES/$c.json --output $d --max-steps 200" > "$d/run.log" 2>&1
  local rc=$?
  local st=$(grep -o '"convergence_status": "[^"]*"' "$d/run_status.json" 2>/dev/null | sed 's/.*: "//;s/"//')
  local fs=$(grep -o '"final_step": [0-9]*' "$d/run_status.json" 2>/dev/null | sed 's/.*: //')
  local wt=$(grep -o '"wall_time_seconds": [0-9.]*' "$d/run_status.json" 2>/dev/null | sed 's/.*: //')
  local fr=$(tail -1 "$d/forces.csv" 2>/dev/null)
  local cd=$(echo "$fr" | cut -d, -f4); local cl=$(echo "$fr" | cut -d, -f3)
  local rl2=$(tail -1 "$d/residuals.csv" 2>/dev/null | cut -d, -f10)
  local note="ok"; [ $rc -ne 0 ] && note="rc=$rc"
  [ -z "$st" ] && note="no_status"
  echo "$c,$np,$wt,$fs,$st,$cd,$cl,$rl2,$note" >> "$OUT/rank_comparison.csv"
  echo "  $c np=$np status=$st wall=${wt}s cd=$cd"
}
echo "NACA m015 inviscid rank sweep:"
for np in 1 2 4 8; do run naca0012_m015_inviscid $np "CFD2D_INNER=4 CFD2D_SGS_SWEEPS=4"; done
echo "Cylinder re20 rank sweep:"
for np in 1 2 4 8; do run cylinder_m010_laminar_re20 $np "CFD2D_LIMCAP=0.3 CFD2D_CFL_CAP=0.2 CFD2D_CFL_INIT=0.2 CFD2D_CFL_RAMP=100000 CFD2D_INNER=8 CFD2D_SGS_SWEEPS=4"; done
echo "RANK SWEEP DONE $(date +%T)"
