#!/bin/bash
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
BIN=/workspace/solver/build/cfd2d
OUT=/workspace/solver/results/prod
NP=8
run() {
  local case="$1" cid="$2"; shift 2
  local d="$OUT/$cid"; mkdir -p "$d"
  echo "=== RUN $cid (np=$NP) args=$* ==="
  mpirun --oversubscribe -np $NP $BIN solve --case "$CASES/$case" --output "$d" "$@" 2>&1 | grep -v "Authorization required" | tail -1
  echo "  final: cl=$(tail -1 $d/forces.csv|cut -d, -f3) cd=$(tail -1 $d/forces.csv|cut -d, -f4) res=$(tail -1 $d/residuals.csv|cut -d, -f10) steps=$(tail -1 $d/residuals.csv|cut -d, -f1)"
}
# Cylinder Re20 steady: low Mach needs conservative CFL; freestream init (default)
run cylinder_m010_laminar_re20.json cylinder_m010_laminar_re20 --cfl-max 30 --cfl-ramp 3000
# Cylinder Re200 transient: BDF2 dual-time, physical dt=0.01, final_time=300
# Matrix-free GMRES with LU-SGS preconditioning. The scalar diagonal point-Jacobi
# solver converges at only ~0.98/iter for this stiff low-Mach (M=0.1) case, making
# the 1e-3 inner-residual target unreachable in a tractable iteration count. GMRES
# uses the true Jacobian-vector product (forward-difference of the full BDF2 residual)
# and converges the inner system to the 1e-3 target for >99% of physical steps.
CFDD_GMRES=1 CFDD_GTOL=0.1 CFDD_MININNER=3 CFDD_MAXINNER=12 CFDD_GMAXITER=30 \
  run cylinder_m010_laminar_re200.json cylinder_m010_laminar_re200
echo "=== ALL CYLINDER DONE ==="
