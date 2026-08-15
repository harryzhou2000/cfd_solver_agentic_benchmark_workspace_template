"""Small end-to-end fixture for the dependency-light report generator."""
from __future__ import annotations

import csv
import importlib.util
import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "tools" / "generate_report.py"
MODULE_SPEC = importlib.util.spec_from_file_location("report_generator_test_module", SCRIPT)
assert MODULE_SPEC and MODULE_SPEC.loader
REPORT_GENERATOR = importlib.util.module_from_spec(MODULE_SPEC)
sys.modules[MODULE_SPEC.name] = REPORT_GENERATOR
MODULE_SPEC.loader.exec_module(REPORT_GENERATOR)


VTU = """<?xml version="1.0"?>
<VTKFile type="UnstructuredGrid"><UnstructuredGrid><Piece NumberOfPoints="4" NumberOfCells="1">
<CellData><DataArray Name="density" format="ascii">1</DataArray><DataArray Name="u" format="ascii">1</DataArray><DataArray Name="v" format="ascii">0</DataArray><DataArray Name="pressure" format="ascii">1</DataArray><DataArray Name="mach" format="ascii">0.1</DataArray><DataArray Name="temperature" format="ascii">1</DataArray></CellData>
<Points><DataArray format="ascii">0 0 0 1 0 0 1 1 0 0 1 0</DataArray></Points>
<Cells><DataArray Name="connectivity" format="ascii">0 1 2 3</DataArray><DataArray Name="offsets" format="ascii">4</DataArray><DataArray Name="types" format="ascii">7</DataArray></Cells>
</Piece></UnstructuredGrid></VTKFile>"""


