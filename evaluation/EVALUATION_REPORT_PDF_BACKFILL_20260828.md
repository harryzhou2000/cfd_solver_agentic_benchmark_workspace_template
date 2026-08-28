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
