# Evaluation — codex_generic_glm53-04_adde03

Submission `codex/generic/glm53-04` at `c06741d9dc5739d9f4a6d6c0285e3c69c8cf8052`; checkpoint `e09ae688025722b07af8a347eba5913c9970ea84`.

## Verdict

**85/100; DQ=true under DQ7 (qualified full calibration).** M2 laminar is labeled converged while drag remains materially unstable; the earlier inviscid residual-only rationale is withdrawn. Historical producing source remains unbound.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: 8dca1b41479ef4204805826f21d444b00ed15a81cf27eeaf89373df24f7f3051. contestant_final_response.md SHA-256: ac8b63ecc8c47ddd70e7292e6b5117e6614873d2806bf3a05dabba5932f9c8db. These unindexed sidecars are not validated by cfdeval check.


## Partial M2 laminar and Re200 review — 2026-10-30

Current selected M2 laminar metadata says completed/converged at step406 with 3.004 residual orders, while last40 Cd rows fall 0.8537653491→0.5291502491 (38.0%); first/second half means 0.76240268→0.59653982. TASK.md:140,172 require clearly achieved early convergence or a justified stable-force plateau. DQ7 applies to this case and its case score 4→1. Report.tex:311 says Cd1.025, inconsistent with current raw final Cd0.529; metadata git_revision=26ef1cb points to an initial commit without solver source, so exact production binding/generation is unproved. The prior DQ7 rationale based on below-target inviscid residual orders alone is insufficient without independent force/plateau assessment, which remains open.

Re200 case4.5→2: immutable c06741d forces.png visually has startup drag excursions roughly −120 to +40, compressing late drag oscillations; no alternate delivered late force plot found. Numeric St0.175 is present in report.tex:497, and independently computed t>=100 raw lift FFT peak0.174991 with Cl range −0.450674 to 0.4611 supports oscillatory lift; these facts do not satisfy the separate plotted late lift/drag floor. The 167 reported inner misses out of 30000 do not imply most steps missed. Other rubric/DQ and report/source provenance partial; no CFD rerun.


## Partial steady plots and plateau review — 2026-10-30

Visually inspected all seven immutable c06741d steady force figures and searched report/figure directory for alternatives. M015 inviscid late Cd≈0.001 is compressed by startup >1; M015/M080 laminar late Cd≈0.057/0.076 is compressed by startup ≈320; Re20 late Cd≈2.072 is compressed by startup ≈156. The interim scores 4→2, 5→2, 4→2, 5→2 were subsequently revised in the comparison below; numerical force tails and report/source limits are in the scorecard. The M080 and M2 inviscid force plots show more late behavior and their existing scores remain unchanged.

Last-10% M080 inviscid residual half-means0.01870→0.01562, Cd0.0094542→0.0094535, but Cl0.002022→−0.000442; M2 residual0.01896→0.01888, Cd0.0886825→0.0886591 and Cl0.001679→0.001681 with bounded oscillations. TASK.md:172 permits justified stable plateaus; residual-order shortfall alone is not a DQ7 basis, and M080 lift stability remains qualified. M2 laminar report contradicts itself: force table :311 gives Cd1.025, text :445 gives Cd2.162, while current raw final Cd0.529. Different generation/source binding remains unproved. Other rubric/DQ partial; no CFD rerun.


## Steady-plot comparison and score correction — 2026-10-30

The four steady plots show drag evolution from startup and independently resolved lift, even where late drag is compressed. Opus-04's immutable steady plots explicitly clip startup and show the settled region more clearly, but similarly penalize presentation rather than automatically cap every compressed force. The steady floor requires readable histories exposing behavior; it does not impose the separate Re200 requirement for readable late lift and drag. On that basis the interim 2 scores are revised to 3 for M015 inviscid, 3 for M015 laminar, 3 for M080 laminar and 4 for Re20; these remain below original 4, 5, 4, 5 for late-force presentation, numerical drift where present, and unresolved source binding. The Re200 cap remains.

For M2 laminar, report.tex:311's Cd=1.025 matches the current last-100-row mean 1.025063, although the table describes steady figures as final; report.tex:445 separately says Cd=2.162 and the raw final is 0.529150. This identifies a summary/claim discrepancy without asserting a different generation. Exact run/source binding remains unproved. No CFD rerun.


## Wake-vorticity credibility — 2026-10-30

The submitted Re200 wake figure is readable and traceable to the selected field, but is not reliable evidence of vorticity. The immutable plotting helper produces zero curl on a two-triangle rigid rotation with exact curl 2; it uses a perpendicular centroid direction and opposite owner contributions. Applying those exact submitted functions to the selected field reproduces the delivered image pixel-for-pixel. An independent neighbor-based least-squares derivative over 3,875 wake cells yields correlation −0.026 with the helper and 5th–95th percentile curl −1.737 to 1.538 versus −0.282 to 0.284 for the helper; the independent estimate is diagnostic, not an exact reference solution. Report/visualization rubric 4→3 and results visualization 4→3. This defect does not establish that the plot came from an unrelated field, so it is not itself charged as DQ8. Re200 remains capped at 2 independently for missing readable late drag. Full source and result binding remain partial.


