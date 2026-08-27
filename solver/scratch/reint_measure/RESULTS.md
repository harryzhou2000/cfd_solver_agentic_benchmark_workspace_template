# Independent re-integration of wall forces from published surface.csv

Measured 2026-08-27 from the CURRENT artifacts in `/workspace/solver/results/`.
Read-only with respect to everything outside this scratch directory.

Scripts: `reintegrate.py` (measurement), `make_table.py` (table), `probe.py` and
`order_check.py` (convention probes). Raw numbers: `reintegrate.json`.

## 1. Normalisation convention (stated explicitly)

surface.csv normals `(nx, ny)` are unit (verified `| |n| - 1 | < 7e-14` on every
face of every case) and point **into the body** (verified: `dot(n, outward_radial) < 0`
on 100% of faces in all 7 cases; mean exactly `-1.0000` for the cylinder).

With `q_inf = 0.5 * rho_inf * V_inf^2` and `A_ref` = case JSON `reference.area`
(both `q_inf = 0.5` and `A_ref = 1` for every case here), and all cases at
`aoa = 0` so drag is the +x component and lift the +y component:

    cp := (p - p_inf) / q_inf                     [verified to 1e-11, see below]
    tangent  t = (-ny, +nx)
    pressure_drag = sum_faces( cp * nx * L ) / A_ref
    pressure_lift = sum_faces( cp * ny * L ) / A_ref
    viscous_drag  = sum_faces( cf * tx * L ) / A_ref  =  sum_faces( cf * (-ny) * L ) / A_ref
    viscous_lift  = sum_faces( cf * ty * L ) / A_ref  =  sum_faces( cf * ( nx) * L ) / A_ref

where `L` is the face arc length, reconstructed independently (Section 3).
Subtracting `p_inf` is exact for a closed body since `sum n L = 0` (measured
closure residual `~1e-16` in y and `4.3e-05` in x for the airfoil, the latter
being the open trailing edge of the 404-face polygon).

### cp definition verified two ways

`cp - (p - p_inf)/q_inf` has max abs deviation `5.4e-13 .. 1.0e-11` across cases,
and is numerically identical to `2/(gamma*M^2)*(p/p_inf - 1)` since
`q_inf = 0.5*gamma*p_inf*M^2` holds exactly for these inputs. Either form works.

### Tangent orientation is not ambiguous

Flipping the tangent to `(ny, -nx)` flips the sign of every viscous drag
(relative error `-2.000` exactly, i.e. `-1` times the solver value). The
orientation `t = (-ny, nx)` is the one that reproduces the solver, in all four
viscous cases.

### Independent confirmation from solver source

Read-only inspection of the solver corroborates every element above:

- `src/post/forces.cpp:36-38` — comment: wall normal "points OUT OF THE FLUID AND
  INTO THE BODY".
- `src/post/forces.cpp:63-64` — `dpx = (p_wall - p_inf) * n.x * area`, i.e. `p_inf`
  is subtracted and the pressure force acts along `+n`.
- `src/post/forces.cpp:117-126` — `denom = dynamic_pressure * ref_area`;
  `pressure_drag = (Fx*d.x + Fy*d.y)/denom` with `d = flow_direction`, so drag IS
  rotated by angle of attack in general (indistinguishable here since aoa = 0).
- `src/post/forces.cpp:130` — `cd = pressure_drag + viscous_drag` exactly.
- `src/post/surface_output.cpp:68` — `row.cp = (row.pressure - p_inf) * inv_q`.
- `src/post/surface_output.cpp:119-122` — `const Vec2 t{-n.y, n.x};` and
  `row.cf = dot(shear_on_body, t) * inv_q`. Confirms both the tangent orientation
  and the `q_inf` normalisation of cf.
- The geometric quantity multiplying the stress is `f.geom.area` (2-D face length),
  `src/post/forces.cpp:30`.

## 2. forces.csv final-row structure — VERIFIED, with one correction

The brief's description of the file structure is confirmed:

