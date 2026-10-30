# Code review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-30T18:39:55.532996+00:00

**Overall score: 3.52**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **code.build** Build system and reproducibility | 0.15 | 4 | Fresh immutable-source CMake build passed in network-disabled cfd-bench with 2 CPUs, 3 GiB and 256-process cap. CTest found no registered tests; actual historical binary remains unbound. |
|   | Clean documented build from a clean checkout; CMake or equivalent with configurable external dependency root (CFD_EXTERNALS_ROOT); no absolute machine paths; build works without manual case edits. | | | |
|   | *Evidence:* CMakeLists.txt and README build commands; try a clean configure/build. | | | |
| 2 | **code.cli** CLI and error handling | 0.10 | 4 | 6b376564 main.cpp:50–60 parses solve/case/output and rejects missing required arguments; case_input.hpp resolves the JSON mesh path. Bad-input execution was not independently repeated. |
|   | Required CLI form `solve --case <json> --output <dir>` works for every case; malformed inputs/missing meshes produce nonzero exit and clear messages; no hard-coded case names. | | | |
|   | *Evidence:* Argument parser, main(), error paths; spot-run bad input. | | | |
| 3 | **code.organization** Code organization and extensibility | 0.12 | 4 | 6b376564 separates mesh.cpp, partition.cpp, solver.cpp, output.cpp, physics.hpp and case_input.hpp; solver and mesh remain compact 2-D-specific abstractions. |
|   | Clear module boundaries; equation/physics interfaces not case-specific; BCs extensible; mesh/geometry abstractions could generalize to 3-D; structure supports EOS/RANS/species extension. | | | |
|   | *Evidence:* Directory layout, class/interface design, absence of case-specific branches. | | | |
| 4 | **code.originality** Originality and license compliance | 0.15 | 4 | Source review did not establish an internal external-solver wrapper. No external authorship or exhaustive license investigation was performed. |
|   | Solver core, time integration, and MPI communication are original; no copied/adapted/wrapped open-source CFD solver; no calls to existing solver executables; licenses respected. | | | |
|   | *Evidence:* Diff against known open-source CFD cores; provenance of large copied blocks; headers/comments. | | | |
| 5 | **code.hardcoding** No hard-coded case logic | 0.08 | 4 | case_input.hpp reads mesh filename from JSON rather than branching on two supplied filenames; output.cpp:338 does special-case the re200 identifier for convergence fraction. |
|   | Cases drive behavior through JSON inputs; no branches on mesh filename or case id; production paths not special-cased per benchmark case. | | | |
|   | *Evidence:* grep for mesh/case names in source; input-driven parameters. | | | |
| 6 | **code.mpi** MPI correctness | 0.15 | 2.5 | 6b376564 main.cpp:73–143 broadcasts the full mesh to all ranks; :164–194 retains it throughout solve and solver.cpp:10–12 stores a reference. This violates TASK.md:46,227 despite actual METIS and neighbor halo communication. Rank comparisons are short and historical binary attribution is not independently hashed. |
|   | Real neighbor halo exchange (isend/irecv or equivalent); global MPI reductions for residuals/forces; no full-state allgather or full-mesh replication during iterations; rank-independent output; partition diagnostics match the actual run. | | | |
|   | *Evidence:* Communication code review; metadata.json halo_exchange field; diagnostics. | | | |
| 7 | **code.robustness** Robustness and memory sanity | 0.08 | 3 | solver.cpp has positivity checks, but submitted viscous forces are hundreds to thousands and metadata labels incomplete states successful; no independently verified historic binary. |
|   | Bounds/error checks on mesh data and allocations; no full-mesh replication per rank during solve; no unbounded recursion or O(n^2) surprises; handles mixed cells and degenerate geometry. | | | |
|   | *Evidence:* Defensive checks; memory footprint on np=8; crash tests. | | | |
| 8 | **code.testing** Testing and validation evidence | 0.10 | 3 | Evaluator clean build succeeded; CTest found no registered tests. Historical validator and short MPI artifacts exist; no evaluator CFD rerun. |
|   | Unit tests for geometry/flux/reconstruction where practical; smoke runs; serial-vs-parallel force comparison; validator run before submission. | | | |
|   | *Evidence:* tests/ dir, CI or run logs, validate_outputs.py output. | | | |
| 9 | **code.hygiene** Code hygiene | 0.07 | 3 | Code is modular, but firstOrder startup logic never clears, transient nPhysicalSteps shadows a member, and status is assigned unconditionally in main.cpp:187–196. |
|   | Readable naming, comments on non-obvious numerics, no dead/debug code in submission, no leftover scratch files in tracked source, consistent formatting. | | | |
|   | *Evidence:* Skim sources; check for TODO/FIXME/abandoned experiments in tracked files. | | | |

## Disqualification flags

- [ ] Existing solver executable called internally
- [ ] Force/residual files generated without solving
- [ ] Code only reads the two benchmark mesh filenames through hard-coded branches
- [ ] MPI rank count changes results by order-one amounts for steady cases
- [ ] Explicit time stepping only (no implicit/steady capability claimed where required)
- [ ] Report claims algorithms that cannot be found in source
- [ ] metadata.json marks incomplete/failed runs as successful
- [ ] Solver core, time integration, or MPI communication copied/adapted/wrapped from an existing open-source CFD codebase

## Summary

- Overall score (0-5): 3.52
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