## Restart and rank-study review — 2026-10-30

Clean c06741d source builds in cfd-bench. Restart persists three state levels by global ID but not step or physical time; resumed transient resets t=0 and takes a backward-Euler first step. Build/contract rubric 10→9. The selected Re20 final Cd at np1/np2/np8 is 2.071663/2.071659/2.071655; NACA M015 is 0.001153759/0.001153984/0.001167319, with Cl −0.0012681/+0.0007543/+0.0006058. Report.tex:529–533 says forces agree to solver roundoff, which overstates NACA agreement; the absolute differences do not establish DQ4. The selected metadata revisions are init-only and do not establish exact solver source. Case-results rubric 7→6 for the documented M2 false convergence and other case/provenance limitations. Remaining criterion reviews partial.


## Criterion-specific source checkpoint — 2026-10-30

Replaced generic repeated notes with criterion-specific source and result evidence. Clean build, CGNS/METIS distribution, Roe/Harten or Rusanov, limited reconstruction, wall shear and BDF target accounting are present in immutable c06741d. The report discloses its CFL cap 50 rather than the supplied 100 and limiter freeze; implicit/transient 14→13. Physics and mesh have separate interfaces with family-driven BC mappings but remain fixed to 2D, four variables and perfect gas; extensibility 1→2. Net rubric remains 85. Report and traceability review points 4→3 for documented M2 summary, overstated NACA rank agreement, invalid vorticity derivative and unbound historic solver. DQ7 remains supported by M2 data; no inference that unbound final source produced the histories. Other DQ and production binding remain partial.


## DQ and remaining case review — 2026-10-30

M2 inviscid 2→3: report PDF pp11–13 deliver the numeric summary, residual/force histories and Mach/pressure fields; last-10% Cd and Cl are stable with bounded oscillation although residual reduction is only about 0.14 orders. This is limited plateau credit, not a residual-target convergence claim. M2 laminar remains DQ7 for materially changing drag. DQ4 is not supported by the reported small absolute rank differences; DQ8 is not assigned to a numerically defective derivative of the demonstrated same field. Source contains METIS, neighbor halos, transient BDF, limited reconstruction and labeled Mach/pressure mappings; no evidence found for the corresponding absent-algorithm, fake-partition, explicit-only or misnaming triggers, but historic run binding limits any production-specific conclusion. No external similarity search was performed and no finding of copying is asserted.

Selected M2 laminar metadata records 2026-09-03 17:08:18–17:08:24 UTC, Re200 17:08:25–17:36:24 UTC and revision 26ef1cb (init with no solver source); final solver.cpp mtime 2026-09-04 08:11:52 UTC, SHA ce98e2a32caaf00f518cda0bcc71f34a599e645d377e203e8d9b7d1a6afda397, identical at source commit 836aedb and final c06741d. These timestamps do not establish what changed or that final source produced earlier results. This record remains partial.


## Qualified full review — 2026-10-31

All ten rubric sections, eight case scores, 27 review points and 13 DQ rules were assessed to the available evidence limit. Rubric remains 85/100 with DQ7 for the M2 laminar 38% final drag decrease; inviscid residual shortfalls alone are not used as a false-success rationale. The reproducible same-field vorticity derivative defect is a visualization/analysis deduction, not proof of an unrelated source for DQ8. Rank force differences are small in absolute terms and do not establish DQ4. Historical selected metadata names an init-only revision, and the final source was modified after the runs, so exact producing source/binary remains unbound; earlier partial sections are historical checkpoints. No CFD, rank or Re200 rerun.

## Operator-authorized terminal attribution — 2026-10-31

Operator decision 2026-10-31: accepted selected root 01a06705-3d6c-7b01-9dc7-cab32b791dae latest completed assistant response matched exactly to task_complete despite literal channel missing (phase=final_answer). Assistant timestamp 2026-09-04T08:19:46.327Z, task_complete 2026-09-04T08:19:46.354Z; stored response-line SHA256 cd1de29558277f344e10fcef44c25822c4ae6be078ce210317e4c0ec2796f161, completion-line SHA256 3d6c68f74862ff42fc61c8f565568e7c0620e3e0afb73ab91406ca02e4ff6237, exact text SHA256 ac8b63ecc8c47ddd70e7292e6b5117e6614873d2806bf3a05dabba5932f9c8db; previous sidecar SHA256 ac8b63ecc8c47ddd70e7292e6b5117e6614873d2806bf3a05dabba5932f9c8db is preserved in Git history. Credential scan found no credential. The selected response is preserved exactly in the unindexed sidecar; cfdeval check does not validate unindexed sidecars.