| case | data rows | last-row step | file max step | structure | final step appears | bit-exact dup |
|---|---|---|---|---|---|---|
| cylinder_m010_laminar_re20 | 2451 | 2450 | 2450 | duplicate final step | 2x | **no** |
| naca0012_m015_inviscid | 2226 | 1861 | 2225 | best-state restore | 2x | **yes** |
| naca0012_m080_inviscid | 3497 | 2546 | 3496 | best-state restore | 2x | **yes** |
| naca0012_m200_inviscid | 4907 | 4854 | 4906 | best-state restore | 2x | **yes** |
| naca0012_m015_laminar_re5000 | 3840 | 3839 | 3839 | duplicate final step | 2x | **no** |
| naca0012_m080_laminar_re5000 | 4640 | 4639 | 4639 | duplicate final step | 2x | **no** |
| naca0012_m200_laminar_re5000 | 37023 | 37022 | 37022 | duplicate final step | 2x | **no** |

The three best-state cases have last step < file max and the appended row is a
**bit-exact, character-for-character duplicate** of the row already at that step
(m015: lines 1862 and 2227; m080: 2547 and 3498; m200: 4855 and 4908). For those
three the choice of row is moot — both candidates are identical.

### CORRECTION: for the other four cases, the FIRST-occurrence rule is wrong

The brief states that where the final step appears twice with two different
values, "the solver's own convention is to use the FIRST occurrence". **The
measurement contradicts this**, and the source confirms the measurement.

My re-integration from surface.csv matches the **LAST** row of forces.csv to
`~1e-12` relative in all four cases, and matches the first occurrence only to
`5e-07 .. 5e-05`:

| case | my lsq value | LAST row | rel to LAST | FIRST occurrence | rel to FIRST |
|---|---|---|---|---|---|
| cylinder_m010_laminar_re20 | 1.220904699297 | 1.220904699297 | +5.7e-14 | 1.220906090839 | -1.1e-06 |
| naca0012_m015_laminar_re5000 | 0.018514047997 | 0.018514047997 | -7.6e-12 | 0.018513916810 | +7.1e-06 |
| naca0012_m080_laminar_re5000 | 0.054253270977 | 0.054253270976 | +5.8e-12 | 0.054256224004 | -5.4e-05 |
| naca0012_m200_laminar_re5000 | 0.096165778353 | 0.096165778353 | -1.5e-12 | 0.096165727928 | +5.2e-07 |

That is a six-to-nine order of magnitude discrimination, far outside any
arc-length ambiguity. The source explains exactly why, at
`src/solve/steady_driver.cpp:636-660`:

> // Recompute the residual and forces from the FINAL state so the last force row
> // matches surface.csv and field_final.vtu exactly.

followed by `computeForces(...)` and `writer.appendForces(outcome.final_step, 0.0, final_forces)`.

So the duplicate step arises because the in-loop force log
(`steady_driver.cpp:292-293`, written every step) already recorded that step, and
then the driver appends one more row recomputed from the final synchronised
state. The last row is by construction the one consistent with surface.csv.
The best-state restore is `steady_driver.cpp:620-635`, which copies `U_best` back
into `U` and sets `outcome.final_step = best_step` **before** that final append —
which is why those three files end with a lower step number.

**Conclusion: the last row of the file is authoritative for all seven cases**, as
the brief's own primary instruction says, and it also equals `final_step` in
run_status.json in every case. The "first occurrence" sub-rule should be dropped
from the report; using it would inject spurious errors of up to 5.4e-05 relative
that are artifacts of row selection, not of the integration.

## 3. Arc-length reconstruction

Two independent estimates, neither reading any length from the solver:

1. **Least-squares polygon closure (headline).** Vertices satisfy
   `v_{i+1} = 2*m_i - v_i`, so `v_i = a_i + (-1)^i v_0`. Each edge must be
   perpendicular to its own normal, `(m_i - v_i) . n_i = 0`, which is linear in
   the two unknowns `v_0`. Solve by least squares, then `L_i = 2|m_i - v_i|`.
