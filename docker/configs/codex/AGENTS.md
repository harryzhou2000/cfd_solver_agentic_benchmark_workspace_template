<!-- CODEGRAPH_START -->
## CodeGraph

In repositories indexed by CodeGraph (a `.codegraph/` directory exists at the repo root), reach for it BEFORE grep/find or reading files when you need to understand or locate code:

- **MCP tool** (when available): `codegraph_explore` answers most code questions in one call — the relevant symbols' verbatim source plus the call paths between them, including dynamic-dispatch hops grep can't follow. Name a file or symbol in the query to read its current line-numbered source. If it's listed but deferred, load it by name via tool search.
- **Shell** (always works): `codegraph explore "<symbol names or question>"` prints the same output.

If there is no `.codegraph/` directory, skip CodeGraph entirely — indexing is the user's decision.
<!-- CODEGRAPH_END -->

## Local services (this machine)

The ComfyUI server lives in `/mnt/ssd-SATARAID5/harry/projects/comfyui` and runs
long-term on `http://127.0.0.1:8188`, shared by several projects.

- Never start, stop, restart, repair, or reconfigure that service on your own
  initiative — not even to pick up new models or configuration.
- If a restart or any other lifecycle action looks necessary, stop and ask the user
  first, and state exactly what needs restarting and why.
- Read-only inspection is always fine: `/system_stats`, `/object_info`, `/queue`,
  `/history`, log files, process and folder listings. Weight files dropped into `models/`
  are picked up without a restart.
- Queueing generation prompts against the running server is fine; that is what it is for.
