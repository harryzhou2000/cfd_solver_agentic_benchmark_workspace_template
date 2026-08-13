import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import server


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


if __name__ == "__main__":
    unittest.main()
