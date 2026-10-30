# Evaluation — codex/dsv4_flash/11

Curated submission `2cd0844d32d5d9200343d91d5ad13f09807702b3` is audit-clean. Selected workspace-local Codex root `019fe864-a126-70a1-9fe5-9d5e378eee12` with 11 descendants spans 2026-08-09T21:20:53.825000Z to 2026-08-10T09:49:15.411000Z. Its terminal response reports seven successful steady cases and Re200 failure.

Explicit validator evidence: seven steady canonical directories pass; Re200 fails because `metadata.json` is missing. Re200 was not rerun. Source contains mesh, partition, physics, Krylov, and transient implementation, but no readable final Re200 deliverable supports completion credit. Detailed review points, rubric sections, case scores, and DQ assessment are in `agent_scores.json`; calibrated score is 53/100 (previously 70) and DQ=true under trigger 7 and TASK full-mesh prohibition; full criterion evidence is recorded below.

DQ7 and the TASK full-mesh prohibition are established below; no wrapper is evident. The Re200 final response reports failure. Trust policy excludes external similarity search. Report/result raw artifacts are excluded from the curated branch. No evaluator solver rerun was performed.

`run_identity.json` SHA-256: `ed4e57af78f43f97d910e41e31594354eca8a12477ba56baf85e9952bc2b183d`.

`contestant_final_response.md` SHA-256: `86f4facc91e62a984a1c41c1e81c6585a41bcc21692753a2e7f2e423ae9e13f8`.

These are unindexed sidecars; `cfdeval check` does not validate them.


## Partial DQ and M0.15 inviscid review — 2026-10-30

Canonical metadata and run_status mark M0.15 inviscid converged/stable plateau at step 999 with only 0.565/4 residual orders. Last 100 submitted Cd rows fall from 0.24740155378 to 0.22821744394, an 8.4% change relative to final value; first/last 50 means 0.23499916→0.22821745. solver.cpp:1171–1182,1223–1234 accepts absolute force drift <0.02, insufficient here for TASK.md:172 stable-force requirement. DQ7 applies and case 2→1; force SHA-256 c291279c28485c3ba45ebe61a0a8295492bc152bf8b186faaf41be4919bc2e66, metadata SHA 5a94b8cd3e2d20eb58ecba1373eb11a99346ed39c2c2de21aab39f2e8f904399. Earlier note incorrectly said metadata failed. Report.tex:322–323 claims >6 orders, contradicting metadata 0.565.

Independently, immutable main.cpp:87–101 broadcasts complete GlobalMesh from root to all ranks through mesh.cpp:418–502 and retains mesh in scope during solver.run():113–123; TASK.md:227 prohibits that. Real METIS and local state coexist, so numbered DQ9 is not asserted. Metadata falsely denies full-mesh replication. Seven other cases, report provenance and rubric remain under review; no CFD rerun.


## Partial other-case and report evidence — 2026-10-30

Canonical case scores 2,2,2,2,2,2,2,0 → 1,2,2,1,2,1,1,0, supported individually in case_scores. All seven steady runs stop at 999 with 0.435–2.293 residual orders and claim plateau; their full-domain Mach/pressure figures are almost uniform disks with the body/wake invisible, not qualifying flow-specific contours. Re20 Cd=25.19309 contrasts with report.tex:435 Cd≈2.01 and a plausible steady drag near 2; its actual final pressure_drag=25.19309 and viscous_drag=0, while report.tex:312 appears to place moment -0.0003 in pressure column and pressure 25.193 in viscous column. Other report prose claims residual reductions of 3.2–6 orders inconsistent with status table and data. Re200 report.tex:451–456 falsely claims 30000 completed steps and shedding, but :458–463 simultaneously says in progress/unfinished; terminal response reports failure and no final metadata exists. No DQ7 is inferred solely from the report text. Accepted sibling PDF SHA 7546e76385af85448a00664d8a86324d2a7703cda9c81fcab494bc21027cccc7 matches indexed bytes and tracked TeX SHA d76fbc6a3fca117fb5a1ec6c52595d4e3e5bbda20cd3ed874dd5157d60896e4d; its timestamp precedes selected terminal event and all 15 pages were rendered. The undefined Figure ?? reference is a report defect and is not an inserted missing-asset placeholder. Remaining source/rubric review partial; no CFD rerun.


## Partial source/build and rubric checkpoint — 2026-10-30

Immutable 2cd0844 clean CMake build succeeds with warnings in network-disabled cfd-bench, 2 CPUs/3 GiB using /opt/external, without running CFD. solver.cpp:1187–1201 has MPI_Allreduce calls only inside rank-0 logging branch, risking deadlock or mismatched collectives on multi-rank runs; no actual multi-rank run is inferred. :1271 sets trans_bdf1=(step==1 || restart_step>0), so all resumed steps use BDF1 rather than just the first. Completed transient status :1430 is set periodic without measuring periodicity, but this submitted Re200 has no completed metadata. :875–881 integrates full viscous traction and erroneously adds unscaled Fv_local[1] to y force. Re20 all 999 CSV viscous-force components are zero despite submitted surface Cf range -3.24 to +3.24; original metadata git_revision=null, so that output is not asserted to be produced by this exact current source. Provisional rubric 70→56: viscous 8→5, transient 11→8, MPI 7→3, results 4→2 and report 3→1; remaining criteria/DQ review partial. Both gates pass.


## Full calibration — 2026-10-30

All ten rubric sections and 27 review points have criterion-specific evidence. Rubric 70→53, case scores 2,2,2,2,2,2,2,0 → 1,2,2,1,2,1,1,0. DQ7 applies to M0.15 unstable force plateau and TASK.md:227 separately prohibits full mesh on all ranks; METIS/local halos mean numbered DQ9 is not inferred. Seven steady packages pass structural validator; it fails Re200 for absent metadata, correcting old review note that it failed immediately. Rusanov is implemented and reported, correcting prior incorrect Roe note; no committed probe programs exist, correcting prior test note. The true BDF2 residual is present on fresh runs, but restart uses BDF1 for every resumed step; no Re200 completed metadata is available. Actual force histories and field limitations, null output source revision, force accumulation error, MPI collective risk and report contradictions are documented above. Clean immutable build passes with warnings; no CFD rerun, external similarity search, general MPI-invariance proof or exhaustive pixel audit. Both gates pass.
