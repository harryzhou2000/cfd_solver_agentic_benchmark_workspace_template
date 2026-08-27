# Negative control: does the mesh-group assertion actually FIRE?
# Copy the results tree to scratch, perturb ONE NACA case's linear-gradient value,
# and confirm the harvester refuses rather than silently picking a value.
import re, shutil, subprocess, os, pathlib
src = pathlib.Path('/workspace/solver/results')
dst = pathlib.Path('/workspace/solver/scratch/assert_test/results')
if dst.parent.exists(): shutil.rmtree(dst.parent)
dst.parent.mkdir(parents=True)
shutil.copytree(src, dst)
log = dst / 'naca0012_m080_inviscid' / 'stdout.log'
t = log.read_text(errors='replace')
t2 = t.replace('linear-gradient error 1.428e-10', 'linear-gradient error 9.999e-09', 1)
assert t2 != t, 'perturbation did not apply'
log.write_text(t2)
print('perturbed one NACA case: linear-gradient 1.428e-10 -> 9.999e-09')
# Run the harvester against the perturbed tree
env = dict(os.environ, CNS_RESULTS_DIR=str(dst))
r = subprocess.run(['/workspace/solver/.venv/bin/python','harvest_numbers.py'],
                   cwd='/workspace/solver/report', capture_output=True, text=True, env=env)
print('exit code:', r.returncode)
print((r.stdout + r.stderr).strip()[:900])
