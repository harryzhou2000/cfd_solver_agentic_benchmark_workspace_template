"""Regression tests for the workspace-local telemetry trust boundary."""

from __future__ import annotations

import tempfile
import unittest
import json
import sqlite3
from pathlib import Path
from types import SimpleNamespace

from cfdeval import codex_data as cd
from cfdeval import expenses as expenses_mod
from cfdeval import metadata as metadata_mod
from cfdeval.sessions import CodexThreadEvents, analyze_opencode, whole_stats


class SessionIsolationTests(unittest.TestCase):
    def test_owned_rollout_continuation_survives_counter_reset(self):
        with tempfile.TemporaryDirectory() as tmp:
            rollout = Path(tmp) / "rollout-root.jsonl"
            rows = [
                {"timestamp": "2026-01-01T00:00:00Z", "type": "session_meta",
                 "payload": {"id": "root", "timestamp": "2026-01-01T00:00:00Z"}},
                {"timestamp": "2026-01-01T00:00:01Z", "type": "event_msg",
                 "payload": {"type": "task_started", "turn_id": "turn-one",
                             "started_at": 1767225601}},
                {"timestamp": "2026-01-01T00:00:02Z", "type": "event_msg",
                 "payload": {"type": "token_count", "info": {
                     "total_token_usage": {"input_tokens": 90, "output_tokens": 10,
                                           "total_tokens": 100}}}},
                {"timestamp": "2026-01-01T00:01:00Z", "type": "event_msg",
                 "payload": {"type": "task_started", "turn_id": "turn-two",
                             "started_at": 1767225660}},
                # A continued turn after compaction starts a new counter epoch.
                {"timestamp": "2026-01-01T00:01:01Z", "type": "event_msg",
                 "payload": {"type": "token_count", "info": {
                     "total_token_usage": {"input_tokens": 18, "output_tokens": 2,
                                           "total_tokens": 20}}}},
                {"timestamp": "2026-01-01T00:01:02Z", "type": "event_msg",
                 "payload": {"type": "token_count", "info": {
                     "total_token_usage": {"input_tokens": 36, "output_tokens": 4,
                                           "total_tokens": 40}}}},
            ]
            rollout.write_text("\n".join(json.dumps(row) for row in rows) + "\n")

            facts = cd.rollout_usage_facts(str(rollout))
            self.assertEqual(facts["total_tokens"], 140)
            self.assertEqual(facts["input_tokens"], 126)
            self.assertEqual(facts["output_tokens"], 14)
            stream = CodexThreadEvents("root", str(rollout))
            stream.load()
            self.assertEqual(stream.cumulative_final, 140)
            self.assertEqual(len(stream.token_events), 3)

    def test_forked_rollout_replay_is_excluded_from_usage_and_tools(self):
        with tempfile.TemporaryDirectory() as tmp:
            parent = Path(tmp) / "rollout-parent.jsonl"
            child = Path(tmp) / "rollout-child.jsonl"
            nested = Path(tmp) / "rollout-nested.jsonl"

            def token(ts, input_t, output_t, total_t, last_input):
                return {"timestamp": ts, "type": "event_msg", "payload": {
                    "type": "token_count", "info": {
                        "total_token_usage": {
                            "input_tokens": input_t,
                            "cached_input_tokens": 0,
                            "output_tokens": output_t,
                            "reasoning_output_tokens": 0,
                            "total_tokens": total_t,
                        },
                        "last_token_usage": {"input_tokens": last_input},
                    }}}

            parent_rows = [
                {"timestamp": "2026-01-01T00:00:00Z", "type": "session_meta",
                 "payload": {"id": "parent", "timestamp": "2026-01-01T00:00:00Z"}},
                {"timestamp": "2026-01-01T00:00:01Z", "type": "event_msg",
                 "payload": {"type": "task_started", "turn_id": "parent-turn",
                             "started_at": 1767225601}},
                token("2026-01-01T00:00:02Z", 90, 10, 100, 90),
                {"timestamp": "2026-01-01T00:00:03Z", "type": "response_item",
                 "payload": {"type": "function_call", "name": "parent_tool"}},
                token("2026-01-01T00:00:04Z", 135, 15, 150, 45),
            ]
            child_rows = [
                {"timestamp": "2026-01-01T00:01:00Z", "type": "session_meta",
                 "payload": {"id": "child", "parent_thread_id": "parent",
                             "forked_from_id": "parent",
                             "timestamp": "2026-01-01T00:01:00Z"}},
                *[{**row, "timestamp": "2026-01-01T00:01:00Z"}
                  for row in parent_rows],
                {"timestamp": "2026-01-01T00:01:01Z", "type": "event_msg",
                 "payload": {"type": "task_started", "turn_id": "child-turn",
                             "started_at": 1767225661}},
                {"timestamp": "2026-01-01T00:01:02Z", "type": "response_item",
                 "payload": {"type": "function_call", "name": "child_tool"}},
                token("2026-01-01T00:01:03Z", 225, 25, 250, 90),
            ]
            nested_rows = [
                {"timestamp": "2026-01-01T00:02:00Z", "type": "session_meta",
                 "payload": {"id": "nested", "parent_thread_id": "child",
                             "forked_from_id": "child",
                             "timestamp": "2026-01-01T00:02:00Z"}},
                *[{**row, "timestamp": "2026-01-01T00:02:00Z"}
                  for row in child_rows],
                {"timestamp": "2026-01-01T00:02:01Z", "type": "event_msg",
                 "payload": {"type": "task_started", "turn_id": "nested-turn",
                             "started_at": 1767225721}},
                {"timestamp": "2026-01-01T00:02:02Z", "type": "response_item",
                 "payload": {"type": "function_call", "name": "nested_tool"}},
                token("2026-01-01T00:02:03Z", 270, 30, 300, 45),
            ]
            parent.write_text("\n".join(json.dumps(row) for row in parent_rows) + "\n")
            child.write_text("\n".join(json.dumps(row) for row in child_rows) + "\n")
            nested.write_text("\n".join(json.dumps(row) for row in nested_rows) + "\n")

            root_facts = cd.rollout_usage_facts(str(parent))
            self.assertEqual(root_facts["total_tokens"], 150)
            root_stream = CodexThreadEvents("parent", str(parent))
            root_stream.load()
            self.assertEqual(root_stream.cumulative_final, 150)
            self.assertEqual([name for _ts, name in root_stream.tool_events],
                             ["parent_tool"])

            facts = cd.rollout_usage_facts(str(child), str(parent))
            self.assertEqual(facts["total_tokens"], 100)
            self.assertEqual(facts["input_tokens"], 90)
            self.assertEqual(facts["output_tokens"], 10)
            self.assertEqual(
                facts["accounting"]["baseline"]["total_tokens"], 150)

            stream = CodexThreadEvents("child", str(child), str(parent))
            stream.load()
            self.assertEqual(stream.cumulative_final, 100)
            self.assertEqual(stream.cumulative_usage["input_tokens"], 90)
            self.assertEqual([name for _ts, name in stream.tool_events],
                             ["child_tool"])

            nested_facts = cd.rollout_usage_facts(str(nested), str(child))
            self.assertEqual(nested_facts["total_tokens"], 50)
            self.assertEqual(nested_facts["input_tokens"], 45)
            self.assertEqual(
                nested_facts["accounting"]["baseline"]["total_tokens"], 250)
            nested_stream = CodexThreadEvents(
                "nested", str(nested), str(child))
            nested_stream.load()
            self.assertEqual(nested_stream.cumulative_final, 50)
            self.assertEqual(
                [name for _ts, name in nested_stream.tool_events],
                ["nested_tool"])

    def test_entrypoints_have_no_home_session_fallbacks(self):
        root = Path(__file__).resolve().parent
        targets = [
            root / "tools" / "summarize.py",
            *(root / "src" / "cfdeval" / name for name in (
                "codex_data.py", "configs.py", "expenses.py", "measurements.py",
                "metadata.py", "sessions.py")),
        ]
        forbidden = (
            "Path.home()", "codex_home()", "default_paths()",
            'choices=("system", "project", "all")',
            'default="system"',
        )
        for target in targets:
            source = target.read_text()
            for marker in forbidden:
                with self.subTest(target=target.name, marker=marker):
                    self.assertNotIn(marker, source)

    def test_structured_codex_session_source_uses_union_tag(self):
        source = {"subagent": {"thread_spawn": {"parent_thread_id": "parent"}}}
        self.assertEqual(metadata_mod.session_meta_tag(source), "subagent")
        self.assertEqual(metadata_mod.session_meta_tag("cli"), "cli")
        self.assertIsNone(metadata_mod.session_meta_tag({"a": {}, "b": {}}))

    def test_project_defaults_are_nested_under_sessions(self):
        with tempfile.TemporaryDirectory() as tmp:
            workspace = Path(tmp) / "workspace"
            paths = cd.project_paths(workspace)
            bundle = (workspace / ".sessions").resolve()
            for key, path in paths.items():
                with self.subTest(key=key):
                    path.resolve().relative_to(bundle)
            self.assertEqual(
                paths["opencode_db"],
                bundle / "opencode-data" / "opencode" / "opencode.db",
            )

    def test_codex_thread_selection_maps_container_workspace(self):
        with tempfile.TemporaryDirectory() as tmp:
            workspace = Path(tmp) / "host-workspace"
            threads = {
                "root": {"cwd": "/workspace"},
                "nested": {"cwd": "/workspace/solver"},
                "unrelated": {"cwd": "/tmp/other"},
            }
            selected = cd.select_threads(threads, str(workspace))
            self.assertEqual(set(selected), {"root", "nested"})

    def test_outside_override_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            workspace = Path(tmp) / "workspace"
            outside = Path(tmp) / "host-state.sqlite"
            with self.assertRaisesRegex(ValueError, "outside"):
                cd.local_telemetry_paths(workspace, state_db=outside)

    def test_symlink_escape_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            workspace = Path(tmp) / "workspace"
            bundle = workspace / ".sessions"
            bundle.mkdir(parents=True)
            outside = Path(tmp) / "host-state.sqlite"
            outside.touch()
            link = bundle / "state.sqlite"
            link.symlink_to(outside)
            with self.assertRaisesRegex(ValueError, "outside"):
                cd.local_telemetry_paths(workspace, state_db=link)

    def test_default_sessions_bundle_symlink_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            workspace = Path(tmp) / "workspace"
            workspace.mkdir()
            outside = Path(tmp) / "outside"
            outside.mkdir()
            (workspace / ".sessions").symlink_to(outside, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "must not be a symlink"):
                cd.local_telemetry_paths(workspace)

    def test_rollout_path_is_rebased_or_removed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "sessions"
            local = root / "2026" / "08" / "rollout-abc-thread-1.jsonl"
            local.parent.mkdir(parents=True)
            local.write_text("{}\n")
            threads = {
                "thread-1": {"rollout_path": "/home/user/.codex/sessions/rollout-abc-thread-1.jsonl"},
                "thread-2": {"rollout_path": "/home/user/.codex/sessions/missing.jsonl"},
            }
            cd.rebase_rollout_paths(threads, root)
            self.assertEqual(threads["thread-1"]["rollout_path"], str(local.resolve()))
            self.assertIsNone(threads["thread-2"]["rollout_path"])

    def test_opencode_metadata_preserves_cache_counters(self):
        with tempfile.TemporaryDirectory() as tmp:
            workspace = Path(tmp) / "workspace"
            workspace.mkdir()
            db_path = workspace / ".sessions" / "opencode-data" / "opencode" / "opencode.db"
            db_path.parent.mkdir(parents=True)
            db = sqlite3.connect(db_path)
            try:
                db.executescript("""
                    CREATE TABLE session (
                      id TEXT, parent_id TEXT, directory TEXT, title TEXT, agent TEXT,
                      model TEXT, tokens_input INTEGER, tokens_output INTEGER,
                      tokens_reasoning INTEGER, tokens_cache_read INTEGER,
                      tokens_cache_write INTEGER, cost REAL, time_created INTEGER,
                      time_updated INTEGER, version TEXT
                    );
                    CREATE TABLE message (id TEXT, session_id TEXT, data TEXT,
                                          time_created INTEGER);
                    CREATE TABLE part (message_id TEXT, data TEXT);
                """)
                db.execute(
                    "INSERT INTO session VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    ("root", None, str(workspace.resolve()), "benchmark", "build",
                     json.dumps({"id": "deepseek-v4-pro", "providerID": "deepseek",
                                 "variant": "high"}),
                     10, 3, 2, 100, 4, 1.25, 1_000, 2_000, "1.18.11"),
                )
                messages = [
                    ("m1", "root", json.dumps({
                        "role": "assistant", "providerID": "kimi-for-coding",
                        "modelID": "k3", "variant": "max", "cost": 0,
                        "time": {"created": 1_000},
                        "tokens": {"input": 4, "output": 1, "reasoning": 0,
                                   "cache": {"read": 60, "write": 1}},
                    }), 1_000),
                    ("m2", "root", json.dumps({
                        "role": "assistant", "providerID": "deepseek",
                        "modelID": "deepseek-v4-pro", "variant": "high",
                        "cost": 1.25, "time": {"created": 1_100},
                        "tokens": {"input": 6, "output": 2, "reasoning": 2,
                                   "cache": {"read": 40, "write": 3}},
                    }), 1_100),
                ]
                db.executemany("INSERT INTO message VALUES (?,?,?,?)", messages)
                db.commit()
            finally:
                db.close()
            config_dir = workspace / ".sessions" / "opencode-config"
            config_dir.mkdir(parents=True)
            args = SimpleNamespace(
                opencode_db=str(db_path), opencode_config_dir=str(config_dir),
                ocx_catalog=str(workspace / ".sessions" / "missing-catalog.json"),
                idle_gap_seconds=600,
            )
            extracted = metadata_mod.extract_opencode(args, str(workspace.resolve()))
            session = extracted["opencode"]["sessions"][0]
            self.assertEqual(session["tokens_cache_read"], 100)
            self.assertEqual(session["tokens_cache_write"], 4)
            self.assertEqual(session["entry_model"], "k3")
            self.assertEqual(session["entry_variant"], "max")
            self.assertEqual(len(session["usage_by_model"]), 2)
            self.assertEqual(extracted["models"]["k3@max"]["tokens_cache_read"], 60)
            self.assertEqual(
                extracted["models"]["deepseek-v4-pro@high"]["tokens_cache_read"], 40)
            self.assertEqual(extracted["subagents"], [])

            prices = Path(tmp) / "prices.json"
            prices.write_text(json.dumps({
                "defaults": {"input_per_mtok": 1, "cached_input_per_mtok": 0.25,
                             "output_per_mtok": 4, "input_share": 0.75},
                "models": {
                    "kimi-for-coding/k3": {"input_per_mtok": 3,
                                            "cached_input_per_mtok": 0.3,
                                            "output_per_mtok": 15},
                    "deepseek/deepseek-v4-pro": {"input_per_mtok": 0.66,
                                                  "cached_input_per_mtok": 0.022,
                                                  "output_per_mtok": 1.98},
                },
            }))
            facts = expenses_mod.opencode_expense_facts(
                extracted, str(workspace.resolve()), prices, ["root"])
            self.assertEqual(set(facts["tokens"]["by_model"]), {
                "kimi-for-coding/k3", "deepseek/deepseek-v4-pro"})
            self.assertEqual(facts["tokens"]["total"], 119)
            self.assertEqual(
                facts["cost_estimate_usd"]["provider_reported_total"], 1.25)

    def test_opencode_extractors_map_container_workspace_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            host_workspace = Path(tmp) / "host-workspace"
            host_workspace.mkdir()
            db_path = host_workspace / ".sessions" / "opencode-data" / "opencode" / "opencode.db"
            db_path.parent.mkdir(parents=True)
            db = sqlite3.connect(db_path)
            try:
                db.executescript("""
                    CREATE TABLE session (
                      id TEXT, parent_id TEXT, directory TEXT, title TEXT, agent TEXT,
                      model TEXT, tokens_input INTEGER, tokens_output INTEGER,
                      tokens_reasoning INTEGER, tokens_cache_read INTEGER,
                      tokens_cache_write INTEGER, cost REAL, time_created INTEGER,
                      time_updated INTEGER, version TEXT
                    );
                    CREATE TABLE message (id TEXT, session_id TEXT, data TEXT,
                                          time_created INTEGER);
                    CREATE TABLE part (message_id TEXT, session_id TEXT, data TEXT,
                                       time_created INTEGER);
                """)
                db.execute(
                    "INSERT INTO session VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    ("root", None, "/workspace", "benchmark", "build",
                     json.dumps({"id": "k3", "providerID": "kimi-for-coding"}),
                     10, 3, 2, 100, 0, 1.25, 1_000, 2_000, "1.18.11"),
                )
                db.commit()
            finally:
                db.close()
            sessions = metadata_mod.extract_opencode(SimpleNamespace(
                opencode_db=str(db_path),
                opencode_config_dir=str(host_workspace / ".sessions" / "opencode-config"),
                ocx_catalog=str(host_workspace / ".sessions" / "missing.json"),
                idle_gap_seconds=600, roots="root",
            ), str(host_workspace.resolve()))["opencode"]["sessions"]
            self.assertEqual([session["session_id"] for session in sessions], ["root"])

    def test_opencode_root_selection_includes_only_descendants(self):
        sessions = [
            {"session_id": "root", "parent_id": None},
            {"session_id": "child", "parent_id": "root"},
            {"session_id": "other", "parent_id": None},
        ]
        selected = cd.select_opencode_session_trees(sessions, ["root"])
        self.assertEqual([session["session_id"] for session in selected],
                         ["root", "child"])
        with self.assertRaisesRegex(ValueError, "unknown OpenCode root"):
            cd.select_opencode_session_trees(sessions, ["missing"])

    def test_opencode_continuation_becomes_extraction_root(self):
        sessions = [
            {"session_id": "parent", "parent_id": None},
            {"session_id": "continuation", "parent_id": "parent"},
            {"session_id": "child", "parent_id": "continuation"},
        ]
        selected = cd.select_opencode_session_trees(sessions, ["continuation"])
        self.assertEqual([s["session_id"] for s in selected],
                         ["continuation", "child"])
        self.assertIsNone(selected[0]["parent_id"])
        self.assertEqual(selected[0]["original_parent_id"], "parent")

    def test_opencode_expenses_are_nonzero_and_root_scoped(self):
        with tempfile.TemporaryDirectory() as tmp:
            prices = Path(tmp) / "prices.json"
            prices.write_text(json.dumps({
                "defaults": {"input_per_mtok": 1, "cached_input_per_mtok": 0.25,
                             "output_per_mtok": 4, "input_share": 0.75},
                "models": {"provider/model": {"input_per_mtok": 2,
                                                     "cached_input_per_mtok": 0.5,
                                                     "output_per_mtok": 8}},
            }))
            metadata = {
                "opencode": {"sessions": [
                    {"session_id": "root", "parent_id": None, "provider": "provider",
                     "model": "model", "tokens_input": 10,
                     "tokens_cache_read": 100, "tokens_cache_write": 5,
                     "tokens_output": 3, "tokens_reasoning": 2, "cost": 1.25},
                    {"session_id": "other", "parent_id": None, "provider": "provider",
                     "model": "model", "tokens_input": 999, "cost": 99},
                ]},
                "session_window": {},
            }
            facts = expenses_mod.opencode_expense_facts(
                metadata, "/host/workspace", prices, ["root"])
            self.assertEqual(facts["tokens"]["total"], 120)
            self.assertEqual(facts["cost_estimate_usd"]["provider_reported_total"], 1.25)
            self.assertGreater(facts["cost_estimate_usd"]["total"], 0)
            self.assertEqual(facts["provenance"]["selected_roots"], ["root"])

    def test_opencode_message_categories_and_parts_are_aggregated_once(self):
        with tempfile.TemporaryDirectory() as tmp:
            workspace = Path(tmp) / "workspace"
            workspace.mkdir()
            db_path = workspace / ".sessions" / "opencode-data" / "opencode" / "opencode.db"
            db_path.parent.mkdir(parents=True)
            db = sqlite3.connect(db_path)
            try:
                db.executescript("""
                    CREATE TABLE session (
                      id TEXT, parent_id TEXT, directory TEXT, title TEXT, agent TEXT,
                      model TEXT, tokens_input INTEGER, tokens_output INTEGER,
                      tokens_reasoning INTEGER, tokens_cache_read INTEGER,
                      tokens_cache_write INTEGER, cost REAL, time_created INTEGER,
                      time_updated INTEGER, version TEXT
                    );
                    CREATE TABLE message (id TEXT, session_id TEXT, data TEXT,
                                          time_created INTEGER);
                    CREATE TABLE part (message_id TEXT, session_id TEXT, data TEXT,
                                       time_created INTEGER);
                """)
                model = json.dumps({"id": "model", "providerID": "provider"})
                rows = [
                    ("root", None, str(workspace.resolve()), "root", "build", model,
                     10, 3, 2, 100, 4, 1.25, 1_000, 2_000, "1"),
                    ("child", "root", str(workspace.resolve()), "child", "task", model,
                     7, 2, 1, 50, 6, 0.5, 1_100, 2_100, "1"),
                    ("other", None, str(workspace.resolve()), "other", "build", model,
                     999, 999, 999, 999, 999, 99, 1_200, 2_200, "1"),
                ]
                db.executemany("INSERT INTO session VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", rows)

                def message(mid, sid, created, raw, read, write, output, reasoning):
                    data = json.dumps({
                        "role": "assistant", "time": {"created": created},
                        "tokens": {
                            # Deliberately wrong: extraction must reconstruct
                            # total from the immutable category counters.
                            "total": 999999, "input": raw, "output": output,
                            "reasoning": reasoning,
                            "cache": {"read": read, "write": write},
                        },
                    })
                    db.execute("INSERT INTO message VALUES (?,?,?,?)",
                               (mid, sid, data, created))

                message("m-root", "root", 1_000, 10, 100, 4, 3, 2)
                message("m-child", "child", 1_100, 7, 50, 6, 2, 1)
                message("m-other", "other", 1_200, 999, 999, 999, 999, 999)
                # Pretty-printed JSON verifies tool detection does not depend
                # on a compact `"type":"tool"` byte pattern.
                db.execute("INSERT INTO part VALUES (?,?,?,?)", (
                    "m-root", "root", json.dumps({"type": "tool", "tool": "bash"},
                                                 indent=2), 1_001))
                db.commit()
            finally:
                db.close()

            doc, _events, token_events, tool_events = analyze_opencode(
                str(db_path), str(workspace.resolve()), ["root"])
            self.assertEqual([s["session_id"] for s in doc["sessions"]],
                             ["root", "child"])
            self.assertEqual(len(token_events), 2)
            totals = whole_stats([], token_events, tool_events, [], [], {})["tokens"]
            self.assertEqual(totals, {
                "input": 177, "cached_input": 150, "cache_write": 10,
                "non_cached_input": 27, "output": 5,
                "reasoning_output": 3, "total": 185,
                "total_from_root_trees": None,
                "total_from_all_sessions": None,
            })
            self.assertEqual(tool_events[0][1], "bash")


if __name__ == "__main__":
    unittest.main()
