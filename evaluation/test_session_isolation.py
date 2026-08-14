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


class SessionIsolationTests(unittest.TestCase):
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
                     json.dumps({"id": "k3", "providerID": "kimi-for-coding"}),
                     10, 3, 2, 100, 4, 0.0, 1_000, 2_000, "1.18.11"),
                )
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
            self.assertEqual(extracted["models"]["k3"]["tokens_cache_read"], 100)
            self.assertEqual(extracted["subagents"], [])

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


if __name__ == "__main__":
    unittest.main()
