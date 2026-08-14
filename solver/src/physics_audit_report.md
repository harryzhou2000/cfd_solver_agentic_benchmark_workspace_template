# Physics Code Audit: /workspace/solver

## 7 Hypothesis Verification

| # | Hypothesis | Verdict | File:Line | Evidence |
|---|---|---|---|---|
| 1 | assemble_residual: +F·n left, −F·n right | **PROVEN** | physics.cpp:486-490 | Interior face: `flux = inviscid_face_flux(UL,UR,lf.n,...)`; if `lf.right==c` then `flux *= -1.0`. Face normal `n` is oriented left→right (global_mesh.cpp:319-328, distributed_mesh.hpp:19). |
| 2 | Wall force: traction = −p n_b + τ·n_b, n_b = −n | **PROVEN** | physics.cpp:421-425, 458-461, 534-549 | `boundary_face_flux` sets `nb=−n, tb={nb.y,−nb.x}, p_wall = p_recon > 0`. `accumulate_wall_forces` uses `f_p = n_b·(−p)`, `f_v = τ·n_b`, total = f_p+f_v. Drag/lift decomposed via freestream direction. |
| 3 | Viscous wall flux: F_v·n = τ·n, du/dn = −u_cell/d, adiabatic q=0 | **PROVEN** | physics.cpp:442-460 | Wall gradient: `gu_w = gu + (−u_cell/dn − gu·n)·n` → du/dn = −u_cell/dn. `res.viscous = {0, −τ·n_b.x, −τ·n_b.y, 0}` = τ·n. Energy component zero (adiabatic, u_wall=0). |
| 4 | Venkatakrishnan limiter formula | **PROVEN** | physics.cpp:79-127 | `eps² = (K√V)³`. δ>0: `φ = ( (Δ⁺²+ε²)δ + 2δ²Δ⁺ ) / ( δ(Δ⁺²+2δ²+δΔ⁺+ε²) )`. δ<0: symmetric with Δ⁻ = q_min−q_c. Standard VK formula per Blazek eq. 5.64. |
| 5 | LSQ gradient: A = Σ w d dᵀ, w = 1/|d|², neighbor j = (left==c ? right : left) | **PROVEN** | physics.cpp:33-66 | `w = 1/(|d|²+1e-24)`, `j = (lf.left==c) ? lf.right : lf.left`, `j<0 || j==c` skipped. RHS `b = Σ w d Δq`. Solve via `solve_2x2` with Tikhonov reg. |
| 6 | Roe flux: wave strengths α, right eigenvectors, Harten-Yee entropy fix, F = 0.5(FL+FR) − 0.5 Σ α ψ(λ) r | **PROVEN** | physics.cpp:201-281 | α₁=(Δp−ρ̄āΔuₙ)/2ā², α₂=Δρ−Δp/ā², α₃=ρ̄Δuₜ, α₄=(Δp+ρ̄āΔuₙ)/2ā². Eigenvectors r1‑r4 in Cartesian. `ψ(λ)=|λ| if |λ|≥δ else (λ²+δ²)/2δ`. `δ = 0.1·max(|un|+a)`. |
| 7 | Farfield characteristic BC: R⁺ from interior, R⁻ from freestream, entropy/tangential from outflow or inflow, supersonic branches | **PROVEN** | physics.cpp:365-416 | `R⁺ = vₙ+2a/(γ-1)` from interior, `R⁻ = vₙ−2a/(γ-1)` from freestream. `vₙ = ½(R⁺+R⁻)`, `a = ¼(γ-1)(R⁺−R⁻)`. Outflow (vₙ≥0): s, vₜ from interior. Inflow: from freestream. Supersonic: vₙ∞ < −a∞ → q_b=q∞; vₙ∞ ≥ a∞ → q_b=q_c. |

## Additional Findings

### F1. Temperature gradient missing 1/R factor (latent)
- **File**: physics.cpp:308
- **Code**: `gT = (gp − grho·T) / rho` (missing ÷ R)
- **Explanation**: T = p/(ρR). True gradient: ∇T = (∇p − T∇ρ)/(ρR). The code omits the 1/R factor. When R=1 (default and all case files), the bug is invisible. With R≠1, heat flux is wrong by factor R.
- **Suggested fix**: Change to `gT = (gp − grho * T) / (rho * gas.R)`.
- **Severity**: **SUSPICIOUS** (latent, all current cases use R=1)

