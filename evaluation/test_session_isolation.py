"""Regression tests for the workspace-local telemetry trust boundary."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from cfdeval import codex_data as cd


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


if __name__ == "__main__":
    unittest.main()