2. **Crude midpoint spacing.** `L_i = 0.5 * |m_{i+1} - m_{i-1}|` (half the distance
   between neighbouring face midpoints, as specified). A second crude variant
   `L_i = 0.5*(|m_{i+1}-m_i| + |m_i-m_{i-1}|)` is also reported as "crudeB".

The published row order is already contiguous around the body, so no re-sorting
is needed: total turning of the normal field is exactly 360.0 deg and the
length-weighted normal closure is `~1e-16`. Sorting by `atan2` reproduces the
published order identically. (Confirmed in source: rows are sorted by angle
around the body centroid after the MPI gather, `src/post/surface_output.cpp:148-159`.)

### Verification against the exact cylinder

The cylinder wall is a 100-sided polygon inscribed in a circle of radius 0.5, so
the exact chord is `2 * 0.5 * sin(pi/100) = 0.031410759078128`.

| quantity | value |
|---|---|
| lsq max abs error vs exact chord | **6.08e-13** |
| crude max abs error vs exact chord | 3.10e-05 |
| perimeter, lsq | 3.141075907813 |
| perimeter, exact 100-gon | 3.141075907813 |
| pi | 3.141592653590 |
| lsq perimeter minus exact 100-gon | 3.06e-14 |
| pi minus 100-gon perimeter (true polygon deficit) | 5.17e-04 (1.64e-04 relative) |

The lsq reconstruction recovers the exact chord to 6e-13 and the exact 100-gon
perimeter to 3e-14. The recovered perimeter is **3.14108**, which differs from pi
by 5.2e-04; that gap is the genuine chord-vs-arc deficit of a 100-sided inscribed
polygon, not an error in the reconstruction. The discretised wall really is a
polygon, and the solver integrates over the polygon, so the polygon perimeter is
the correct comparison target.

Max lsq residual per case: **2.48e-13** (cylinder), **2.75e-12** (all six airfoil
cases, identical mesh).

## 4. Results

### Pressure drag

| case | re-integrated (lsq) | forces.csv (last row) | abs diff | rel diff | crude-L rel diff |
|---|---|---|---|---|---|
| cylinder_m010_laminar_re20 | 1.220904699297 | 1.220904699297 | +6.99e-14 | +5.73e-14 | -9.87e-04 |
| naca0012_m015_inviscid | 0.000988356161 | 0.000988356155 | +5.53e-12 | +5.60e-09 | -4.73e-02 |
| naca0012_m080_inviscid | 0.008770977829 | 0.008770977827 | +1.65e-12 | +1.88e-10 | -2.25e-03 |
| naca0012_m200_inviscid | 0.087492909671 | 0.087492909670 | +6.02e-13 | +6.88e-12 | -2.80e-04 |
| naca0012_m015_laminar_re5000 | 0.018514047997 | 0.018514047997 | -1.41e-13 | -7.61e-12 | -1.59e-03 |
| naca0012_m080_laminar_re5000 | 0.054253270977 | 0.054253270976 | +3.16e-13 | +5.83e-12 | -5.70e-04 |
| naca0012_m200_laminar_re5000 | 0.096165778353 | 0.096165778353 | -1.48e-13 | -1.54e-12 | -4.25e-05 |

### Viscous drag

| case | re-integrated (lsq) | forces.csv (last row) | abs diff | rel diff | crude-L rel diff |
|---|---|---|---|---|---|
| cylinder_m010_laminar_re20 | 0.797469788759 | 0.797469788759 | +2.12e-14 | +2.66e-14 | -9.87e-04 |
| naca0012_m015_inviscid | 0 (exact) | 0 (exact) | 0 | exactly 0 | exactly 0 |
| naca0012_m080_inviscid | 0 (exact) | 0 (exact) | 0 | exactly 0 | exactly 0 |
| naca0012_m200_inviscid | 0 (exact) | 0 (exact) | 0 | exactly 0 | exactly 0 |
| naca0012_m015_laminar_re5000 | 0.036666792672 | 0.036666792672 | -2.67e-15 | -7.29e-14 | +1.63e-04 |
| naca0012_m080_laminar_re5000 | 0.027219317359 | 0.027219317358 | +3.29e-14 | +1.21e-12 | +3.56e-04 |
| naca0012_m200_laminar_re5000 | 0.040975691724 | 0.040975691725 | -3.40e-14 | -8.29e-13 | +4.40e-05 |

