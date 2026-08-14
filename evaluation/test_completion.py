import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from cfdeval import completion


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "outputs" / "codex_gpt56_03_93b257"
RECORD = ROOT / "tools" / "record_agent_results.py"


class CompletionGateTests(unittest.TestCase):
    def copy_complete_snapshot(self, raw: str) -> Path:
        target = Path(raw) / SOURCE.name
        shutil.copytree(SOURCE, target)
        result = subprocess.run(
            [sys.executable, str(RECORD), "--folder", str(target)],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return target

    def test_complete_snapshot_passes_after_overalls_are_recomputed(self):
        with tempfile.TemporaryDirectory() as raw:
            target = self.copy_complete_snapshot(raw)
            self.assertEqual(completion.completion_errors(target), [])

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


if __name__ == "__main__":
    unittest.main()
