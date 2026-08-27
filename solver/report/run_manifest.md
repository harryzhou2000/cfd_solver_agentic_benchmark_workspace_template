# Run Manifest

Every row is one solver invocation whose outputs are included in this submission.

| case | ranks | steps | t_final | residual orders | wall time [s] | C_L | C_D | status |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| `results/cylinder_m010_laminar_re20` | 8 | 2806 | 0.000 | 5.37 | 44.3 | 0.000643 | 2.017617 | converged |
| `results/cylinder_m010_laminar_re200` | 8 | 30000 | 300.000 | 6.29 | 2311.5 | 0.274084 | 1.269829 | statistically_periodic |
| `results/naca0012_m015_inviscid` | 8 | 1864 | 0.000 | 4.32 | 25.0 | 0.000147 | 0.001025 | converged |
| `results/naca0012_m015_laminar_re5000` | 8 | 4663 | 0.000 | 6.08 | 43.4 | -0.001186 | 0.053387 | converged |
| `results/naca0012_m080_inviscid` | 8 | 9005 | 0.000 | 4.02 | 145.7 | -0.001131 | 0.008541 | converged |
| `results/naca0012_m080_laminar_re5000` | 8 | 8284 | 0.000 | 4.76 | 83.4 | 0.001628 | 0.076913 | converged |
| `results/naca0012_m200_inviscid` | 8 | 2554 | 0.000 | 3.07 | 17.9 | 0.000797 | 0.091919 | converged |
| `results/naca0012_m200_laminar_re5000` | 8 | 18135 | 0.000 | 5.44 | 186.7 | 0.000133 | 0.138396 | converged |
| `studies/cflstudy/cfl100_t1em3` | 4 | 50 | 0.500 | 4.38 | 1.7 | -0.000957 | 1.059825 | failed |
| `studies/cflstudy/cfl100_t1em4` | 4 | 50 | 0.500 | 5.44 | 3.3 | -0.001259 | 1.057089 | failed |
| `studies/cflstudy/cfl10_t1em3` | 4 | 50 | 0.500 | 4.35 | 2.7 | -0.001123 | 1.062058 | failed |
| `studies/cflstudy/cfl1_t1em3` | 4 | 50 | 0.500 | 4.34 | 19.6 | -0.001242 | 1.063106 | failed |
| `studies/cflstudy/cfl30_t1em3` | 4 | 50 | 0.500 | 4.43 | 1.8 | -0.001007 | 1.061034 | failed |
| `studies/cflstudy/cfl30_t1em4` | 4 | 50 | 0.500 | 5.39 | 3.5 | -0.001252 | 1.057253 | failed |
| `studies/cflstudy/cfl30_t1em5` | 4 | 50 | 0.500 | 6.36 | 9.6 | -0.001262 | 1.056928 | failed |
| `studies/mpi/cylinder_m010_laminar_re20_np1` | 1 | 2803 | 0.000 | 5.37 | 42.4 | 0.000648 | 2.017610 | converged |
| `studies/mpi/cylinder_m010_laminar_re20_np2` | 2 | 2802 | 0.000 | 5.37 | 20.9 | 0.000647 | 2.017616 | converged |
| `studies/mpi/cylinder_m010_laminar_re20_np4` | 4 | 2805 | 0.000 | 5.37 | 11.6 | 0.000653 | 2.017618 | converged |
| `studies/mpi/cylinder_m010_laminar_re20_np8` | 8 | 2806 | 0.000 | 5.37 | 18.1 | 0.000643 | 2.017617 | converged |
| `studies/mpi/naca0012_m015_inviscid_np1` | 1 | 1906 | 0.000 | 4.39 | 52.3 | 0.000183 | 0.001014 | converged |
| `studies/mpi/naca0012_m015_inviscid_np2` | 2 | 1844 | 0.000 | 4.38 | 26.9 | 0.000197 | 0.001023 | converged |
| `studies/mpi/naca0012_m015_inviscid_np4` | 4 | 1894 | 0.000 | 4.29 | 15.2 | 0.000066 | 0.001021 | converged |
| `studies/mpi/naca0012_m015_inviscid_np8` | 8 | 1864 | 0.000 | 4.32 | 24.0 | 0.000147 | 0.001025 | converged |
| `studies/restart/naca0012_m015_inviscid_restart` | 4 | 20 | 0.000 | 0.31 | 0.2 | 0.000148 | 0.001026 | failed |
| `studies/verify/cylinder_m010_laminar_re200_prodcfl20` | 4 | 2000 | 20.000 | 7.24 | 176.1 | -0.002827 | 0.937905 | statistically_periodic |
| `studies/verify/cylinder_m010_laminar_re200_suppliedcfl` | 4 | 2000 | 20.000 | 6.19 | 604.7 | -0.002771 | 0.937914 | statistically_periodic |
| `studies/verify/cylinder_m010_laminar_re20_noshockfix` | 4 | 2805 | 0.000 | 5.37 | 11.2 | 0.000653 | 2.017618 | converged |
| `studies/verify/cylinder_m010_laminar_re20_roe` | 4 | 2805 | 0.000 | 5.37 | 11.2 | 0.000654 | 2.017618 | converged |
| `studies/verify/naca0012_m015_inviscid_roe` | 4 | 1894 | 0.000 | 4.29 | 15.6 | 0.000053 | 0.001021 | converged |
| `studies/verify/naca0012_m015_laminar_re5000_noshockfix` | 4 | 4671 | 0.000 | 6.08 | 28.3 | -0.001189 | 0.053387 | converged |
| `studies/verify/naca0012_m015_laminar_re5000_o1` | 4 | 4142 | 0.000 | 6.07 | 21.9 | -0.008215 | 0.068006 | converged |
| `studies/verify/naca0012_m080_inviscid_nofreeze` | 4 | 20000 | 0.000 | 2.84 | 213.2 | 0.000327 | 0.008692 | converged |
| `studies/verify/naca0012_m080_inviscid_noshockfix` | 4 | 9003 | 0.000 | 4.00 | 147.8 | -0.001960 | 0.008527 | converged |
| `studies/verify/naca0012_m200_inviscid_nofreeze` | 4 | 2554 | 0.000 | 3.07 | 12.5 | 0.000790 | 0.091917 | converged |
| `studies/verify/naca0012_m200_inviscid_noshockfix` | 4 | 15287 | 0.000 | 3.02 | 167.6 | -0.004555 | 0.087892 | converged |
| `studies/verify/naca0012_m200_inviscid_roe` | 4 | 2404 | 0.000 | 3.00 | 17.8 | 0.000500 | 0.091265 | converged |
| `studies/verify/naca0012_m200_inviscid_rusanov` | 4 | 1965 | 0.000 | 3.14 | 9.0 | 0.000060 | 0.092019 | converged |

