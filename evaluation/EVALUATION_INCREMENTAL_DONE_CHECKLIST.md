# Incremental Done Evaluation Checklist

| Workspace / operator | Initial provenance | Run ID | Status | Score / DQ | Gates | Contestant commit | Manager commit | Limitations |
|---|---|---|---|---|---|---|---|---|
| `workspace/codex/dsv4_flash/12` / `12` | v1.0 historical setup snapshot: `codex/dsv4_flash/init` at `e757c4e0bac4065c4a5ab7ead230b236e4a9984c` | `codex_dsv4_flash_12_7e965b` | [x] complete | 90/100; no DQ | immutable audit passed; `cfdeval check` and `check-complete` passed | `97bdc69583d3f31441466cb7349d5632494e3897` | pending manager commit | History-preserving cleanup removes 316 prohibited paths only at tip; raw workspace evidence was validated read-only, never copied; Re200 was not rerun. |
| `workspace/codex/glm52-m3/06` / `06` | pre-run snapshot: `codex/glm52-m3/init` at `a14f203babe6794ad032aea760aed8e302e62b5a` | — | in progress | — | — | — | — | Curate a fresh result commit rooted at the declared initial state; preserve attempt branch and raw evidence. |
