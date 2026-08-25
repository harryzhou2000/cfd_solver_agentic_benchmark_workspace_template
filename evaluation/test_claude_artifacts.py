"""Synthetic contract tests for workspace-local Claude Code artifacts."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from collections import Counter
from pathlib import Path

from cfdeval import claude_data, query
from cfdeval.expenses import claude_expense_facts
from cfdeval import validation


class ClaudeArtifactTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.ws = Path(self.tmp.name) / "contestant"
        self.project = self.ws / ".sessions" / "claude" / "projects" / "-workspace"
        self.project.mkdir(parents=True)

    @staticmethod
    def _write(path: Path, rows: list[dict]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("\n".join(json.dumps(row) for row in rows) + "\n")

    @staticmethod
    def _assistant(ts: str | None, message_id: str, model: str, usage: dict,
                   content: list[dict], stop: str = "tool_use") -> dict:
        row = {
            "sessionId": "root-1", "uuid": f"uuid-{message_id}",
            "cwd": "/workspace", "type": "assistant",
            "message": {"id": message_id, "role": "assistant", "model": model,
                        "usage": usage, "content": content, "stop_reason": stop},
            "effort": "high",
        }
        if ts is not None:
            row["timestamp"] = ts
        return row

    def _fixture(self) -> Path:
        claude_home = self.ws / ".sessions" / "claude"
        (claude_home / "settings.json").write_text('{"model":"claude-sonnet"}\n')
        (claude_home / ".credentials.json").write_text('{"token":"secret-value"}\n')
        (claude_home / "history.jsonl").write_text('{"secret":"trajectory"}\n')
        root = self.project / "root-1.jsonl"
        user = {"sessionId": "root-1", "uuid": "user-1", "cwd": "/workspace",
                "type": "user", "timestamp": "2026-08-01T00:00:00Z",
                "message": {"role": "user", "content": "Solve the CFD task"}}
        usage = {"input_tokens": 10, "cache_read_input_tokens": 20,
                 "cache_creation_input_tokens": 30, "output_tokens": 5}
        tool = {"type": "tool_use", "id": "tool-1", "name": "Bash",
                "input": {"command": "cmake --build build"}}
        first = self._assistant("2026-08-01T00:00:01Z", "msg-1", "claude-sonnet", usage,
                                [{"type": "text", "text": "Working."}])
        # A split fragment of the same assistant API message contributes its
        # tool block, but must not double-count usage.
        duplicate = self._assistant("2026-08-01T00:00:02Z", "msg-1", "claude-sonnet",
                                    usage, [tool])
        sidechain = self._assistant(
            "2026-08-01T00:00:02.250Z", "msg-side", "claude-haiku",
            {"input_tokens": 8, "output_tokens": 2},
            [{"type": "tool_use", "id": "tool-side", "name": "Bash",
              "input": {"command": "ignored"}}])
        sidechain["isSidechain"] = True
        final_intro = self._assistant(
            "2026-08-01T00:00:02.750Z", "msg-final", "claude-sonnet",
            {"input_tokens": 1, "output_tokens": 2},
            [{"type": "text", "text": "Done. "}])
        final = self._assistant(
            "2026-08-01T00:00:03Z", "msg-final", "claude-sonnet",
            {"input_tokens": 1, "output_tokens": 2},
            [{"type": "text", "text": "key " + "sk-" + "abcdefghijklmnop"}],
            "end_turn")
        self._write(root, [user, first, duplicate, sidechain, final_intro, final])
        sub = self.project / "root-1" / "subagents" / "agent-worker.jsonl"
        self._write(sub, [
            {"sessionId": "root-1", "agentId": "worker", "cwd": "/workspace",
             "type": "assistant", "timestamp": "2026-08-01T00:00:02.500Z",
             "effort": "low",
             "message": {"id": "sub-msg", "role": "assistant",
                         "model": "claude-haiku",
                         "usage": {"input_tokens": 4, "output_tokens": 6},
                         "content": [{"type": "tool_use", "id": "tool-sub",
                                      "name": "Read", "input": {"file_path": "TASK.md"}}],
                         "stop_reason": "tool_use"}},
        ])
        return root

    def test_selected_root_includes_subagent_and_accounts_cache(self):
        self._fixture()
        data = claude_data.facts(self.ws, ["root-1"])
        self.assertEqual({e["entity_id"] for e in data["entities"]},
                         {"root-1", "root-1:worker", "root-1:inline:unknown"})
        self.assertEqual(data["by_model"]["claude-sonnet"]["input"], 61)
        self.assertEqual(data["by_model"]["claude-sonnet"]["cached_input"], 20)
        self.assertEqual(data["by_model"]["claude-sonnet"]["cache_write"], 30)
        self.assertEqual(data["by_model"]["claude-sonnet"]["output"], 7)
        self.assertEqual(data["by_model"]["claude-sonnet"]["total"], 68)
        self.assertEqual(data["by_model"]["claude-haiku"]["total"], 20)
        self.assertEqual(Counter(name for _ts, name, _eid in data["tool_events"]),
                         Counter({"Bash": 2, "Read": 1}))
        self.assertEqual(data["efforts_by_model"],
                         {"claude-sonnet": ["high"],
                          "claude-haiku": ["low", "high"]})
        self.assertEqual(data["inline_sidechain_records_excluded"], 0)
        self.assertEqual(data["prompts"]["root-1"]["initial_user_prompt"]["text"],
                         "Solve the CFD task")

    def test_final_response_is_root_only_exact_order_and_redacted(self):
        root = self._fixture()
        result = claude_data.final_response(self.ws, "root-1")
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["text"], "Done. key [REDACTED_CREDENTIAL]")
        self.assertEqual(result["part_count"], 2)
        self.assertTrue(result["redacted"])
        raw = b"".join(root.read_bytes().splitlines(keepends=True)[-2:])
        import hashlib
        self.assertEqual(result["stored_message_sha256"], hashlib.sha256(raw).hexdigest())

    def test_untimed_usage_is_retained_but_not_put_on_timeline(self):
        root = self.project / "root-1.jsonl"
        self._write(root, [
            self._assistant(None, "msg-1", "claude-sonnet",
                            {"input_tokens": 7, "output_tokens": 3}, []),
        ])
        data = claude_data.facts(self.ws, ["root-1"])
        self.assertEqual(data["by_model"]["claude-sonnet"]["total"], 10)
        self.assertEqual(len(data["untimed_usage"]), 1)
        self.assertEqual(data["token_events"], [])

    def test_one_entity_preserves_exact_model_effort_pairs(self):
        root = self.project / "root-1.jsonl"
        first = self._assistant("2026-08-01T00:00:00Z", "a", "model-a",
                                {"input_tokens": 5, "output_tokens": 1}, [])
        second = self._assistant("2026-08-01T00:00:01Z", "b", "model-b",
                                 {"input_tokens": 7, "output_tokens": 2}, [])
        second["effort"] = "low"
        self._write(root, [first, second])
        prices = Path(self.tmp.name) / "prices.json"
        prices.write_text(json.dumps({
            "defaults": {"input_per_mtok": 1, "cached_input_per_mtok": 1,
                         "output_per_mtok": 1, "input_share": 0.8},
            "models": {},
        }))
        expenses = claude_expense_facts(
            self.ws, ["root-1"], self.ws / ".sessions" / "claude", prices)
        metadata = {"harness": {"harness": "claude"}, "threads": {
            "root-1": {"reasoning_effort": ["high", "low"]}}}
        decomposition = query.current_model_decomposition(expenses, metadata)
        self.assertEqual([row["key"] for row in decomposition["rows"]],
                         ["model-a + high", "model-b + low"])

    def test_unknown_duplicate_and_malformed_selected_roots_fail_closed(self):
        self._fixture()
        with self.assertRaisesRegex(ValueError, "unknown Claude"):
            claude_data.facts(self.ws, ["missing"])
        other = self.project.parent / "another" / "other-name.jsonl"
        self._write(other, [{"sessionId": "root-1", "cwd": "/workspace"}])
        with self.assertRaisesRegex(ValueError, "ambiguous Claude"):
            claude_data.facts(self.ws, ["root-1"])
        other.unlink()
        (self.project / "root-1.jsonl").write_text("{bad json\n")
        with self.assertRaisesRegex(ValueError, "malformed"):
            claude_data.facts(self.ws, ["root-1"])

    def test_external_override_and_symlink_transcript_are_rejected(self):
        outside = Path(self.tmp.name) / "outside"
        outside.mkdir()
        with self.assertRaisesRegex(ValueError, "outside"):
            claude_data.discover(self.ws, outside)
        secret = outside / "root-1.jsonl"
        secret.write_text('{"sessionId":"root-1","secret":"do-not-read"}\n')
        link = self.project / "root-1.jsonl"
        link.symlink_to(secret)
        disc = claude_data.discover(self.ws)
        self.assertEqual(disc["sessions"], [])
        self.assertIn("symlink", disc["malformed_records"][0]["error"])

    def test_expenses_preserve_mixed_model_decomposition(self):
        self._fixture()
        prices = Path(self.tmp.name) / "prices.json"
        prices.write_text(json.dumps({
            "defaults": {"input_per_mtok": 1, "cached_input_per_mtok": 1,
                         "output_per_mtok": 1, "input_share": 0.8},
            "models": {},
        }))
        result = claude_expense_facts(self.ws, ["root-1"],
                                      self.ws / ".sessions" / "claude", prices)
        self.assertEqual(result["tokens"]["total"], 88)
        self.assertEqual(result["tokens"]["main_vs_subagent"],
                         {"main": 68, "subagent": 20})
        self.assertIsNone(result["cost_estimate_usd"]["total"])
        self.assertEqual(result["cost_estimate_usd"]["unpriced_tokens"], 88)
        metadata = {
            "harness": {"harness": "claude"},
            "threads": {
                "root-1": {"reasoning_effort": ["high"]},
                "root-1:worker": {"reasoning_effort": ["low"]},
                "root-1:inline:unknown": {"reasoning_effort": ["high"]},
            },
        }
        decomposition = query.current_model_decomposition(result, metadata)
        self.assertEqual([row["key"] for row in decomposition["rows"]],
                         ["claude-haiku + high", "claude-haiku + low",
                          "claude-sonnet + high"])

    def test_summarize_end_to_end_writes_valid_sidecars_and_final_response(self):
        self._fixture()
        outside_agents = Path(self.tmp.name) / "outside-agents"
        outside_agents.mkdir()
        (outside_agents / "secret.md").write_text("DO NOT CAPTURE")
        claude_home = self.ws / ".sessions" / "claude"
        (claude_home / "agents").symlink_to(outside_agents)
        outside_version = Path(self.tmp.name) / "outside-version.json"
        outside_version.write_text('{"version":"LEAKED"}')
        (claude_home / "version.json").symlink_to(outside_version)
        out = Path(self.tmp.name) / "snapshot"
        root = Path(__file__).resolve().parent
        result = subprocess.run([
            sys.executable, str(root / "tools" / "summarize.py"),
            "--workspace", str(self.ws), "--out", str(out),
            "--harness", "claude", "--roots", "root-1",
        ], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        metadata = json.loads((out / "metadata.json").read_text())
        expenses = json.loads((out / "expenses.json").read_text())
        measurements = json.loads((out / "measurements.json").read_text())
        sessions = json.loads((out / "sessions.json").read_text())
        configs = json.loads((out / "configs.json").read_text())
        self.assertEqual(metadata["harness"]["harness"], "claude")
        self.assertIsNone(metadata["harness"]["cli_version"])
        self.assertEqual(metadata["models"]["claude-sonnet"]["reasoning_efforts_seen"],
                         ["high"])
        self.assertEqual(expenses["tokens"]["total"], 88)
        self.assertEqual(measurements["tool_usage"]["total"], 3)
        self.assertEqual(
            sessions["analysis"]["whole_session_stats"]["tools"]["by_category"],
            {"shell": 2, "files": 1},
        )
        credential = next(e for e in configs["configs"]
                          if e["path"].endswith("/.credentials.json"))
        self.assertTrue(credential["exists"])
        self.assertFalse(credential["content_included"])
        self.assertNotIn("history.jsonl", {Path(e["path"]).name
                                           for e in configs["configs"]})
        self.assertFalse(any(e["role"] == "claude_agent" for e in configs["configs"]))
        self.assertTrue((out / "contestant_final_response.md").is_file())
        self.assertEqual(
            (out / "contestant_final_response.md").read_text(),
            "Done. key [REDACTED_CREDENTIAL]",
        )
        self.assertEqual(
            validation.check_cli([str(out), "--schemas", str(root / "schemas")]), 0)


if __name__ == "__main__":
    unittest.main()
