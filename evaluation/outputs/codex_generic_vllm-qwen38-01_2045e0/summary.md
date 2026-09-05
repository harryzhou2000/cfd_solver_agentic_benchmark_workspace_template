# Final Result Summary — vllm-qwen38-01

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/vllm-qwen38-01`
- Branch: `codex/generic/vllm-qwen38-01` commit `e185a0a43f42781f93348a4dc85c7f8aecfb046c`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/vllm-qwen38-01/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/vllm-qwen38-01/solver/.smoke`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/vllm-qwen38-01/solver/report`)
- Session window: 2026-08-22T04:04:11.736000+00:00 → 2026-08-28T06:50:11.189000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-22T04:04:11.736000+00:00 → 2026-08-28T06:50:11.189000+00:00; 294×1800s buckets; idle 96 gaps / 372940s excluded; permission-wait candidates 0; tokens 348,215,121 (cache hit 0.9608)

## Expenses

- Goal time: **456994 s**
- Wall time: **528360 s**
- Tokens: **348,215,121** (main 245,536,885 / subagents 102,678,236)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `01a027a3` | complete | vllm/qwen3.8-27b-int8-w8a16-mtp | 11 | 348,215,121 | 456994 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| vllm/qwen3.8-27b-int8-w8a16-mtp | 343,152,963 | 329,711,088 | 5,062,158 | 348,215,121 |

- Cost estimate: **$33.75** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **2,970**; top tools: exec=2604, exec_command=134, wait_agent=76, wait=46, apply_patch=31, update_plan=26
- Subagent spawns: 10
- LOC (file scan): 17,662 lines / 108 files
- LOC (git tracked): 6,895 lines

## Metadata

- Harness: codex cli 0.148.0 (codex-tui, provider openai)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote plugin-management, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents
- AGENTS.md: sha256 7aa6dde4c59a (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| vllm/qwen3.8-27b-int8-w8a16-mtp | ultra | 256000 | 124,735,672 | 11 |

### opencodex router (non-vanilla models: vllm/qwen3.8-27b-int8-w8a16-mtp)

- opencodex version: None (submodule None)
- config facts: {"default_provider": "deepseek", "providers": ["BLSC", "deepseek", "kimi-code", "ollama", "openai", "opencode-free", "vllm", "xiaomi-mimo"], "multi_agent_mode": "v2", "subagent_models": ["gpt-5.6-terra", "gpt-5.6-luna", "deepseek/deepseek-v4-flash", "BLSC/GLM-5.3", "kimi-code/k3"], "disabled_models": ["BLSC/Baichuan-M2", "BLSC/Baichuan-M3", "BLSC/Baichuan-M2-128K", "BLSC/DeepSeek-R1", "BLSC/DeepSe

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `01a0293f` | `01a027a3` | Erdos | audit_outputs | vllm/qwen3.8-27b-int8-w8a16-mtp | ultra | 2,378,069 |
| `01a0293f` | `01a027a3` | Carson | audit_numerics | vllm/qwen3.8-27b-int8-w8a16-mtp | ultra | 2,205,763 |
| `01a02b48` | `01a027a3` | Aristotle | report_writer | vllm/qwen3.8-27b-int8-w8a16-mtp | ultra | 30,399,608 |
| `01a02bf1` | `01a027a3` | Huygens | pipeline_prep | vllm/qwen3.8-27b-int8-w8a16-mtp | ultra | 7,652,052 |
| `01a02c21` | `01a027a3` | Hilbert | pipeline_review | vllm/qwen3.8-27b-int8-w8a16-mtp | ultra | 2,929,242 |
| `01a03343` | `01a027a3` | Ampere | tooling_fixes | vllm/qwen3.8-27b-int8-w8a16-mtp | ultra | 2,997,880 |
| `01a03d4b` | `01a027a3` | Averroes | report_fill | vllm/qwen3.8-27b-int8-w8a16-mtp | ultra | 4,013,651 |
| `01a03d93` | `01a027a3` | McClintock | report_fill2 | vllm/qwen3.8-27b-int8-w8a16-mtp | ultra | 44,764,753 |
| `01a03d9c` | `01a03d93` | Arendt | figs_sanity | vllm/qwen3.8-27b-int8-w8a16-mtp | ultra | 1,170,424 |
| `01a03df8` | `01a027a3` | Epicurus | aitk_np2_audit | vllm/qwen3.8-27b-int8-w8a16-mtp | ultra | 4,166,794 |

### Prompts

- `01a027a3` goal: Complete the task defined in cfd_solver_agentic_benchmark/ .
  - initial: /goal Complete the task defined in cfd_solver_agentic_benchmark/ .
  - resume: /goal resume
  - resume: /goal resume
  - resume: /goal resume

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `01a027a3` | d solver && python3 -m venv .venv 2>&1 | tail -2; .venv/bin/pip install --quiet numpy matplotlib h5py 2>&1 | tail -3; echo DONE"}); text(d.output) |
| medium | network_access | `01a027a3` | tools.exec_command({cmd: "cd /workspace/solver && .venv/bin/pip install numpy matplotlib h5py 2>&1 | tail -5; .venv/bin/python -c \"import numpy, h5py, matplotlib; print('py ok')\"", yield_ti |
| medium | network_access | `01a02b48` | const cmd = 'cd /workspace/solver/report && (curl -sL --max-time 30 https://mirrors.ctan.org/macros/latex/contrib/caption/subcaption.sty -o _ctan_subcaption.sty 2>&1 || w |
| medium | network_access | `01a02b48` | const cmd = 'cd /workspace/solver/report && curl -sL --max-time 30 https://mirrors.ctan.org/macros/latex/contrib/caption/subcaption.sty -o _ctan_subcaption.sty; ls -la _ |
| medium | network_access | `01a02b48` | const cmd = 'cd /workspace/solver/report && curl -sL --max-time 40 https://raw.githubusercontent.com/TeX-Live/texlive/master/texmf-dist/tex/latex/caption/subcaption.sty  |
| medium | network_access | `01a02b48` | const cmd = 'cd /workspace/solver/report && curl -sL --max-time 60 https://mirrors.ctan.org/macros/latex/contrib/caption.zip -o _caption.zip; ls -la _caption.zip; file _ |
| high | unauthorized_remote_mutations | `01a027a3` | const p2 = await tools.exec_command({cmd: "cd /workspace && git checkout -b solver/attempt1 && git add solver && git status --short | wc -l && git commit -q -m 'w10c: W-set volume knob (CFD_WVO |
| medium | suspicious_patterns | `01a027a3` | 5636 2>/dev/null; then echo 'R4 still alive, sending KILL'; kill -9 65636; sleep 1; fi; kill -0 65636 2>/dev/null && echo 'R4 STILL ALIVE' || echo 'R4 dead'; echo '--- R4 final ---'; tail  |
| medium | suspicious_patterns | `01a027a3` | 4; for p in 66285 66308; do if kill -0 $p 2>/dev/null; then kill -9 $p; sleep 1; fi; kill -0 $p 2>/dev/null && echo \"$p STILL ALIVE\" || echo \"$p dead\"; done; echo '--- visc15_V1 final  |
| medium | suspicious_patterns | `01a027a3` | eep 4; for p in 65617 65803; do kill -0 $p 2>/dev/null && { kill -9 $p; echo $p killed-9; } || echo $p dead; done; echo '--- archive m080inv J1 ---'; rm -rf /workspace/solver/results/naca0 |
| medium | suspicious_patterns | `01a027a3` | eep 4; for p in 65617 65803; do kill -0 $p 2>/dev/null && { kill -9 $p; echo $p killed-9; } || echo $p dead; done", workdir: "/workspace/solver"}); text(r.output); |
| medium | suspicious_patterns | `01a027a3` | lver && kill 66809; sleep 3; kill -0 66809 2>/dev/null && { kill -9 66809; sleep 1; }; kill -0 66809 2>/dev/null && echo 'STILL ALIVE' || echo 'visc15_V2 dead'; tail -2 .smoke/visc15_V2/re |
| medium | suspicious_patterns | `01a027a3` | lver && kill 66821; sleep 3; kill -0 66821 2>/dev/null && { kill -9 66821; sleep 1; }; kill -0 66821 2>/dev/null && echo 'STILL ALIVE' || echo 'visc200_V2 dead'; bash .smoke/launch_artif_m |
| medium | suspicious_patterns | `01a027a3` | driver.cpp st_acand/acand sized nown+nghost + halo copy pre-eval (steady+transient), rank-uniform MPI_MAX any (no-op np1), residual.cpp NaN->inf norm guard + limits include; np1-neutrali |
| medium | suspicious_patterns | `01a027a3` | sleep 2 if kill -0 74229 2>/dev/null; then echo KILL_RETRY; kill -9 74229; sleep 1; fi kill -0 74229 2>/dev/null && echo KILL_FAILED || echo KILL_OK_74229 kill -0 74301 2>/dev/null && echo |
| medium | suspicious_patterns | `01a027a3` |  4722f3a): st_acand/acand sized nown+nghost + halo copy pre-eval (steady+transient), rank-uniform MPI_MAX on any (no-op at np1), residual.cpp NaN->inf norm guard.\n- Verification chain ( |
| medium | suspicious_patterns | `01a02b48` | ' '; echo; ps -o pid,ppid,etimes,cmd -p 34388 2>/dev/null; pkill -9 -f 'pdflatex -interaction' ; sleep 1; pgrep -a pdflatex || echo 'none left'; ps -ef | grep -c '[r]eport.tex'"}); text(r. |
| medium | suspicious_patterns | `01a02b48` | const r = await tools.exec_command({cmd: "pkill -9 -f 'pdflatex -interaction' 2>/dev/null; sleep 1; pgrep -a pdflatex || echo none; cd /workspace/solver/report && kpsewhic |
| medium | suspicious_patterns | `01a02b48` | const r1 = await tools.exec_command({cmd: "pkill -9 -f pdflatex 2>/dev/null; sleep 2; pgrep -a pdflatex || echo CLEAR1; sleep 3; pgrep -a pdflatex || echo CLEAR2"}); text(r |
| medium | suspicious_patterns | `01a02b48` | fFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg== | base64 -d > t2_img.png'); function figdoc(preamble, panel) {   return ['\\documentclass{article}','\\usepackage{graphicx}',preamb |
| medium | suspicious_patterns | `01a02b48` | const r = await tools.exec_command({cmd: "pkill -9 -f pdflatex 2>/dev/null; sleep 1; pgrep -a pdflatex; pgrep -a pdflatex; cd /workspace/solver/report && : > report.aux && |
| medium | suspicious_patterns | `01a02b48` | const r = await tools.exec_command({cmd: "pkill -9 -f pdflatex 2>/dev/null; sleep 1; pgrep -a pdflatex; pgrep -a pdflatex; cd /workspace/solver/report && : > report.aux && |

## Reviews

- Code review scorecard: `codex_generic_vllm-qwen38-01_2045e0/review_code.md` (overall: 2.0)
- CFD methods review: `codex_generic_vllm-qwen38-01_2045e0/review_cfd.md` (overall: 2.0)
- Result review: `codex_generic_vllm-qwen38-01_2045e0/review_results.md` (overall: 2.0)
