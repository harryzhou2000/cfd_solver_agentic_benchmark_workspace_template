/* ====================================================================
 * CFD Benchmark Evaluation — frontend SPA
 * Vanilla JS, no build step, no dependencies. Talks to /api/* via fetch.
 * Two views: (A) snapshot list, (B) detail. Hash routing.
 * ==================================================================== */
(() => {
  "use strict";

  /* ----------------------------------------------------------------
   * Constants & helpers
   * ---------------------------------------------------------------- */
  const TABLE_COLUMNS = [
    { key: "run_id",         label: "Run ID",       type: "text",  sortType: "str" },
    { key: "primary_model_effort", label: "Primary model", type: "text", sortType: "str" },
    { key: "harness",        label: "Harness",      type: "text",  sortType: "str" },
    { key: "status",         label: "Status",       type: "pill-status", sortType: "status" },
    { key: "goal_time_s",    label: "Goal time",    type: "duration", sortType: "num" },
    { key: "wall_time_s",    label: "Wall time",    type: "duration", sortType: "num" },
    { key: "activity_time_s",label: "Activity",     type: "duration", sortType: "num" },
    { key: "input_tokens",   label: "Input",        type: "tokens",  sortType: "num" },
    { key: "cached_input_tokens", label: "Cached",  type: "tokens",  sortType: "num" },
    { key: "output_tokens",  label: "Output",       type: "tokens",  sortType: "num" },
    { key: "tokens",         label: "Total",        type: "tokens",  sortType: "num" },
    { key: "cost_usd",       label: "Cost",         type: "money",   sortType: "num" },
    { key: "subagents",      label: "Subagents",    type: "int",     sortType: "num" },
    { key: "code_score",     label: "Code",         type: "score",   sortType: "num" },
    { key: "cfd_score",      label: "CFD",          type: "score",   sortType: "num" },
    { key: "result_score",   label: "Results",      type: "score",   sortType: "num" },
    { key: "rubric_total",   label: "Rubric",       type: "int",     sortType: "num" },
    { key: "disqualified",   label: "DQ",           type: "dq",      sortType: "bool" },
    { key: "execution_date", label: "Execution",    type: "text",    sortType: "str" },
    { key: "cache_hit",      label: "Cache hit",    type: "pct",     sortType: "num" },
    { key: "session_buckets",label: "Buckets",      type: "int",     sortType: "num" },
    { key: "env_capture_phase", label: "Env",       type: "text",    sortType: "str" },
    { key: "agent_reviewed", label: "Reviewed",     type: "bool",    sortType: "bool" },
    { key: "codegraph",      label: "Codegraph",    type: "bool",    sortType: "bool" },
    { key: "submodule",      label: "Submodule",    type: "text",    sortType: "str" },
    { key: "branch",         label: "Branch",       type: "text",    sortType: "str" },
  ];

  const $  = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

  const escapeHtml = (s) => String(s ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");

  const emDash = "\u2014";

  const isNullish = (v) => v === null || v === undefined;

  function fmtInt(n) {
    if (isNullish(n)) return emDash;
    return Math.round(n).toLocaleString("en-US");
  }

  function fmtTokens(n) {
    if (isNullish(n)) return emDash;
    const abs = Math.abs(n);
    if (abs >= 1e9) return (n / 1e9).toFixed(2).replace(/\.?0+$/, "") + "B";
    if (abs >= 1e6) return (n / 1e6).toFixed(2).replace(/\.?0+$/, "") + "M";
    if (abs >= 1e3) return (n / 1e3).toFixed(1).replace(/\.?0+$/, "") + "k";
    return String(Math.round(n));
  }

  function fmtMoney(n) {
    if (isNullish(n)) return emDash;
    if (n >= 1000) return "$" + (n / 1000).toFixed(2) + "k";
    if (n >= 1)    return "$" + n.toFixed(2);
    return "$" + n.toFixed(4);
  }

  function fmtScore(n) {
    if (isNullish(n)) return emDash;
    const v = Number(n);
    if (Number.isNaN(v)) return emDash;
    if (v <= 1) return (v * 100).toFixed(0) + "%";
    return v.toFixed(2);
  }

  function fmtPct(n) {
    if (isNullish(n)) return emDash;
    const v = Number(n);
    if (Number.isNaN(v)) return emDash;
    return (v * 100).toFixed(1) + "%";
  }

  function fmtDuration(secs) {
    if (isNullish(secs)) return emDash;
    const s = Number(secs);
    if (!Number.isFinite(s)) return emDash;
    if (s < 60) return s.toFixed(1) + "s";
    const totalMin = Math.floor(s / 60);
    const h = Math.floor(totalMin / 60);
    const m = totalMin % 60;
    if (h > 0) return `${h}h ${m}m`;
    const remSec = Math.round(s - totalMin * 60);
    return `${m}m ${remSec}s`;
  }

  function fmtBytes(n) {
    if (isNullish(n)) return emDash;
    const b = Number(n);
    if (!Number.isFinite(b)) return emDash;
    if (b < 1024) return b + " B";
    if (b < 1024 * 1024) return (b / 1024).toFixed(1) + " KB";
    if (b < 1024 * 1024 * 1024) return (b / 1024 / 1024).toFixed(2) + " MB";
    return (b / 1024 / 1024 / 1024).toFixed(2) + " GB";
  }

  function fmtDate(iso) {
    if (!iso) return emDash;
    try {
      const d = new Date(iso);
      if (isNaN(d.getTime())) return iso;
      return d.toISOString().replace("T", " ").replace(/\.\d+Z$/, "Z");
    } catch (_) { return String(iso); }
  }

  function pillFor(value, palette) {
    const s = escapeHtml(value == null ? emDash : value);
    const cls = palette || "pill-info";
    return `<span class="pill ${cls}">${s}</span>`;
  }

  function statusPill(status) {
    if (!status) return pillFor(emDash, "pill-status-other");
    const s = String(status).toLowerCase();
    if (s === "complete") return pillFor(status, "pill-status-complete");
    if (s === "blocked")  return pillFor(status, "pill-status-blocked");
    if (s === "paused")   return pillFor(status, "pill-status-paused");
    if (s === "needs_user_input") return pillFor(status, "pill-status-needs_user_input");
    return pillFor(status, "pill-status-other");
  }

  function boolPill(v) {
    if (v === true)  return `<span class="pill pill-yes">yes</span>`;
    if (v === false) return `<span class="pill pill-no">no</span>`;
    return `<span class="pill pill-status-other">${emDash}</span>`;
  }

  function dqPill(v) {
    if (v === true)  return `<span class="pill pill-status-blocked">yes</span>`;
    if (v === false) return `<span class="pill pill-no">no</span>`;
    return `<span class="pill pill-status-other">${emDash}</span>`;
  }

  function cellRenderer(col, row) {
    const v = row[col.key];
    switch (col.type) {
      case "text":        return `<span title="${escapeHtml(v ?? "")}">${escapeHtml(v ?? emDash)}</span>`;
      case "int":         return fmtInt(v);
      case "duration":    return fmtDuration(v);
      case "tokens":      return fmtTokens(v);
      case "money":       return fmtMoney(v);
      case "score":       return fmtScore(v);
      case "pct":         return fmtPct(v);
      case "bool":        return boolPill(v);
      case "dq":          return dqPill(v);
      case "pill-status": return statusPill(v);
      default:            return escapeHtml(v ?? emDash);
    }
  }

  /* ----------------------------------------------------------------
   * App state
   * ---------------------------------------------------------------- */
  const state = {
    snapshots: [],
    sortKey: "run_id",
    sortDir: "asc",
    filter: "",
    currentName: null,
    detailCache: new Map(),   // contestant name -> detail payload
    detailTab: "summary",
  };

  /* ----------------------------------------------------------------
   * HTTP
   * ---------------------------------------------------------------- */
  async function fetchJson(url, opts = {}) {
    const resp = await fetch(url, opts);
    if (!resp.ok) {
      let detail = "";
      try { detail = (await resp.json()).error || ""; } catch (_) {}
      throw new Error(`HTTP ${resp.status} ${resp.statusText}${detail ? " — " + detail : ""}`);
    }
    return resp.json();
  }

  /* ----------------------------------------------------------------
   * Spinner / banner helpers
   * ---------------------------------------------------------------- */
  const spinner = () => $("#spinner");
  function showSpinner() { spinner().hidden = false; }
  function hideSpinner() { spinner().hidden = true; }

  function showBanner(el, msg, kind = "err") {
    if (!el) return;
    el.className = "banner" + (kind === "info" ? " info" : kind === "warn" ? " warn" : "");
    el.textContent = msg;
    el.hidden = false;
  }
  function hideBanner(el) {
    if (!el) return;
    el.hidden = true;
    el.textContent = "";
  }

  /* ----------------------------------------------------------------
   * List view: header, sort, render
   * ---------------------------------------------------------------- */
  function buildTableHeader() {
    const tr = $("#snapshots-thead-row");
    tr.innerHTML = TABLE_COLUMNS.map(col => {
      const sorted = state.sortKey === col.key;
      const arrow = sorted ? (state.sortDir === "asc" ? "\u25B2" : "\u25BC") : "\u00A0";
      return `<th class="${sorted ? "is-sorted" : ""}" data-key="${col.key}">` +
        `<span>${escapeHtml(col.label)}</span>` +
        `<span class="sort-ind">${arrow}</span></th>`;
    }).join("");
    $$("#snapshots-thead-row th").forEach(th => {
      th.addEventListener("click", () => {
        const key = th.getAttribute("data-key");
        if (state.sortKey === key) {
          state.sortDir = state.sortDir === "asc" ? "desc" : "asc";
        } else {
          state.sortKey = key;
          state.sortDir = "asc";
        }
        renderTable();
      });
    });
  }

  function compareRows(a, b, key, type) {
    const av = a[key], bv = b[key];
    if (isNullish(av) && isNullish(bv)) return 0;
    if (isNullish(av)) return 1;     // nulls sort last in asc
    if (isNullish(bv)) return -1;
    let cmp;
    if (type === "str") cmp = String(av).localeCompare(String(bv));
    else if (type === "status") cmp = String(av).localeCompare(String(bv));
    else if (type === "bool") cmp = (av === true ? 1 : 0) - (bv === true ? 1 : 0);
    else cmp = Number(av) - Number(bv);
    return cmp;
  }

  function applyFilter(rows) {
    const q = state.filter.trim().toLowerCase();
    if (!q) return rows;
    return rows.filter(r => {
      const a = (r.contestant || "").toLowerCase();
      const b = (r.harness    || "").toLowerCase();
      const c = (r.branch     || "").toLowerCase();
      return a.includes(q) || b.includes(q) || c.includes(q);
    });
  }

  function applySort(rows) {
    const col = TABLE_COLUMNS.find(c => c.key === state.sortKey) || TABLE_COLUMNS[0];
    const dir = state.sortDir === "asc" ? 1 : -1;
    return rows.slice().sort((a, b) => compareRows(a, b, col.key, col.sortType) * dir);
  }

  function renderTable() {
    buildTableHeader();
    const tbody = $("#snapshots-tbody");
    const rows = applySort(applyFilter(state.snapshots));

    if (state.snapshots.length === 0) {
      $("#table-wrap").hidden = true;
      $("#empty-state").hidden = false;
      return;
    }
    if (rows.length === 0) {
      $("#table-wrap").hidden = true;
      $("#empty-state").hidden = false;
      $("#empty-state").innerHTML =
        `<p>No snapshots match <code>${escapeHtml(state.filter)}</code>.</p>`;
      return;
    }
    $("#empty-state").hidden = true;
    $("#table-wrap").hidden = false;
    tbody.innerHTML = rows.map((row, i) => {
      const cells = TABLE_COLUMNS.map(col => {
        const cls = (col.type === "num" || col.type === "duration" ||
                     col.type === "tokens" || col.type === "money" ||
                     col.type === "score" || col.type === "pct" ||
                     col.type === "int")
                     ? " class=\"num\"" : "";
        return `<td${cls}>${cellRenderer(col, row)}</td>`;
      }).join("");
      return `<tr data-name="${escapeHtml(row.contestant)}">${cells}</tr>`;
    }).join("");

    $$("#snapshots-tbody tr").forEach(tr => {
      tr.addEventListener("click", () => {
        const name = tr.getAttribute("data-name");
        if (name) location.hash = "#/detail/" + encodeURIComponent(name);
      });
    });
  }

  async function loadSnapshots() {
    const banner = $("#banner");
    hideBanner(banner);
    showSpinner();
    try {
      const data = await fetchJson("/api/snapshots");
      state.snapshots = Array.isArray(data.snapshots) ? data.snapshots : [];
      renderTable();
      $("#topbar-status").textContent =
        `${state.snapshots.length} snapshot${state.snapshots.length === 1 ? "" : "s"}`;
    } catch (err) {
      state.snapshots = [];
      renderTable();
      showBanner(banner, "Failed to load snapshots: " + err.message, "err");
    } finally {
      hideSpinner();
    }
  }

  /* ----------------------------------------------------------------
   * Routing
   * ---------------------------------------------------------------- */
  function parseHash() {
    const h = location.hash || "#/";
    const m = h.match(/^#\/detail\/(.+)$/);
    if (m) return { view: "detail", name: decodeURIComponent(m[1]) };
    return { view: "list", name: null };
  }

  async function route() {
    const r = parseHash();
    if (r.view === "list") {
      $("#view-detail").hidden = true;
      $("#view-list").hidden = false;
      state.currentName = null;
      return;
    }
    $("#view-list").hidden = true;
    $("#view-detail").hidden = false;
    if (state.currentName !== r.name) {
      state.currentName = r.name;
      state.detailTab = "summary";
      await renderDetail(r.name);
    }
  }

  window.addEventListener("hashchange", route);

  /* ----------------------------------------------------------------
   * Detail view
   * ---------------------------------------------------------------- */
  async function loadDetail(name) {
    if (state.detailCache.has(name)) return state.detailCache.get(name);
    const banner = $("#detail-banner");
    hideBanner(banner);
    showSpinner();
    try {
      const data = await fetchJson(`/api/snapshot/${encodeURIComponent(name)}`);
      state.detailCache.set(name, data);
      return data;
    } catch (err) {
      showBanner(banner, "Failed to load detail: " + err.message, "err");
      throw err;
    } finally {
      hideSpinner();
    }
  }

  async function renderDetail(name) {
    const detail = await loadDetail(name).catch(() => null);
    if (!detail) return;

    // header
    $("#detail-name").textContent = detail.name || name;
    const sum = detail.summary || {};
    const snap = sum.snapshot || {};
    const c = sum.contestant || {};
    const md = detail.metadata || sum.metadata || {};
    const har = md.harness || {};
    const status = md.status || (detail.name ? "unknown" : null);
    $("#detail-status").outerHTML = `<span id="detail-status" class="${status ? "pill " + statusPillClass(status) : "pill pill-status-other"}">${escapeHtml(status || emDash)}</span>`;

    const subtitleBits = [];
    if (c.branch || c.git_commit) {
      const branch = c.branch || "";
      const sha = (c.git_commit || "").slice(0, 10);
      subtitleBits.push(`<span class="kv"><span class="k">branch</span><span class="v">${escapeHtml(branch)}${sha ? " @ " + escapeHtml(sha) : ""}</span></span>`);
    }
    const sw = snap.session_window || md.session_window || {};
    if (sw.start || sw.end) {
      subtitleBits.push(`<span class="kv"><span class="k">window</span><span class="v">${escapeHtml(fmtDate(sw.start))} → ${escapeHtml(fmtDate(sw.end))}</span></span>`);
    }
    if (har.harness || har.version) {
      subtitleBits.push(`<span class="kv"><span class="k">harness</span><span class="v">${escapeHtml(har.harness || emDash)}${har.version ? " " + escapeHtml(har.version) : ""}</span></span>`);
    }
    const identity = detail.run_identity || {};
    if (identity.result_branch || identity.submission_commit) {
      subtitleBits.push(`<span class="kv"><span class="k">result</span><span class="v">${escapeHtml(identity.result_branch || emDash)}${identity.submission_commit ? " @ " + escapeHtml(identity.submission_commit.slice(0, 10)) : ""}</span></span>`);
    }
    $("#detail-subtitle").innerHTML = subtitleBits.join("");

    // metrics cards
    renderMetricsCards(sum, detail);

    // tab buttons (rebind)
    bindTabs();

    // render active tab
    switchTab(state.detailTab);
  }

  function statusPillClass(status) {
    const s = String(status).toLowerCase();
    if (s === "complete") return "pill-status-complete";
    if (s === "blocked")  return "pill-status-blocked";
    if (s === "paused")   return "pill-status-paused";
    if (s === "needs_user_input") return "pill-status-needs_user_input";
    return "pill-status-other";
  }

  function renderMetricsCards(sum, detail) {
    const ex = sum.expenses || {};
    const ts = ex.time_seconds || {};
    const sessionTokens = (((detail.sessions || {}).analysis || {}).whole_session_stats || {}).tokens || {};
    const selection = (detail.agent_scores || {}).session_selection || {};
    const primaryId = (selection.roots || [])[0];
    const primary = ((detail.metadata || {}).threads || {})[primaryId] || {};
    const efforts = Array.isArray(primary.reasoning_effort) ? primary.reasoning_effort : (primary.reasoning_effort ? [primary.reasoning_effort] : []);
    const cards = [
      { label: "Primary model", value: [primary.entry_model || primary.model, primary.entry_reasoning_effort || efforts[0]].filter(Boolean).join(" ") || emDash, sub: primaryId ? primaryId.slice(0, 13) : "" },
      { label: "Context window", value: fmtTokens(primary.model_context_window_recorded), sub: primary.context_used_max_input == null ? "usage unavailable" : `${fmtTokens(primary.context_used_max_input)} max observed input` },
      { label: "Total tokens",  value: fmtTokens(sessionTokens.total ?? ex.tokens?.total), sub: "selected root tree", cls: "is-strong" },
      { label: "Input", value: fmtTokens(sessionTokens.input), sub: `${fmtTokens(sessionTokens.non_cached_input)} non-cached` },
      { label: "Cached input", value: fmtTokens(sessionTokens.cached_input), sub: fmtPct(sessionTokens.input ? sessionTokens.cached_input / sessionTokens.input : null) },
      { label: "Output", value: fmtTokens(sessionTokens.output), sub: `${fmtTokens(sessionTokens.reasoning_output)} reasoning` },
      { label: "Cost",          value: fmtMoney((detail.current_cost_estimate || ex.cost_estimate_usd || {}).total), sub: detail.current_cost_estimate ? "latest price metadata" : (ex.cost_estimate_usd?.estimate ? "snapshot estimate" : "") },
      { label: "Goal time",     value: fmtDuration(ts.goal_time),    sub: ts.started_at ? "started " + fmtDate(ts.started_at).slice(0,16) : "" },
      { label: "Wall time",     value: fmtDuration(ts.wall_time),    sub: ts.ended_at   ? "ended "   + fmtDate(ts.ended_at).slice(0,16)   : "" },
      { label: "Activity time", value: fmtDuration(ex.time_seconds?.activity_time_seconds) },
    ];
    // sessions-derived cards
    const sess = detail.sessions;
    if (sess && sess.analysis) {
      const an = sess.analysis;
      const ws = an.whole_session_stats || {};
      const hit = (ws.cache || {}).hit_ratio;
      cards.push({
        label: "Cache hit", value: fmtPct(hit),
        sub: fmtInt((ws.cache || {}).cached_tokens) + " cached / " + fmtInt((ws.cache || {}).input_tokens) + " in",
      });
      cards.push({
        label: "Session buckets", value: (an.buckets || []).length,
        sub: an.bucket_seconds ? `${an.bucket_seconds}s each` : "",
      });
      const perms = an.permission_waits || {};
      cards.push({
        label: "Permission candidates", value: perms.candidate_count ?? emDash,
        sub: (perms.explicit_approval_events_found ? "explicit seen" : "no explicit events"),
      });
    }
    cards.push({
      label: "Subagents",
      value: ((detail.metadata && detail.metadata.subagents) || (sum.metadata && sum.metadata.subagents) || []).length || 0,
      sub: "metadata.subagents",
    });
    const scoreAreas = (detail.agent_scores || {}).scores || {};
    const cr = scoreAreas.code_review || sum.code_review || {};
    const cfd = scoreAreas.cfd_review || sum.cfd_review || {};
    const rr = scoreAreas.result_review || sum.result_review || {};
    cards.push({ label: "Code score",    value: fmtScore(cr.overall_score) });
    cards.push({ label: "CFD score",     value: fmtScore(cfd.overall_score) });
    cards.push({ label: "Result score",  value: fmtScore(rr.overall_score) });
    const rubric = (detail.agent_scores || {}).rubric || {};
    cards.push({ label: "Rubric total",  value: rubric.total_scored == null ? emDash : `${rubric.total_scored}/${rubric.total_possible ?? emDash}` });
    const dq = (detail.agent_scores || {}).disqualification || {};
    cards.push({ label: "Disqualified", value: dq.triggered === true ? "yes" : dq.triggered === false ? "no" : emDash, cls: dq.triggered ? "is-err" : "" });
    cards.push({ label: "Execution date", value: selection.execution_date || emDash, sub: "UTC primary-session start" });
    const env = detail.env_snapshot || {};
    const envCap = detail.env_captured === true;
    const legacyPostRun = env.provenance && (env.provenance.pre_run_authority === false || String(env.provenance.capture_kind || "").toLowerCase().includes("post-run"));
    const envPhase = env.capture_phase || (legacyPostRun ? "post_run" : envCap ? "pre_run" : null);
    cards.push({
      label: "Environment provenance",
      value: envPhase || "missing",
      sub: env.run_environment_available === false ? "runtime unavailable" : "",
      cls: envPhase === "post_run" || !envCap ? "is-warn" : "",
    });
    const reviewed = rubric.total_scored != null || [cr, cfd, rr].some(area => area.overall_score != null);
    cards.push({
      label: "Agent reviewed",
      value: reviewed ? "yes" : "no",
      cls: reviewed ? "is-strong" : "",
    });

    $("#metrics-cards").innerHTML = cards.map(c => `
      <div class="metric-card ${c.cls || ""}">
        <div class="metric-label">${escapeHtml(c.label)}</div>
        <div class="metric-value">${escapeHtml(String(c.value ?? emDash))}</div>
        ${c.sub ? `<div class="metric-sub">${escapeHtml(String(c.sub))}</div>` : ""}
      </div>
    `).join("");
  }

  /* ----------------------------------------------------------------
   * Tabs
   * ---------------------------------------------------------------- */
  function bindTabs() {
    $$(".tab").forEach(btn => {
      btn.onclick = () => {
        const t = btn.getAttribute("data-tab");
        if (!t) return;
        state.detailTab = t;
        switchTab(t);
      };
    });
  }

  function switchTab(name) {
    $$(".tab").forEach(btn => {
      const active = btn.getAttribute("data-tab") === name;
      btn.classList.toggle("is-active", active);
      btn.setAttribute("aria-selected", active ? "true" : "false");
    });
    $$(".panel").forEach(p => {
      const isMatch = p.getAttribute("data-tab-panel") === name;
      p.classList.toggle("is-active", isMatch);
      p.hidden = !isMatch;
    });
    const detail = state.detailCache.get(state.currentName);
    if (!detail) return;
    switch (name) {
      case "summary":  renderSummaryTab(detail); break;
      case "evaluation": renderEvaluationTab(detail); break;
      case "report":   renderReportTab(detail);  break;
      case "contestant": renderContestantTab(detail); break;
      case "pdf":      renderPdfTab(detail); break;
      case "reviews":  renderReviewsTab(detail); break;
      case "sessions": renderSessionsTab(detail);break;
      case "metadata": renderMetadataTab(detail);break;
      case "configs":  renderConfigsTab(detail); break;
      case "env":      renderEnvTab(detail);     break;
    }
  }

  /* ----------------------------------------------------------------
   * Markdown renderer (safe)
   * ---------------------------------------------------------------- */
  function renderMarkdown(src) {
    if (!src || !src.trim()) return `<p class="muted">${emDash}</p>`;
    // First escape HTML, then apply transformations. Tokens from the
    // markdown source carry escaped text throughout; we only unescape
    // inside well-defined markdown output elements.
    let s = escapeHtml(src);
    s = s.replace(/\r\n?/g, "\n");

    // Extract fenced code blocks first so their contents aren't touched
    // by inline transforms. Fences are ``` optionally followed by lang.
    const codeBlocks = [];
    s = s.replace(/```([a-zA-Z0-9_+\-#.]*)\n([\s\S]*?)```/g, (m, lang, body) => {
      const idx = codeBlocks.length;
      codeBlocks.push({ lang, body });
      return `\u0000CODE${idx}\u0000`;
    });

    // Inline code spans
    s = s.replace(/`([^`\n]+)`/g, (m, body) => `\u0000IC${codeBlocks.push({ inline: true, body }) - 1}\u0000`);

    // Headings
    s = s.replace(/^######\s+(.*)$/gm, "<h6>$1</h6>");
    s = s.replace(/^#####\s+(.*)$/gm, "<h5>$1</h5>");
    s = s.replace(/^####\s+(.*)$/gm, "<h4>$1</h4>");
    s = s.replace(/^###\s+(.*)$/gm,  "<h3>$1</h3>");
    s = s.replace(/^##\s+(.*)$/gm,   "<h2>$1</h2>");
    s = s.replace(/^#\s+(.*)$/gm,    "<h1>$1</h1>");

    // Horizontal rule
    s = s.replace(/^\s*---\s*$/gm, "<hr>");

    // Blockquote
    s = s.replace(/(^|\n)&gt;\s?([^\n]*)(?=\n|$)/g, "$1<blockquote>$2</blockquote>");
    // collapse consecutive blockquotes onto one
    s = s.replace(/<\/blockquote>\n<blockquote>/g, "<br>");

    // Tables (GFM-ish). Process line-by-line.
    s = renderMarkdownTables(s);

    // Lists (ordered and unordered)
    s = renderMarkdownLists(s);

    // Bold / italic
    s = s.replace(/\*\*([^*\n]+)\*\*/g, "<strong>$1</strong>");
    s = s.replace(/__([^_\n]+)__/g,     "<strong>$1</strong>");
    s = s.replace(/(^|[^*])\*([^*\n]+)\*/g, "$1<em>$2</em>");
    s = s.replace(/(^|[^_])_([^_\n]+)_/g, "$1<em>$2</em>");

    // Links [text](url)
    s = s.replace(/\[([^\]]+)\]\(([^)\s]+)(?:\s+"[^"]*")?\)/g, (m, text, url) => {
      const safeUrl = /^(https?:|mailto:|#|\/)/i.test(url) ? url : "#";
      return `<a href="${safeUrl}" target="_blank" rel="noopener noreferrer">${text}</a>`;
    });

    // Paragraph wrapping BEFORE restoring code tokens so the atomic
    // placeholders stay on one line and the restored <pre> blocks don't
    // get split by their internal newlines.
    // Strategy: split into blank-separated blocks. If a block contains
    // ANY line that is a known block element, emit its lines verbatim.
    // Otherwise wrap the block in <p>...</p>, joining consecutive lines
    // with <br>.
    const blockTag = /^(<(?:h[1-6]|hr|pre|blockquote|ul|ol|table|thead|tbody|tr|td|th|p|div|article|section)[\s>])|^\u0000CODE\d+\u0000$/;
    const lines = s.split("\n");
    const blocks = [];
    let cur = [];
    const flush = () => { if (cur.length) blocks.push(cur); cur = []; };
    for (const line of lines) {
      if (line.trim() === "") { flush(); continue; }
      cur.push(line);
    }
    flush();
    s = blocks.map(block => {
      if (block.some(l => blockTag.test(l.trim()))) {
        return block.join("\n");
      }
      return "<p>" + block.join("<br>") + "</p>";
    }).join("\n");

    // Restore code tokens after paragraph wrapping so multi-line code
    // bodies do not get broken up.
    s = s.replace(/\u0000(IC|CODE)(\d+)\u0000/g, (m, kind, idx) => {
      const i = Number(idx);
      const c = codeBlocks[i];
      if (kind === "CODE") {
        // Convert any remaining newlines inside the body to literal \n
        // (they will be preserved as-is inside <pre><code>).
        return `<pre data-md-code data-lang="${escapeHtml(c.lang || "")}"><code>${c.body}</code><button class="copy-btn" type="button">copy</button></pre>`;
      }
      return `<code>${c.body}</code>`;
    });

    return s;
  }

  function renderMarkdownTables(s) {
    // Find lines that look like table rows: | ... |
    return s.replace(/(^|\n)((?:\|[^\n]*\|\n?)+)/g, (full, lead, block) => {
      const rows = block.trim().split("\n").filter(l => l.trim().startsWith("|"));
      if (rows.length < 2) return full;
      // Check separator: | --- | --- |
      const sep = rows[1];
      if (!/^\|[\s:|-]+\|$/.test(sep)) return full;
      const split = (r) => r.trim().replace(/^\|/, "").replace(/\|$/, "").split("|").map(c => c.trim());
      const head = split(rows[0]);
      const body = rows.slice(2).map(split);
      let html = '<table class="md-table"><thead><tr>';
      head.forEach(h => html += `<th>${h}</th>`);
      html += '</tr></thead><tbody>';
      body.forEach(row => {
        html += '<tr>';
        row.forEach(cell => html += `<td>${cell}</td>`);
        html += '</tr>';
      });
      html += '</tbody></table>';
      return lead + html;
    });
  }

  function renderMarkdownLists(s) {
    // Process line-by-line so we can detect list boundaries.
    const lines = s.split("\n");
    const out = [];
    let mode = null;   // null | 'ul' | 'ol'
    let buf = [];
    const flush = () => {
      if (mode && buf.length) {
        const tag = mode;
        out.push(`<${tag}>` + buf.map(b => `<li>${b}</li>`).join("") + `</${tag}>`);
        buf = []; mode = null;
      }
    };
    const ulRe = /^[ \t]*[-*+]\s+(.*)$/;
    const olRe = /^[ \t]*\d+\.\s+(.*)$/;
    for (const line of lines) {
      let m;
      if ((m = line.match(ulRe))) {
        if (mode !== "ul") { flush(); mode = "ul"; }
        buf.push(m[1]);
      } else if ((m = line.match(olRe))) {
        if (mode !== "ol") { flush(); mode = "ol"; }
        buf.push(m[1]);
      } else {
        flush();
        out.push(line);
      }
    }
    flush();
    return out.join("\n");
  }

  function bindCopyButtons(root) {
    $$(root.querySelectorAll("pre[data-md-code]")).forEach(pre => {
      const btn = pre.querySelector(".copy-btn");
      if (!btn) return;
      btn.addEventListener("click", async (ev) => {
        ev.preventDefault();
        const code = pre.querySelector("code");
        const text = code ? code.innerText : pre.innerText;
        try {
          if (navigator.clipboard && navigator.clipboard.writeText) {
            await navigator.clipboard.writeText(text);
          } else {
            const ta = document.createElement("textarea");
            ta.value = text; document.body.appendChild(ta); ta.select();
            document.execCommand("copy"); document.body.removeChild(ta);
          }
          btn.textContent = "copied";
          btn.classList.add("is-copied");
          setTimeout(() => { btn.textContent = "copy"; btn.classList.remove("is-copied"); }, 1400);
        } catch (_) { /* ignore */ }
      });
    });
  }

  /* ----------------------------------------------------------------
   * Tab renderers
   * ---------------------------------------------------------------- */
  function renderSummaryTab(detail) {
    const panel = $("#panel-summary");
    const md = (detail.markdown || {})["summary.md"];
    panel.innerHTML = md
      ? renderMarkdown(md)
      : `<p class="muted">No summary.md for this snapshot.</p>`;
    bindCopyButtons(panel);
  }

  function renderEvaluationTab(detail) {
    const panel = $("#panel-evaluation");
    const scores = detail.agent_scores;
    const identity = detail.run_identity || {};
    if (!scores) {
      panel.innerHTML = `<p class="muted">No agent_scores.json is available for this snapshot.</p>`;
      return;
    }
    const dq = scores.disqualification || {};
    const found = (dq.flags || []).filter(f => f.found === true);
    const rubric = scores.rubric || {};
    const sections = rubric.sections || [];
    const idRows = [
      ["run ID", identity.run_id || detail.name],
      ["initial", identity.initial_branch && `${identity.initial_branch} @ ${(identity.initial_commit || "").slice(0, 12)}`],
      ["result", identity.result_branch && `${identity.result_branch} @ ${(identity.submission_commit || "").slice(0, 12)}`],
      ["operator number", identity.operator_number],
      ["execution date", (scores.session_selection || {}).execution_date],
      ["evaluated at", scores.evaluated_at],
    ];
    let html = `<div class="evaluation-grid">
      <div class="env-card"><h3>Identity and dates</h3>${kvTable(idRows)}</div>
      <div class="env-card"><h3>Verdict</h3>${kvTable([
        ["rubric", rubric.total_scored == null ? emDash : `${rubric.total_scored}/${rubric.total_possible ?? emDash}`],
        ["disqualified", dq.triggered === true ? "yes" : dq.triggered === false ? "no" : emDash],
        ["evaluator", (scores.evaluator || {}).agent],
      ])}</div></div>`;
    if (found.length) {
      html += `<div class="banner"><strong>Disqualification triggered</strong><ul>${found.map(f => `<li><strong>${escapeHtml(String(f.id ?? ""))}: ${escapeHtml(f.text || "")}</strong>${f.evidence ? `<br>${escapeHtml(f.evidence)}` : ""}</li>`).join("")}</ul></div>`;
    }
    if (sections.length) {
      html += `<h2>100-point rubric</h2><div class="rubric-table-wrap"><table class="md-table"><thead><tr><th>Section</th><th>Score</th><th>Notes</th></tr></thead><tbody>${sections.map(s => `<tr><td>${escapeHtml(s.title || s.id)}</td><td class="mono">${escapeHtml(s.score == null ? emDash : `${s.score}/${s.max_points}`)}</td><td>${escapeHtml(s.notes || emDash)}</td></tr>`).join("")}</tbody></table></div>`;
    }
    if ((scores.limitations || []).length) {
      html += `<h2>Limitations</h2><ul>${scores.limitations.map(x => `<li>${escapeHtml(x)}</li>`).join("")}</ul>`;
    }
    panel.innerHTML = html;

    function kvTable(pairs) {
      return `<table><tbody>${pairs.filter(([,v]) => v != null && v !== "").map(([k,v]) => `<tr><th>${escapeHtml(k)}</th><td class="mono">${escapeHtml(v)}</td></tr>`).join("")}</tbody></table>`;
    }
  }

  function renderReportTab(detail) {
    const panel = $("#panel-report");
    if (detail.has_agent_report) {
      const md = (detail.markdown || {})["agent_report.md"] || "";
      panel.innerHTML = renderMarkdown(md);
    } else {
      panel.innerHTML = `<div class="env-notice">
        <strong>Not yet evaluated.</strong>
        <p>The evaluation agent hasn't produced <code>agent_report.md</code> for this snapshot.</p>
        <p>Run <code>generate_agent_report.py --out &lt;snapshot&gt; --workspace &lt;workspace&gt;</code>, complete the evaluation, then record it.</p>
      </div>`;
    }
    bindCopyButtons(panel);
  }

  function renderContestantTab(detail) {
    const panel = $("#panel-contestant");
    const md = (detail.markdown || {})["contestant_final_response.md"];
    panel.innerHTML = md
      ? renderMarkdown(md)
      : `<p class="muted">No attributed final response is available for this snapshot.</p>`;
    bindCopyButtons(panel);
  }

  function renderPdfTab(detail) {
    const panel = $("#panel-pdf");
    const pdf = detail.report_pdf;
    if (!pdf) {
      panel.innerHTML = `<div class="env-notice"><strong>No workspace report PDF found.</strong>
        <p>The dashboard searched the matching contestant workspace for a report PDF. PDFs are viewed in place and are not copied into the evaluation snapshot.</p></div>`;
      return;
    }
    panel.innerHTML = `<div class="pdf-toolbar">
        <span class="mono">${escapeHtml(pdf.relative_path)}</span>
        <span class="muted">${escapeHtml(fmtBytes(pdf.bytes))}</span>
        <a class="btn" href="${escapeHtml(pdf.url)}" target="_blank" rel="noopener">Open PDF</a>
      </div>
      <iframe class="pdf-viewport" src="${escapeHtml(pdf.url)}#view=FitH" title="Contestant report PDF"></iframe>`;
  }

  function renderReviewsTab(detail) {
    const panel = $("#panel-reviews");
    const md = detail.markdown || {};
    const cards = [
      { title: "Code review",     src: md["review_code.md"]     },
      { title: "CFD review",      src: md["review_cfd.md"]      },
      { title: "Results review",  src: md["review_results.md"]  },
    ];
    const any = cards.some(c => c.src);
    if (!any) {
      panel.innerHTML = `<p class="muted">No review artifacts found.</p>`;
      return;
    }
    panel.innerHTML = `<div class="reviews-grid">${
      cards.map(c => `<div class="review-card"><h2>${escapeHtml(c.title)}</h2>${
        c.src ? renderMarkdown(c.src) : `<p class="muted">Not produced yet.</p>`
      }</div>`).join("")
    }</div>`;
    cards.forEach((c, i) => c.src && bindCopyButtons(panel.querySelectorAll(".review-card")[i]));
  }

  function renderSessionsTab(detail) {
    const panel = $("#panel-sessions");
    const sess = detail.sessions;
    if (!sess) {
      panel.innerHTML = `<div class="env-notice">
        <strong>No sessions data.</strong>
        <p>This snapshot was generated before session analysis was wired in.</p>
      </div>`;
      return;
    }
    const an = sess.analysis || {};
    const ws = an.whole_session_stats || {};
    const ie = an.idle_exclusion || {};
    const pw = an.permission_waits || {};
    const buckets = an.buckets || [];
    const win = an.window || {};

    let html = `<div class="sessions-meta">`;
    html += metaCard("Window",
      `<div class="mono">${escapeHtml(fmtDate(win.start))} → ${escapeHtml(fmtDate(win.end))}</div>
       <div class="muted" style="font-size:11.5px">bucket = ${escapeHtml(String(an.bucket_seconds ?? emDash))}s</div>`);
    html += metaCard("Whole-session stats",
      kvList([
        ["Active entities",  ws.per_entity ? Object.keys(ws.per_entity).length : emDash],
        ["Tools",            fmtInt((ws.tools || {}).total)],
        ["Tokens (own)",     fmtTokens((ws.tokens || {}).total || (ws.tokens || {}).own)],
        ["Cache hit",        fmtPct((ws.cache || {}).hit_ratio)],
      ]));
    html += metaCard("Idle exclusion",
      kvList([
        ["Method",      ie.method || emDash],
        ["Gaps",        ie.gap_count ?? emDash],
        ["Idle seconds", ie.idle_seconds_total == null ? emDash : Number(ie.idle_seconds_total).toFixed(0)],
      ]));
    const permBody = kvList([
      ["Method",        pw.method || emDash],
      ["Candidates",    pw.candidate_count ?? emDash],
      ["Explicit seen", pw.explicit_approval_events_found ? "yes" : "no"],
      ["Policy",        (pw.policy_observed || []).join(", ") || emDash],
    ]) + (pw.limitations && pw.limitations.length
      ? `<div style="margin-top:6px"><strong class="muted" style="font-size:11.5px">limitations</strong><ul>${pw.limitations.map(x => `<li>${escapeHtml(x)}</li>`).join("")}</ul></div>`
      : "");
    html += metaCard("Permission waits", permBody);
    html += `</div>`;

    // bucket bar strip
    html += `<div class="bucket-bars-legend">
      <span><span class="sw active"></span>active seconds</span>
      <span><span class="sw idle"></span>idle seconds</span>
      <span style="margin-left:auto">${buckets.length} buckets</span>
    </div>`;
    html += `<div class="bucket-bars">${buckets.map((b, i) => {
      const active = Number(b.active_seconds) || 0;
      const idle = Number(b.idle_seconds) || 0;
      const total = active + idle;
      const ah = total > 0 ? Math.max(2, Math.round(active / total * 38)) : 0;
      const ih = total > 0 ? Math.max(0, 38 - ah) : 0;
      return `<div class="bucket-bar" style="grid-row: span 2">
        <div class="active" style="height:${ah}px"></div>
        <div class="idle" style="height:${ih}px"></div>
        <div class="tip">#${i} active ${active.toFixed(0)}s / idle ${idle.toFixed(0)}s · tokens ${fmtTokens((b.tokens || {}).total)}</div>
      </div>`;
    }).join("")}</div>`;

    // bucket table
    html += `<div class="bucket-table-wrap"><table class="buckets"><thead><tr>
      <th>idx</th><th>start</th><th>end</th>
      <th>active&nbsp;s</th><th>idle&nbsp;s</th>
      <th>tokens.total</th><th>cache.hit%</th><th>tools.total</th><th>entities</th>
    </tr></thead><tbody>`;
    buckets.forEach((b, i) => {
      const totalT = (b.tokens || {}).total;
      const hit = (b.cache || {}).hit_ratio;
      const toolsT = (b.tools || {}).total;
      html += `<tr>
        <td>${i}</td>
        <td>${escapeHtml(fmtDate(b.start))}</td>
        <td>${escapeHtml(fmtDate(b.end))}</td>
        <td>${Number(b.active_seconds || 0).toFixed(0)}</td>
        <td>${Number(b.idle_seconds || 0).toFixed(0)}</td>
        <td>${fmtTokens(totalT)}</td>
        <td>${fmtPct(hit)}</td>
        <td>${fmtInt(toolsT)}</td>
        <td>${escapeHtml(String(b.active_entity_count ?? emDash))}</td>
      </tr>`;
    });
    html += `</tbody></table></div>`;
    panel.innerHTML = html;

    function metaCard(title, body) {
      return `<div class="card"><h4>${escapeHtml(title)}</h4><div class="body">${body}</div></div>`;
    }
    function kvList(pairs) {
      return `<table style="width:100%;font-size:12px"><tbody>${
        pairs.map(([k,v]) => `<tr><td style="color:var(--text-mute);padding:2px 6px 2px 0">${escapeHtml(k)}</td><td class="mono">${escapeHtml(String(v))}</td></tr>`).join("")
      }</tbody></table>`;
    }
  }

  function renderMetadataTab(detail) {
    const panel = $("#panel-metadata");
    const md = detail.metadata || (detail.summary && detail.summary.metadata) || null;
    if (!md || Object.keys(md).length === 0) {
      panel.innerHTML = `<p class="muted">No metadata.json.</p>`;
      return;
    }
    const sections = [
      { key: "models",      pick: () => md.models,          label: "Models" },
      { key: "context",     pick: () => md.context,         label: "Context" },
      { key: "subagents",   pick: () => md.subagents,       label: "Subagents" },
      { key: "prompts",     pick: () => md.prompts,         label: "Prompts" },
      { key: "workspace",   pick: () => md.workspace,       label: "Workspace" },
      { key: "opencodex",   pick: () => md.opencodex,       label: "Opencodex" },
      { key: "questions",   pick: () => md.questions,       label: "Questions" },
      { key: "harness",     pick: () => md.harness,         label: "Harness" },
      { key: "session_window", pick: () => md.session_window, label: "Session window" },
      { key: "provenance",  pick: () => md.provenance,      label: "Provenance" },
      { key: "other",       pick: () => {
          const known = new Set(["models","context","subagents","prompts","workspace","opencodex","questions","harness","session_window","provenance","status"]);
          const rest = {};
          for (const k of Object.keys(md)) if (!known.has(k)) rest[k] = md[k];
          return Object.keys(rest).length ? rest : null;
        }, label: "Other" },
    ];
    let html = "";
    for (const s of sections) {
      const v = s.pick();
      if (v == null) continue;
      if (Array.isArray(v) && v.length === 0) continue;
      html += `<div class="tree-section"><h3>${escapeHtml(s.label)}</h3>${renderJsonTree(v)}</div>`;
    }
    panel.innerHTML = html;
    bindTreeToggles(panel);
  }

  function renderConfigsTab(detail) {
    const panel = $("#panel-configs");
    const cfg = detail.configs || {};
    const files = cfg.files || [];
    const summary = `
      <div class="configs-summary">
        <div class="card"><div class="k">captured_at</div><div class="v">${escapeHtml(fmtDate(cfg.captured_at))}</div></div>
        <div class="card"><div class="k">count</div><div class="v">${files.length}</div></div>
        <div class="card"><div class="k">redaction hits</div><div class="v">${escapeHtml(String(cfg.redaction_hits ?? emDash))}</div></div>
        <div class="card"><div class="k">roles</div><div class="v">${escapeHtml((cfg.roles || []).join(", ") || emDash)}</div></div>
      </div>`;

    if (files.length === 0) {
      panel.innerHTML = summary + `<p class="muted">No configs captured.</p>`;
      return;
    }

    let html = summary + `<div class="configs-table-wrap"><table class="configs-table"><thead><tr>
      <th>role</th><th>path</th><th>exists</th><th>content</th><th>redacted</th><th>bytes</th>
    </tr></thead><tbody>`;
    files.forEach((f, i) => {
      const included = f.content_included === true;
      const redacted = f.redacted === true;
      html += `<tr data-idx="${i}">
        <td>${escapeHtml(f.role || emDash)}</td>
        <td class="mono" style="font-size:11.5px">${escapeHtml(f.path || emDash)}</td>
        <td>${boolPill(f.exists)}</td>
        <td>${boolPill(f.content_included)}</td>
        <td>${boolPill(f.redacted)}</td>
        <td class="num">${escapeHtml(fmtBytes(f.bytes))}</td>
      </tr>`;
    });
    html += `</tbody></table></div>`;
    html += `<div id="config-content-host"></div>`;
    panel.innerHTML = html;

    // Row click → fetch content (already embedded, but use file endpoint per spec)
    $$("#panel-configs .configs-table tbody tr").forEach(tr => {
      tr.addEventListener("click", async () => {
        const idx = Number(tr.getAttribute("data-idx"));
        const f = files[idx];
        if (!f || !f.content_included) {
          $("#config-content-host").innerHTML = `<p class="muted">No content available for this file.</p>`;
          return;
        }
        const host = $("#config-content-host");
        host.innerHTML = `<div class="config-content">
          <h4 style="margin-top:0">${escapeHtml(f.path || "(file)")}</h4>
          ${f.redacted ? `<div class="redacted-notice">credentials redacted in this capture</div>` : ""}
          <pre><code>${escapeHtml(f.content || "")}</code></pre>
        </div>`;
        $$("#panel-configs tbody tr").forEach(r => r.classList.remove("is-open"));
        tr.classList.add("is-open");
      });
    });
  }

  function renderEnvTab(detail) {
    const panel = $("#panel-env");
    if (!detail.env_captured) {
      panel.innerHTML = `<div class="env-notice">
        <strong>Env snapshot not captured.</strong>
        <p>Run <code>env_snapshot.py</code> before the agent starts. Legacy runs may instead carry an explicitly reconstructed post-run snapshot.</p>
      </div>`;
      return;
    }
    const env = detail.env_snapshot || {};
    const host = env.host || {};
    const tools = env.tools || {};
    const ws = env.workspace || {};
    const net = env.network || {};
    const cap = env.captured_at;
    const legacyPostRun = env.provenance && (env.provenance.pre_run_authority === false || String(env.provenance.capture_kind || "").toLowerCase().includes("post-run"));
    const capturePhase = env.capture_phase || (legacyPostRun ? "post_run" : "pre_run");
    const warning = capturePhase === "post_run" || env.run_environment_available === false
      ? `<div class="banner warn"><strong>Post-run provenance reconstruction.</strong> Original execution-time harness, container, host, tools, and environment are unavailable. Capture-time values below are diagnostics only.</div>`
      : "";

    const cards = [];
    cards.push(envCard("Host", [
      ["hostname", host.hostname],
      ["os",       host.os],
      ["machine",  host.machine],
      ["python",   host.python],
      ["cpus",     host.cpus],
      ["memory_gb", host.memory_gb],
      ["loadavg (1,5,15)", Array.isArray(host.loadavg_1_5_15) ? host.loadavg_1_5_15.join(", ") : null],
      ["in_docker", host.in_docker === true ? "yes" : host.in_docker === false ? "no" : null],
    ]));
    cards.push(envCard("Tools", toolPairs(tools)));
    cards.push(envCard("Workspace", [
      ["path",      ws.path],
      ["exists",    ws.exists === true ? "yes" : ws.exists === false ? "no" : null],
      ["branch",    ws.branch || ws.git_branch],
      ["commit",    ws.commit || ws.git_commit],
      ["submodule", ws.benchmark_submodule ? (ws.benchmark_submodule.commit || "").slice(0,12) : null],
      ["external",  ws.external],
    ]));
    cards.push(envCard("Network", [
      ["hostname",  net.hostname],
      ["ip",        net.ip],
      ["interfaces", Array.isArray(net.interfaces) ? net.interfaces.length + " interfaces" : null],
      ["dns",       Array.isArray(net.dns) ? net.dns.join(", ") : null],
    ]));
    cards.push(envCard("Capture", [
      ["captured_at", fmtDate(cap || env.captured_at)],
      ["schema",      env.schema],
      ["version",     env.version],
      ["capture_phase", capturePhase],
      ["run environment", capturePhase === "post_run" || env.run_environment_available === false ? "unavailable" : "captured"],
    ]));

    panel.innerHTML = warning + `<div class="env-grid">${cards.join("")}</div>`;
  }

  function envCard(title, pairs) {
    const rows = pairs.filter(([_, v]) => v != null && v !== "");
    if (rows.length === 0) {
      return `<div class="env-card"><h3>${escapeHtml(title)}</h3><p class="muted">no data</p></div>`;
    }
    return `<div class="env-card"><h3>${escapeHtml(title)}</h3><table><tbody>${
      rows.map(([k, v]) =>
        `<tr><th>${escapeHtml(k)}</th><td>${escapeHtml(typeof v === "object" ? JSON.stringify(v) : String(v))}</td></tr>`
      ).join("")
    }</tbody></table></div>`;
  }

  function toolPairs(tools) {
    if (!tools || typeof tools !== "object") return [];
    const out = [];
    for (const [k, v] of Object.entries(tools)) {
      if (v && typeof v === "object") {
        for (const [k2, v2] of Object.entries(v)) {
          out.push([`${k}.${k2}`, typeof v2 === "object" ? JSON.stringify(v2) : String(v2)]);
        }
      } else {
        out.push([k, String(v)]);
      }
    }
    return out;
  }

  /* ----------------------------------------------------------------
   * JSON tree (collapsible)
   * ---------------------------------------------------------------- */
  function renderJsonTree(value) {
    if (value === null) return `<span class="tree-null">null</span>`;
    if (typeof value === "string") return `<span class="tree-string">"${escapeHtml(value)}"</span>`;
    if (typeof value === "number") return `<span class="tree-number">${value}</span>`;
    if (typeof value === "boolean") return `<span class="tree-bool">${value}</span>`;
    if (Array.isArray(value)) {
      if (value.length === 0) return `<span class="dim">[]</span>`;
      const inner = value.map((v, i) =>
        `<div class="tree-node"><div class="tree-row"><span class="tree-toggle"></span><span class="tree-key">${i}</span>: ${renderJsonTree(v)}</div></div>`
      ).join("");
      const meta = `<span class="tree-meta">[${value.length} items]</span>`;
      return `<div class="tree-node"><div class="tree-row"><span class="tree-toggle"></span><span class="tree-key">array</span>${meta}</div><div class="tree-children">${inner}</div></div>`;
    }
    if (typeof value === "object") {
      const keys = Object.keys(value);
      if (keys.length === 0) return `<span class="dim">{}</span>`;
      const inner = keys.map(k =>
        `<div class="tree-node"><div class="tree-row"><span class="tree-toggle"></span><span class="tree-key">${escapeHtml(k)}</span>: ${renderJsonTree(value[k])}</div></div>`
      ).join("");
      const meta = `<span class="tree-meta">${keys.length} keys</span>`;
      return `<div class="tree-node"><div class="tree-row"><span class="tree-toggle"></span><span class="tree-key">object</span>${meta}</div><div class="tree-children">${inner}</div></div>`;
    }
    return escapeHtml(String(value));
  }

  function bindTreeToggles(root) {
    $$(root.querySelectorAll(".tree-toggle")).forEach(t => {
      t.addEventListener("click", (ev) => {
        ev.stopPropagation();
        const node = t.closest(".tree-node");
        if (node) node.classList.toggle("is-collapsed");
      });
    });
  }

  /* ----------------------------------------------------------------
   * Wire UI
   * ---------------------------------------------------------------- */
  function wireListView() {
    $("#filter-input").addEventListener("input", (ev) => {
      state.filter = ev.target.value || "";
      renderTable();
    });
    $("#reload-btn").addEventListener("click", () => {
      state.detailCache.clear();
      loadSnapshots();
    });
  }

  function wireDetailView() {
    $("#back-btn").addEventListener("click", () => {
      location.hash = "#/";
    });
  }

  /* ----------------------------------------------------------------
   * Init
   * ---------------------------------------------------------------- */
  document.addEventListener("DOMContentLoaded", () => {
    wireListView();
    wireDetailView();
    // Always apply the initial route. Assigning an already-current "#/" hash
    // does not emit hashchange, which previously left both views hidden.
    const initial = parseHash();
    if (!location.hash) history.replaceState(null, "", "#/");
    route();
    if (initial.view === "list") loadSnapshots();
  });
})();
