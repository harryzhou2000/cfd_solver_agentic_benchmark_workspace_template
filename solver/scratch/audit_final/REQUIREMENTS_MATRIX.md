# Requirement Compliance Matrix - final adversarial audit

Audit date: 2026-08-27. Read-only audit. No file outside this directory was modified.
Python: /workspace/solver/.venv/bin/python

## CRITICAL CONTEXT - THE STALENESS FAULT LINE

All 7 completed steady cases were re-run between 13:50:10 and 14:02:20 today:

| case | metadata.json mtime |
|---|---|
| cylinder_m010_laminar_re20 | 13:50:10 |
| naca0012_m015_inviscid | 13:50:31 |
| naca0012_m080_inviscid | 13:51:26 |
| naca0012_m200_inviscid | 13:52:10 |
| naca0012_m015_laminar_re5000 | 13:52:48 |
| naca0012_m080_laminar_re5000 | 13:53:30 |
| naca0012_m200_laminar_re5000 | 14:02:20 |

Every derived report artifact was generated at 13:24, i.e. BEFORE all seven re-runs:
numbers_auto.tex, tab_forces.tex, tab_runstatus.tex, tab_scaling.tex, tab_partition.tex,
run_manifest.csv (13:24:12), sanity_checks.json (13:24:13), figure_manifest.csv (13:24:54),
and ALL 71 FIGURES (13:24:12-13:24:54).

Command evidence:

    find /workspace/solver/report/figures -name '*.png' -newermt '2026-08-27 13:50' | wc -l
    ->  0        (out of 71 total)

report.pdf was rebuilt at 14:51 from the 13:24 numbers, so the PDF looks current but its
content describes runs that no longer exist on disk. This single root cause produces the
bulk of the findings below.

---

## 1. TASK.md requirements

