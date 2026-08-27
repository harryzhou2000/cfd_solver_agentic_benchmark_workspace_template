# Code Audit — cns2d 2-D Unstructured Compressible NS Solver

Read-only audit of `/workspace/solver/src/` (~8.4k lines). No solver file was modified,
no MPI run was launched, no process was killed. Numerical claims below were checked by
re-deriving the algebra and by standalone scalar reproductions of the kernels.

Note on concurrency: `src/solve/steady_driver.cpp` and `src/io/output_writer.cpp` were
being edited by another agent during this audit. Line numbers for `steady_driver.cpp`
refer to the version present at audit time (post-edit, `kForceWindow` present).

---

## (a) DEFINITE BUGS

### A1 — HLLC star-state energy has a sign error (severity: high in principle, low in practice)

`numerics/riemann_flux.cpp:139`

```cpp
Us[kRhoE] = coef * (e_total + (sStar - un) * (sStar - p / (rho * (s - un))));
```

The Toro star-state energy is

    (rho E)*_K = coef * ( E_K + (S* - u_K) * ( S* + p_K / (rho_K (S_K - u_K)) ) )

i.e. the term is `S* + p/(rho(S-u))`, **not** `S* - p/(rho(S-u))`. Verified numerically by
testing the energy Rankine–Hugoniot condition across the acoustic wave,
`S_K((rhoE)*_K - (rhoE)_K) = S*((rhoE)*_K + p*) - u_K((rhoE)_K + p_K)` with
`p* = p_K + rho_K(S_K-u_K)(S*-u_K)`:

| state | code (`-`) RH residual | corrected (`+`) RH residual |
|---|---|---|
| Sod-like L / R | -1.27e0 / -1.67e-1 | 1.1e-16 / 2.2e-16 |
| oblique+tangential L / R | 1.77e-1 / -6.89e-1 | 2.2e-16 / 4.4e-16 |
| strong reversed L / R | -6.07e0 / 1.56e-1 | -8.9e-16 / -1.1e-16 |

The corrected form satisfies the jump condition to machine precision for every test; the
shipped form violates it by O(1). Identical left/right states still give the exact flux
(the `(S*-u)` prefactor vanishes), so the consistency unit test cannot catch this.

**Fix:** change the `-` to `+`:
`Us[kRhoE] = coef * (e_total + (sStar - un) * (sStar + p / (rho * (s - un))));`

**Practical impact on the submitted results: essentially none.** HLLC is never selected in
production — `solve/solver_context.cpp:30` hardwires `kRoeEntropyFix` for every case, and the
only path into `hllcFlux` is the Roe-inadmissibility fallback at `riemann_flux.cpp:186-193,337-339`,
which requires `a2_hat <= 0`. All six completed runs report
`positivity_fallback_events: 0`, and the entropy fix keeps the Roe average admissible. So this
is a latent bug in a dormant code path rather than a defect in the reported numbers. It must
still be fixed, and the report must not claim HLLC is verified.

### A2 — `residual_reduction_orders` for the transient case mixes two different norms

`solve/transient_driver.cpp:239-244` (final) vs `:105,119` (initial)

`first_step_residual` is captured from `computeNorms(total_residual)` — the **total transient**
residual including the BDF2 physical-time term. The final value is
`computeNorms(spatial_residual)` — the **spatial-only** residual. Their ratio is reported as
`residual_reduction_orders` in `run_status.json`.

These are different quantities with different magnitudes (the time term dominates early), so
the logarithm of their ratio is not a convergence measure of anything. For a periodic vortex
street the honest statement is that no residual reduction is expected at all.

**Fix:** either compute the final norm from `total_residual` on the same basis as the initial
one, or set `residual_reduction_orders = 0.0` for transient runs and let the notes field explain
that statistical periodicity, not residual decay, is the convergence criterion. Do not compare
the two norms.

---

## (b) QUESTIONABLE — defensible, but must be disclosed in the report

### B1 — The Roe linear-wave dissipation floor is a real modification of the scheme

`numerics/riemann_flux.cpp:233-246`, `const Real linear_floor = 0.05 * max_speed;`

Applying Harten smoothing to the acoustic waves is standard Harten–Hyman. Applying a floor of
5 % of the local maximum wave speed to the **entropy/shear** eigenvalue `u·n` is not standard
Harten–Hyman — it is an added-dissipation cure for the carbuncle/checkerboard mode. The comment
is honest about this, and the magnitude is quantified: at a face with `u·n = 0` carrying a 2 %
density jump and a shear jump, the floor changes the flux from exactly
`[0, 1, 0, 0]` to `[-2.96e-4, 1.0, 2.13e-4, 7.42e-5]`.

