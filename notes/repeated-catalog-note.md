# Repeated catalog-change note (live reproduction)

Exact `<multi_agent_mode>` line injected repeatedly in this session:

> The model catalog changed after Codex started; do not set model or
> reasoning_effort overrides until Codex restarts.

Provenance: this is the guidance-reinjection bug (see
`opencodex-guidance-reinjection.md`) reproduced live — the same developer
guidance is appended on every turn because stateless HTTP clients send no
`previous_response_id`, so the dedup prefix length is zero.
