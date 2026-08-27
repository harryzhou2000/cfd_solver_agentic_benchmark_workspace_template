Review notes (read-only audit), sec_bc / sec_mesh / sec:cyl200
Run state at audit time: forces.csv reached step 23437, t=234.37 (needs 30000 / t=300).

CONFIRMED WRONG
1. sec_bc.tex:47-50 eq:slipflux -- Roe flux at slip wall is NOT [0,p nx,p ny,0].
   Numerically the normal momentum carries an extra rho*un*(un+a_hat).
   e.g. W=(1,0.3,0.1,0.7143), n=(0,1): flux_y=0.8244 vs p=0.7143, excess 0.1101.
   Mass and energy flux ARE zero (exact). Identity holds only when un=0.
   Cause: ghost has dun=-2un, so alpha_1/alpha_3 nonzero; the u+a and u-a
   dissipation terms do not cancel in the normal momentum.
2. sec_verification.tex:98-99 -- table row "Slip-wall flux [0,pnx,pny,0] exactly /
   exact / exact" claims a check that does NOT exist anywhere in
   tests/unit_tests.cpp or src/mesh/mesh_verification.cpp. And it is false anyway (1).
3. sec_mesh.tex:9 -- "Both supplied meshes declare PhysicalDimension = 3".
   CylinderB1.cgns is phys dim 2 (launch logs), NACA0012_H2 is phys dim 3.
4. tab:saturation (sec_results.tex:816-819) is stale: computed on a record ending
   ~t=98.7-101. Recomputed on live record the spread rows change
   (t>=16.8: 37%->40.8%, t>=30: 3.5%->4.0%, t>=40: 0.7%->1.13%).
   NOTE: with transient_status.py's OWN period convention (zc[i+2]-zc[i]) the
   t>=60 row reproduces exactly (0.040%, 5.4652, 1.2468) and stays 0.04% on the
   full record. So only the early rows drift; the 0.04% figure is robust.
   "thirteen independent period estimates" (line 831) is tied to t_end~98.7;
   full record gives 60.
5. sec_results.tex:834-838 -- "St and mean drag approach their literature values
   monotonically from below ... still saturating". FALSE on current data:
   non-overlapping late windows give St 0.1830/0.1829/0.1829/0.1829 and
   meanCD 1.2466/1.2470/1.2466/1.2462. Both are FLAT, not still climbing.
   St plateaus at 0.183 vs literature 0.19-0.20; CD at 1.247 vs 1.3-1.4.
   The gap is a standing discrepancy, not transient undershoot.

OVERSTATED
6. sec_results.tex:851-855 -- "against the $6$--$9$ observed" contradicts the
   9/10/14 quoted at 846. The 6/7/9 numbers belong to the *reimplementation*
   introduced later at 870. Verified by running both scripts on field_00076.vtu:
   count_vortices2.py (binned) -> 9/10/14 exactly (matches 846).
   count_vortices.py (naive, band 0.05) -> 12/6/10, NOT 6/7/9.
   Band sweep on centreline reproduces 1..73 (band 0.02->0.4): 1,12,10,22,73. OK.
7. sec_bc.tex:66-78 eq:noslipface -- "density following from the equation of state
   so the wall state is thermodynamically consistent rather than an ad-hoc copy"
   is vacuous: rho=p/(R T) with T=T_i and p=p_i returns EXACTLY the interior
   density. Code comment admits it (boundary_conditions.cpp:69).
8. sec_bc.tex:76 -- (rho E)^face = p/(gamma-1) is dimensionally an energy DENSITY
   (rho e), not rho*E per unit mass notation used in sec_equations.tex:6,31.
   Correct numerically (zero KE) but notation clashes.
9. sec_mesh.tex:60-61 -- "Every cell in both meshes is stored counter-clockwise,
   so all signed areas are positive; a negative area is a fatal mesh error."
   Code REORIENTS negative-area cells (global_mesh.cpp:230-234) and only throws
   on zero/degenerate area (240-241). Logs show no reorientation happened, so the
   premise is true for these meshes, but the mechanism described is wrong.
