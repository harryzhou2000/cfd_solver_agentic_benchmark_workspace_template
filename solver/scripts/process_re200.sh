#!/bin/bash
# Post-processing script for Re200 case - run after solver completes
set -e

RESULTS=/workspace/solver/results/cylinder_m010_laminar_re200
REPORT=/workspace/solver/report

echo "=== Step 1: Validate Re200 output ==="
python3 /workspace/cfd_solver_agentic_benchmark/examiner/validate_outputs.py "$RESULTS"

echo "=== Step 2: Generate visualizations ==="
python3 /workspace/solver/scripts/visualize.py /workspace/solver/results "$REPORT"

echo "=== Step 3: Generate report data ==="
python3 /workspace/solver/scripts/generate_report_data.py /workspace/solver/results "$REPORT"

echo "=== Step 4: Full validation (all 8 cases + report) ==="
python3 /workspace/cfd_solver_agentic_benchmark/examiner/validate_outputs.py \
    /workspace/solver/results/naca0012_m015_inviscid \
    /workspace/solver/results/naca0012_m015_laminar_re5000 \
    /workspace/solver/results/naca0012_m080_inviscid \
    /workspace/solver/results/naca0012_m080_laminar_re5000 \
    /workspace/solver/results/naca0012_m200_inviscid \
    /workspace/solver/results/naca0012_m200_laminar_re5000 \
    /workspace/solver/results/cylinder_m010_laminar_re20 \
    "$RESULTS" \
    --report "$REPORT"

echo "=== Step 5: Extract Re200 metadata ==="
python3 -c "
import json
m = json.load(open('$RESULTS/metadata.json'))
print('=== Re200 Metadata ===')
for k in sorted(m.keys()):
    print(f'  {k}: {m[k]}')
"

echo "=== Step 6: Re200 force summary ==="
python3 -c "
import csv
with open('$RESULTS/forces.csv') as f:
    rows = list(csv.DictReader(f))
cd = [float(r['cd']) for r in rows]
cl = [float(r['cl']) for r in rows]
print(f'Final step: {rows[-1][\"step\"]}')
print(f'Final Cd: {cd[-1]:.8f}')
print(f'Final Cl: {cl[-1]:.8f}')
# Mean over last 1000 steps (if available)
n = min(1000, len(cd))
import statistics
print(f'Mean Cd (last {n} steps): {statistics.mean(cd[-n:]):.6f}')
print(f'Mean Cl (last {n} steps): {statistics.mean(cl[-n:]):.6f}')
print(f'Std Cd (last {n} steps): {statistics.stdev(cd[-n:]):.6f}')
print(f'Std Cl (last {n} steps): {statistics.stdev(cl[-n:]):.6f}')
# Estimate Strouhal from Cl oscillation
import numpy as np
cl_arr = np.array(cl[-n:])
cl_mean = np.mean(cl_arr)
cl_centered = cl_arr - cl_mean
# Zero crossings
crossings = np.where(np.diff(np.sign(cl_centered)))[0]
if len(crossings) >= 4:
    periods = np.diff(crossings[::2])  # full periods
    avg_period_steps = np.mean(periods)
    dt = 0.01  # time step
    frequency = 1.0 / (avg_period_steps * dt)
    D = 1.0  # cylinder diameter
    V = 1.0  # freestream velocity
    St = frequency * D / V
    print(f'Estimated Strouhal number: {St:.4f}')
    print(f'Cl amplitude: {(np.max(cl_arr) - np.min(cl_arr))/2:.6f}')
else:
    print('Not enough oscillation data for Strouhal estimate')
"

echo "=== Step 7: Re200 run_status ==="
cat "$RESULTS/run_status.json"

echo ""
echo "=== ALL DONE ==="
