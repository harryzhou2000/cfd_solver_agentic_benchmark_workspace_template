# Report-PDF Backfill Audit — 2026-08-28

Initial inventory: 77 canonical snapshots, 51 accepted PDFs and 26 explicit
absence records. This pass recovered 15 absence records through a verified
workspace sibling PDF: the matching immutable `report.tex` exists at the
recorded submission commit, `pdfinfo` confirmed a valid multi-page PDF, and
`vendor_report_pdf.py` bound the copied bytes to the immutable snapshot with
`workspace_existing` provenance. No contestant source, branch, score, or
judgment was changed.

Recovered: `claude_generic_opus-06_40b02c`, `claude_generic_opus-08_853afe`,
`codex_dsv4_flash_03_294b64`, `codex_dsv4_flash_11_6e6ac8`,
`codex_generic_02_ed5ee6`, `codex_generic_03_5f2165`,
`codex_generic_opus46-02_d8fed8`, `codex_generic_sonnet5-01_540ddf`,
`codex_glm52_02_06cf18`, `codex_gpt56_02_38616e`,
`codex_kimik3_05_382d41`, `omo_slim_dsv4_05_7a7141`,
`omo_slim_dsv4_06_d8d434`, `omo_slim_dsv4_07_770c37`, and
`omo_slim_dsv4_08_c49b64`.

The remaining absence records were not replaced in this partial recovery:
they require an immutable scratch build attempt or a separately verified
workspace mapping/candidate; smoke reports and figure PDFs remain excluded.
Every recovered snapshot was re-recorded and passed its completion gate.

## Follow-up verification — 2026-09-04

Two further workspace sibling PDFs were accepted only after confirming the
tracked immutable entrypoint and reviewing the actual multi-page main report:
`codex_gpt56_03_93b257` (13 pages) and `codex_gpt56_07_b7c2c8` (36 pages).
They are recorded with `workspace_existing` provenance and their snapshot
PDF/index records were regenerated.

The prior acceptance for `codex_generic_sonnet5-01_540ddf` was revoked. A
visual review of its 76-page sibling PDF found pervasive missing-asset/error
markers through most pages; a syntactically valid PDF is not sufficient
main-report evidence. Its snapshot now records an explicit absence and has no
vendored `report.pdf`.

The seven remaining snapshots with a tracked `solver/report/report.tex` were
rebuilt in fresh `/tmp` git-archive checkouts at the exact submission commits.
All failed before producing a PDF, without changing contestant workspaces:

- `codex_dsv4_flash_02_3447d3`: a committed figure
  `naca0012_m015_inviscid_mach_zoom.png` is missing.
- `codex_dsv4_flash_06_5b6b08`: a committed figure
  `naca0012_m015_inviscid_residual.png` is missing.
- `codex_dsv4_flash_07_aeae21`, `codex_glm52-m3_04_32f2d0`, and
  `codex_glm52_03_e7d38e`: TeX `Missing $ inserted` fatal error.
- `codex_glm52-m3_03_674665`: referenced
  `naca0012_m080_inviscid_residuals.png` is missing.
- `codex_glm52_01_1307c9`: TeX `There's no line here to end` fatal error.

Their explicit absence records remain correct. `codex_gpt56_11_14a0ed` and
`omo_slim_dsv4_03_2fbbe7` also remain absent because their immutable
submissions have no tracked main TeX entrypoint.
