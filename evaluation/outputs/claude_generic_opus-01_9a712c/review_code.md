# Code review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-29T11:22:18.913492+00:00

**Overall score: 4.45**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **code.build** Build system and reproducibility | 0.15 | 5 | Clean immutable CMake build and kernel ctest passed in a network-disabled, resource-limited scratch container with configured external roots; README gives build commands. |
|   | Clean documented build from a clean checkout; CMake or equivalent with configurable external dependency root (CFD_EXTERNALS_ROOT); no absolute machine paths; build works without manual case edits. | | | |
|   | *Evidence:* CMakeLists.txt and README build commands; try a clean configure/build. | | | |
| 2 | **code.cli** CLI and error handling | 0.10 | 4 | main.cpp parses solve, case, output and restart, rejects missing or unknown arguments; report.tex:595–610 gives bad-input examples. Exhaustive malformed-input testing was not performed. |
|   | Required CLI form `solve --case <json> --output <dir>` works for every case; malformed inputs/missing meshes produce nonzero exit and clear messages; no hard-coded case names. | | | |
|   | *Evidence:* Argument parser, main(), error paths; spot-run bad input. | | | |
| 3 | **code.organization** Code organization and extensibility | 0.12 | 4.5 | solver/src separates case configuration, CGNS/mesh/partition, physics, residual/implicit driver and output. Interfaces are substantially two-dimensional. |
|   | Clear module boundaries; equation/physics interfaces not case-specific; BCs extensible; mesh/geometry abstractions could generalize to 3-D; structure supports EOS/RANS/species extension. | | | |
|   | *Evidence:* Directory layout, class/interface design, absence of case-specific branches. | | | |
| 4 | **code.originality** Originality and license compliance | 0.15 | 4 | Submitted CMake builds the C++ modules with MPI, CGNS and METIS; no direct external CFD solver call was identified. A comprehensive source-similarity or license audit was not performed. |
|   | Solver core, time integration, and MPI communication are original; no copied/adapted/wrapped open-source CFD solver; no calls to existing solver executables; licenses respected. | | | |
|   | *Evidence:* Diff against known open-source CFD cores; provenance of large copied blocks; headers/comments. | | | |
| 5 | **code.hardcoding** No hard-coded case logic | 0.08 | 4.5 | main.cpp and CaseConfig.cpp take the case and mesh paths from inputs; CgnsReader.cpp reads mesh contents. Arbitrary unseen meshes were not run. |
|   | Cases drive behavior through JSON inputs; no branches on mesh filename or case id; production paths not special-cased per benchmark case. | | | |
|   | *Evidence:* grep for mesh/case names in source; input-driven parameters. | | | |
| 6 | **code.mpi** MPI correctness | 0.15 | 5 | Partitioner.cpp:127 uses METIS and HaloExchanger.cpp:23,33 exchanges neighbors; original np1/4/8 Re20 and M0.15 NACA force rows agree closely with the committed rank-study table. |
|   | Real neighbor halo exchange (isend/irecv or equivalent); global MPI reductions for residuals/forces; no full-state allgather or full-mesh replication during iterations; rank-independent output; partition diagnostics match the actual run. | | | |
|   | *Evidence:* Communication code review; metadata.json halo_exchange field; diagnostics. | | | |
| 7 | **code.robustness** Robustness and memory sanity | 0.08 | 4 | Residual.cpp:140–149 falls back for nonpositive face states and Driver.cpp checks finite transient residuals. Writers.cpp:381–409 stores no prior transient time level, yet Driver.cpp:588,621 uses BDF2 after restart. |
|   | Bounds/error checks on mesh data and allocations; no full-mesh replication per rank during solve; no unbounded recursion or O(n^2) surprises; handles mixed cells and degenerate geometry. | | | |
|   | *Evidence:* Defensive checks; memory footprint on np=8; crash tests. | | | |
| 8 | **code.testing** Testing and validation evidence | 0.10 | 4.5 | Isolated kernel ctest passed. report.tex:316–329 reports 2.022 and 2.013 reconstruction refinement orders; that test does not establish full solver residual convergence order. |
|   | Unit tests for geometry/flux/reconstruction where practical; smoke runs; serial-vs-parallel force comparison; validator run before submission. | | | |
|   | *Evidence:* tests/ dir, CI or run logs, validate_outputs.py output. | | | |
| 9 | **code.hygiene** Code hygiene | 0.07 | 4 | The source has separate modules and a documented build; curated raw outputs and generated PDF are absent at the result tip, retained as original workspace evidence. Full dependency audit was not performed. |
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

- Overall score (0-5): 4.45
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
