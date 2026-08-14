# Run Manifest

| case | ranks | steps | physical time | wall time (s) | residual orders | status | command |
|---|---|---|---|---|---|---|---|
| cylinder_m010_laminar_re200 | 3 | 30000 | 300.000 | 12972.8 | 2.25 | statistically_periodic | `./build/cfd_fv2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json --output results/final_cylinder_m010_laminar_re200_np3` |
| cylinder_m010_laminar_re20 | 1 | 999 | 0.000 | 725.8 | 11.57 | converged | `./build/cfd_fv2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json --output results/final_cylinder_m010_laminar_re20_np1` |
| naca0012_m015_inviscid | 1 | 1780 | 0.000 | 1476.7 | 6.06 | converged | `./build/cfd_fv2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json --output results/final_naca0012_m015_inviscid_np1` |
| naca0012_m015_laminar_re5000 | 1 | 1292 | 0.000 | 1896.0 | 7.76 | converged | `./build/cfd_fv2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_laminar_re5000.json --output results/final_naca0012_m015_laminar_re5000_np1` |
| naca0012_m080_inviscid | 1 | 1988 | 0.000 | 1619.7 | 5.58 | converged | `./build/cfd_fv2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m080_inviscid.json --output results/final_naca0012_m080_inviscid_np1` |
| naca0012_m080_laminar_re5000 | 1 | 1283 | 0.000 | 1959.1 | 6.10 | converged | `./build/cfd_fv2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m080_laminar_re5000.json --output results/final_naca0012_m080_laminar_re5000_np1` |
| naca0012_m200_inviscid | 1 | 801 | 0.000 | 1044.3 | 2.40 | converged | `./build/cfd_fv2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m200_inviscid.json --output results/final_naca0012_m200_inviscid_np1` |
| naca0012_m200_laminar_re5000 | 1 | 1174 | 0.000 | 2228.2 | 5.51 | converged | `./build/cfd_fv2d solve --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m200_laminar_re5000.json --output results/final_naca0012_m200_laminar_re5000_np1` |
