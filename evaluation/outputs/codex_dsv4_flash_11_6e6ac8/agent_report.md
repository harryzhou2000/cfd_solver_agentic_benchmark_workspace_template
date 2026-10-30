# Evaluation — codex/dsv4_flash/11

Curated submission `2cd0844d32d5d9200343d91d5ad13f09807702b3` is audit-clean. Selected workspace-local Codex root `019fe864-a126-70a1-9fe5-9d5e378eee12` with 11 descendants spans 2026-08-09T21:20:53.825000Z to 2026-08-10T09:49:15.411000Z. Its terminal response reports seven successful steady cases and Re200 failure.

Explicit validator evidence: seven steady canonical directories pass; Re200 fails because `metadata.json` is missing. Re200 was not rerun. Source contains mesh, partition, physics, Krylov, and transient implementation, but no readable final Re200 deliverable supports completion credit. Detailed review points, rubric sections, case scores, and DQ assessment are in `agent_scores.json`; provisional score is 70/100 and DQ=true under trigger 7 and TASK full-mesh prohibition; remaining scoring is under review.

DQ7 and the TASK full-mesh prohibition are established below; no wrapper is evident. The Re200 final response reports failure. Trust policy excludes external similarity search. Report/result raw artifacts are excluded from the curated branch. No evaluator solver rerun was performed.

`run_identity.json` SHA-256: `ed4e57af78f43f97d910e41e31594354eca8a12477ba56baf85e9952bc2b183d`.

`contestant_final_response.md` SHA-256: `86f4facc91e62a984a1c41c1e81c6585a41bcc21692753a2e7f2e423ae9e13f8`.

These are unindexed sidecars; `cfdeval check` does not validate them.


## Partial DQ and M0.15 inviscid review — 2026-10-30

Canonical metadata and run_status mark M0.15 inviscid converged/stable plateau at step 999 with only 0.565/4 residual orders. Last 100 submitted Cd rows fall from 0.24740155378 to 0.22821744394, an 8.4% change relative to final value; first/last 50 means 0.23499916→0.22821745. solver.cpp:1171–1182,1223–1234 accepts absolute force drift <0.02, insufficient here for TASK.md:172 stable-force requirement. DQ7 applies and case 2→1; force SHA-256 c291279c28485c3ba45ebe61a0a8295492bc152bf8b186faaf41be4919bc2e66, metadata SHA 5a94b8cd3e2d20eb58ecba1373eb11a99346ed39c2c2de21aab39f2e8f904399. Earlier note incorrectly said metadata failed. Report.tex:322–323 claims >6 orders, contradicting metadata 0.565.

Independently, immutable main.cpp:87–101 broadcasts complete GlobalMesh from root to all ranks through mesh.cpp:418–502 and retains mesh in scope during solver.run():113–123; TASK.md:227 prohibits that. Real METIS and local state coexist, so numbered DQ9 is not asserted. Metadata falsely denies full-mesh replication. Seven other cases, report provenance and rubric remain under review; no CFD rerun.
