# Evaluation — codex_generic_glm53-02_ff20c7

Submission `codex/generic/glm53-02` at `1bc8fb6ec6c765ec3c4bf7c06770463580dbecb1`; checkpoint `d1ff189a361b99b13381007bbb7bb896adcee698`.

## Verdict

**75/100; DQ=true under DQ7 (partial calibration).** The previous inviscid-plateau rationale was unsupported, but a separate M2 laminar result is labeled converged while its drag is still changing materially; evidence below. Other triggers remain under review.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: 1d16f3880d29da32fd7905b50fd117ea2186b70a952db1f563904a28aed28570. contestant_final_response.md SHA-256: 33b1e6f88dcebd4de4f388514dd7a2242fdafe32412e50d21937b8c783deab38. These unindexed sidecars are not validated by cfdeval check.


## Partial DQ7 and cylinder post-processing review — 2026-10-30

The previous DQ7 flag treated inviscid reductions 2.509, 2.383 and −0.040 orders as automatically failed. TASK.md:172 permits a clearly justified plateau with stable forces and bounded residuals; report.tex:501–509 explicitly discloses terminal plateau acceptance. Independently reading the last 10% of M015/M080/M200 inviscid histories gives residual half-means 1.460e-5→1.433e-5, 1.01311e-6→1.01318e-6, 1.32099e-4→1.31991e-4; Cd half-means 0.00228491→0.00228905, 0.00928821490→0.00928821492, 0.092531847→0.092531879; Cl half-means 0.00041351→0.00041146, 0.000435354436→0.000435354431, −0.000174608→−0.000174055. These support bounded stable plateaus; residual-order shortfall alone is not proof of false metadata. DQ7 removed, DQ=false provisionally while other triggers remain under review.

Re20 case 5→2: report_data.tex:19 reports Cd1.997/Cl−0.001196 and last-10% raw Cd1.99656–1.99838 supports plausible stable drag. Visually reviewed immutable force image and accepted PDF p10: startup Cd≈103 compresses the late force tail. Its Mach/pressure panels and standalone vorticity contour are compressed to thin strips, not wake-revealing; no suitable alternate delivered wake plot found. Re200 old case note had a numeric attribution error: report_data.tex:20 reports mean Cd0.9727 and Cl0.001315±0.01511, while line32 is residual statistics; St0.14 is present at :42. Raw lift FFT independently peaks at 0.14 for t>=100,150,200, but PDF p10 force plot lacks readable late lift/drag and score remains2. PDF p11 shows wake zoom. Output metadata has git_revision=null, and source linkage and remaining rubric/DQ are partial. No CFD rerun.


## Separate M2 laminar false-success finding — 2026-10-30

Selected solver/results/naca0012_m200_laminar_re5000/metadata.json and run_status.json say completed/converged at step627 with 3.004 residual orders, yet the last 62 force rows show Cd decreasing monotonically 0.24406605467→0.19384628823 (20.6% of its starting value); first and second half means 0.2296330056→0.2042442508. This is not a stable force result. TASK.md:140 requires max_steps at least 40000 unless convergence is clearly achieved early; :172 requires convergence or a justified plateau with stable forces and bounded residuals. DQ7 applies on this separate case, and its independent case score 2→1. This does not reinstate the unsupported inviscid rationale: those plateaus remain as previously documented. Metadata git_revision=null, force file mtime 2026-08-30 14:00:50 UTC precedes final solver.cpp mtime 19:24:44 UTC; final source :1159–1165 tests residual orders alone, but is not proved to have generated these historical rows. Other DQ/rubric review partial; no CFD rerun.


## Partial build and method calibration — 2026-10-30

Exact 1bc8fb6 solver source configured and built successfully in network-disabled cfd-bench with two CPUs, 3 GiB memory and 256 process limit (`cmake -S /work/solver -B /work/build -DCFD_EXTERNALS_ROOT=/opt/external/cfd_externals/install`; build -j2). Production metadata has git_revision=null and selected M2 laminar output predates final solver.cpp, so this source-to-result linkage is not assumed.

Final solver.cpp:190–204 stores primitive gradients rho,u,v,p; :330–336 passes pressure gradient slot 3 as the temperature gradient to Fourier heat flux. Directional face correction replaces only the connecting-direction component, leaving the transverse part generally incorrect. At :782–794 the integrated viscous force split uses the full traction; surface Cf itself is tangentially projected at :820–826. Report.tex:193–197 correctly describes Cf, so a claimed-missing-algorithm DQ is not inferred. Viscous rubric 10→6. Transient :1014–1021 refreshes gradients but freezes limiter throughout the inner solve; measured target therefore applies to lagged limiter, and fully refreshed strict miss counts remain unknown. Steady metadata reports all inner targets missed, distinct from accepted outer plateaus. Implicit rubric14→11. M2 laminar still-changing drag reduces case-results rubric7→5; compressed/unreadable figures reduce report rubric4→2. Total86→75; review points reflect independent evidence.

All 11 accepted PDF pages (SHA d69b0785f55386c3999f6f8355fb12abdc2fd60914d188b3efcdc7a7f92ed44f) were visually reviewed; snapshot matches sibling bytes, PDF mtime 2026-08-30 19:38:46 UTC follows tracked-matching TeX 19:38:33 UTC, and no missing-asset placeholders found. Other source/DQ criteria and provenance remain partial; no CFD rerun.
