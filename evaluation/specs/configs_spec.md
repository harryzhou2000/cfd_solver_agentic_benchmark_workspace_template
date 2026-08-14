# Configuration Snapshot Specification

## Purpose

Each run may have a slightly different config stack. The snapshot captures
only the run-time configs bundled under `<workspace>/.sessions/`, so it remains
independent of the evaluator account and reproducible.

Produced by `evaluation/tools/extract_configs.py` (cfdeval package:
`cfdeval.configs`), embedded as `configs.json` (schema:
`evaluation/schemas/configs.schema.json`).

## Captured config classes

| Role | Files |
|---|---|
| `codex` | captured `config.toml`, `ocx.config.toml`, `opencodex.config.toml`, `AGENTS.md`, version/runtime files, catalogs, and rules beneath `.sessions/codex/` |
| `plugin_manifest` | captured plugin manifests beneath `.sessions/codex/plugins/`, when bundled |
| `opencode` | captured config files beneath `.sessions/opencode-config/`, when bundled |
| `opencodex` | captured config/version/catalog files beneath the applicable `.sessions/` config root, when bundled |

Repository evidence such as the submitted `AGENTS.md`, `.gitmodules`, and
benchmark rubric is recorded by the workspace/benchmark metadata collectors;
it is not a fallback execution-config source.

The extractor must reject every config root or file outside the resolved
`<workspace>/.sessions/` boundary. It never reads shell startup files or any
evaluator-account config. Missing bundled config is reported as unavailable,
not silently replaced from another location.

## Redaction policy

- **Credential files** (`auth.json`, `codex-accounts.json`, `admin-api-token`,
  `installation_id`) are recorded as presence + sha256 only; content is never
  embedded.
- Secret-like values in every other file (sk- keys, Bearer tokens,
  api keys/tokens/secrets/passwords, proxy `user:pass@` URLs, long base64)
  are replaced with `***REDACTED***`; `redaction_hits` counts replacements.
- Content is capped per file (default 128 KiB; catalogs 512 KiB);
  `truncated` marks capped files.

The redactor lives in `cfdeval.redact` and is unit-testable; the policy is
conservative (over-redaction is acceptable, leakage is not).