class ReportToolsTest(unittest.TestCase):
    def test_residual_reader_is_bounded_and_preserves_endpoints(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "residuals.csv"
            rows = ["step,inner_iter,residual_l2,residual_linf"]
            for step in range(10):
                rows.extend((f"{step},1,{1.0 / (step + 1)},2", f"{step},2,{0.5 / (step + 1)},1"))
            path.write_text("\n".join(rows) + "\n")
            sampled = REPORT_GENERATOR.read_residuals(path, max_outer_samples=3)
            self.assertLessEqual(len(sampled), 5)  # first, last, and bounded outer-step samples
            self.assertEqual(sampled[0]["step"], "0")
            self.assertEqual(sampled[-1]["step"], "9")
            self.assertEqual(sampled[-1]["inner_iter"], "2")

    def test_one_complete_case_generates_traceable_figures(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); case = root / "results" / "naca0012_m015_laminar_re5000"; case.mkdir(parents=True)
            solver_argv = "cfd_solver solve --case naca.json --output result"
            command = f"mpirun -np 1 {solver_argv}"
            (case / "metadata.json").write_text(json.dumps({"case_id": case.name, "mpi_ranks": 1, "viscous_flux": "required", "completed": True, "convergence_status": "converged"}))
            (case / "run_status.json").write_text(json.dumps({"case_id": case.name, "command": solver_argv, "mpi_ranks": 1, "final_step": 2, "final_physical_time": 0, "wall_time_seconds": 1, "residual_reduction_orders": 5, "convergence_status": "converged"}))
            (case / "residuals.csv").write_text("\n".join([
                "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf",
                "1,0,1,1,1,1,1,1,1,1,2", "2,0,1,1,1,1,1,1,1,.001,.002",
            ]) + "\n")
            (case / "forces.csv").write_text("\n".join([
                "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift",
                "1,0,0.001,.02,0,.01,.01,.001,0", "2,0,0.001,.02,0,.01,.01,.001,0",
            ]) + "\n")
            (case / "surface.csv").write_text("\n".join([
                "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag",
                "0,0,0,1,1,-1,.01,1,0,0,.1,WALL",
                "1,0,0,1,1,1,.02,1,0,0,.1,WALL",
            ]) + "\n")
            (case / "field_final_rank0000.vtu").write_text(VTU)
            (case / "field_final.pvtu").write_text("<VTKFile type=\"PUnstructuredGrid\"/>")
            report = root / "report"
            completed = subprocess.run([sys.executable, str(SCRIPT), "--results", str(root / "results"), "--report", str(report)], capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertTrue((report / "figures" / "naca0012_m015_laminar_re5000_mach.png").is_file())
            figures = (report / "figure_manifest.csv").read_text()
            self.assertIn("field_final.pvtu", figures)
            self.assertIn("naca0012_m015_laminar_re5000_mach.png", figures)
            self.assertIn("cp,cf", figures)
            self.assertNotIn("figures/naca0012_m015_laminar_re5000_mach.png", figures)
            tex = (report / "report.tex").read_text()
            self.assertIn(r"\includegraphics[width=0.88\linewidth]{figures/naca0012_m015_laminar_re5000_mach.png}", tex)
            self.assertIn(r"Figure~\ref{fig:naca0012_m015_laminar_re5000_mach}", tex)
            self.assertIn(r"\texttt{run\_manifest.csv}", tex)
            with (report / "run_manifest.csv").open(newline="") as handle:
                manifest = list(csv.DictReader(handle))
            self.assertEqual(manifest[3]["command"], command)
            checks = json.loads((report / "sanity_checks.json").read_text())
            case_checks = checks["cases"][case.name]
            self.assertTrue(case_checks["checks"]["positive_pressure"])
            self.assertTrue(case_checks["checks"]["no_slip_wall_speed"])
            self.assertTrue(case_checks["checks"]["viscous_wall_nonzero_cf"])
            self.assertTrue(case_checks["checks"]["figure_manifest_mach_mapping"])

    def test_re200_spectrum_and_rank_comparisons_are_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); case = root / "results" / "cylinder_m010_laminar_re200"; case.mkdir(parents=True)
            command = "mpirun -np 8 cfd_solver solve --case re200.json --output result"
            (case / "metadata.json").write_text(json.dumps({"case_id": case.name, "mpi_ranks": 8, "viscous_flux": "required", "true_bdf2_inner_loop": True, "completed": True, "convergence_status": "statistically_periodic"}))
            (case / "run_status.json").write_text(json.dumps({"case_id": case.name, "command": command, "mpi_ranks": 8, "final_step": 127, "final_physical_time": 12.7, "wall_time_seconds": 8, "residual_reduction_orders": 4, "convergence_status": "statistically_periodic"}))
            (case / "residuals.csv").write_text("step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n1,0,1,1,.1,1,1,1,1,1,2\n127,12.7,1,1,.1,1,1,1,1,.001,.002\n")
            force_rows = ["step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift"]
            for step in range(128):
                time = step * 0.1; lift = math.sin(2.0 * math.pi * 0.5 * time)
                force_rows.append(f"{step},{time},{lift},1.2,0,.8,.4,{lift},0")
            (case / "forces.csv").write_text("\n".join(force_rows) + "\n")
            (case / "surface.csv").write_text("x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n0,0,0,1,1,-1,.1,1,0,0,0,WALL\n1,0,0,1,1,1,.1,1,0,0,0,WALL\n")
            (case / "field_final_rank0000.vtu").write_text(VTU)
            inputs = root / "cases"; inputs.mkdir()
            (inputs / "cylinder_m010_laminar_re200.json").write_text(json.dumps({"case_id": case.name, "reference": {"length": 1.0}, "freestream": {"velocity_magnitude": 1.0}}))
            comparisons = root / "results" / "rank_comparisons"; comparisons.mkdir()
            header = ["case_id", "mpi_ranks", "command", "wall_time_seconds", "final_step", "final_physical_time", "cd", "cl", "cmz", "residual_reduction_orders", "owned_cells_min", "owned_cells_max", "ghost_cells_mean", "load_balance_ratio", "partition_edge_cut", "notes"]
            rows = [
                ["naca0012_m080_inviscid", "1", "mpirun -np 1 solver solve --case naca.json", "10", "100", "0", ".1", "0", "0", "4", "100", "100", "0", "1", "0", "reference"],
                ["naca0012_m080_inviscid", "8", "mpirun -np 8 solver solve --case naca.json", "3", "100", "0", ".101", ".001", "0", "4", "12", "13", "3", "1.08", "20", "parallel"],
                [case.name, "1", "mpirun -np 1 solver solve --case re200.json", "12", "127", "12.7", "1.2", "0", "0", "4", "100", "100", "0", "1", "0", "reference"],
                [case.name, "8", "mpirun -np 8 solver solve --case re200.json", "4", "127", "12.7", "1.21", ".002", "0", "4", "12", "13", "3", "1.08", "20", "parallel"],
            ]
            with (comparisons / "mpi_comparison.csv").open("w", newline="") as handle:
                writer = csv.writer(handle); writer.writerow(header); writer.writerows(rows)
            runner_output = comparisons / case.name / "np8"; runner_output.mkdir(parents=True)
            (runner_output / "partition_diagnostics.csv").write_text("rank,num_cells_owned,num_cells_ghost\n0,12,3\n1,13,4\n")
            runner_header = ["case_id", "ranks", "command", "wall_time_seconds", "solver_wall_time_seconds", "launch_returncode", "validator_command", "validator_returncode", "validation_passed", "convergence_status", "final_step", "final_physical_time", "residual_l2", "residual_linf", "cl", "cd", "cmz", "pressure_drag", "viscous_drag", "pressure_lift", "viscous_lift", "partition_edge_cut", "owned_cells_min", "owned_cells_max", "owned_cells_mean"]
            runner_row = [case.name, "8", "mpirun -np 8 runner cfd_solver solve --case re200.json", "4", "3.9", "0", "python validator.py", "0", "true", "statistically_periodic", "127", "12.7", ".001", ".002", ".002", "1.21", "0", ".8", ".4", ".002", "0", "20", "12", "13", "12.5"]
            with (comparisons / "comparison.csv").open("w", newline="") as handle:
                writer = csv.writer(handle); writer.writerow(runner_header); writer.writerow(runner_row)
            report = root / "report"
            completed = subprocess.run([sys.executable, str(SCRIPT), "--results", str(root / "results"), "--report", str(report), "--case-inputs", str(inputs)], capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            with (report / "re200_lift_spectrum.csv").open(newline="") as handle:
                spectrum = list(csv.DictReader(handle))
            self.assertTrue(spectrum)
            self.assertEqual(sum(int(row["is_dominant"]) for row in spectrum), 1)
            checks = json.loads((report / "sanity_checks.json").read_text())
            self.assertTrue(checks["re200_lift_spectrum"]["available"])
            self.assertGreater(checks["re200_lift_spectrum"]["dominant_frequency"], 0.0)
            self.assertGreater(checks["re200_lift_spectrum"]["strouhal"], 0.0)
            self.assertGreater(checks["re200_lift_spectrum"]["mean_drag"], 0.0)
            self.assertGreater(checks["re200_lift_spectrum"]["lift_amplitude"], 0.0)
            tex = (report / "report.tex").read_text()
            self.assertIn("MPI parallelization and METIS partitioning", tex)
            self.assertIn("true BDF2 inner loop", tex)
            self.assertIn("Limitations and failure analysis", tex)
            self.assertIn(command, tex)
            self.assertNotIn("\texttt", tex)
            rank_manifest = (report / "rank_comparison_manifest.csv").read_text()
            self.assertIn("mpirun -np 8 solver solve --case re200.json", rank_manifest)
            self.assertIn("mpirun -np 8 runner cfd_solver solve --case re200.json", rank_manifest)
            self.assertTrue((report / "figures" / "cylinder_m010_laminar_re200_lift_spectrum.png").is_file())


if __name__ == "__main__":
    unittest.main()
