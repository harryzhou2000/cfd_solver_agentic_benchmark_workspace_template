import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parent / "tools" / "audit_pdf_case_evidence_leads.py"
SPEC = importlib.util.spec_from_file_location("audit_pdf_case_evidence_leads", SCRIPT)
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class PdfCaseEvidenceLeadTests(unittest.TestCase):
    def test_accepted_workspace_pdf_and_absence_note_are_leads_only(self):
        scores = {"case_scores": {
            "a": {"score": 2, "notes": "no committed/referenced force plot"},
            "b": {"score": 1, "notes": "missing committed contour"},
            "c": {"score": 3, "notes": "no committed plot"},
            "d": {"score": 2, "notes": "failed result"},
        }}
        record = {"status": "accepted", "source": {"mode": "workspace_existing"}}
        self.assertEqual([x["case"] for x in audit.case_leads(scores, record)], ["a", "b"])
        record["source"]["mode"] = "compiled_from_submission"
        self.assertEqual(audit.case_leads(scores, record), [])

    def test_missing_record_is_reported_not_silently_skipped(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            complete = root / "complete"
            complete.mkdir()
            (complete / "agent_scores.json").write_text(json.dumps({"case_scores": {}}), encoding="utf-8")
            (complete / "report_pdf.json").write_text(json.dumps({"status": "absent"}), encoding="utf-8")
            (root / "incomplete").mkdir()
            result = audit.audit(root)
            self.assertEqual(result["snapshots_checked"], 1)
            self.assertEqual(result["case_leads"], 0)
            self.assertEqual(result["errors"][0]["snapshot"], "incomplete")


if __name__ == "__main__":
    unittest.main()
