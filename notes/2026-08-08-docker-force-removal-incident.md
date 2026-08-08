# Incident: four benchmark containers force-removed

Date/time: 2026-08-08, 15:20:10.9 - 15:20:11.5 CST (UTC+8)

Status: mechanism established with evidence; the signal sender is unidentified
(no auditd, no surviving trace).

Impact: runs 08 (codex_dsv4_flash), 09 (codex_dsv4_flash-1), 04 (codex_glm52),
01 (oc-goal_gpt56-dsv4) killed mid-turn. Session/DB persistence worked as
designed: all four were resumed the same evening from the same workspaces.

---

## TL;DR

The four launcher foreground process groups (start.sh + docker run -it in each
tmux pane) received a signal at ~15:20:10.8-11.2. docker run's signal proxy
(default on) carried it into each container (PID 1 = /bin/bash). Interactive
bash exits on SIGHUP and forwards HUP to its jobs, so the codex/opencode
TUI + app-server died "handled" - the glm52 TUI's last screen shows
"Conversation interrupted - tell the model what to do". The containers' tasks
ended, containerd fired task-delete, dockerd's --rm auto-removal started, and
each launcher's EXIT trap then raced it with `docker rm -f`, producing the
harmless "error removing container ... is already in progress" lines.
The rm -f was a reaction, not the cause.

---

## Containers

  docker ID      name                  run / workspace                     task-delete CST
  d5c889b6f...   codex_dsv4_flash      08 / workspace/codex/dsv4_flash/08  15:20:10.923
  a9c761019...   codex_dsv4_flash-1    09 / workspace/codex/dsv4_flash/09  15:20:10.934
  f5b33c0ca...   codex_glm52           04 / workspace/codex/glm52-m3/04    15:20:11.011
  b7efe2679...   oc-goal_gpt56-dsv4    01 / workspace/oc-goal/gpt56-dsv4/01 15:20:11.295

Scope CPU consumed: d5c889 2h30m31s, a9c761 25m55s, f5b33c 4d23h41m,
b7efe2 4d1h14m. 08/09 IDs are pinned via ocx conversation IDs matching the
rollouts' last tool calls; glm52/oc-goal by elimination.

---

## Timeline (CST)

  Jul 18 18:08    tmux server started (pid 1035649; socket birth 18:08:59) - still running
  Aug 3-4         glm52 / oc-goal containers started (~5 days old at death)
  11:46:33        tmux new -s codex_dsv4_flash (original session; died 20:09:12 by user
                  `exit`, NOT the incident)
  11:49:41        original start.sh for run 08
  11:59:35        original codex_dsv4_flash-1 session (shell pid 4140073, alive through incident)
  12:00:49        original start.sh for run 09
  13:58:33-14:00  user attached to benchmark sessions (tmux attach)
  15:19:02.9      run 09's last LLM request starts (ocx k04); no first output for ~68 s
  15:19:10-11     last 31081-tunnel automation SSH session (72981) - no -R bind error logged
                  (behaved differently); then 102 s connection gap until 15:20:53
  15:19:21.6      glm52 request (k05) starts; first output 15:19:35.6; completes 200 at 15:19:58.6
  15:19:44.8      run 08's last LLM request starts (ocx k07); no first output
  15:19:48.0      oc-goal opencode starts a stream (last opencode.log line)
  15:20:10.0x     two host socat children (581661, 1081642) log "Connection reset by peer"
                  - the dying containers' keep-alive proxy connections (effect, not cause)
  15:20:10.913/.923  ocx logs client_closed_request / client_cancel (499) for k07 / k04
  15:20:10.923/.934  containerd task-delete + shim disconnected for d5c889 / a9c761;
                     docker scopes deactivated
  15:20:10.990/.994  dockerd "error removing container ... already in progress"
                     (trap rm -f raced --rm)
  15:20:11.011/11.295  task-delete for f5b33c / b7efe2; removal errors 11.095 / 11.539
  20:05:08        commit c8b6f4a adds --detach mode ("survives terminal loss")
                  - landed ~5 h after the incident
  20:09:12/20:09:23  user `exit` kills original flash session; tmux new -s codex_dsv4_flash
  20:10:09/20:12:19/20:16:26/20:30:52  restarts of runs 08/09/04/01
                  (09, 04, 01 inside the original surviving sessions)

---

## Signal chain (best-supported reconstruction)

1. ~15:20:10.8-11.2: an unknown sender delivers a SIGHUP-class signal to the four
   launcher foreground process groups {start.sh, docker run} (per-pane fg group;
   verified layout on the restarted runs: pgid = start.sh pid, sid = pane shell,
   tty = pane pts). Sender unknown: no auditd, no journal trace, no process from
   that minute survives.
