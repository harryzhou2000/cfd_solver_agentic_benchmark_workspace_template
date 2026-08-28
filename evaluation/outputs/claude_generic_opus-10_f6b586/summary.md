# Final Result Summary — opus-10

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-10`
- Branch: `claude/generic/opus-10` commit `d25cd3cb7c1c75e75812670fd717eca3b61d68ce`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-10/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-10/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-10/solver/report`)
- Session window: 2026-08-28T01:43:36.307000+00:00 → 2026-08-28T04:41:41.816000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-28T01:43:36.307000+00:00 → 2026-08-28T04:41:41.816000+00:00; 6×1800s buckets; idle 0 gaps / 0s excluded; permission-wait candidates 0; tokens 121,129,727 (cache hit 0.9933)

## Expenses

- Goal time: **unavailable**
- Wall time: **10686 s**
- Tokens: **121,129,727** (main 119,862,076 / subagents 1,267,651)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `54c28431` | None | None | 5 | 121,129,727 | None |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| claude-opus-4-8 | 120,673,487 | 119,868,758 | 456,240 | 121,129,727 |

- Cost estimate: **$76.37** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **396**; top tools: Bash=235, Read=63, Edit=61, Write=31, Agent=4, ListAgents=2
- Subagent spawns: 4
- LOC (file scan): 4,264 lines / 21 files
- LOC (git tracked): 4,264 lines

## Metadata