The three inviscid cases are **exactly zero on both sides**: every `cf` entry in
their surface.csv is identically `0.000000000000e+00`, and the `viscous_drag`
column is identically zero. This is a bit-level exact match, not a small number.

### Headline number and arc-length sensitivity

The **least-squares reconstruction gives the headline number** in every case. It
agrees with forces.csv to `6e-12` or better on six of seven cases, and to
`5.6e-09` on naca0012_m015_inviscid (whose pressure drag is only `9.9e-04`, so a
`5.5e-12` absolute discrepancy is amplified in relative terms).

Under the crude arc length the agreement degrades to `4e-05 .. 5e-02` relative.
The spread between the two estimates is therefore `10^3` to `10^7` times larger
than the lsq-vs-solver discrepancy itself.

**Honest upper bound on the true inconsistency:** `< 6e-12` relative on pressure
drag (six cases) and `< 1.3e-12` on viscous drag, using the arc lengths that are
demonstrably exact to 6e-13 against the analytic cylinder. The 4.7e-02 figure for
m015_inviscid under the crude length is a property of the crude length estimator,
not evidence of any inconsistency in the solver: the same case agrees to 5.6e-09
with the verified-exact reconstruction.

### Lift cross-check (not requested, reported as corroboration)

pressure_lift and viscous_lift also reproduce to the same `~1e-12` precision in
all seven cases, including sign, which independently confirms the tangent
orientation and the into-body normal convention.

## 5. Anomalies and caveats

1. **The first-occurrence rule in the brief is refuted** (Section 2). This is the
   single most consequential finding: a table built on the first occurrence would
   show 5e-07 to 5e-05 relative "errors" that are pure row-selection artifacts.
2. **Recovered perimeter is 3.14108, not pi.** Expected and correct: the wall is a
   100-gon, whose exact perimeter is 3.141075907813. The 5.2e-04 shortfall from pi
   is the chord-vs-arc deficit. Anyone quoting "perimeter vs pi" as an error
   measure would be mis-attributing a geometry discretisation property to the
   reconstruction.
3. **The airfoil normal closure is 4.3e-05 in x** (vs 6e-16 in y), reflecting the
   open trailing edge of the published 404-face wall polygon. This is a real
   feature of the surface data. It does not affect the comparison because both
   sides integrate the same face set, and `p_inf` subtraction is only exactly
   neutral to that 4.3e-05 tolerance.
4. **naca0012_m015_inviscid has the weakest relative agreement** (5.6e-09) purely
   because its pressure drag is the smallest number in the set (9.88e-04); in
   absolute terms (5.5e-12) it is as good as the rest.
5. All six airfoil cases share one mesh, hence identical perimeters and identical
   lsq residuals — a useful consistency check that the reconstruction is
   deterministic and mesh-driven rather than solution-driven.
6. The cylinder's two candidate rows differ by only 1.1e-06 relative, so on that
   case alone the row-selection error would have been easy to overlook; on
   naca0012_m080_laminar_re5000 it is 5.4e-05, which is large enough to dominate a
   consistency table.

## 6. Bottom line

The published surface.csv and forces.csv are **mutually consistent to
floating-point round-off** (`<= 6e-12` relative, and bit-exact zero for inviscid
viscous drag) once (a) the arc lengths are reconstructed with the least-squares
closure solve, verified exact to 6e-13 on the analytic cylinder, and (b) the
authoritative row is taken as the last row of forces.csv for all seven cases.
No genuine inconsistency was found.
