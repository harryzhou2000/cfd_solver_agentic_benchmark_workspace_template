import importlib.util
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


if __name__ == "__main__":
    unittest.main()
