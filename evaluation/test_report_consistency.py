import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parent / "tools" / "audit_report_score_consistency.py"
SPEC = importlib.util.spec_from_file_location("audit_report_score_consistency", SCRIPT)
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class ReportConsistencyTests(unittest.TestCase):
    def test_explicit_totals_respect_printed_precision(self):
        scores = {
            "rubric": {"total_scored": 98},
            "scores": {
                "code_review": {"overall_score": 4.65},
                "cfd_review": {"overall_score": 4.92},
                "result_review": {"overall_score": 5.0},
            },
        }
        text = "Rubric 91/100; Code 4.6/5, CFD 4.6/5, Results 4.7/5."
        found = audit.compare_report(text, scores)
        self.assertEqual([x["criterion"] for x in found], ["rubric", "code", "cfd", "results"])
        self.assertEqual([x["disagreement"] for x in found], [True, False, True, True])

    def test_unlabeled_case_scores_and_wrong_denominators_are_ignored(self):
        scores = {"rubric": {"total_scored": 82}, "scores": {}}
        self.assertEqual(audit.compare_report("Case 4/5; rubric 4/5; code 90/100.", scores), [])

    def test_inventory_reports_missing_artifacts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            complete = root / "complete"
            complete.mkdir()
            (complete / "agent_report.md").write_text("Rubric 80/100\n", encoding="utf-8")
            (complete / "agent_scores.json").write_text(json.dumps({"rubric": {"total_scored": 80}}), encoding="utf-8")
            (root / "missing").mkdir()
            result = audit.audit(root)
            self.assertEqual(result["snapshots_checked"], 1)
            self.assertEqual(result["snapshots_with_mentions"], 1)
            self.assertEqual(result["snapshots_with_disagreements"], 0)
            self.assertEqual(result["errors"][0]["snapshot"], "missing")


if __name__ == "__main__":
    unittest.main()
