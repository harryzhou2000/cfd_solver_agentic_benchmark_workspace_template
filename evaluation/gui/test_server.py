import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import server
from cfdeval import codex_data
from cfdeval.sessions import CodexThreadEvents


class ReportPdfDiscoveryTests(unittest.TestCase):
    def test_prefers_report_pdf_and_skips_build_and_venv(self):
        with tempfile.TemporaryDirectory() as raw:
            workspace = Path(raw)
            (workspace / "solver" / "report").mkdir(parents=True)
            (workspace / "solver" / "report" / "report.pdf").write_bytes(b"%PDF-report")
            (workspace / "build").mkdir()
            (workspace / "build" / "report.pdf").write_bytes(b"%PDF-build")
            (workspace / ".venv").mkdir()
            (workspace / ".venv" / "report.pdf").write_bytes(b"%PDF-venv")
            found = server.find_report_pdf(workspace)
            self.assertEqual(found["relative_path"], "solver/report/report.pdf")

    def test_workspace_identity_cannot_escape_workspace_root(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "workspace"
            valid = root / "codex" / "model" / "01"
            valid.mkdir(parents=True)
            with patch.object(server, "WORKSPACE_ROOT", root):
                self.assertEqual(
                    server.workspace_for_snapshot({
                        "workspace": str(valid),
                        "initial_branch": "codex/model/init",
                        "operator_number": "01",
                    }),
                    valid.resolve(),
                )
                self.assertIsNone(server.workspace_for_snapshot({
                    "workspace": str(Path(raw).parent),
                    "initial_branch": "../../escape/init",
                    "operator_number": "01",
                }))


class SnapshotProtocolTests(unittest.TestCase):
    def test_detail_exposes_identity_final_response_and_pdf_metadata(self):
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            workspace_root = base / "workspace"
            workspace = workspace_root / "codex" / "model" / "01"
            (workspace / "report").mkdir(parents=True)
            (workspace / "report" / "report.pdf").write_bytes(b"%PDF-test")
            folder = base / "outputs" / "codex_model_01_deadbe"
            folder.mkdir(parents=True)
            (folder / "summary.json").write_text(json.dumps({"contestant": {}}))
            (folder / "metadata.json").write_text(json.dumps({"models": {}}))
            (folder / "index.json").write_text("{}")
            identity = {
                "run_id": folder.name,
                "workspace": str(workspace),
                "initial_branch": "codex/model/init",
                "operator_number": "01",
            }
            (folder / "run_identity.json").write_text(json.dumps(identity))
            (folder / "contestant_final_response.md").write_text("final prose\n")
            with patch.object(server, "WORKSPACE_ROOT", workspace_root):
                detail = server.snapshot_detail(folder)
            self.assertEqual(detail["run_identity"]["run_id"], folder.name)
            self.assertEqual(detail["metadata"], {"models": {}})
            self.assertEqual(detail["markdown"]["contestant_final_response.md"], "final prose\n")
            self.assertEqual(detail["report_pdf"]["relative_path"], "report/report.pdf")

    def test_table_uses_agent_scores_and_legacy_post_run_protocol(self):
        with tempfile.TemporaryDirectory() as raw:
            folder = Path(raw)
            summary = {
                "snapshot": {"env_snapshot_captured": True},
                "code_review": {"overall_score": 1.0},
                "cfd_review": {"overall_score": 1.0},
                "result_review": {"overall_score": 1.0},
            }
            (folder / "agent_scores.json").write_text(json.dumps({
                "session_selection": {"execution_date": "2026-07-31"},
                "scores": {
                    "code_review": {"overall_score": 3.2},
                    "cfd_review": {"overall_score": 3.3},
                    "result_review": {"overall_score": 1.4},
                },
                "rubric": {"total_scored": 58},
                "disqualification": {"triggered": True},
            }))
            (folder / "run_identity.json").write_text(json.dumps({"run_id": "codex_model_01_deadbe"}))
            (folder / "env_snapshot.json").write_text(json.dumps({
                "provenance": {"capture_kind": "post-run reconstruction", "pre_run_authority": False},
            }))
            row = server.query.row_for(folder, summary)
            self.assertEqual(row["run_id"], "codex_model_01_deadbe")
            self.assertEqual((row["code_score"], row["cfd_score"], row["result_score"]),
                             (3.2, 3.3, 1.4))
            self.assertEqual(row["execution_date"], "2026-07-31")
            self.assertTrue(row["disqualified"])
            self.assertEqual(row["env_capture_phase"], "post_run")

    def test_hidden_attribute_beats_component_display_rules(self):
        css = (Path(__file__).parent / "static" / "styles.css").read_text()
        self.assertIn("[hidden] { display: none !important; }", css)


class RolloutUsageTests(unittest.TestCase):
    def test_rollout_usage_and_context_window_are_persisted(self):
        with tempfile.TemporaryDirectory() as raw:
            rollout = Path(raw) / "rollout.jsonl"
            rows = [
                {"timestamp": "2026-01-01T00:00:00Z", "type": "event_msg", "payload": {
                    "type": "token_count", "info": {
                        "total_token_usage": {"input_tokens": 100, "cached_input_tokens": 80,
                            "output_tokens": 10, "reasoning_output_tokens": 4, "total_tokens": 110},
                        "last_token_usage": {"input_tokens": 100},
                        "model_context_window": 258400,
                    }}},
                {"timestamp": "2026-01-01T00:01:00Z", "type": "event_msg", "payload": {
                    "type": "token_count", "info": {
                        "total_token_usage": {"input_tokens": 160, "cached_input_tokens": 128,
                            "output_tokens": 20, "reasoning_output_tokens": 6, "total_tokens": 180},
                        "last_token_usage": {"input_tokens": 60},
                        "model_context_window": 258400,
                    }}},
            ]
            rollout.write_text("\n".join(json.dumps(row) for row in rows) + "\n")
            facts = codex_data.rollout_usage_facts(str(rollout))
            self.assertEqual(facts["input_tokens"], 160)
            self.assertEqual(facts["cached_input_tokens"], 128)
            self.assertEqual(facts["non_cached_input_tokens"], 32)
            self.assertEqual(facts["output_tokens"], 20)
            self.assertEqual(facts["model_context_window"], 258400)
            self.assertEqual(facts["max_prompt_input_tokens"], 100)
            stream = CodexThreadEvents("root", str(rollout))
            stream.load()
            self.assertEqual(stream.cumulative_usage["total_tokens"], 180)
            self.assertEqual(stream.model_context_window, 258400)

    def test_entry_settings_use_first_applied_thread_settings(self):
        with tempfile.TemporaryDirectory() as raw:
            rollout = Path(raw) / "rollout.jsonl"
            rows = [
                {"type": "event_msg", "payload": {"type": "thread_settings_applied",
                    "thread_settings": {"model": "gpt-5.6-sol", "reasoning_effort": "ultra"}}},
                {"type": "event_msg", "payload": {"type": "thread_settings_applied",
                    "thread_settings": {"model": "gpt-5.6-terra", "reasoning_effort": "medium"}}},
            ]
            rollout.write_text("\n".join(json.dumps(row) for row in rows) + "\n")
            self.assertEqual(codex_data.rollout_entry_settings(str(rollout)), {
                "model": "gpt-5.6-sol", "reasoning_effort": "ultra"})


if __name__ == "__main__":
    unittest.main()
