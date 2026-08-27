Goal complete. Final state of the benchmark:

**Deliverables (all committed on branch `solver/kimi-k3-attempt1`, benchmark repo untouched):**
- `solver/` — original C++17/MPI cell-centered FV compressible Navier–Stokes solver: CGNS multi-zone reader with union-find node merge, METIS k-way partitioning, one ghost layer with neighbor-scoped Isend/Irecv exchange, LSQ second-order reconstruction (interior stencils) + Barth–Jespersen limiter, Rusanov/HLLC fluxes with a Rieper-style low-Mach fix, corrected-central viscous fluxes, LU-SGS pseudo-time for steady cases and BDF2 dual-time (frozen histories, limiter frozen in the inner solve) for the transient case.
- All 8 production cases run at np=8 with the exact case-file parameters; every result directory passes the examiner's `validate_outputs.py`, as does `solver/report` (verified on the final state just now).
- Rank-count studies (np=1/2/4/8) for one NACA and one cylinder case, with force-consistency tables.
- 32-page LaTeX report + PDF with mesh/case/numerics/partition/run-status/force-split tables, labeled and text-referenced figures for every case, and an honest limitations section.

**Key physics results:** NACA stagnation cp matches compressible theory (1.026 at M=0.15, 1.18 vs 1.17 at M=0.8); m200 laminar hit its 3-order residual target; Re20 cylinder cd=1.99 vs Tritton ≈2.0–2.1; Re200 vortex street with St=0.178–0.189, cl=0.00±0.41, cd=1.20±0.04, 100% inner-solve converged fraction over all 30000 steps.

The decisive fixes this session: limiter freezing inside the BDF2 inner loop (broke an LU-SGS limit cycle that was failing 35% of steps), excluding boundary mirror ghosts from the LSQ gradient fit (cured a stationary wall-pressure sawtooth that had biased cylinder drag ~35% high), and a VTU triangulation-ordering bug in the plotting tools that had been rendering smooth solutions as grid-scale noise.

Goal usage: 4,330,256 tokens over ~7.1 hours.