10. sec_mesh.tex:39-40 "the supplied interface point lists are bit-identical, so
    the maximum merge distance is exactly zero" -- log confirms "max merge
    distance 0" for 320 fused pairs. VERIFIED OK.
11. sec_results.tex:914 "the statistics reported here and the run-time diagnostics
    cannot disagree" -- overclaim; report/cylinder_m010_laminar_re200_transient.json
    is a DIFFERENT tool (analyze_transient.py, FFT, 40% window) and is stale at
    t=10.9 with St=0.2281, period=4.3836, meanCD=1.0504. Two tools, two answers.
12. sec_results.tex:780-785 "first clean cycles expected around t=40-80" and
    "roughly 55 shedding cycles" at t=300: 300/5.4664=54.9 OK.
13. sec_results.tex:793 "from 2.6e-5 at startup to an amplitude above 0.5":
    cl[0]=2.577e-5 OK; max|cl|=0.5321 at t=83.66 OK.

VERIFIED CORRECT (do not flag)
- ghost vs face split: 4 consumers use face state (gradients.cpp:43,
  limiter.cpp:67, residual.cpp:134, surface_output.cpp:56); only residual.cpp:46
  and mesh_verification.cpp:171 use ghost. Matches sec_bc.tex:14-23.
- slip ghost eq:slipghost matches boundary_conditions.cpp:19-25.
- slip face state matches slipWallFaceState (29-49) incl. KE correction.
- no-slip ghost u->-u matches noSlipWallGhostState (51-60).
- farfield branches: M_n>=1 interior (103-105), M_n<=-1 freestream (107-109),
  subsonic invariants R+/R- and a_b=0.25*gm1*(R+-R-) match (112-115). Identical
  states reproduce exactly. Fallbacks at 116-120 and 142-144 match text.
  Entropy/tangential from upwind side (124-129) matches.
- traction eq:traction sign pair matches forces.cpp:36-38,63-64,100-101.
- eq:shear tangential projection matches wallShearTraction (viscous_flux.cpp:98-102).
- owned-once rule + single Allreduce of 6 accumulators: forces.cpp:27,19,116. OK.
- moment r x f positive CCW: forces.cpp:107-110. OK.
- eq:wallgrad correction matches residual.cpp:157-163 and adiabatic dT/dn
  projection at 164-168. OK.
- mesh table arithmetic all self-consistent: tri+quad=cells both meshes;
  wall+far=boundary faces both; sum cell edges = 2*interior+boundary both;
  zone merge 7079+3236-320=9995, 6901+3284=10185. Euler V-E+C+1=1 (=annulus, genus
  correct for a domain with 2 boundary loops). OK.
- shoelace + centroid cx/(3*a2) == 1/(6A) form: geometry.cpp:14-22 matches
  eq:shoelace exactly. OK.
- normal n=(ty,-tx)/len then flipped to point away from left cell:
  distributed_mesh.cpp:546-559 matches sec_mesh.tex:64-71. Boundary normal points
  out of fluid into body -- consistent with forces.cpp:31-32. OK.
- dual graph for partitioning: partitioner.cpp:13 buildDualGraph -> METIS. OK.
- BDF2 with BDF1 first step: transient_driver.cpp:72-80 (a0,a1,a2 = 1.5,-2,0.5;
  step 1 -> 1,-1,0). Report says "dual-time BDF2" without mentioning BDF1
  startup -- minor omission, standard practice, harmless for t=234.
- run controls dt=0.01, t_f=300, 30000 steps, inner 5..1000, target 1e-3 all match
  the case file exactly.
- statistically_periodic gate: horizon AND converged_fraction>=0.95
  (transient_driver.cpp:284-292) matches sec_results.tex:765-767.
- drag at twice lift frequency: measured f_CD/f_CL = 1.96. OK.
- 4 of 7 re200 figures missing but \cnsfig has a guarded placeholder
  (preamble.tex) so the build degrades gracefully. OK.
- cylinder r=0.5: wall face CENTROIDS at r=0.49975 (chord midpoints of a 100-gon
  inscribed at r=0.5); "r=0.5 exactly" is defensible for the node radius.
- NACA bc-4: x in [1.9e-5, 1.005313], max thickness 0.120008. Matches text.