Consistency is preserved (identical states give the exact physical flux to 0.0e0 for an
arbitrary rotated normal, verified). The concern is accuracy, not correctness: 5 % of
`|u·n|+a` on the linear waves adds numerical diffusion to contact discontinuities and to
boundary-layer shear. At Re 5000 this competes with physical viscosity.

**Recommendation:** state the coefficient explicitly in the report as a scheme parameter, and
support it with a sensitivity check (e.g. the Re 20 cylinder `C_D` at floor = 0.05 vs 0.01 vs 0)
so a reader can see the drag is not an artifact of the added dissipation. Note `0.05` is a
hand-tuned constant; it is applied uniformly to all cases, not per case.

### B2 — Plateau detection can declare "converged" below the requested residual target

`solve/steady_driver.cpp:319-329` (`res_change < 0.05 && forces_stationary` after step 2500,
1500-step residual window, 500-step force window, `kCdRelTol = 1e-3`)

This writes `convergence_status = "converged"` with fewer orders than requested. It is
defensible for a limiter-dominated steady state and the notes string does state the achieved
orders explicitly rather than the requested ones. Two cautions:

1. Flat residual + stationary drag is necessary but not sufficient for a converged steady
   state — a limit cycle in a small region can satisfy both. The report should show the
   residual history so a reader can judge.
2. As it happens no submitted run used this path: all six completed runs hit their target
   (notes read "residual reduced by N orders (target N)", 3.00–5.00 orders). So the loosened
   detector did not manufacture any reported convergence. Worth stating in the report.

A related earlier defect **has already been fixed** during this audit: the `max_steps`
exhaustion path now distinguishes `converged` from `not_converged` based on force
stationarity (`steady_driver.cpp:345-368`), and `completed` is gated on the status
(`:413-414`) instead of being set unconditionally.

### B3 — Venkatakrishnan `K = 5.0` is hardcoded, not case-derived

`solve/solver_context.cpp:36`, `scheme_.venkat_k = 5.0;`

Applied identically to all cases and overridable only via a debug CLI flag. `K=5` is a common
choice and `eps^2 = (K h)^3` matches the published form (`numerics/limiter.cpp:86-89`), so this
is not case tuning — but it is a free parameter that should be named in the report.

### B4 — Viscous spectral radius and LU-SGS viscous coupling use ad-hoc factors

`solve/steady_driver.cpp:122-123` passes `viscous_factor = 4.0`; `solve/lusgs.cpp:122` uses
`2.0 * mu / (rho * dist)`. These are conventional safety factors for the diffusive time-step
limit, not derived constants. They affect only the pseudo-time step and the implicit diagonal,
so they cannot bias the converged answer — but the 4.0 and 2.0 should be labelled as
stabilization parameters rather than presented as theory.

### B5 — First-order-in-time on the first transient step

`solve/transient_driver.cpp:69-73` uses BDF1 for step 1. Standard and correct for
self-starting; with 30000 steps the global effect is negligible. Mention it for completeness.

---

## (c) VERIFIED CLEAN

**No case-specific branching or hardcoded physics anywhere.** A search for
`naca|cylinder|case_id ==|m080|m200|re5000|re200|0012` across all of `src/` returns **zero**
matches. No hardcoded drag/lift values, no per-case constants. Everything comes from the case
JSON. This is the single most important negative result in the audit.

**No forbidden MPI patterns during iterations.** `MPI_Allgather`/`Allgatherv` appear only at
`parallel/distributed_mesh.cpp:375,384`, inside setup Stage 5 (building neighbour plans),
documented as setup-only. Iteration-time exchange is neighbour-scoped `Irecv`/`Isend`/`Waitall`
(`parallel/halo_exchange.cpp:37-64`) with receives posted before sends. All `MPI_Gather`/`Gatherv`
calls are in output paths only (`vtu_writer`, `surface_output`, `restart_io`, `output_writer`).
In-loop collectives are scalar `Allreduce` for norms (`solver_context.cpp:112-114`), forces
(`forces.cpp:105`, 6 doubles), and the linear-residual ratio (`lusgs.cpp:263`, 2 doubles) — all
legitimate.

**Roe flux algebra is correct.** Roe averages with sqrt-density weighting, `a2_hat = (γ-1)(h̃ - q̃²/2)`,
the four right eigenvectors, and jump coefficients `α₁,₃ = (Δp ∓ ρ̂âΔuₙ)/(2â²)`,
`α₂ = Δρ - Δp/â²`, shear `ρ̂Δu_t` (`riemann_flux.cpp:254-314`) all match the standard
eigenstructure. Consistency verified: identical L/R states reproduce the exact physical flux to
**0.0e0** for a rotated normal (θ=0.7 rad). Rotational treatment via `t = (-n_y, n_x)` is correct.