### F2. Cell center = vertex average, not area centroid
- **File**: global_mesh.cpp:224
- **Code**: `cell.center = polygon_center(pts)` (arithmetic mean of corners)
- **Explanation**: For skewed unstructured cells, vertex-average ≠ centroid. This degrades LSQ gradient accuracy, reconstruction, and CFL-based time-step. The error is largest in high-aspect-ratio cells near the airfoil/cylinder surface.
- **Suggested fix**: Compute the true centroid: `polygon_centroid(pts)` (area-weighted average of triangle decomposition).
- **Severity**: **SUSPICIOUS** (accuracy degradation, contributes to spurious drag)

### F3. Limiter uses neighbor-center distance instead of face distance
- **File**: physics.cpp:107-109
- **Code**: `delta = grad·(cell_center[j] − cell_center[c])` (neighbor center, not face center)
- **Explanation**: The VK limiter should use the reconstruction increment at the face center: `delta = grad·(face_center − cell_center[c])`. Using the neighbor-center distance systematically overestimates the increment magnitude, making the limiter more aggressive. On highly stretched meshes this can cause asymmetry.
- **Suggested fix**: Replace with `d = face.center − cell_center[c]` in the limiter.
- **Severity**: **SUSPICIOUS** (inconsistency, over-limiting)

### F4. compute_lsq_gradients in physics.cpp is dead code
- **File**: physics.cpp:29-67
- **Explanation**: `cfd::compute_lsq_gradients` is never called. `Solver::compute_gradients()` (solver.cpp:137-170) has its own inline LSQ implementation. Dead code with no test coverage.
- **Severity**: Low (dead code, but the duplicate diverges from solver.cpp's version)

### F5. LU-SGS implicit solver diagonal/coupling missing face-area factor
- **File**: solver.cpp:616-666 (inner_iteration), 242-270 (inner_iteration_gmres)
- **Code**: `diag[c] += rho` (no ·area), `contrib = 0.5(sgn·A − ρI)·dU[j]` (no ·area)
- **Explanation**: The residual `R_c = Σ_f flux·area` includes the face area. The Jacobian coupling should be `±A_f·0.5(A − ρI)·dU_j`. The code omits the `A_f` factor on both the diagonal ρ-sum and the off-diagonal coupling. On meshes with small cells (A_f ≪ 1), the diagonal ρ-sum dominates the pseudo-time term λ_c/cfl in the wrong proportion, and the off-diagonal coupling is too weak. This makes the LU-SGS a poor preconditioner on non-uniform meshes.
- **Observed symptom**: `inner_iter` always hits the max (50 or 80) with residual ratio ~0.2 (not reducing). The inner solver does not converge. As CFL ramps beyond ~5, the outer iteration diverges (Mach → 1e4, p → 0, forces → 1e260).
- **Suggested fix**: `diag[c] += rho * lf.area` and `contrib *= lf.area` in the coupling.
- **Severity**: **PROVEN BUG** (causes CFL>5 divergence and poor inner convergence)

### F6. Same missing-area issue in GMRES preconditioner
- **File**: solver.cpp:253-270
- **Same as F5**, applies to both `inner_iteration` and `inner_iteration_gmres`.
- **Severity**: **PROVEN BUG**

### F7. Mesh asymmetry (NACA0012_H2)
- **File**: cfd_solver_agentic_benchmark/inputs/meshes/NACA0012_H2.cgns
- **Evidence**: 5199 of 15682 nodes lack an exact y-mirror. Median distance from a node to its nearest mirror point in the near-field is 0.0275 chords (worst ≈ 1.06 chords). The airfoil surface is symmetric (404 wall nodes all have exact mirrors), but the interior mesh is not.
- **Impact**: Explains the small Cl ≈ 0.14 in steady runs but not the magnitude of the divergence. The asymmetric mesh seeds the asymmetry, but the poor preconditioner (F5) amplifies it.
- **Severity**: **SUSPICIOUS** (mesh quality issue)

## Root Cause Summary

The physics.cpp code implements the standard discretization formulas correctly (all 7 hypotheses PROVEN). The solver's divergence on symmetric cases (NACA0012, cylinder) stems primarily from an inconsistent LU-SGS preconditioner (F5) that omits the face-area factor in the diagonal and coupling. This causes the inner iterations to stall at residual ratio ~0.2, so the outer pseudo-time stepping operates with a poor update direction. As CFL ramps beyond ~5, the method diverges: the trailing-edge flow develops a near-vacuum supersonic jet (Mach 1e4), pressure over-compression at the leading edge (p > p₀), and asymmetric lift/cd forces that grow to 1e260. The mesh asymmetry (F7) seeds the asymmetry, and the missing 1/R factor (F1) and vertex-average centroid (F2) contribute additional inaccuracy.
