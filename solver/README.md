# Distributed finite-volume CFD solver

Build an optimized executable with `cmake -S solver -B build -DCMAKE_BUILD_TYPE=Release -DCFD_EXTERNALS_ROOT=external/cfd_externals/install && cmake --build build -j`.

Run from the workspace root, for example:

```bash
mpirun -np 4 build/cfd_solver solve --case cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json --output results/naca0012_m015_inviscid
```

The executable imports the CGNS mesh on rank zero, partitions the cell graph
with METIS, and writes one compact binary partition per rank under the output
directory's `.partitions/` staging area. Each rank then loads only its owned
cells plus the one-ring ghost geometry needed by its stencil. Rank zero drops
the preprocessing mesh before nonlinear iterations begin. Conservative states
are stored only for owned and ghost cells, and neighbor-scoped nonblocking MPI
exchanges synchronize halo values; neither the full mesh nor the full state is
replicated during iterations. After the solve, rank zero reloads the global
mesh and gathers owned states solely to write the serial final field, surface,
and restart artifacts.

`metadata.json` reports the actual local owned/ghost counts,
`full_mesh_replication_during_iterations=false`,
`full_state_replication_during_iterations=false`, and a
`mesh_replication_note` describing this lifecycle. Metadata also records UTC
start/end timestamps and the final output step/time used for field/force
provenance; `run_status.json` and `stdout.log` preserve the exact MPI command.

The solver uses piecewise-linear least-squares gradients with a
Barth--Jespersen limiter, Rusanov fluxes, Newtonian/Fourier viscous terms, and
matrix-free block-Jacobi pseudo-implicit relaxation. The transient path is a
BDF2 dual-time outer/inner loop.

Before adding a new CGNS mesh, inspect its zone, section, and boundary-tag
topology with:

```bash
build/cgns_mesh_inspect path/to/mesh.cgns
```

The supplied cylinder mesh is multi-zone; its `con-*` BAR_2 sections are
interfaces and must be paired into interior faces, while `WALL` and `FAR` are
the physical boundary names consumed through the case JSON mapping.