- Harness: claude cli 2.1.241 (None, provider None)
- AGENTS.md: sha256 2c0ded94ff6e (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| claude-opus-4-8 | high | ? | n/a | 5 |

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `54c28431` | `54c28431` |  | claude_subagent | claude-opus-4-8 | high | 137,957 |
| `54c28431` | `54c28431` |  | claude_subagent | claude-opus-4-8 | high | 400,813 |
| `54c28431` | `54c28431` |  | claude_subagent | claude-opus-4-8 | high | 202,106 |
| `54c28431` | `54c28431` |  | claude_subagent | claude-opus-4-8 | high | 526,775 |

### Prompts

- `54c28431` goal: none
  - initial: <command-name>/goal</command-name>
            <command-message>goal</command-message>
            <command-args>Complete the task defined in @cfd_solver_agentic_benchmark/ .</command-args>
  - resume: <local-command-stdout>Goal set: Complete the task defined in @cfd_solver_agentic_benchmark/ .</local-command-stdout>

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| high | unauthorized_remote_mutations | `54c28431` | {"command": "cd /workspace\ngit branch --show-current\ngit checkout -b solver/cns-fvm-2d 2>&1 | tail -2\nmkdir -p solver/src solver/tools solver/report/figures solver/results solver/script |
| medium | network_access | `54c28431` | e/solver\npython3 -m venv .venv 2>&1 | tail -1\n./.venv/bin/pip install --quiet numpy matplotlib 2>&1 | tail -2\n./.venv/bin/python -c \"import numpy,matplotlib;print('numpy',numpy.__version_ |
| medium | suspicious_patterns | `54c28431` | {"command": "cd /workspace/solver\nfor i in 1 2 3; do pkill -9 -f cns2d 2>/dev/null; pkill -9 -f \"run_all\" 2>/dev/null; sleep 1; done\nsleep 2\nn=$(pgrep -af \"cns2d solve\" | grep  |
| medium | suspicious_patterns | `54c28431` | pace/solver\nrm -rf results/_r200med results/_r200med.out\npkill -9 -f cns2d 2>/dev/null; sleep 2\nnohup bash scripts/run_all.sh > results/run_all.log 2>&1 &\necho \"run_all PID $!\"\nslee |
| medium | suspicious_patterns | `54c28431` | {"command": "cd /workspace/solver\nfor i in 1 2 3; do pkill -9 -f run_all.sh; pkill -9 -f cns2d; pkill -9 -f \"mpirun\"; sleep 1; done\nsleep 2\necho \"=== procs ===\"; pgrep -af \"ru |
| medium | suspicious_patterns | `54c28431` | {"command": "cd /workspace/solver\nfor i in 1 2 3; do pkill -9 -f run_all.sh; pkill -9 -f cns2d; pkill -9 -f mpirun; sleep 1; done\nsleep 2\npgrep -af \"cns2d solve\" | grep -c cases  |
| medium | suspicious_patterns | `54c28431` | all run_all.sh bash processes and everything they spawned\npkill -9 -f \"scripts/run_all.sh\" 2>/dev/null\npkill -9 -f \"cns2d\" 2>/dev/null\npkill -9 -f \"mpirun -np\" 2>/dev/null\nsleep  |
| medium | suspicious_patterns | `54c28431` | {"command": "cd /workspace/solver\npkill -9 -x cns2d 2>/dev/null; sleep 1\npkill -9 cns2d 2>/dev/null; sleep 1\nkillall -9 cns2d 2>/dev/null; sleep 2\necho \"cns2d  |
| medium | suspicious_patterns | `54c28431` | 015_inviscid ranks (leave Re200 and the run_all sequence)\npkill -9 -f \"m015_inviscid --output\" 2>/dev/null; sleep 2\nexport LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib |
| medium | suspicious_patterns | `54c28431` | {"command": "cd /workspace/solver\npkill -9 -f \"scripts/run_all.sh\"; killall -9 cns2d 2>/dev/null; pkill -9 -f mpirun 2>/dev/null\nsleep 3; killall -9 cns2d 2>/de |
| medium | suspicious_patterns | `54c28431` | lling by explicit PID ===\"\nfor p in $(pgrep -x cns2d); do kill -9 $p 2>/dev/null; done\nsleep 3\necho \"remaining: $(pgrep -x cns2d | wc -l)\"", "description": "Identify and PID-kill orp |
| medium | suspicious_patterns | `54c28431` | ver\npgrep -x cns2d | wc -l | xargs echo \"cns2d procs:\"\npkill -9 -f \"scripts/run_all.sh\" 2>/dev/null; sleep 1\nexport LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:$LD |
| medium | suspicious_patterns | `54c28431` | tep ' results/_pf/stdout.log 2>/dev/null|tail -1)]\"\ndone\nkill -9 $PID 2>/dev/null; killall -9 cns2d 2>/dev/null", "description": "Fine-grained monitoring of prod.json np=8"} |
| medium | suspicious_patterns | `54c28431` | rep \"step \" results/_pf/stdout.log 2>/dev/null | tail -2\nkill -9 $PID 2>/dev/null; killall -9 cns2d 2>/dev/null; rm -rf results/_pf results/_pf.out", "description": "Verify live step lo |
| medium | suspicious_patterns | `54c28431` | and": "cd /workspace/solver\nkillall -9 cns2d 2>/dev/null; pkill -9 -f run_all.sh 2>/dev/null; sleep 2\nfor c in naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid naca00 |
| medium | suspicious_patterns | `54c28431` | and": "cd /workspace/solver\nkillall -9 cns2d 2>/dev/null; pkill -9 -f run_all.sh 2>/dev/null; sleep 2\nkillall -9 cns2d 2>/dev/null; sleep 1\necho \"cns2d: $(pgrep -x cns2d|wc -l)  run_al |
| medium | suspicious_patterns | `54c28431` | er\n# stop the background production\nkill %1 2>/dev/null; pkill -9 -f run_all.sh 2>/dev/null; killall -9 cns2d 2>/dev/null; sleep 2\nkillall -9 cns2d 2>/dev/null; sleep 1\necho \"cns2d: $ |
| medium | suspicious_patterns | `54c28431` | and": "cd /workspace/solver; for p in $(pgrep -x cns2d); do kill -9 $p; done; sleep 2; echo \"cns2d: $(pgrep -x cns2d|wc -l)\"", "description": "PID-kill remaining ranks"} |
| medium | suspicious_patterns | `54c28431` | {"command": "cd /workspace/solver\npkill -9 -f run_all.sh 2>/dev/null; for p in $(pgrep -x cns2d); do kill -9 $p; done; sleep 2\necho \"cns2d: $(pgrep -x cns2d|wc - |
| medium | suspicious_patterns | `54c28431` |  /workspace/solver; sleep 2; for p in $(pgrep -x cns2d); do kill -9 $p; done; sleep 1; echo \"cns2d: $(pgrep -x cns2d|wc -l)\"", "description": "Confirm all killed"} |
| medium | suspicious_patterns | `54c28431` | and": "cd /workspace/solver\nfor p in $(pgrep -x cns2d); do kill -9 $p; done 2>/dev/null; sleep 1\nfor c in naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid naca0012_m0 |
| medium | suspicious_patterns | `54c28431` | p -iE \"error:\" | head; echo built\ncd /workspace/solver\npkill -9 -f run_all.sh 2>/dev/null; for p in $(pgrep -x cns2d); do kill -9 $p; done; sleep 3\nfor p in $(pgrep -x cns2d); do kill |
| medium | suspicious_patterns | `54c28431` |  /workspace/solver; sleep 2; for p in $(pgrep -x cns2d); do kill -9 $p; done 2>/dev/null; sleep 1; echo \"cns2d: $(pgrep -x cns2d|wc -l)\"\n# verify the CFL cap builds & works on m080 quic |
| medium | suspicious_patterns | `54c28431` | \" | head -1)\necho \"run_all pid: $RA\"\n[ -n \"$RA\" ] && kill -9 $RA 2>/dev/null\nsleep 1\n# kill m200_inviscid ranks specifically\nfor p in $(pgrep -f \"m200_inviscid --output\"); do k |
| medium | suspicious_patterns | `54c28431` | solver\nfor p in $(pgrep -f \"naca0012_m200_inviscid\"); do kill -9 $p 2>/dev/null; done\nsleep 2\necho \"=== surviving (should be only Re200) ===\"; pgrep -af \"cns2d solve\"|grep -oE \"c |
| medium | suspicious_patterns | `54c28431` | ace/solver\nfor p in $(pgrep -f naca0012_m200_inviscid); do kill -9 $p 2>/dev/null; done; sleep 1\nexport LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:$LD_LIBRARY_PATH\nrm |
| medium | suspicious_patterns | `54c28431` | ace/solver\nfor p in $(pgrep -f naca0012_m200_inviscid); do kill -9 $p 2>/dev/null; done; sleep 1\nexport LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:$LD_LIBRARY_PATH\nrm |
| medium | suspicious_patterns | `54c28431` | ace/solver\nfor p in $(pgrep -f naca0012_m200_inviscid); do kill -9 $p 2>/dev/null; done; sleep 2\nfor p in $(pgrep -f naca0012_m200_inviscid); do kill -9 $p 2>/dev/null; done\nrm -rf resu |

## Reviews

- Code review scorecard: `claude_generic_opus-10_f6b586/review_code.md` (overall: 4.5)
- CFD methods review: `claude_generic_opus-10_f6b586/review_cfd.md` (overall: 4.5)
- Result review: `claude_generic_opus-10_f6b586/review_results.md` (overall: 4.5)
