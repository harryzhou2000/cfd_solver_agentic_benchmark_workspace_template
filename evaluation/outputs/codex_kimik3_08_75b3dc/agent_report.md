# Agent Evaluation Report — codex/kimik3/08

**80/100; DQ=true (trigger 7).** Two laminar NACA results are marked converged while their force histories remain materially unsteady. M0.15 at step 2888 has Cd 0.005318–0.009276 over the final 500 samples, compared with the reported final Cd 0.00775. M0.80 at step 25000 has consecutive final 1000-step mean Cl values 0.03071, 0.02684, 0.02646, 0.02248 and 0.02114. Its final 5000-step Cd span is about 2.5%, rather than the reported under 1%. These findings concern forces; the recorded residual reductions are not disputed.

The contestant performed a real 8000-step M0.15 restart continuation from the submitted final restart. Its last 1000 Cd samples span 0.007607–0.007870, bracketing the reported final value. The complete continuation does not remain in that band: its last 5000 span 0.006761–0.007870 and the first recorded Cd is 0.14855. This later evidence supports the eventual drag level but does not make the submitted final state force-stable. The report's wording overstates the duration of the narrow band.

The accepted 37-page report contains readable case-specific histories and fields. Re20 Cd 0.889 is far below the approximately 2.0 benchmark range, a low-Mach limitation the contestant discloses. Re200 exhibits resolved late lift and drag; its submitted force history for t≥200 gives mean Cd 0.99540 and St≈0.2025 from 20 upward crossings. Individual case scores, independent of the 100-point rubric, are 2.5, 4, 3.5, 1.5, 1.5, 4, 2.5 and 3.5 in canonical order.

Immutable source implements METIS, neighbor state/reconstruction/correction halos, finite-volume fluxes, limiting, viscous traction projection and SGS/BDF2. The transient inner residual is tested with a frozen limiter and is not checked again after refreshing it, so the fully refreshed miss fraction is unknown. Saved production logs record second-order activation at step 300. The Re20 rank comparison consists of failed pre-asymptotic runs: fixed-step Cd is 0.851695, 0.934107 and 0.914190 at np2/4/8, a roughly 9.7% np2–4 spread rather than the report's few percent; it is not an order-one mismatch. The comparison script names `fv2d.v2`, while production names `fv2d`, and historical binary equivalence is unproven.

Original contestant commit `97128af` tracks all eight force, residual, status and metadata files, each matching the workspace bytes; its source matches curated tip `80fdf699daa4729355996f603d61ebbc23819762`. The exact historical executable is unbound. The accepted PDF SHA256 is `4f6862073ada8f13d871f9d57e4fb0f4cca0f01f4a71c09c62dd4997a873c5c1`. As a provenance check the submitted plotting code reproduced the original Re20 force, residual and Mach-zoom images pixel-identically from tracked workspace inputs. Other figure bindings remain qualified. An isolated network-disabled clean build passed; CTest reports no registered tests. No solver case was rerun.

All ten rubric sections, 27 review points, eight cases and DQ1–13 were examined; precise evidence is in `agent_scores.json` and the calibration ledger. The validator is structural and cannot establish physical convergence. No external authorship investigation was performed under the trust policy.

SHA-256 `run_identity.json`: `2947c446d0239093f21a457db72a1a9310e5aa9497c3e3ff89b2869abc2653c1`.

SHA-256 `contestant_final_response.md`: `8629c6b48726bc8ef4363cce6fb60d2664ada9aea8e7f42d6b397b745b747296`.

These are unindexed sidecars; `cfdeval check` does not validate them.

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/kimik3/08` branch `codex/kimik3/08` commit `80fdf699daa4729355996f603d61ebbc23819762`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-27T03:49:27.177000+00:00 → 2026-08-27T15:15:55.698000+00:00
- Time: goal 35833.0s, wall 41188.5s
- Tokens: 232,705,274 (main 227,016,604 / subagents 5,688,670); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 4 threads / opencode 0 sessions
- Window: 2026-08-27T03:49:27.177000+00:00 → 2026-08-27T15:15:55.698000+00:00
- Idle excluded: 7 gaps, 9918s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 232,705,274, cache hit 0.9783, tools 1084

### Structural result checks (auto)

- Required cases: 8; found: 120; missing: none
- Figure manifest: {"manifest_present": true, "entries": 58, "missing_figures": [], "missing_sources": ["forces.csv", "field_final.vtk", "field_final.vtk", "field_final.vtk", "field_final.vtk", "residuals.csv", "surface.csv", "fields/field_t0300.000.vtk", "forces.csv", "field_final.vtk", "field_final.vtk", "field_final.vtk", "field_final.vtk", "residuals.csv", "surface.csv", "field_final.vtk", "forces.csv", "field_f
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic
- `naca0012_m015_inviscid`: missing=[] completed=True status=converged
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m080_inviscid`: missing=[] completed=True status=converged
- `naca0012_m080_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m200_inviscid`: missing=[] completed=True status=converged
- `naca0012_m200_laminar_re5000`: missing=[] completed=True status=converged
- `np2`: missing=[] completed=True status=converged
- `np4`: missing=[] completed=True status=converged
- `np8`: missing=[] completed=True status=converged
- `np1`: missing=[] completed=False status=failed


## Scope and limitations

The exact historical executable and every figure-to-data binding are not individually verified. No Re200 or unsteady solver rerun was performed.
