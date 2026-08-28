import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from cfdeval import recording, report_pdf, validation


ROOT = Path(__file__).resolve().parent
SCHEMAS = ROOT / "schemas"
TOOL = ROOT / "tools" / "vendor_report_pdf.py"
PDF = b"%PDF-1.4\n1 0 obj <<>> endobj\n%%EOF\n"


def git_workspace(root: Path) -> tuple[Path, str]:
    workspace = root / "workspace"
    report = workspace / "report"
    report.mkdir(parents=True)
    (report / "report.tex").write_text("\\documentclass{article}\n")
    subprocess.run(["git", "init", "-q"], cwd=workspace, check=True)
    subprocess.run(["git", "add", "report/report.tex"], cwd=workspace, check=True)
    subprocess.run([
        "git", "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
        "commit", "-q", "-m", "report source",
    ], cwd=workspace, check=True)
    commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=workspace, text=True).strip()
    return workspace, commit


class ReportPdfTests(unittest.TestCase):
    def test_accepted_copy_is_exact_and_binary_index_validates(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            workspace, commit = git_workspace(root)
            source = workspace / "report" / "report.pdf"
            source.write_bytes(PDF)
            snapshot = root / "snapshot"
            record = report_pdf.record_accepted(
                snapshot=snapshot, workspace=workspace, source=source,
                source_mode="workspace_existing", source_tex="report/report.tex",
                submission_commit=commit, evaluator="terra",
                notes="Opened every page; this is the readable main benchmark report.",
                visually_reviewed=True, main_report_confirmed=True, readable=True)
            self.assertEqual((snapshot / "report.pdf").read_bytes(), PDF)
            self.assertEqual(record["snapshot"]["sha256"], hashlib.sha256(PDF).hexdigest())
            recording.write_index(
                snapshot, "run", {
                    "report_pdf.json": "report_pdf.schema.json", "report.pdf": None,
                }, ["vendor_report_pdf.py"], SCHEMAS)
            index = json.loads((snapshot / "index.json").read_text())
            self.assertEqual(index["artifacts"]["report.pdf"]["artifact_kind"], "binary")
            self.assertEqual(index["artifacts"]["report.pdf"]["schema"], None)
            self.assertEqual(validation.check_cli([
                str(snapshot), "--schemas", str(SCHEMAS)]), 0)

    def test_accepted_requires_manual_review_and_rejects_source_symlink(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            workspace, commit = git_workspace(root)
            real = workspace / "report" / "real.pdf"
            real.write_bytes(PDF)
            source = workspace / "report" / "report.pdf"
            source.symlink_to(real.name)
            with self.assertRaisesRegex(ValueError, "symlink"):
                report_pdf.record_accepted(
                    snapshot=root / "snapshot", workspace=workspace, source=source,
                    source_mode="workspace_existing", source_tex="report/report.tex",
                    submission_commit=commit, evaluator="terra", notes="reviewed",
                    visually_reviewed=True, main_report_confirmed=True, readable=True)
            source.unlink()
            source.write_bytes(PDF)
            with self.assertRaisesRegex(ValueError, "visual review"):
                report_pdf.record_accepted(
                    snapshot=root / "snapshot", workspace=workspace, source=source,
                    source_mode="workspace_existing", source_tex="report/report.tex",
                    submission_commit=commit, evaluator="terra", notes="reviewed")

    def test_cli_rejects_submission_mismatch(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            workspace, commit = git_workspace(root)
            source = workspace / "report" / "report.pdf"
            source.write_bytes(PDF)
            snapshot = root / "snapshot"
            snapshot.mkdir()
            (snapshot / "run_identity.json").write_text(json.dumps({
                "submission_commit": commit,
            }))
            result = subprocess.run([
                sys.executable, str(TOOL), "--snapshot", str(snapshot),
                "--workspace", str(workspace), "--source", str(source),
                "--source-mode", "workspace_existing",
                "--source-tex", "report/report.tex",
                "--submission-commit", "0" * 40,
                "--evaluator", "terra", "--notes", "reviewed",
                "--approved", "--visually-reviewed", "--main-report-confirmed",
                "--readable",
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn("does not match", result.stderr)

    def test_absent_record_requires_commit_and_refuses_stale_pdf(self):
        with tempfile.TemporaryDirectory() as raw:
            snapshot = Path(raw) / "snapshot"
            with self.assertRaisesRegex(ValueError, "submission commit"):
                report_pdf.record_absent(
                    snapshot=snapshot, evaluator="terra", reason="No report.")
            snapshot.mkdir(exist_ok=True)
            (snapshot / "report.pdf").write_bytes(PDF)
            with self.assertRaisesRegex(ValueError, "stale"):
                report_pdf.record_absent(
                    snapshot=snapshot, evaluator="terra", reason="No report.",
                    submission_commit="1" * 40)

    def test_binary_tamper_and_unsafe_index_path_fail(self):
        with tempfile.TemporaryDirectory() as raw:
            snapshot = Path(raw)
            (snapshot / "report.pdf").write_bytes(PDF)
            recording.write_index(
                snapshot, "run", {"report.pdf": None}, ["test"], SCHEMAS)
            (snapshot / "report.pdf").write_bytes(PDF + b"tamper")
            self.assertEqual(validation.check_cli([
                str(snapshot), "--schemas", str(SCHEMAS)]), 1)
            with self.assertRaisesRegex(ValueError, "top-level"):
                recording.write_index(
                    snapshot, "run", {"../report.pdf": None}, ["test"], SCHEMAS)


if __name__ == "__main__":
    unittest.main()
