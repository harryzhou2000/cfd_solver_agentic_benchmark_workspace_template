# Code review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-08-15T07:01:58.021413+00:00

**Overall score: 3.16**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **code.build** Build system and reproducibility | 0.15 | 3 | CMake source is substantial and the terminal response claims a build, but no independent evaluator build was performed and the delivered immutable commit has no build artifact. |
|   | Clean documented build from a clean checkout; CMake or equivalent with configurable external dependency root (CFD_EXTERNALS_ROOT); no absolute machine paths; build works without manual case edits. | | | |
|   | *Evidence:* CMakeLists.txt and README build commands; try a clean configure/build. | | | |
| 2 | **code.cli** CLI and error handling | 0.10 | 4 | main.cpp implements solve --case/--output/--restart/--report-level and argparse error handling. |
|   | Required CLI form `solve --case <json> --output <dir>` works for every case; malformed inputs/missing meshes produce nonzero exit and clear messages; no hard-coded case names. | | | |
|   | *Evidence:* Argument parser, main(), error paths; spot-run bad input. | | | |
| 3 | **code.organization** Code organization and extensibility | 0.12 | 4 | Separate C++ modules cover case parsing, CGNS, geometry, partitioning, halo, reconstruction, flux, output, and solver logic. |
|   | Clear module boundaries; equation/physics interfaces not case-specific; BCs extensible; mesh/geometry abstractions could generalize to 3-D; structure supports EOS/RANS/species extension. | | | |
|   | *Evidence:* Directory layout, class/interface design, absence of case-specific branches. | | | |
| 4 | **code.originality** Originality and license compliance | 0.15 | 3 | No direct internal wrapper/external-executable evidence was found. Trust policy prohibits external-copy investigation. |
|   | Solver core, time integration, and MPI communication are original; no copied/adapted/wrapped open-source CFD solver; no calls to existing solver executables; licenses respected. | | | |
|   | *Evidence:* Diff against known open-source CFD cores; provenance of large copied blocks; headers/comments. | | | |
| 5 | **code.hardcoding** No hard-coded case logic | 0.08 | 4 | JSON case reader and generic case paths are present; no two-mesh-only branch was established. |
|   | Cases drive behavior through JSON inputs; no branches on mesh filename or case id; production paths not special-cased per benchmark case. | | | |
|   | *Evidence:* grep for mesh/case names in source; input-driven parameters. | | | |
| 6 | **code.mpi** MPI correctness | 0.15 | 4 | partition.cpp calls METIS_PartGraphKway and halo.cpp implements neighbor MPI_Isend/Irecv; no delivered rank-consistency demonstration. |
|   | Real neighbor halo exchange (isend/irecv or equivalent); global MPI reductions for residuals/forces; no full-state allgather or full-mesh replication during iterations; rank-independent output; partition diagnostics match the actual run. | | | |
|   | *Evidence:* Communication code review; metadata.json halo_exchange field; diagnostics. | | | |
| 7 | **code.robustness** Robustness and memory sanity | 0.08 | 1 | Six readable NACA outputs are marked failed, laminar histories become non-finite, and both cylinder final packages are absent. |
|   | Bounds/error checks on mesh data and allocations; no full-mesh replication per rank during solve; no unbounded recursion or O(n^2) surprises; handles mixed cells and degenerate geometry. | | | |
|   | *Evidence:* Defensive checks; memory footprint on np=8; crash tests. | | | |
| 8 | **code.testing** Testing and validation evidence | 0.10 | 1 | No passing final validator/test evidence: the official validator aborts on the first canonical package's failed metadata. |
|   | Unit tests for geometry/flux/reconstruction where practical; smoke runs; serial-vs-parallel force comparison; validator run before submission. | | | |
|   | *Evidence:* tests/ dir, CI or run logs, validate_outputs.py output. | | | |
| 9 | **code.hygiene** Code hygiene | 0.07 | 4 | The immutable source tree is reasonably modular and has no committed build/result/session bulk artifacts. |
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

- Overall score (0-5): 3.16
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
