import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from cfdeval import completion, report_pdf


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "outputs" / "codex_gpt56_03_93b257"
RECORD = ROOT / "tools" / "record_agent_results.py"


class CompletionGateTests(unittest.TestCase):
    def copy_complete_snapshot(self, raw: str) -> Path:
        target = Path(raw) / SOURCE.name
        shutil.copytree(SOURCE, target)
        identity = json.loads((target / "run_identity.json").read_text())
        report_pdf.record_absent(
            snapshot=target, evaluator="completion-test",
            reason="Fixture intentionally records no vendored report PDF.",
            submission_commit=identity["submission_commit"])
        result = subprocess.run(
            [sys.executable, str(RECORD), "--folder", str(target)],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return target

    def test_complete_snapshot_passes_after_overalls_are_recomputed(self):
        with tempfile.TemporaryDirectory() as raw:
            target = self.copy_complete_snapshot(raw)
            self.assertEqual(completion.completion_errors(target), [])

    def test_missing_report_pdf_record_fails_completion(self):
        with tempfile.TemporaryDirectory() as raw:
            target = self.copy_complete_snapshot(raw)
            (target / "report_pdf.json").unlink()
            errors = completion.completion_errors(target)
            self.assertTrue(any("missing required file: report_pdf.json" in e
                                for e in errors))

    def test_accepted_report_pdf_tamper_fails_completion(self):
        with tempfile.TemporaryDirectory() as raw:
            target = self.copy_complete_snapshot(raw)
            identity = json.loads((target / "run_identity.json").read_text())
            data = b"%PDF-1.4\n1 0 obj <<>> endobj\n%%EOF\n"
            digest = __import__("hashlib").sha256(data).hexdigest()
            (target / "report.pdf").write_bytes(data)
            (target / "report_pdf.json").write_text(json.dumps({
                "schema_version": "1.0", "status": "accepted",
                "recorded_at": "2026-08-28T00:00:00Z", "evaluator": "test",
                "submission_commit": identity["submission_commit"],
                "source": {"mode": "compiled_from_submission",
                           "report_tex": "report/report.tex", "sha256": digest,
                           "bytes": len(data), "workspace_relative_pdf": None,
                           "build_command": "latexmk -pdf report.tex"},
                "snapshot": {"filename": "report.pdf", "media_type": "application/pdf",
                             "sha256": digest, "bytes": len(data),
                             "pdf_header_valid": True, "pdf_eof_valid": True},
                "appropriateness": {"approved": True, "visually_reviewed": True,
                                    "main_report_confirmed": True, "readable": True,
                                    "notes": "Reviewed fixture report."},
            }, indent=2))
            result = subprocess.run(
                [sys.executable, str(RECORD), "--folder", str(target)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(completion.completion_errors(target), [])
            (target / "report.pdf").write_bytes(data + b"tamper")
            errors = completion.completion_errors(target)
            self.assertTrue(any("report.pdf" in error and "mismatch" in error
                                for error in errors))

    def test_null_review_point_and_digest_mismatch_fail(self):
        with tempfile.TemporaryDirectory() as raw:
            target = self.copy_complete_snapshot(raw)
            path = target / "agent_scores.json"
            scores = json.loads(path.read_text())
            scores["scores"]["code_review"]["points"][0]["score"] = None
            path.write_text(json.dumps(scores, indent=2) + "\n")
            errors = completion.completion_errors(target)
            self.assertTrue(any("index digest mismatch: agent_scores.json" in e
                                for e in errors))
            self.assertTrue(any("code_review.code.build: score must be complete" in e
                                for e in errors))

    def test_partial_rubric_is_not_scaled_to_a_final_total(self):
        with tempfile.TemporaryDirectory() as raw:
            target = self.copy_complete_snapshot(raw)
            path = target / "agent_scores.json"
            scores = json.loads(path.read_text())
            scores["rubric"]["sections"][0]["score"] = None
            path.write_text(json.dumps(scores, indent=2) + "\n")
            result = subprocess.run(
                [sys.executable, str(RECORD), "--folder", str(target)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 3)
            recorded = json.loads(path.read_text())
            self.assertIsNone(recorded["rubric"]["total_scored"])

    def test_null_case_score_fails_even_with_a_limitation_note(self):
        with tempfile.TemporaryDirectory() as raw:
            target = self.copy_complete_snapshot(raw)
            path = target / "agent_scores.json"
            scores = json.loads(path.read_text())
            case_id = next(iter(scores["case_scores"]))
            scores["case_scores"][case_id] = {
                "score": None,
                "notes": "Evidence is unavailable.",
            }
            path.write_text(json.dumps(scores, indent=2) + "\n")
            errors = completion.completion_errors(target)
            self.assertTrue(any(
                f"case_scores.{case_id}: score must be complete and within 0-5" in e
                for e in errors
            ))


if __name__ == "__main__":
    unittest.main()
