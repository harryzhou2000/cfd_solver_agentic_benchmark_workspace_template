"""Focused parser and command-construction tests for rank comparison runner."""

from __future__ import annotations

import csv
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "tools" / "run_rank_comparisons.py"
SPEC = importlib.util.spec_from_file_location("rank_comparisons", SCRIPT)
assert SPEC and SPEC.loader
RUNNER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = RUNNER
SPEC.loader.exec_module(RUNNER)


class RankComparisonRunnerTest(unittest.TestCase):
    def test_re200_process_guard_is_specific_to_solver_launches(self) -> None:
        self.assertTrue(RUNNER.re200_job_active("mpirun -np 32 cfd_solver solve --case cylinder_m010_laminar_re200.json"))
        self.assertTrue(RUNNER.re200_job_active("/work/cfd_solver --case Re200"))
        self.assertFalse(RUNNER.re200_job_active("python generate_report.py --case re200"))
        self.assertFalse(RUNNER.re200_job_active("mpirun -np 8 cfd_solver solve --case naca0012_m200_inviscid.json"))

    def test_parse_summary_uses_final_rows_and_partition_range(self) -> None:
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as temporary:
            output = Path(temporary)
            (output / "metadata.json").write_text(json.dumps({"partition_edge_cut": 17}))
            (output / "run_status.json").write_text(json.dumps({
                "wall_time_seconds": 2.5,
                "convergence_status": "converged",
                "final_step": 12,
                "final_physical_time": 0.0,
            }))
            (output / "residuals.csv").write_text(
                "step,residual_l2,residual_linf\n1,4,8\n12,0.25,0.5\n"
            )
            (output / "forces.csv").write_text(
                "step,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n"
                "1,0,0,0,0,0,0,0\n12,1,2,3,4,5,6,7\n"
            )
            with (output / "partition_diagnostics.csv").open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=["rank", "num_cells_owned"])
                writer.writeheader()
                writer.writerows([
                    {"rank": 0, "num_cells_owned": 4},
                    {"rank": 1, "num_cells_owned": 9},
                    {"rank": 2, "num_cells_owned": 5},
                ])
            summary = RUNNER.parse_summary(output)
            self.assertEqual(summary["final_step"], 12)
            self.assertEqual(summary["residual_l2"], 0.25)
            self.assertEqual(summary["cd"], 2.0)
            self.assertEqual(summary["partition_edge_cut"], 17)
            self.assertEqual(summary["owned_cells_min"], 4)
            self.assertEqual(summary["owned_cells_max"], 9)
            self.assertEqual(summary["owned_cells_mean"], 6.0)

    def test_make_request_preserves_literal_arguments(self) -> None:
        root = Path("/comparison root")
        solver = Path("/solver path/cfd_solver")
        request = RUNNER.make_request(
            case_id="naca0012_m200_inviscid", ranks=8, solver=solver,
            mpi_launcher="mpirun", output_root=root,
        )
        self.assertEqual(request.command[:4], ("mpirun", "-np", "8", str(solver)))
        self.assertIn(str(root / "naca0012_m200_inviscid" / "np8"), request.command)


if __name__ == "__main__":
    unittest.main()