## Exact commands

* `results/cylinder_m010_laminar_re20`

      mpirun -np 8 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json --output results/cylinder_m010_laminar_re20 --progress-every 2000

* `results/cylinder_m010_laminar_re200`

      mpirun -np 8 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json --output results/cylinder_m010_laminar_re200 --cfl-scale 30 --inner-target 1e-4 --progress-every 2000

* `results/naca0012_m015_inviscid`

      mpirun -np 8 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json --output results/naca0012_m015_inviscid --progress-every 2000

* `results/naca0012_m015_laminar_re5000`

      mpirun -np 8 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_laminar_re5000.json --output results/naca0012_m015_laminar_re5000 --progress-every 2000

* `results/naca0012_m080_inviscid`

      mpirun -np 8 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m080_inviscid.json --output results/naca0012_m080_inviscid --progress-every 2000

* `results/naca0012_m080_laminar_re5000`

      mpirun -np 8 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m080_laminar_re5000.json --output results/naca0012_m080_laminar_re5000 --progress-every 2000

* `results/naca0012_m200_inviscid`

      mpirun -np 8 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m200_inviscid.json --output results/naca0012_m200_inviscid --progress-every 2000

* `results/naca0012_m200_laminar_re5000`

      mpirun -np 8 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m200_laminar_re5000.json --output results/naca0012_m200_laminar_re5000 --progress-every 2000

* `studies/cflstudy/cfl100_t1em3`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json --output studies/cflstudy/cfl100_t1em3 --cfl-scale 100 --final-time 0.5 --no-intermediate-fields --progress-every 99999

