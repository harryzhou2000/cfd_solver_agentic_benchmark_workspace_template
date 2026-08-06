# Environment Snapshot Specification

## Purpose

Record the **real environment** in which a contestant agent ran, before it
starts: host/container facts, tool versions, proxy/network env (redacted),
and the workspace git state. Old runs did not capture this, so the field is
optional and explicitly marked absent when missing.

Produced by `evaluation/tools/env_snapshot.py` (stdlib only, runs on a bare
host), default output `<workspace>/.eval/env_snapshot.json`. It is vendored
into `docker/scripts/setup-workspace.sh` (run at workspace setup, before the
agent starts). The evaluation pipeline copies it into the snapshot as
`env_snapshot.json` (schema: `evaluation/schemas/env_snapshot.schema.json`)
and records `env_snapshot.captured` in `sessions.json` / `summary.snapshot`.

## Contents

| Section | Fields |
|---|---|
| `host` | hostname, OS/kernel, machine, python, cpus, memory GB, loadavg, `in_docker`, container id/cgroup when inside docker |
| `tools` | installed/version for codex, opencode, opencodex, uv, python3, git, cmake, mpirun, mpicc, gcc, docker, codegraph |
| `environment` | selected env vars (proxy, harness homes, OCX_PORT/OCX_HOST, shell/locale) with values **redacted**; credential env keys recorded as presence + redacted preview |
| `network` | optional `--probe-proxy`: local TCP probes of configured proxy endpoints (no external requests) |
| `workspace` | path, git branch/commit/dirty files, AGENTS.md sha256, codegraph presence, done marker, external symlink, benchmark submodule commit/branch |

Values never include full secrets: redaction mirrors `cfdeval.redact`
(`sk-*`, Bearer, key assignments, proxy credentials, truncated previews).
