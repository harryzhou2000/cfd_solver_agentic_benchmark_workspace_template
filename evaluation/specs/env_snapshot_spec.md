# Environment Snapshot Specification

## Purpose

Record the **real environment** in which a contestant agent ran, before it
starts: host/container facts, tool versions, proxy/network env (redacted),
and the workspace git state. For an operator-approved legacy run that lacks
this evidence, a `post_run` snapshot may preserve reconstructed initial Git
identity while explicitly marking the original run environment unavailable.
Never treat post-run host/tool/environment values as execution-time evidence.

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

## Legacy post-run capture

Use `--capture-phase post_run` only after the operator approves reconstructed
provenance. Supply the exact initial branch, full initial commit, and at least
one evidence description. The snapshot stores those immutable initial values
in `workspace.branch` and `workspace.commit` for identity tooling, stores the
actual capture-time Git state separately under
`provenance.captured_workspace_state`, and records fixed limitations including
the unavailable original harness/container version. `runtime_at_run` records
both unavailable values explicitly as null and `run_environment_available` is
false. A post-run snapshot is provenance recovery, not proof of the runtime
environment.