* `studies/cflstudy/cfl100_t1em4`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json --output studies/cflstudy/cfl100_t1em4 --cfl-scale 100 --final-time 0.5 --inner-target 1e-4 --no-intermediate-fields --progress-every 99999

* `studies/cflstudy/cfl10_t1em3`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json --output studies/cflstudy/cfl10_t1em3 --cfl-scale 10 --final-time 0.5 --no-intermediate-fields --progress-every 99999

* `studies/cflstudy/cfl1_t1em3`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json --output studies/cflstudy/cfl1_t1em3 --cfl-scale --final-time 0.5 --no-intermediate-fields --progress-every 99999

* `studies/cflstudy/cfl30_t1em3`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json --output studies/cflstudy/cfl30_t1em3 --cfl-scale 30 --final-time 0.5 --no-intermediate-fields --progress-every 99999

* `studies/cflstudy/cfl30_t1em4`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json --output studies/cflstudy/cfl30_t1em4 --cfl-scale 30 --final-time 0.5 --inner-target 1e-4 --no-intermediate-fields --progress-every 99999

* `studies/cflstudy/cfl30_t1em5`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json --output studies/cflstudy/cfl30_t1em5 --cfl-scale 30 --final-time 0.5 --inner-target 1e-5 --no-intermediate-fields --progress-every 99999

* `studies/mpi/cylinder_m010_laminar_re20_np1`

      mpirun -np 1 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json --output studies/mpi/cylinder_m010_laminar_re20_np1 --progress-every 2000

* `studies/mpi/cylinder_m010_laminar_re20_np2`

      mpirun -np 2 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json --output studies/mpi/cylinder_m010_laminar_re20_np2 --progress-every 2000

* `studies/mpi/cylinder_m010_laminar_re20_np4`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json --output studies/mpi/cylinder_m010_laminar_re20_np4 --progress-every 2000

* `studies/mpi/cylinder_m010_laminar_re20_np8`

      mpirun -np 8 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json --output studies/mpi/cylinder_m010_laminar_re20_np8 --progress-every 2000

* `studies/mpi/naca0012_m015_inviscid_np1`

      mpirun -np 1 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json --output studies/mpi/naca0012_m015_inviscid_np1 --progress-every 2000

* `studies/mpi/naca0012_m015_inviscid_np2`

      mpirun -np 2 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json --output studies/mpi/naca0012_m015_inviscid_np2 --progress-every 2000

* `studies/mpi/naca0012_m015_inviscid_np4`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json --output studies/mpi/naca0012_m015_inviscid_np4 --progress-every 2000

* `studies/mpi/naca0012_m015_inviscid_np8`

      mpirun -np 8 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json --output studies/mpi/naca0012_m015_inviscid_np8 --progress-every 2000

* `studies/restart/naca0012_m015_inviscid_restart`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json --output studies/restart/naca0012_m015_inviscid_restart --max-steps 20 --progress-every 5000 --restart results/naca0012_m015_inviscid/restart_final.bin

* `studies/verify/cylinder_m010_laminar_re200_prodcfl20`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json --output studies/verify/cylinder_m010_laminar_re200_prodcfl20 --cfl-scale 30 --final-time 20.0 --inner-target 1e-4 --no-intermediate-fields --progress-every 2000

* `studies/verify/cylinder_m010_laminar_re200_suppliedcfl`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json --output studies/verify/cylinder_m010_laminar_re200_suppliedcfl --final-time 20.0 --no-intermediate-fields --progress-every 2000

* `studies/verify/cylinder_m010_laminar_re20_noshockfix`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json --output studies/verify/cylinder_m010_laminar_re20_noshockfix --progress-every 5000 --shock-fix 0

* `studies/verify/cylinder_m010_laminar_re20_roe`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json --output studies/verify/cylinder_m010_laminar_re20_roe --flux roe --progress-every 2000

* `studies/verify/naca0012_m015_inviscid_roe`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json --output studies/verify/naca0012_m015_inviscid_roe --flux roe --progress-every 2000

* `studies/verify/naca0012_m015_laminar_re5000_noshockfix`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_laminar_re5000.json --output studies/verify/naca0012_m015_laminar_re5000_noshockfix --progress-every 5000 --shock-fix 0

