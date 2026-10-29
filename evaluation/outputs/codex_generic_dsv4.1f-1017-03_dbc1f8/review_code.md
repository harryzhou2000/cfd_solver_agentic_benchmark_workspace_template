# Code review scorecard

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-29T09:30:59.046532+00:00

**Overall score: 3.87**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **code.build** Build system and reproducibility | 0.15 | 5 | Clean immutable source CMake build exited 0 in network-disabled cfd-bench with configured external root; run 02 also uses its documented CFD_HEADER_ROOT. No host credentials or network exposed. |
|   | Clean documented build from a clean checkout; CMake or equivalent with configurable external dependency root (CFD_EXTERNALS_ROOT); no absolute machine paths; build works without manual case edits. | | | |
|   | *Evidence:* CMakeLists.txt and README build commands; try a clean configure/build. | | | |
| 2 | **code.cli** CLI and error handling | 0.10 | 3 | Submitted run metadata and scripts record the required solve --case --output form; direct isolated invocation without a case exits nonzero with ERROR: --case is required. A full documented solve was not rerun because submitted results exist; standalone --help is rejected as missing case. Other malformed-input modes remain unverified. |
|   | Required CLI form `solve --case <json> --output <dir>` works for every case; malformed inputs/missing meshes produce nonzero exit and clear messages; no hard-coded case names. | | | |
|   | *Evidence:* Argument parser, main(), error paths; spot-run bad input. | | | |
| 3 | **code.organization** Code organization and extensibility | 0.12 | 4 | CMakeLists.txt:28–37 separates configuration, reader, global/local mesh, partition, solver and output sources. Numerical physics remains largely concentrated in solver.cpp; documented reduced-slope and 2-D assumptions limit generalization. |
|   | Clear module boundaries; equation/physics interfaces not case-specific; BCs extensible; mesh/geometry abstractions could generalize to 3-D; structure supports EOS/RANS/species extension. | | | |
|   | *Evidence:* Directory layout, class/interface design, absence of case-specific branches. | | | |
| 4 | **code.originality** Originality and license compliance | 0.15 | 4 | CMakeLists.txt builds the submitted C++ source and links MPI/CGNS/METIS/HDF5/zlib; direct source inspection found no invocation of an external CFD executable. Per trust policy no authorship or external similarity search was performed. |
|   | Solver core, time integration, and MPI communication are original; no copied/adapted/wrapped open-source CFD solver; no calls to existing solver executables; licenses respected. | | | |
|   | *Evidence:* Diff against known open-source CFD cores; provenance of large copied blocks; headers/comments. | | | |
| 5 | **code.hardcoding** No hard-coded case logic | 0.08 | 4 | case_config.cpp:70 resolves the supplied mesh path and :123–124 parses boundary configuration; solver accepts configuration rather than two hard-coded mesh filenames. General arbitrary mesh compatibility remains unverified. |
|   | Cases drive behavior through JSON inputs; no branches on mesh filename or case id; production paths not special-cased per benchmark case. | | | |
|   | *Evidence:* grep for mesh/case names in source; input-driven parameters. | | | |
| 6 | **code.mpi** MPI correctness | 0.15 | 4 | global_mesh.cpp:296 calls METIS; solver.cpp:231–249 and :329–349 exchange state and gradients with neighbors, :913–915 and :1110 reduce residuals/forces. Submitted np=1/2/4/8 short continuation histories agree within 1e-9 in Cd for a cylinder and a NACA case; report.tex:248–266 documents the shared-restart test and 4-CPU quota. It is not an independent full convergence comparison. |
|   | Real neighbor halo exchange (isend/irecv or equivalent); global MPI reductions for residuals/forces; no full-state allgather or full-mesh replication during iterations; rank-independent output; partition diagnostics match the actual run. | | | |
|   | *Evidence:* Communication code review; metadata.json halo_exchange field; diagnostics. | | | |
| 7 | **code.robustness** Robustness and memory sanity | 0.08 | 3 | solver.cpp:524–545 and :946–969 apply reconstructed-state and update positivity safeguards. Production uses a first-order fallback and reduced slopes for stability, disclosed in report.tex:554–567; malformed-input and resource-stress coverage remains incomplete. |
|   | Bounds/error checks on mesh data and allocations; no full-mesh replication per rank during solve; no unbounded recursion or O(n^2) surprises; handles mixed cells and degenerate geometry. | | | |
|   | *Evidence:* Defensive checks; memory footprint on np=8; crash tests. | | | |
| 8 | **code.testing** Testing and validation evidence | 0.10 | 3 | report.tex:268–287 describes uniform-field, linear-gradient and rank continuation checks; submitted eight result packages pass structural validation. Independent order-of-accuracy and full reruns were not performed. |
|   | Unit tests for geometry/flux/reconstruction where practical; smoke runs; serial-vs-parallel force comparison; validator run before submission. | | | |
|   | *Evidence:* tests/ dir, CI or run logs, validate_outputs.py output. | | | |
| 9 | **code.hygiene** Code hygiene | 0.07 | 4 | Clean build succeeds with warnings for an unused function; CMakeLists.txt builds separated source units. Curation excluded generated artifacts from the result tip; preserved earlier contestant history retains those blobs. |
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

- Overall score (0-5): 3.87
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:
