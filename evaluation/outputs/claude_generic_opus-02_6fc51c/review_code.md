# Code review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-29T10:56:39.161083+00:00

**Overall score: 3.34**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **code.build** Build system and reproducibility | 0.15 | 4 | Immutable CMake build passes in network-disabled container with CFD_EXTERNALS_ROOT and JSON_ROOT configured; no complete documented build procedure was identified. |
|   | Clean documented build from a clean checkout; CMake or equivalent with configurable external dependency root (CFD_EXTERNALS_ROOT); no absolute machine paths; build works without manual case edits. | | | |
|   | *Evidence:* CMakeLists.txt and README build commands; try a clean configure/build. | | | |
| 2 | **code.cli** CLI and error handling | 0.10 | 2 | main.cpp:108–110 parses case and output only, ignores --restart and report-level; :195–198 writes only two-line restart placeholders. Missing-file invocation exits nonzero with clear diagnostic. |
|   | Required CLI form `solve --case <json> --output <dir>` works for every case; malformed inputs/missing meshes produce nonzero exit and clear messages; no hard-coded case names. | | | |
|   | *Evidence:* Argument parser, main(), error paths; spot-run bad input. | | | |
| 3 | **code.organization** Code organization and extensibility | 0.12 | 3 | CMake builds separate mesh, solver, io and main files, but core physics/time stepping is concentrated in solver.cpp and restart logic is not implemented. |
|   | Clear module boundaries; equation/physics interfaces not case-specific; BCs extensible; mesh/geometry abstractions could generalize to 3-D; structure supports EOS/RANS/species extension. | | | |
|   | *Evidence:* Directory layout, class/interface design, absence of case-specific branches. | | | |
| 4 | **code.originality** Originality and license compliance | 0.15 | 4 | CMake builds submitted C++ with MPI/CGNS/METIS; no direct internal external-solver wrapper found. Per trust policy no external similarity or authorship investigation. |
|   | Solver core, time integration, and MPI communication are original; no copied/adapted/wrapped open-source CFD solver; no calls to existing solver executables; licenses respected. | | | |
|   | *Evidence:* Diff against known open-source CFD cores; provenance of large copied blocks; headers/comments. | | | |
| 5 | **code.hardcoding** No hard-coded case logic | 0.08 | 4 | Case parser and CGNS path are configuration driven; actual meshes and case inputs were used. General independent arbitrary-case tests were not run. |
|   | Cases drive behavior through JSON inputs; no branches on mesh filename or case id; production paths not special-cased per benchmark case. | | | |
|   | *Evidence:* grep for mesh/case names in source; input-driven parameters. | | | |
| 6 | **code.mpi** MPI correctness | 0.15 | 4 | mesh.cpp:484 METIS; solver.cpp:131–172 neighbor halo exchange and :616–620,689 global reductions; submitted np2 and np8 NACA/Re20 runs agree in Cd. Global mesh is broadcast temporarily during setup then destroyed on return, not retained for iteration. |
|   | Real neighbor halo exchange (isend/irecv or equivalent); global MPI reductions for residuals/forces; no full-state allgather or full-mesh replication during iterations; rank-independent output; partition diagnostics match the actual run. | | | |
|   | *Evidence:* Communication code review; metadata.json halo_exchange field; diagnostics. | | | |
| 7 | **code.robustness** Robustness and memory sanity | 0.08 | 2.5 | First-order steady fallback and frozen transient gradients stabilize the supplied cases, but restart output lacks state and unrecognized CLI options are ignored; no independent stress test. |
|   | Bounds/error checks on mesh data and allocations; no full-mesh replication per rank during solve; no unbounded recursion or O(n^2) surprises; handles mixed cells and degenerate geometry. | | | |
|   | *Evidence:* Defensive checks; memory footprint on np=8; crash tests. | | | |
| 8 | **code.testing** Testing and validation evidence | 0.10 | 2.5 | Eight final directories/report pass structural validator and np2/np8 runs are submitted; no reliable restart test, clean unit-test suite or independent order validation was established. |
|   | Unit tests for geometry/flux/reconstruction where practical; smoke runs; serial-vs-parallel force comparison; validator run before submission. | | | |
|   | *Evidence:* tests/ dir, CI or run logs, validate_outputs.py output. | | | |
| 9 | **code.hygiene** Code hygiene | 0.07 | 3 | Build succeeds with configured dependency roots, but no complete build instructions were found and required restart writer is a two-line stub. Generated result paths were curated from the tip while preserved in history. |
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

- Overall score (0-5): 3.34
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