**Viscous flux matches the compressible NS equations.** `numerics/viscous_flux.cpp:8-25`:
`τ_xx = 2μu_x - (2/3)μ(u_x+v_y)`, `τ_yy = 2μv_y - (2/3)μ(u_x+v_y)`, `τ_xy = μ(u_y+v_x)` —
the -2/3 bulk term is present and correct in 2-D. Energy flux `u·(τ·n) - q_n` with
`q_n = -k∇T·n` and `k = μc_p/Pr` (`perfect_gas.h:73`), Pr = 0.72 from the case file. Sign
convention consistent: `residual.cpp:218` does `flux[k] -= fv[k]`, correct given `R = -Σ(F_inv - F_visc)·n`.

**Face-gradient averaging is corrected, not a plain average.** `residual.cpp:185-194`: the
averaged cell gradients are corrected along the cell-to-cell direction so the face gradient
reproduces the actual cell-value difference. This removes the odd-even decoupling that a plain
arithmetic average suffers on stretched boundary-layer cells. The wall-face equivalent
(`:152-168`) corrects against the imposed wall state and, for an adiabatic wall, projects out
the normal temperature gradient to enforce `∂T/∂n = 0` exactly.

**LSQ gradient is a genuine weighted normal-equations solve.** `distributed_mesh.cpp:623-710`:
inverse-distance weights `w = 1/|Δx|`, 2×2 weighted normal matrix, explicit determinant with a
rank-deficiency guard that regularises rather than producing an unbounded gradient, inverse
folded into per-neighbour weight vectors. Exact for a linear field by construction. Boundary
faces contribute at the face centroid using the **physical** boundary state
(`gradients.cpp:39-44`) — correctly avoiding the doubled wall-normal gradient a mirrored ghost
would give.

**Limiter form is correct.** `limiter.cpp:116-120`: `φ = (y²+2y+ε²/d²)/(y²+y+2+ε²/d²)` with
`y = bound/d` is the published Venkatakrishnan function; `ε² = (Kh)³` (`:86-89`); clamped to
[0,1] and reduced by `min` over faces, so it can neither exceed 1 nor go negative. Barth–Jespersen
is `min(1, bound/d)`. Both use the neighbour min/max envelope including boundary states.

**GHOST vs FACE boundary-state distinction is applied consistently and correctly.**
`boundary_conditions.cpp:148-172` provides two entry points. The Riemann solver gets the mirrored
ghost (`residual.cpp:44-47`); gradients, limiter, viscous flux, forces and surface output get the
physical face state. Slip wall reflects normal momentum with energy unchanged
(`boundary_conditions.cpp:10-27`) so the wall flux carries only pressure; no-slip ghost reverses
the full velocity so the face average is exactly zero (`:51-60`); the adiabatic wall face state
has zero velocity, interior pressure and interior temperature (`:62-76`).

**Farfield characteristic BC branches are correct.** `boundary_conditions.cpp:100-145`:
supersonic outflow takes the interior, supersonic inflow the freestream — correct for the M=2.0
cases (all characteristics in at inflow, out at outflow). Subsonic uses
`R± = uₙ ± 2a/(γ-1)` with entropy and tangential velocity taken from the upwind side, plus
guards against non-positive `a_b`, `ρ_b`, `p_b`.

**Force integration signs are right.** `forces.cpp:32-52,89-90`: with `n` fluid-outward, pressure
force is `+(p-p_inf)n·dA` (referencing `p_inf` so uniform ambient pressure contributes nothing on
a closed body) and viscous force is `-shear·dA`. Skin friction uses tangential traction only
(`viscous_flux.cpp:29-41` removes the normal component), matching the contract requirement.
Moment is `r × F` about `moment_center`, counter-clockwise positive. Nondimensionalisation by
`q_inf · ref_area` with lift/drag projected on freestream-aligned axes. Each wall face is counted
once globally via the `f.left >= numOwned()` owned-once rule (`:27`).

**Residual assembly is conservative.** `residual.cpp:224-243`: each face flux computed once and
scattered with opposite signs, guarded so only owned cells accumulate. Telescopes exactly.

**LU-SGS is a real forward/backward SGS solve with an exact matrix-free Jacobian-vector
product.** `lusgs.cpp:27-67` differentiates the normal flux correctly (verified structurally;
the main agent separately measured 1.7e-8 against central FD). Sweeps are genuine forward-ascending
then backward-descending using updated neighbours (`:178-211`). **No diagonal inflation** — the
diagonal is `V/Δτ + 0.5Σ|λ|A + visc` with no β>1 fudge factor (`:80-91`), which matters because
inflating it produces fake inner-loop convergence. The inner-loop exit criterion uses
`linearResidualRatio` (`:215-265`), a true defect norm `||rhs - (D+O)dU||/||rhs||` computed with the
same operator the sweeps use — not a proxy. The `reset_increment` parameter (`:148`) correctly
preserves the increment when the driver adds sweeps.