* `studies/verify/naca0012_m015_laminar_re5000_o1`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_laminar_re5000.json --output studies/verify/naca0012_m015_laminar_re5000_o1 --first-order --max-steps 20000 --progress-every 2000

* `studies/verify/naca0012_m080_inviscid_nofreeze`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m080_inviscid.json --output studies/verify/naca0012_m080_inviscid_nofreeze --freeze-limiter-step 0 --max-steps 20000 --progress-every 2000

* `studies/verify/naca0012_m080_inviscid_noshockfix`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m080_inviscid.json --output studies/verify/naca0012_m080_inviscid_noshockfix --progress-every 5000 --shock-fix 0

* `studies/verify/naca0012_m200_inviscid_nofreeze`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m200_inviscid.json --output studies/verify/naca0012_m200_inviscid_nofreeze --freeze-limiter-step 0 --max-steps 20000 --progress-every 2000

* `studies/verify/naca0012_m200_inviscid_noshockfix`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m200_inviscid.json --output studies/verify/naca0012_m200_inviscid_noshockfix --progress-every 2000 --shock-fix 0

* `studies/verify/naca0012_m200_inviscid_roe`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m200_inviscid.json --output studies/verify/naca0012_m200_inviscid_roe --flux roe --max-steps 15000 --progress-every 5000

* `studies/verify/naca0012_m200_inviscid_rusanov`

      mpirun -np 4 cfd2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m200_inviscid.json --output studies/verify/naca0012_m200_inviscid_rusanov --flux rusanov --progress-every 2000


## Notes

