# Run Manifest

All runs use the cfd2d binary built from this repository (see
README.md for the build command). Production runs use MPI ranks=8,
Roe flux, and the Venkatakrishnan limiter. Exact commands are recorded
below as executed (also stored per case in run_status.json).

| case | ranks | steps | final time | residual orders | wall [s] | status | command extras |
|---|---|---|---|---|---|---|---|
| naca0012_m015_inviscid | 8 | 1482 | 0.00 | 4.02 | 254.7 | converged | |
| naca0012_m080_inviscid | 8 | 2189 | 0.00 | 4.00 | 205.3 | converged | |
| naca0012_m200_inviscid | 8 | 2528 | 0.00 | 3.00 | 207.1 | converged | |
| naca0012_m015_laminar_re5000 | 8 | 1917 | 0.00 | 4.00 | 133.5 | converged | |
| naca0012_m080_laminar_re5000 | 8 | 8696 | 0.00 | 4.00 | 506.7 | converged |--restart results/naca0012_m080_inviscid/restart_final.bin --cfl-max 4 |
| naca0012_m200_laminar_re5000 | 8 | 37786 | 0.00 | 5.00 | 1229.3 | converged |--restart results/naca0012_m200_inviscid/restart_final.bin --cfl-max 5 --residual-target 5.0 |
| cylinder_m010_laminar_re20 | 8 | 2129 | 0.00 | 5.00 | 88.1 | converged | |
| cylinder_m010_laminar_re200 | 8 | 31233 | 300.00 | 3.06 | 3715.9 | statistically_periodic |--restart results/cylinder_m010_laminar_re200_steadyinit/restart_final.bin --pert-aoa-deg 2.0 --pert-duration 10.0 |

Base command for every case:

    mpirun --bind-to none -np 8 ./build/cfd2d solve --case <case>.json
      --output results/<case_id> --limiter venkat --flux roe

The Re 200 transient was initialized from a steady pseudo-converged
state (cases/cylinder_m010_laminar_re200_steadyinit.json, a steady
control deck over the identical physics/mesh) via --restart, plus the
2 deg / 10-time-unit startup angle-of-attack perturbation.

## Rank-count study

results/rankstudy/<case>_np<N>/ holds np=1,2,4,8 reruns of
naca0012_m015_inviscid and cylinder_m010_laminar_re20 with identical
numerics, executed back-to-back in one machine-load window by
tools/rank_sweep.sh. The np=8 sweep runs reproduce the production
np=8 runs exactly (identical step counts and final forces). Timings
and force spreads are in report/sanity_checks.json (mpi_rank_study).