2. docker run (sig-proxy default on) forwards the signal to the container's PID 1,
   /bin/bash (verified in-container trees: PID 1 = bash, `codex -p ocx resume` /
   `opencode -c` as child).
3. Interactive bash exits on SIGHUP and forwards HUP to its jobs; the codex
   app-server/TUI catches it (glm52 old TUI printed "Conversation interrupted" -
   a handled signal, impossible under a rm -f SIGKILL), the ocx HTTP client dies,
   ocx logs the cancel at the same millisecond.
4. Container task ends -> containerd task-delete -> dockerd --rm auto-removal starts.
5. docker run returns; start.sh's `wait` returns; EXIT trap fires `docker rm -f`
   -> races --rm -> dockerd "already in progress" (no-op).

## Signal identity

SIGHUP is the best fit. Interactive bash ignores SIGINT and SIGTERM for itself,
so a proxied INT/TERM to PID 1 would not have killed the container; HUP is the
one of the three that makes interactive bash exit (and forward HUP to jobs).
The stdin-EOF path (CLI dies -> bash gets EOF) is possible in principle but does
not explain the TUI's "Conversation interrupted" message (remaining container
processes would be SIGKILLed).

---

## Ruled out

- Network/upstream: ocx (host pid 2862984, port 10109) stayed up; oc-goal-kimik3
  and probe clients had healthy status-200 requests at 15:20:10-11; glm52's
  request completed 200 at 15:19:58; user SSH (59.66.142.39) stayed connected.
  The 102 s gap was only in the 31081-SOCKS-tunnel automation (216.228.127.131),
  which is this host's own ~/.ssh/config proxy infra, not the host's network.
- Tmux panes/sessions: three of the four pane shells survived (pids 4140073 @
  11:59:35, 1831664 @ Aug 7 03:17, 1975082 @ Aug 7 03:41 - alive after incident);
  codex_dsv4_flash's session died only at 20:09:12 from a typed `exit`. Server
  alive since Jul 18; destroy-unattached off.
- User action: zsh history empty between 14:00:20 and 19:38:13.
- Cron/timers/systemd: user crontab has only restoreConnectivity.sh (docker
  restart commented; single instance stuck in scp since Aug 4) and
  refreshCronLog.sh; user systemd has only opencodex-proxy.service and
  launchpadlib-cache-clean.timer; no system timers at :10.
- OOM/kernel: no oom-kill, no NIC/link events; only the constant aTrustTray
  segfault loop (~every 4 s, 5 days old; the atrust container no longer exists
  but the crash loop continues per kernel log).
- dockerd/containerd/logind: dockerd up 3+ weeks, NRestarts=0; no logind session
  teardown at 15:20:10.
- The trap's rm -f as cause: could not have produced the TUI's handled
  interruption message; ordering shows the task was already dead when the
  removal raced.

---

## Open questions

- Who sent the signal? Only suspicious coincidence: the kills landed ~60.9-61.3 s
  after SSH session 72981 (15:19:10-11), which did not log the usual 31081 bind
  error - consistent with a `sleep 60 && kill`-style one-shot from the host's own
  tunnel automation, but there is no evidence of the command content (sshd does
  not log commands; the process is gone).
- Exact signal (SIGHUP most likely, see above).

## Recommendations

- Install auditd rules for kill/tgkill/killpg by the user to make future signal
  senders identifiable.
- start.sh: interactive mode should not `docker rm -f` on EXIT (race with --rm),
  should use --sig-proxy=false, and should prefer --detach (added at 20:05 the
  same day, after this incident).
- Keep the session-persistence design: it worked - all four runs resumed cleanly
  from the same workspaces.

## Evidence locations

- Journal: journalctl -u docker / -u containerd / -k / _COMM=sshd /
  -u systemd-logind --since '2026-08-08 15:18' --until '2026-08-08 15:22'
- ocx usage: ~/.opencodex/usage.jsonl lines 31372-31373 (k04/k07 client_cancel
  499 at death ms)
- Codex logs: <ws>/.sessions/codex/logs_2.sqlite -
  select datetime(ts,'unixepoch'),level,target,... where ts between 1786173480 and 1786173660
- Rollouts: workspace/codex/dsv4_flash/{08,09}/.sessions/codex/sessions/2026/08/08/
  rollout-2026-08-08T{03-50-03,04-01-04}-*.jsonl (last entries 07:19:44Z / 07:19:02Z;
  resumed into the same files after restart - entries absent 07:20:11Z-12:10Z)
- oc-goal: <ws>/.sessions/opencode-data/opencode/log/opencode.log lines 22969-22998
  (last stream 07:19:48Z)
- tmux: pane shells pids above; tmux-spawn scopes under
  /run/user/1004/systemd/transient/ (mtimes = session creation)
