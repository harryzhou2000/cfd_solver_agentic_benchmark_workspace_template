I've reached the end of the principled approaches. Here's my honest final report.

## Summary

I picked up the 8/8 pursuit and implemented the one remaining principled fix: the **AUSM⁺-up low-Mach flux** (Liou 2006). This was a genuine improvement to the solver, but it confirmed the mesh-resolution deadlock is fundamental, not flux-limited.

### What I built (committed to `solver/attempt-1`)

**AUSM⁺-up flux** ([src/physics.cpp](/workspace/solver/src/physics.cpp:46)) — the correct all-speed form, cross-confirmed against SU2/myFoam/CATO reference implementations: enthalpy-based interface sound speed, M₄ Mach splitting (β=1/8), P₅ pressure splitting (α=3/16 all-speed), f_a=M₀(2−M₀) scaling, and the two coupled low-Mach terms (M_p: Δp→mass flux, p_u: Δu_n→pressure) that pstab lacked.

**The key structural fix** — I linearized the AUSM⁺-up coupling into the scalar LU-SGS implicit Jacobian ([src/implicit.cpp](/workspace/solver/src/implicit.cpp:39)). Without this, the explicit-only coupling (like pstab) can't stabilize the inner solve and the state blows up at t=2-3. With it, the run is stable and the inner solve converges.

**Supporting infrastructure** — pressure-only 4th-order AV (damps ρ/ρE only, preserving the shedding shear), spatially-varying AV (tanh scaling, precomputed per-face for speed), a sponge layer, and a time-dependent cap ramp — all env-gated, off by default, no effect on the 7 steady cases.

### The breakthrough that wasn't sustainable

The AUSM⁺-up flux **does excite the shedding instability** (cl amplitude reaches 0.07–0.22 with the correct Strouhal period ~5) where the committed Rusanov flux gives flat cl ≈ 0.002. This is real progress — it proves the flux isn't the bottleneck.

But a **mesh-resolution deadlock** prevents sustained shedding: the weak dissipation needed for cl > 0.05 also allows a numerical blowup at t ≈ 10, when the shedding vortices convect ~10 diameters into the coarse downstream mesh (CylinderB1 extends to r=200 with rapidly increasing cell spacing). I confirmed this across an extensive parameter sweep — every stable configuration gives cl < 0.05, and every configuration with cl > 0.05 blows up. The blowup is amplitude-independent (happens regardless of cl value at t=10) and mesh-location-specific (the far-field coarse cells).

### Final state

7/8 cases pass the validator (unchanged); Re200 reaches the full t=300 horizon and is honestly marked `failed` (cl amplitude < 0.05 with both the Rusanov and AUSM⁺-up fluxes). The report, README, and metadata all document the AUSM⁺-up implementation and the mesh-resolution analysis honestly.

Reaching 8/8 would require an external-state change I can't make in-scope: a finer wake mesh (the benchmark mesh is read-only) or a block-4×4 implicit refactor (a major undertaking with uncertain payoff given the deadlock is spatial, not implicit-related). I've marked the goal blocked on this basis, as the same blocker has now recurred across 4+ goal turns with all principled approaches exhausted.