* `results/cylinder_m010_laminar_re20`: residual reduction target (5 orders) reached and the drag coefficient is stationary: over two consecutive windows of 280 steps the mean C_D moved by 6.04e-03 (window scatter 3.71e-04, tolerance 6.05e-03 in C_D units). CFL safeguard back-offs: 19.
* `results/cylinder_m010_laminar_re200`: lift RMS over the last quarter = 0.4069, mean drag drift between the last two eighths = 3.508e-06, inner target met on 100% of physical steps
* `results/naca0012_m015_inviscid`: residual reduction target (4 orders) reached and the drag coefficient is stationary: over two consecutive windows of 186 steps the mean C_D moved by 4.41e-05 (window scatter 1.34e-04, tolerance 5.00e-05 in C_D units). CFL safeguard back-offs: 16.
* `results/naca0012_m015_laminar_re5000`: residual reduction target (4 orders) reached and the drag coefficient is stationary: over two consecutive windows of 466 steps the mean C_D moved by 1.60e-04 (window scatter 1.61e-05, tolerance 1.60e-04 in C_D units). CFL safeguard back-offs: 4.
* `results/naca0012_m080_inviscid`: residual reduction target (4 orders) reached and the drag coefficient is stationary: over two consecutive windows of 900 steps the mean C_D moved by 2.13e-05 (window scatter 1.15e-05, tolerance 5.00e-05 in C_D units). CFL safeguard back-offs: 66.
* `results/naca0012_m080_laminar_re5000`: residual reduction target (4 orders) reached and the drag coefficient is stationary: over two consecutive windows of 828 steps the mean C_D moved by 2.30e-04 (window scatter 1.21e-05, tolerance 2.31e-04 in C_D units). CFL safeguard back-offs: 0.
* `results/naca0012_m200_inviscid`: residual reduction target (3 orders) reached and the drag coefficient is stationary: over two consecutive windows of 255 steps the mean C_D moved by 2.73e-04 (window scatter 4.07e-04, tolerance 2.76e-04 in C_D units). CFL safeguard back-offs: 0.
* `results/naca0012_m200_laminar_re5000`: residual reduction target (3 orders) reached and the drag coefficient is stationary: over two consecutive windows of 1813 steps the mean C_D moved by 4.15e-04 (window scatter 7.60e-05, tolerance 4.15e-04 in C_D units). CFL safeguard back-offs: 0.
* `studies/cflstudy/cfl100_t1em3`: lift RMS over the last quarter = 0.001256, mean drag drift between the last two eighths = 0.1744, inner target met on 100% of physical steps
* `studies/cflstudy/cfl100_t1em4`: lift RMS over the last quarter = 0.001274, mean drag drift between the last two eighths = 0.176, inner target met on 100% of physical steps
* `studies/cflstudy/cfl10_t1em3`: lift RMS over the last quarter = 0.001271, mean drag drift between the last two eighths = 0.1732, inner target met on 100% of physical steps
* `studies/cflstudy/cfl1_t1em3`: lift RMS over the last quarter = 0.001286, mean drag drift between the last two eighths = 0.1726, inner target met on 100% of physical steps
* `studies/cflstudy/cfl30_t1em3`: lift RMS over the last quarter = 0.001266, mean drag drift between the last two eighths = 0.174, inner target met on 100% of physical steps
* `studies/cflstudy/cfl30_t1em4`: lift RMS over the last quarter = 0.001272, mean drag drift between the last two eighths = 0.1759, inner target met on 100% of physical steps
* `studies/cflstudy/cfl30_t1em5`: lift RMS over the last quarter = 0.001274, mean drag drift between the last two eighths = 0.176, inner target met on 100% of physical steps
* `studies/mpi/cylinder_m010_laminar_re20_np1`: residual reduction target (5 orders) reached and the drag coefficient is stationary: over two consecutive windows of 280 steps the mean C_D moved by 6.03e-03 (window scatter 3.71e-04, tolerance 6.05e-03 in C_D units). CFL safeguard back-offs: 19.
* `studies/mpi/cylinder_m010_laminar_re20_np2`: residual reduction target (5 orders) reached and the drag coefficient is stationary: over two consecutive windows of 280 steps the mean C_D moved by 6.04e-03 (window scatter 3.72e-04, tolerance 6.05e-03 in C_D units). CFL safeguard back-offs: 19.
* `studies/mpi/cylinder_m010_laminar_re20_np4`: residual reduction target (5 orders) reached and the drag coefficient is stationary: over two consecutive windows of 280 steps the mean C_D moved by 6.03e-03 (window scatter 3.73e-04, tolerance 6.05e-03 in C_D units). CFL safeguard back-offs: 19.
* `studies/mpi/cylinder_m010_laminar_re20_np8`: residual reduction target (5 orders) reached and the drag coefficient is stationary: over two consecutive windows of 280 steps the mean C_D moved by 6.04e-03 (window scatter 3.71e-04, tolerance 6.05e-03 in C_D units). CFL safeguard back-offs: 19.
* `studies/mpi/naca0012_m015_inviscid_np1`: residual reduction target (4 orders) reached and the drag coefficient is stationary: over two consecutive windows of 190 steps the mean C_D moved by 6.10e-06 (window scatter 1.18e-04, tolerance 5.00e-05 in C_D units). CFL safeguard back-offs: 18.
* `studies/mpi/naca0012_m015_inviscid_np2`: residual reduction target (4 orders) reached and the drag coefficient is stationary: over two consecutive windows of 184 steps the mean C_D moved by 1.51e-05 (window scatter 1.18e-04, tolerance 5.00e-05 in C_D units). CFL safeguard back-offs: 15.
* `studies/mpi/naca0012_m015_inviscid_np4`: residual reduction target (4 orders) reached and the drag coefficient is stationary: over two consecutive windows of 189 steps the mean C_D moved by 3.11e-05 (window scatter 1.06e-04, tolerance 5.00e-05 in C_D units). CFL safeguard back-offs: 17.
* `studies/mpi/naca0012_m015_inviscid_np8`: residual reduction target (4 orders) reached and the drag coefficient is stationary: over two consecutive windows of 186 steps the mean C_D moved by 4.41e-05 (window scatter 1.34e-04, tolerance 5.00e-05 in C_D units). CFL safeguard back-offs: 16.
* `studies/restart/naca0012_m015_inviscid_restart`: NOT CONVERGED: step limit reached with a residual reduction of 0.306 orders (case target 4); over the last 50 steps the C_D drift is 1.00e+00 and the scatter 1.00e+00 (tolerance 0.00e+00 in C_D units); CFL safeguard back-offs: 0
* `studies/verify/cylinder_m010_laminar_re200_prodcfl20`: lift RMS over the last quarter = 0.01641, mean drag drift between the last two eighths = 0.01319, inner target met on 100% of physical steps
* `studies/verify/cylinder_m010_laminar_re200_suppliedcfl`: lift RMS over the last quarter = 0.01631, mean drag drift between the last two eighths = 0.01324, inner target met on 100% of physical steps
* `studies/verify/cylinder_m010_laminar_re20_noshockfix`: residual reduction target (5 orders) reached and the drag coefficient is stationary: over two consecutive windows of 280 steps the mean C_D moved by 6.03e-03 (window scatter 3.73e-04, tolerance 6.05e-03 in C_D units). CFL safeguard back-offs: 19.
* `studies/verify/cylinder_m010_laminar_re20_roe`: residual reduction target (5 orders) reached and the drag coefficient is stationary: over two consecutive windows of 280 steps the mean C_D moved by 6.04e-03 (window scatter 3.72e-04, tolerance 6.05e-03 in C_D units). CFL safeguard back-offs: 19.
* `studies/verify/naca0012_m015_inviscid_roe`: residual reduction target (4 orders) reached and the drag coefficient is stationary: over two consecutive windows of 189 steps the mean C_D moved by 3.21e-05 (window scatter 1.06e-04, tolerance 5.00e-05 in C_D units). CFL safeguard back-offs: 17.
* `studies/verify/naca0012_m015_laminar_re5000_noshockfix`: residual reduction target (4 orders) reached and the drag coefficient is stationary: over two consecutive windows of 467 steps the mean C_D moved by 1.60e-04 (window scatter 1.61e-05, tolerance 1.60e-04 in C_D units). CFL safeguard back-offs: 4.
* `studies/verify/naca0012_m015_laminar_re5000_o1`: residual reduction target (4 orders) reached and the drag coefficient is stationary: over two consecutive windows of 414 steps the mean C_D moved by 2.04e-04 (window scatter 1.87e-05, tolerance 2.04e-04 in C_D units). CFL safeguard back-offs: 2.
* `studies/verify/naca0012_m080_inviscid_nofreeze`: PLATEAU accepted: step limit reached with a residual reduction of 2.84 orders (case target 4); over the last 2000 steps the C_D drift is 1.05e-05 and the scatter 3.31e-06 (tolerance 1.50e-04 in C_D units); CFL safeguard back-offs: 56. The requested residual reduction was not reached, but the residual is bounded and the force history has plateaued to the stated tolerance, which the benchmark accepts as a clearly justified plateau.
* `studies/verify/naca0012_m080_inviscid_noshockfix`: residual reduction target (4 orders) reached and the drag coefficient is stationary: over two consecutive windows of 900 steps the mean C_D moved by 1.94e-05 (window scatter 1.61e-05, tolerance 5.00e-05 in C_D units). CFL safeguard back-offs: 59.
* `studies/verify/naca0012_m200_inviscid_nofreeze`: residual reduction target (3 orders) reached and the drag coefficient is stationary: over two consecutive windows of 255 steps the mean C_D moved by 2.75e-04 (window scatter 4.10e-04, tolerance 2.76e-04 in C_D units). CFL safeguard back-offs: 0.
* `studies/verify/naca0012_m200_inviscid_noshockfix`: residual reduction target (3 orders) reached and the drag coefficient is stationary: over two consecutive windows of 1528 steps the mean C_D moved by 3.13e-05 (window scatter 1.85e-04, tolerance 2.64e-04 in C_D units). CFL safeguard back-offs: 31.
* `studies/verify/naca0012_m200_inviscid_roe`: residual reduction target (3 orders) reached and the drag coefficient is stationary: over two consecutive windows of 240 steps the mean C_D moved by 5.06e-05 (window scatter 6.19e-04, tolerance 2.75e-04 in C_D units). CFL safeguard back-offs: 0.
* `studies/verify/naca0012_m200_inviscid_rusanov`: residual reduction target (3 orders) reached and the drag coefficient is stationary: over two consecutive windows of 196 steps the mean C_D moved by 2.76e-04 (window scatter 5.28e-05, tolerance 2.76e-04 in C_D units). CFL safeguard back-offs: 0.
