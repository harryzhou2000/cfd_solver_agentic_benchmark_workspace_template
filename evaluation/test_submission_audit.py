import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = (
    Path(__file__).resolve().parents[1]
    / ".codex/skills/cfd-benchmark-evaluation/scripts/audit_submission_commit.py"
)
SPEC = importlib.util.spec_from_file_location("audit_submission_commit", SCRIPT)
audit = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(audit)


class SubmissionAuditTests(unittest.TestCase):
    def test_output_namespace_inside_source_tree_is_allowed(self):
        self.assertIsNone(audit.classify("solver/src/output/csv_writer.cpp"))
        self.assertIsNone(audit.classify("solver/include/output/metadata.hpp"))
        self.assertEqual(
            audit.classify("solver/src/output/generated.csv"),
            "prohibited generated/data extension .csv",
        )
        self.assertEqual(
            audit.classify("solver/output/generated.cpp"),
            "raw/generated/build directory",
        )
        self.assertEqual(
            audit.classify("solver/results/src/generated.cpp"),
            "raw/generated/build directory",
        )

    def test_png_referenced_from_local_input_and_graphicspath(self):
        blobs = {
            "solver/report/report.tex": (
                r"\graphicspath{{figures/}}" "\n"
                r"\input{generated_results}" "\n"
            ),
            "solver/report/generated_results.tex": (
                r"\includegraphics[width=.9\linewidth]{case\_np8\_mach.png}" "\n"
            ),
        }
        with mock.patch.object(audit, "committed_text", side_effect=lambda _w, _c, p: blobs.get(p)):
            self.assertTrue(audit.png_is_referenced(
                Path("/unused"), "deadbeef", "solver/report/figures/case_np8_mach.png"
            ))
            self.assertFalse(audit.png_is_referenced(
                Path("/unused"), "deadbeef", "solver/report/figures/unreferenced.png"
            ))

    def test_tex_dependency_cannot_escape_report_directory(self):
        blobs = {
            "solver/report/report.tex": r"\input{../../outside}",
            "outside.tex": r"\includegraphics{figures/secret.png}",
        }
        with mock.patch.object(audit, "committed_text", side_effect=lambda _w, _c, p: blobs.get(p)):
            sources = audit._report_sources(
                Path("/unused"), "deadbeef", "solver/report/report.tex"
            )
        self.assertEqual([path for path, _text in sources], ["solver/report/report.tex"])


class SubmissionAuditHistoryTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.repo = Path(self.tempdir.name)
        self.git("init")
        self.git("config", "user.email", "audit@example.invalid")
        self.git("config", "user.name", "Audit Test")
        (self.repo / "solver/src").mkdir(parents=True)
        (self.repo / "solver/src/main.cpp").write_text("int main() { return 0; }\n")
        self.git("add", "solver/src/main.cpp")
        self.git("commit", "-m", "initial")
        self.initial = self.git("rev-parse", "HEAD").stdout.strip()
        (self.repo / ".eval").mkdir()
        (self.repo / ".eval/env_snapshot.json").write_text(json.dumps({
            "workspace": {
                "branch": "codex/test/init",
                "commit": self.initial,
            }
        }))

    def tearDown(self):
        self.tempdir.cleanup()

    def git(self, *args):
        return subprocess.run(
            ["git", "-C", str(self.repo), *args],
            check=True,
            capture_output=True,
            text=True,
        )

    def audit_result(self, submission, checkpoint):
        result = subprocess.run(
            [
                "python3", str(SCRIPT),
                "--workspace", str(self.repo),
                "--submission-commit", submission,
                "--contestant-checkpoint-commit", checkpoint,
            ],
            capture_output=True,
            text=True,
        )
        return result, json.loads(result.stdout)

    def add_contestant_raw_result(self):
        (self.repo / "solver/results/run").mkdir(parents=True)
        (self.repo / "solver/results/run/residuals.csv").write_text("iter,res\n1,1\n")
        (self.repo / "solver/src/main.cpp").write_text("int main() { return 1; }\n")
        self.git("add", "solver/src/main.cpp", "solver/results/run/residuals.csv")
        self.git("commit", "-m", "contestant checkpoint")
        return self.git("rev-parse", "HEAD").stdout.strip()

    def test_preserves_contestant_history_and_accepts_tip_cleanup(self):
        checkpoint = self.add_contestant_raw_result()
        self.git("commit", "--allow-empty", "-m", "incomplete evaluator curation")
        self.git("rm", "--cached", "solver/results/run/residuals.csv")
        self.git("commit", "-m", "results: codex/test/01")
        submission = self.git("rev-parse", "HEAD").stdout.strip()

        process, result = self.audit_result(submission, checkpoint)

        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertTrue(result["passed"])
        self.assertEqual(result["contestant_checkpoint_commit"], checkpoint)
        self.assertTrue(result["initial_is_ancestor_of_checkpoint"])
        self.assertTrue(result["checkpoint_is_submission_ancestor"])
        self.assertIn(
            "solver/results/run/residuals.csv",
            [item["path"] for item in result["removed_prohibited"]],
        )
        self.assertTrue((self.repo / "solver/results/run/residuals.csv").is_file())

    def test_rejects_prohibited_artifact_still_present_at_tip(self):
        checkpoint = self.add_contestant_raw_result()
        self.git("commit", "--allow-empty", "-m", "results: codex/test/01")
        submission = self.git("rev-parse", "HEAD").stdout.strip()

        process, result = self.audit_result(submission, checkpoint)

        self.assertEqual(process.returncode, 1)
        self.assertFalse(result["passed"])
        self.assertIn(
            "solver/results/run/residuals.csv",
            [item["path"] for item in result["violations"]],
        )


if __name__ == "__main__":
    unittest.main()
