import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parent / "tools" / "summarize_force_history.py"
SPEC = importlib.util.spec_from_file_location("summarize_force_history", SCRIPT)
summary = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(summary)


class ForceHistorySummaryTests(unittest.TestCase):
    def write(self, directory, content):
        path = Path(directory) / "forces.csv"
        path.write_text(content, encoding="utf-8")
        return path

    def test_tail_uses_rows_and_reports_coverage(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.write(tmp, "physical_time,cd,cl\n0,5,0\n0.1,4,1\n1,3,2\n10,2,3\n")
            result = summary.summarize(path, ["cd", "cl"], 0.5, None)
            self.assertEqual((result["rows_total"], result["rows_selected"]), (4, 2))
            self.assertEqual((result["physical_time_first"], result["physical_time_last"]), (1, 10))
            self.assertEqual(result["columns"]["cd"], {"first": 3, "last": 2, "min": 2, "max": 3, "mean": 2.5})

    def test_time_window_is_inclusive(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.write(tmp, "physical_time,cd\n0,5\n1,4\n2,3\n")
            result = summary.summarize(path, ["cd"], None, 1)
            self.assertEqual(result["rows_selected"], 2)
            self.assertEqual(result["columns"]["cd"]["mean"], 3.5)

    def test_invalid_numeric_data_and_nonmonotonic_time_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.write(tmp, "physical_time,cd\n0,NaN\n")
            with self.assertRaisesRegex(ValueError, "not finite"):
                summary.summarize(path, ["cd"], 0.1, None)
            path = self.write(tmp, "physical_time,cd\n2,1\n1,2\n")
            with self.assertRaisesRegex(ValueError, "not nondecreasing"):
                summary.summarize(path, ["cd"], 0.1, None)

    def test_missing_column_and_empty_selection_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.write(tmp, "physical_time,cd\n0,5\n")
            with self.assertRaisesRegex(ValueError, "missing CSV columns"):
                summary.summarize(path, ["cl"], 0.1, None)
            with self.assertRaisesRegex(ValueError, "selection contains no"):
                summary.summarize(path, ["cd"], None, 1)


if __name__ == "__main__":
    unittest.main()
