# Code review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-29T09:39:22.984438+00:00

**Overall score: 3.94**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **code.build** Build system and reproducibility | 0.15 | 5 | Clean immutable source CMake build exited 0 in network-disabled cfd-bench with configured external root; run 02 also uses its documented CFD_HEADER_ROOT. No host credentials or network exposed. |
|   | Clean documented build from a clean checkout; CMake or equivalent with configurable external dependency root (CFD_EXTERNALS_ROOT); no absolute machine paths; build works without manual case edits. | | | |
|   | *Evidence:* CMakeLists.txt and README build commands; try a clean configure/build. | | | |
| 2 | **code.cli** CLI and error handling | 0.10 | 4 | Isolated --help succeeds; missing-case solve exits nonzero with explicit missing file error. README documents required solve --case --output form; full solve was not repeated. |
|   | Required CLI form `solve --case <json> --output <dir>` works for every case; malformed inputs/missing meshes produce nonzero exit and clear messages; no hard-coded case names. | | | |
|   | *Evidence:* Argument parser, main(), error paths; spot-run bad input. | | | |
| 3 | **code.organization** Code organization and extensibility | 0.12 | 4 | CMake builds separate case_config, mesh, solver and io sources; solver.cpp concentrates several numerical algorithms and debug controls. |
|   | Clear module boundaries; equation/physics interfaces not case-specific; BCs extensible; mesh/geometry abstractions could generalize to 3-D; structure supports EOS/RANS/species extension. | | | |
|   | *Evidence:* Directory layout, class/interface design, absence of case-specific branches. | | | |
| 4 | **code.originality** Originality and license compliance | 0.15 | 4 | Submitted C++ is built directly and links MPI/CGNS/METIS rather than an external solver executable. No external authorship or similarity search was performed under trust policy. |
|   | Solver core, time integration, and MPI communication are original; no copied/adapted/wrapped open-source CFD solver; no calls to existing solver executables; licenses respected. | | | |
|   | *Evidence:* Diff against known open-source CFD cores; provenance of large copied blocks; headers/comments. | | | |
| 5 | **code.hardcoding** No hard-coded case logic | 0.08 | 4 | case_config.cpp:67–82 resolves JSON mesh paths and :139–146 parses boundary types; no filename-only solve branches identified in inspected core. General arbitrary-case behavior not exhaustively tested. |
|   | Cases drive behavior through JSON inputs; no branches on mesh filename or case id; production paths not special-cased per benchmark case. | | | |
|   | *Evidence:* grep for mesh/case names in source; input-driven parameters. | | | |
| 6 | **code.mpi** MPI correctness | 0.15 | 4 | mesh.cpp:465 invokes METIS; solver.cpp:237–249 exchanges neighbor data and :851–854 uses global reductions. Submitted np2/np8 short restarts agree closely for cylinder and NACA; diagnostic histories lack CSV headers and full independent rank convergence is unverified. |
|   | Real neighbor halo exchange (isend/irecv or equivalent); global MPI reductions for residuals/forces; no full-state allgather or full-mesh replication during iterations; rank-independent output; partition diagnostics match the actual run. | | | |
|   | *Evidence:* Communication code review; metadata.json halo_exchange field; diagnostics. | | | |
| 7 | **code.robustness** Robustness and memory sanity | 0.08 | 3 | solver.cpp implements positivity fallback and numerical options; production needs small-cell first-order fallback and conservative controls. M0.15 steady result remains oscillatory; no resource-stress test was performed. |
|   | Bounds/error checks on mesh data and allocations; no full-mesh replication per rank during solve; no unbounded recursion or O(n^2) surprises; handles mixed cells and degenerate geometry. | | | |
|   | *Evidence:* Defensive checks; memory footprint on np=8; crash tests. | | | |
| 8 | **code.testing** Testing and validation evidence | 0.10 | 3 | Eight canonical packages and report pass structural validator, and rank-study continuations are supplied; source and CFD accuracy are not established by that validator. No independent full-order convergence study was verified. |
|   | Unit tests for geometry/flux/reconstruction where practical; smoke runs; serial-vs-parallel force comparison; validator run before submission. | | | |
|   | *Evidence:* tests/ dir, CI or run logs, validate_outputs.py output. | | | |
| 9 | **code.hygiene** Code hygiene | 0.07 | 3.5 | Clean isolated build succeeds with documented CFD_HEADER_ROOT and warnings. Large debug configuration collection was removed from curated tip except four report-documented inputs; generated data remain in preserved history. |
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

- Overall score (0-5): 3.94
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
