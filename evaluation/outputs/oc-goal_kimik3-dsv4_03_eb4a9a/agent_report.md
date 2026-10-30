# Evaluation

Validated all eight raw contestant-result packages in immutable attempt `8285817960158113d900fad3335bccef9c0f764f`; no Re200 rerun. Selected OpenCode root has no deterministic final answer; legacy `done` marker is authoritative reviewed-normal status.

run_identity.json SHA-256: `4e64cc2ba8fc7984cfd4efd2775ef8228cad9d8918ae48780cac7a7301a0066b`.

run_identity.json is an unindexed sidecar; cfdeval check does not validate unindexed sidecars.


## Full qualified calibration

**82/100; DQ=true, trigger 7**, superseding 72/100, DQ=false. Original attempt M2 laminar is marked converged at step989 while Cd falls 0.1311815→0.1161380 over the final200 steps. Its actual three-order residual drop does not establish force-stable early convergence. Case scores are 4, 4, 4, 4, 4, 1.5, 4.5 and 4 in canonical order. Re20 Cd2.10053 is stable and plausible; Re200 original force data supports mean Cd1.19536 and upcrossing St≈0.1784. The committed report defines FFT St0.1833 and its figure shows both late lift and drag; an original attempt residual plot was omitted from the curated subset, correcting the old case-floor note.

The source clean-builds in a network-disabled benchmark container (2 CPUs, 3 GiB, 256 processes), with no registered CTests. Root-only METIS preprocessing and neighbor exchange are present; original Re20 rank-study drag agrees across np1/2/4/8 within 0.000021. The wall-force code integrates full viscous traction while the report asserts tangential-only, a viscous/report deduction rather than a claim of fabricated data or automatic DQ6. Current transient source refreshes limiter and complete residual each inner update. Result metadata names fbb2e6e, a revision before final low-Mach changes; uncommitted changes may have been used, but the exact historical executable cannot be inferred. An isolated Re20 plot rerender differs slightly in pixels/dimensions, which does not establish figure misrepresentation. Both gates pass. No external similarity investigation or unsteady solver rerun.