| # | Requirement | Verdict | Evidence |
|---|---|---|---|
| MLP-1 | Core solver C++17 | MET | src/ 40+ .cpp/.h; CMakeLists.txt |
| MLP-2 | MPI domain decomposition + halo | MET | src/parallel/halo_exchange.cpp, distributed_mesh.cpp; metadata halo_exchange=neighbor_isend_irecv all 8 cases |
| MLP-3 | Python only for wrappers/plots | MET | tools/*.py are post-processing only |
| MLP-4 | Builds/runs on Linux without DNDSR | MET | report/build.log, compile_rc.txt |
| MLP-5 | Solver in solver/, benchmark repo untouched | MET | /workspace/solver/; benchmark dir file mtimes all 03:38 |
| MPI-1..4 | Distributed mesh, no full replication | MET | metadata full_state_replication=false, full_mesh_replication=false, all 8 cases |
| MPI-5 | METIS/ParMETIS cell-graph partitioning | MET | partitioner=metis_kway all 8; src/parallel/partitioner.cpp |
| MPI-10 | np=8 on at least 1 NACA and 1 cylinder | MET | results/scaling/cylinder_m010_laminar_re20_np8 and naca0012_m015_laminar_re5000_np8, metadata mpi_ranks=8 |
| MPI-11 | Report describes partitioning, ghosts, per-rank counts, edge cut, load balance | PARTIAL | sec_mpi.tex + tab_partition.tex present, but edge-cut numbers are 13:24-vintage: report 120/97 vs actual 241/282 (Finding 2) |
| PHYS-1..7 | Conservative NS, fluxes, perfect gas, inviscid mode, Re-matched mu | MET | src/numerics/residual.cpp, riemann_flux.cpp, viscous_flux.cpp; physics/perfect_gas.h |
| PHYS-8 | farfield, slip wall, no-slip adiabatic | MET | src/physics/boundary_conditions.cpp; all case BC maps honored |
| PHYS-9 | Surface output distinguishes boundary vs cell-center | MET | no-slip: max abs(u) = max abs(v) = max mach = 0.000e+00 on all 4 viscous cases. slip: max abs(Vn) <= 1.7e-13, abs(Vt) up to 1.0889. surface_cell_center.csv also present in every dir |
| NUM-1..7 | Cell-centred FV, 2nd order, limiter, positivity, Riemann, viscous gradients | MET | reconstruction=piecewise_linear_least_squares_primitive_gradients, limiter=venkatakrishnan, spatial_order_claimed=2 in all 8 metadata |
| NUM-8..11 | Implicit main path, CFL-controlled local dt, LU-SGS inner | MET | src/solve/lusgs.cpp, local_time_step.cpp, steady_driver.cpp |
| NUM-12..14 | 2nd-order transient, true two-level loop | MET (structure) | src/solve/transient_driver.cpp; metadata true_bdf2_inner_loop=true. Numbers = CANNOT VERIFY YET |
| PARAM-1 | Steady cases use supplied cfl_initial/cfl_max/ramp | MET | observed CFL within supplied bounds every case (m080_inv 1->100; m200_inv 0.2->42.6) |
| PARAM-2 | NACA laminar Re5000 max_steps=40000 unless converged earlier | MET | m015 3839 steps at 5.47 orders; m080 4639 at 4.00 - both meet/exceed the 4-order target |
| PARAM-3 | Cylinder Re20 converges, positive drag | MET | 5.86 orders, C_D=2.0184 > 0 |
| PARAM-4..8 | Re200 dt=0.01, t_f=300, BDF2, 5-1000 inner, target 1e-3, CFL 1.0 | CANNOT VERIFY YET | in-flight run; live forces.csv at t=115.5 of 300 |
| PARAM-9 | Metadata records actual inner-iteration stats | MET | observed_min/max, inner_target_misses, inner_target_converged_fraction, last_inner_residual_ratio present all 8 |
| VAL-1 | No placeholder results | MET | all dirs from real solver runs |
| VAL-2 | Steady cases converged or justified plateau | PARTIAL | m080_inviscid 3.92 orders vs JSON target 4.0; m200_inviscid 2.90 vs 3.0 - both labelled converged (Finding 3) |
| VAL-3/4 | Re200 past transient, full t_f | CANNOT VERIFY YET | in flight |
| VAL-5 | Convergence evidence in report | MET structurally | sec_results.tex + residual/force figures; numbers stale |
| VAL-6 | Field contour visualizations | MET | 8 field figs per case: mach, pressure, velocity, vorticity, mach_farfield |
| VAL-7 | Rank-count validation, 1 NACA + 1 cylinder | MET | results/scaling/ 8 dirs at np=1,2,4,8 |
| VAL-8 | Final force row matches final surface/field state | MET | forces last step == run_status.final_step == residuals last step, all 7 steady cases |
| VAL-9 | Every figure has matching source data + naming | MET (mapping) | figure_manifest.csv 142 rows, 64 distinct source refs, 0 unresolvable; 0 filename/variable mismatches |
| SUB-1..8 | Source, README, CLI, result dirs, LaTeX+PDF, scripts, figures dir, run manifest | MET | all present; README.md 28KB with deps, build, run, reproduce section at line 506 |
| GATE-2 | Reproducible from clean checkout | MET | README "Reproduce everything from a clean checkout", 10 numbered steps |
| GATE-3 | No manual edits between cases | MET | single cns2d binary, --case switch |
| GATE-4 | Report consistent with submitted data files | NOT MET | 54 numeric mismatches + 50 sanity_checks mismatches + all figures stale (Findings 1-2) |
| GATE-5 | Failures represented as failures | MET | no failed case marked complete |

## 2. OUTPUT_CONTRACT.md requirements

| Requirement | Verdict | Evidence |
|---|---|---|
| Required CLI form | MET | run_status.command matches cns2d solve --case ... --output ... |
| Directory layout, 9 items | MET, all 8 cases | metadata.json, partition_diagnostics.csv+json, residuals.csv, forces.csv, surface.csv, field_final.vtu, restart_final.bin, stdout.log, run_status.json - 0 missing across all 8 |
| metadata.json required keys | MET | all 40 contract keys present in all 8 cases; 0 missing. Extra: mean_inner_iterations, run_details (benign) |
| completed=true | MET | all 8 |
| convergence_status in allowed set | MET | 7 converged, 1 statistically_periodic |
| partitioner names METIS | MET | metis_kway all 8 |
| halo_exchange neighbor-scoped | MET | neighbor_isend_irecv all 8 |
| true_bdf2_inner_loop for Re200 | MET | true |
| partition_diagnostics header | MET | exact match rank,num_cells_owned,...,recv_cells all 8 |
| residuals.csv header | MET | exact byte match all 8 |
| forces.csv header | MET | exact byte match all 8 |
| surface.csv header | MET | exact byte match all 8 |
| CSV integrity, widths, no NaN/inf | MET for 7 steady | 0 bad-width rows, 0 nan/inf. Re200 has 1 partial row in residuals/forces = live write in progress, expected |
| Inviscid viscous columns near 0 | MET | max abs(viscous_drag) = max abs(viscous_lift) = 0.000e+00 exactly, all 3 inviscid cases |
| No-slip wall u,v,mach near 0 | MET | exactly 0.0 on all 4 viscous cases |
| Slip wall Vn near 0, Vt nonzero | MET | max abs(Vn) 9.7e-14 / 1.7e-13 / 1.4e-13; abs(Vt) ranges [0.039,0.999], [0.034,1.089], [0.009,1.042] |
| Surface covers both airfoil sides | MET | 404 rows = 202 y>0 + 202 y<0 per NACA; cylinder 100 rows, 50/50 |
| Field file, 6 required variables | MET | CellData arrays: density, velocity, pressure, mach, temperature, total_energy, vorticity, rank - all 8 cases |
| Field positivity | MET, independently verified | min rho 0.9817/0.6771/0.0040/0.9908/0.7189/0.4319/0.9916; min p 31.19/0.6517/3.89e-4/31.55/0.8498/0.1273/70.93 - all > 0 |
| run_status.json required keys | MET | all 9 keys, all 8 cases |
| Required report files | MET | report.tex, report.pdf, figures/, run_manifest.csv, sanity_checks.json, figure_manifest.csv |
| Vis req 1-7, residual/force/cp/cf/mach/pressure/vortex-street per case | MET coverage | 71 figures; every case has residuals, forces, cp, mach, pressure, velocity, vorticity, plus cf for viscous |
| Vis req 8, quantitative analysis | PARTIAL | present but numbers stale |
| Vis req 9, per-case status statement | MET | tab_runstatus.tex |
| Vis req 10, MPI diagnostics + np=8 | PARTIAL | present; edge-cut values stale |
| figure_manifest maps every report figure | PARTIAL | 70 of 71 mapped; naca_stagnation_cp.png on disk but NOT in manifest |
| Physics sanity gate file + 9 checks | file MET, values NOT MET | sanity_checks.json has all 9 check families but 50 stale values |

## 3. report_requirements.tex sections

| Section | Verdict | Evidence |
|---|---|---|
| Abstract | MET | report.tex lines 13-57 |
| Introduction | MET | sec_intro.tex |
| Governing Equations + nondimensionalization | MET | sec_equations.tex |
| Meshes, Boundary Tags, Case Inputs | MET | sec_mesh.tex |
| Spatial Discretization | MET | sec_spatial.tex |
| Second-Order Reconstruction | MET | sec_spatial.tex |
| Limiter and Positivity Control | MET | sec_spatial.tex |
| Inviscid and Viscous Fluxes | MET | sec_spatial.tex |
| Boundary Conditions | MET | sec_bc.tex |
| Implicit and Transient Time Integration | MET | sec_implicit.tex |
| MPI Parallelization and METIS | MET | sec_mpi.tex |
| Output, Reproducibility, Sanity Checks | MET | sec_verification.tex |
| Results, at least 2 summary tables | present, values stale | tab_runstatus.tex, tab_forces.tex |
| Results, NACA cases | MET structure | sec_results.tex |
| Results, Cylinder Re20 | MET | sec_results.tex line 789 |
| Results, Cylinder Re200 vortex street | CANNOT VERIFY YET | sec_results.tex 1052-1082 driven by cnsShed macros |
| Parallel Validation | MET structure | sec_parallel.tex + tab_scaling.tex; numbers stale |
| Visualization/Plot Style | MET | figures use tools/plot_style.py |
| Limitations and Failure Analysis | MET | sec_limitations.tex |

## 4. SCORING_RUBRIC.md line items

| Rubric line | Pts | Verdict | Evidence |
|---|---|---|---|
| 1.1 clean documented build | 4 | MET | README Build section, build.log |
| 1.2 CLI without manual edits | 2 | MET | single binary + --case |
| 1.3 required files correct headers/schema | 2 | MET | verified exhaustively, 0 header deviations |
| 1.4 clear error handling | 2 | MET | src/core/exceptions.h, case_input.cpp validation |
| 2.1 CGNS unstructured + BC tags | 4 | MET | src/mesh/cgns_reader.cpp |
| 2.2 volumes/areas/normals/centers | 2 | MET | face_closure 1e-16, volume_closure 1.1e-10, area_vs_line_integral 1.4e-14 |
| 2.3 mixed cells/multi-zone/family names | 2 | MET | VTU cell types 5 and 9; BC families read from case map |
| 2.4 cell-face adjacency graph | 2 | MET | src/mesh/global_mesh.cpp |
| 3.1 conservative residual assembly | 5 | MET | discrete_conservation_defect 9.2e-17 |
| 3.2 robust approximate Riemann flux | 4 | MET | Roe + Harten-Hyman, HLLC, Rusanov in riemann_flux.cpp |
| 3.3 farfield + slip wall | 3 | MET | max abs(Vn) <= 1.7e-13 |
| 3.4 no-slip adiabatic wall | 3 | MET | wall speed exactly 0 |
| 4.1 linear reconstruction | 4 | MET | lsq_linear_gradient_error 3.1e-13 |
| 4.2 limiter active in production | 3 | MET | limiter=venkatakrishnan in all 8 production metadata |
| 4.3 positivity preservation | 2 | MET | positivity_preservation key + min rho/p > 0 verified |
| 4.4 2nd-order evidence + honest fallback | 1 | MET | sec_spatial + sec_verification |
| 5.1 velocity/temperature gradients | 3 | MET | src/numerics/gradients.cpp |
| 5.2 Newtonian stress + Fourier, Re-matched mu | 3 | MET | viscous_flux.cpp; mu = rho*U*L/Re |
| 5.3 no-slip near-zero wall velocity | 2 | MET | exactly 0 |
| 5.4 pressure/viscous split, tangential cf | 2 | MET | pressure_drag + viscous_drag sum to cd; cf from tangential shear |
| 6.1 steady implicit + CFL ramp | 4 | MET | observed CFL ramps within supplied bounds |
| 6.2 LU-SGS/implicit linear solve | 4 | MET | src/solve/lusgs.cpp |
| 6.3 2nd-order transient dt=0.01 t_f=300 frozen BDF2 | 4 | CANNOT VERIFY YET | in flight |
| 6.4 inner convergence stats/logs | 3 | MET | observed_min/max/mean, misses, converged_fraction, last ratio |
| 7.1 METIS per-rank owned/ghost | 2 | MET | partition_diagnostics.csv 4 ranks, owned sums match global |
| 7.2 neighbor halo, no allgather | 3 | MET | neighbor_isend_irecv; halo_exchange.cpp |
| 7.3 global reductions | 2 | MET | MPI_Allreduce in residual/force paths |
| 7.4 rank-independent output | 1 | MET | 0 corrupt rows in np=1..8 dirs |
| 7.5 consistent across rank counts incl np=8 | 2 | MET | cd at step 1500, rel dev <= 4.2e-4 cylinder and <= 1.1e-4 naca across np=1,2,4,8 |
| 8.1 NACA inviscid converged/plateaued | 2 | PARTIAL | m080 3.92 < 4.0 and m200 2.90 < 3.0 target yet labelled converged |
| 8.2 NACA laminar converged | 2 | MET | 5.47, 4.00, 4.78 orders |
| 8.3 cylinder Re20 steady wake | 2 | MET | 5.86 orders, C_D=2.018 |
| 8.4 Re200 post-transient shedding | 2 | CANNOT VERIFY YET | in flight |
| 8.5 fields finite/visualizable | 1 | MET | verified min rho/p, no NaN |
| 8.6 rank-count comparison | 1 | MET | results/scaling/ |
| 9.1 equations described with formulas | 1 | MET | sec_equations.tex |
| 9.2 residual/force/surface histories all cases | 1 | PARTIAL | figures exist but all predate the re-runs |
| 9.3 Mach+pressure figures correct names | 1 | naming MET, content stale | 0 filename/variable mismatches; but stale |
| 9.4 convergence/vortex/MPI/limitations honest | 1 | PARTIAL | text honest in style but quotes superseded numbers |
| 9.5 artifacts reproducible, figures traced to source | 1 | PARTIAL | manifest resolves 64/64 sources but 1 figure unmapped, all figures stale |
| 10.1 physics interfaces not case-specific | 2 | MET | src/physics/ generic |
| 10.2 BCs extensible | 1 | MET | boundary_conditions.cpp dispatch |
| 10.3 mesh/geometry generalizes to 3-D | 1 | MET | mesh_types.h dimension-parameterized |
| 10.4 EOS/RANS/species organization | 1 | MET | perfect_gas.h behind interface |

## 5. Disqualification triggers

| Trigger | Status | Evidence |
|---|---|---|
| 1 existing solver called internally | CLEAR | no external solver invocation |
| 2 forces/residuals generated without solving | CLEAR | all from real runs |
| 3 hard-coded mesh filenames | CLEAR | mesh_file read from case JSON |
| 4 rank count changes steady results by order one | CLEAR | <= 4.2e-4 rel dev np=1 to 8 |
| 5 explicit time stepping only | CLEAR | LU-SGS implicit |
| 6 report claims algorithms not in source | CLEAR | Roe/HH, Venkatakrishnan, LU-SGS, METIS all in src/ |
| 7 metadata marks failed/incomplete as successful | BORDERLINE | see Finding 3 |
| 8 report figures do not correspond to submitted outputs | TRIGGERED | all 71 figures 13:24; all 7 steady result sets 13:50-14:02 |
| 9 metadata says METIS but code geometric | CLEAR | partitioner.cpp calls METIS_PartGraphKway |
| 10 claimed 2nd order/limiter disabled in production | CLEAR | active in all production metadata |
| 11 no-slip rows actually cell-center, undocumented | CLEAR | wall rows exactly 0; separate surface_cell_center.csv |
| 12 misnamed figure files | CLEAR | 0 mismatches in manifest |
| 13 copied solver core | not re-audited here | prior audit reported clean, sec_verification line 1074 |

---

# TOP FINDINGS, RANKED BY SCORING COST

## Finding 1 - CRITICAL: all 71 figures predate the re-run of every steady case (disqualification trigger 8)

Every figure was written 13:24:12-13:24:54. Every steady result set was rewritten 13:50:10-14:02:20.

    find /workspace/solver/report/figures -name '*.png' -newermt '2026-08-27 13:50' | wc -l   ->  0

No residual history, force history, cp, cf, mach, pressure, velocity or vorticity figure in the report
was produced from the data now in results/. Rubric 9.2/9.3 and validation standard VAL-9 depend on this,
and SCORING_RUBRIC disqualification trigger 8 names it explicitly.
Cost: up to 3 rubric points plus disqualification exposure.
Fix: regenerate all figures from current results, then rebuild the PDF.

## Finding 2 - CRITICAL: 54 stale numbers in report macros, 50 more in sanity_checks.json (GATE-4)

numbers_auto.tex (13:24) disagrees with results/ on 54 values. Worst offenders:

| case | quantity | report | actual |
|---|---|---|---|
| naca0012_m200_inviscid | C_L | -0.000249 | -0.004891 (20x, wrong magnitude) |
| naca0012_m200_inviscid | steps | 3245 | 4854 |
| naca0012_m200_inviscid | C_D | 0.09027 | 0.087493 |
| naca0012_m200_inviscid | orders | 2.61 | 2.90 |
| naca0012_m200_laminar_re5000 | steps | 48319 | 37022 |
| naca0012_m200_laminar_re5000 | wall seconds | 6947 | 529.3 (13x) |
| naca0012_m015_inviscid | steps | 2398 | 1861 |
| naca0012_m015_inviscid | orders | 4.85 | 5.26 |
| naca0012_m015_inviscid | wall seconds | 303.2 | 20.2 (15x) |
| naca0012_m015_inviscid | C_D | 0.0009604 | 0.00098836 |
| all 7 steady cases | MPI ranks | 2 | 4 |
| all NACA cases | partition edge cut | 120 | 241 |
| cylinder Re20 | partition edge cut | 97 | 282 |

sanity_checks.json shows 50 further mismatches of the same origin, e.g. m200_inviscid
final_force_row_step 3245 vs actual 4854; force_rows 3386 vs 4907; ranks_in_diagnostics 2 vs 4.
run_manifest.csv (13:24) likewise reports ranks=2 and old step counts for all 7 cases while every
metadata.json says mpi_ranks=4.
Cost: GATE-4 is a stated completion gate; rubric 9.5 and 1.3 exposure.
Fix: re-run harvest_numbers.py plus the sanity/manifest generators, then rebuild.

## Finding 3 - HIGH: two inviscid cases labelled converged below their JSON residual target

- naca0012_m080_inviscid: JSON residual_reduction_target 4.0, achieved 3.9158, status converged.
- naca0012_m200_inviscid: JSON target 3.0, achieved 2.8964, status converged.

Both re-derived independently from residuals.csv as log10(r0/r_best) = 3.9158 and 2.8964.
The run_status notes do document the plateau and force stationarity honestly, and
NUMERICAL_PARAMETERS.md permits a documented plateau - that is the mitigating factor. But
convergence_status is the machine-readable field the validator and rubric 8.1 read, and it asserts
a target that was not met. This is the one place the deliverable brushes disqualification trigger 7.
Cost: rubric 8.1 (2 pts) plus honesty exposure.

## Finding 4 - MEDIUM: report quotes a stagnation C_p that does not exist in the data

sec_results.tex line 203 states the surface row nearest the nose reports C_p = 0.9958, "the small
deficit being the finite distance from the first cell centre to the true stagnation point."
Actual nearest-nose row in results/naca0012_m015_inviscid/surface.csv: x=1.9135e-05, cp=1.002528,
which is also the surface maximum. The real value is an excess over 1, not a deficit, so the
physical explanation as written is backwards for the current data.
Cost: rubric 9.1/9.4 credibility.

## Finding 5 - MEDIUM: scaling table disagrees with the scaling directories

tab_scaling.tex reports C_D at step 1500 of 2.12600766 (cylinder np=1) and 0.11884045 (naca np=1).
Actual values in results/scaling/*_np1/forces.csv: 2.12627534 and 0.11899922.
Wall times also differ: cylinder np=1 reported 40.86 s vs actual 24.32 s; naca np=8 reported
70.38 s vs actual 13.76 s. Note the reported naca np=8 time is slower than np=1, which makes the
scaling story look worse than reality - the correct number is a 3.4x speedup.
Separately, all 8 scaling runs carry convergence_status "not_converged", which is correct for a fixed
1500-step comparison but should be stated explicitly in sec_parallel.tex.

## Finding 6 - LOW: one figure on disk is absent from figure_manifest.csv

report/figures/naca_stagnation_cp.png (mtime 08:25) is not among the manifest's 70 mapped entries.
OUTPUT_CONTRACT requires the manifest to map every report figure. If unused, remove it; if used, map it.
All 64 distinct source_file references in the manifest do resolve, and there are zero
filename/variable mismatches.

## Finding 7 - LOW: 16 pending macros remain defined in numbers_auto.tex

The pending macro renders as bold italic "[run in progress]" (numbers.tex). All 16 are
cnsSpanRel and cnsMeanDriftRel families. I confirmed none of the 16 is referenced anywhere in the
report body, so nothing renders as a placeholder today - but they become live placeholders the moment
a section uses them. The cnsShed family (Re200) is defined pending-then-overwritten and IS used at
sec_results.tex 1052-1082; those must be regenerated after the transient finishes or the vortex-street
table will print "[run in progress]".

## Genuinely complete, stated briefly

Output-contract structure is fully compliant: all 9 required files present in all 8 case dirs, all 40
metadata keys and all 9 run_status keys present everywhere, all four CSV headers byte-exact, zero
corrupt rows, VTU CellData carries all 6 required variables plus rank, field positivity verified
independently, wall-velocity and slip-wall semantics exact, final force row matches final step in all
7 steady cases, inviscid viscous columns identically zero, np=1 to 8 force agreement <= 4.2e-4, and
README covers dependencies, build, run, CLI and a 10-step clean-checkout reproduction.