**The transient driver is a true dual-time BDF2.** `transient_driver.cpp:64` is the outer
physical-time loop; `:83` the inner nonlinear loop. `U_n`/`U_nm1` are read-only inside the inner
loop and shifted **only after** it exits (`:162-165`) — genuinely frozen histories. The inner
residual is the full transient residual `R* = R_spatial - V(a₀Uⁿ⁺¹+a₁Uⁿ+a₂Uⁿ⁻¹)/Δt` (`:99-102`),
the ratio is measured on it (`:121`), and the diagonal carries both pseudo-time and physical-time
terms (`:141-142`), making it Newton-like rather than a pseudo-time march with a source. Forces
are sampled by physical time (`:204`). Under-convergence is reported as `failed`, not
`statistically_periodic` (`:270-279`) — the honest choice.

**Residual norms are consistent across ranks.** `solver_context.cpp:83-127`: volume-weighted
`sqrt(Σ(r/V)²V / ΣV)`, reduced with `MPI_Allreduce` over sums and a MAX for L∞. Rank-count
independent by construction; the same reduction is used for every reported residual. The
volume normalisation makes the norm a rate-of-change of the cell average — a legitimate,
documented convention, not a trick to shrink the number (it is applied identically to the
initial residual, so the reported *ratio* is unaffected).

**Contract compliance — headers are byte-exact.** `residuals.csv` (`output_writer.cpp:74`),
`forces.csv` (`:82`) and `surface.csv` (`:119`) match `REQUIRED_*` in `validate_outputs.py:19-58`
exactly, in order. Extra surface columns are correctly diverted to the companion
`surface_cell_center.csv` (`:136-149`) so the required header stays exact — this is the right
resolution of the contract's "add explicit columns or documentation" clause.
`partition_diagnostics.csv` header (`:195-196`) matches the contract. All `REQUIRED_METADATA` and
`REQUIRED_STATUS` keys are written (`:271-347`, `:351-366`), including the eight Re200 inner-solve
statistics and `true_bdf2_inner_loop`. `full_state/mesh_replication_during_iterations` are
hardcoded `false` — and that is truthful per the MPI finding above.

**.vtu format is valid.** `vtu_writer.cpp`: single `<Piece>` (`:151`), ascii only, **cumulative
end** offsets (`:171-177`, offset incremented before writing — the common off-by-one is absent),
3-component points with z=0 (`:155-157`), types 5/9 (`:181`), and all six contract-required cell
arrays plus `rank` (`:197-214`). Cells are sorted by global id (`:114`) so the file is
rank-count independent apart from the `rank` array itself. Node dedup uses exact bit comparison,
which is sound because shared nodes are bit-identical.

**Originality: no evidence of copying.** A search for `SU2|OpenFOAM|CFluid|CNumerics|CSolver|Foam::|volScalarField|GNU General Public|Copyright`
returns **zero** matches. The code has a consistent idiosyncratic style throughout —
`cns2d` namespace, `kDim`/`kNumVars` compile-time constants, `Index`/`GlobalIndex`/`Real` typedefs,
`StencilKind` enums, precomputed folded LSQ weight vectors, the GHOST/FACE dual boundary-state
API, and long explanatory comments that argue for specific design choices. SU2 would show
`CNumerics`/`CConfig` class hierarchies; OpenFOAM would show `Foam::` and field-algebra
templates. Neither is present. The architecture (compile-time dimension constants, no
case_id branching, single templated-by-constant residual path) is coherent and self-consistent
in a way copied fragments are not.

---

## Priority summary

| # | Finding | Severity | Affects submitted numbers? |
|---|---|---|---|
| A1 | HLLC star-state energy sign | High (latent) | No — HLLC never engages, 0 fallbacks |
| A2 | Transient `residual_reduction_orders` mixes norms | Medium | Yes — one metadata field for re200 |
| B1 | Roe 5 % linear-wave floor | Disclose | Yes — small added dissipation; quantify |
| B2 | Plateau detector | Disclose | No — all runs hit target |
| B3 | `venkat_k = 5.0` hardcoded | Disclose | Minor |
| B4 | Viscous factors 4.0 / 2.0 | Disclose | No — stabilization only |
| B5 | BDF1 first step | Mention | Negligible |

Recommended before submission: fix A1 (one character), fix A2, and add B1's coefficient plus a
sensitivity number to the report's discretization section.
