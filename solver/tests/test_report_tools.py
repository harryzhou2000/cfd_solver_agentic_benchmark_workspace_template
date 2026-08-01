from __future__ import annotations

import csv
import json
import math
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from build_report import BuildError, REQUIRED_CASES, build_report


RESIDUAL_HEADER = ["step", "physical_time", "inner_iter", "cfl", "dt", "rho", "rhou", "rhov", "rhoE", "residual_l2", "residual_linf"]
FORCE_HEADER = ["step", "physical_time", "cl", "cd", "cmz", "pressure_drag", "viscous_drag", "pressure_lift", "viscous_lift"]
SURFACE_HEADER = ["x", "y", "nx", "ny", "pressure", "cp", "cf", "rho", "u", "v", "mach", "tag"]


def _write_csv(path: Path, header: list[str], rows: list[list[object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(header)
        writer.writerows(rows)


def _write_field(path: Path) -> None:
    # A real, tiny, unstructured legacy VTK field—not a plotted-image fixture.
    path.write_text(
        """# vtk DataFile Version 3.0
tiny actual unstructured field
ASCII
DATASET UNSTRUCTURED_GRID
POINTS 4 float
0 0 0  1 0 0  1 1 0  0 1 0
CELLS 1 5
4 0 1 2 3
CELL_TYPES 1
9
POINT_DATA 4
SCALARS density float 1
LOOKUP_TABLE default
1 1 1 1
SCALARS pressure float 1
LOOKUP_TABLE default
1 1.1 1.2 1.1
SCALARS mach float 1
LOOKUP_TABLE default
0.1 0.2 0.3 0.2
VECTORS velocity float
0 0 0  0 1 0  -1 1 0  -1 0 0
""",
        encoding="utf-8",
    )


def create_results(root: Path) -> Path:
    results = root / "results"
    for case_id in REQUIRED_CASES:
        case = results / case_id
        case.mkdir(parents=True)
        transient = "re200" in case_id
        inviscid = "inviscid" in case_id
        metadata = {
            "case_id": case_id,
            "solver_name": "fixture_solver",
            "solver_version": "test",
            "mpi_ranks": 1,
            "mesh_file": "actual_mesh.cgns",
            "num_cells_global": 1,
            "num_faces_global": 4,
            "num_cells_owned_local": 1,
            "num_cells_ghost_local": 0,
            "partitioner": "metis_kway",
            "halo_exchange": "neighbor_isend_irecv",
            "equation_set": "compressible_navier_stokes_2d",
            "inviscid_flux": "rusanov",
            "viscous_flux": "none" if inviscid else "gradient",
            "time_integrator": "bdf2" if transient else "implicit",
            "implicit_solver": "jacobi",
            "reconstruction": "least_squares",
            "limiter": "barth_jespersen",
            "positivity_preservation": "face_scaling_and_update_backtracking",
            "wall_boundary_output_semantics": "boundary_value",
            "full_state_replication_during_iterations": False,
            "full_mesh_replication_during_iterations": False,
            "spatial_order_claimed": 2,
            "completed": True,
            "convergence_status": "statistically_periodic" if transient else "converged",
        }
        if transient:
            metadata.update({
                "true_bdf2_inner_loop": True,
                "min_inner_iterations": 5,
                "max_inner_iterations": 10,
                "observed_min_inner_iterations": 5,
                "observed_max_inner_iterations": 8,
                "inner_residual_reduction_target": 1e-3,
                "inner_target_misses": 0,
                "inner_target_converged_fraction": 1.0,
                "last_inner_residual_ratio": 1e-4,
            })
        status = {
            "case_id": case_id,
            "command": "mpirun -np 1 ./solver solve",
            "mpi_ranks": 1,
            "wall_time_seconds": 1.0,
            "final_step": 30000 if transient else 60,
            "final_physical_time": 300.0 if transient else 0.0,
            "convergence_status": metadata["convergence_status"],
            "residual_reduction_orders": 4.0,
            "notes": "synthetic test fixture generated from a minimal unstructured field; not solver evidence",
        }
        (case / "metadata.json").write_text(json.dumps(metadata), encoding="utf-8")
        (case / "run_status.json").write_text(json.dumps(status), encoding="utf-8")
        viscous = 0.0 if inviscid else 0.1
        cl_final = 0.2 if transient else 0.0
        if transient:
            residual_rows = []
            force_rows = []
            for step in range(1, 30001):
                time = step * 0.01
                cl = 0.2 * math.sin(2.0 * math.pi * 0.2 * time)
                residual_rows.append([step, time, 5, 1, 0.01, 0.1, 0.1, 0.1, 0.1,
                                      0.01 + 1.0e-5 * abs(math.sin(time)), 0.02])
                force_rows.append([step, time, cl, 1.1 + 0.01 * math.cos(2.0 * math.pi * 0.4 * time),
                                   0.0, 1.0, viscous, cl, 0.0])
        else:
            residual_rows = [[step, 0.0, 3, 1.0, 0.0, 1.0, 1.0, 1.0, 1.0,
                              10.0 ** (-0.1 * step), 2.0 * 10.0 ** (-0.1 * step)]
                             for step in range(1, 61)]
            force_rows = [[step, 0.0, cl_final, 1.1 + 1.0e-4 * math.sin(step), 0.0,
                           1.0, viscous, cl_final, 0.0] for step in range(1, 61)]
        _write_csv(case / "residuals.csv", RESIDUAL_HEADER, residual_rows)
        _write_csv(case / "forces.csv", FORCE_HEADER, force_rows)
        cf = 0.0 if inviscid else 0.01
        _write_csv(case / "surface.csv", SURFACE_HEADER, [[0, 0, 0, 1, 1.0, -1.0, cf, 1, 0, 0, 0, "wall"], [1, 0, 0, 1, 1.1, 1.0, cf, 1, 0, 0, 0, "wall"]])
        _write_csv(case / "partition_diagnostics.csv",
                   ["rank", "num_cells_owned", "num_cells_ghost", "num_boundary_faces",
                    "num_neighbor_ranks", "neighbor_ranks", "send_cells", "recv_cells"],
                   [[0, 1, 0, 4, 0, "", "", ""]])
        _write_field(case / "field_final.vtk")
    return results


def create_rank_results(root: Path, results: Path) -> tuple[Path, ...]:
    directories: list[Path] = []
    for case_id in ("naca0012_m015_inviscid", "cylinder_m010_laminar_re20"):
        for ranks in (1, 8):
            target = root / "rank_results" / f"{case_id}_np{ranks}"
            shutil.copytree(results / case_id, target)
            for name in ("metadata.json", "run_status.json"):
                path = target / name
                payload = json.loads(path.read_text(encoding="utf-8"))
                payload["mpi_ranks"] = ranks
                path.write_text(json.dumps(payload), encoding="utf-8")
            rows = [[rank, 1, 1 if ranks > 1 else 0, 1, 1 if ranks > 1 else 0,
                     str((rank + 1) % ranks) if ranks > 1 else "", "0", "0"]
                    for rank in range(ranks)]
            _write_csv(target / "partition_diagnostics.csv",
                       ["rank", "num_cells_owned", "num_cells_ghost", "num_boundary_faces",
                        "num_neighbor_ranks", "neighbor_ranks", "send_cells", "recv_cells"],
                       rows)
            directories.append(target)
    return tuple(directories)


class ReportAutomationTests(unittest.TestCase):
    def test_builds_complete_written_contract_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            results = create_results(root)
            rank_results = create_rank_results(root, results)
            report = root / "report"
            build_report(results, report, rank_results=rank_results)
            for name in ("report.tex", "run_manifest.csv", "sanity_checks.json", "figure_manifest.csv"):
                self.assertTrue((report / name).is_file(), name)
            self.assertTrue((report / "figures").is_dir())
            with (report / "figure_manifest.csv").open(newline="", encoding="utf-8") as handle:
                entries = list(csv.DictReader(handle))
            self.assertEqual({entry["case_id"] for entry in entries}, set(REQUIRED_CASES))
            self.assertTrue(all((report / "figures" / entry["figure_file"]).is_file() for entry in entries))
            self.assertTrue(all(entry["variable"] for entry in entries))
            for case_id in REQUIRED_CASES:
                variables = {entry["variable"] for entry in entries if entry["case_id"] == case_id}
                self.assertTrue({"residual", "force_coefficients", "surface_cp", "mach", "pressure"}.issubset(variables))
            re200_variables = {entry["variable"] for entry in entries if entry["case_id"] == "cylinder_m010_laminar_re200"}
            self.assertIn("vorticity", re200_variables)
            self.assertIn("lift_spectrum", re200_variables)
            sanity = json.loads((report / "sanity_checks.json").read_text(encoding="utf-8"))
            self.assertEqual(len(sanity["cases"]), 8)
            self.assertIn("submitted solver output only", sanity["generated_from"])
            tex = (report / "report.tex").read_text(encoding="utf-8")
            self.assertIn("Final force coefficients", tex)
            self.assertIn("cylinder\\_m010\\_laminar\\_re200", tex)

    def test_refuses_failed_case(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            results = create_results(root)
            rank_results = create_rank_results(root, results)
            target = results / REQUIRED_CASES[0] / "run_status.json"
            status = json.loads(target.read_text(encoding="utf-8"))
            status["convergence_status"] = "failed"
            target.write_text(json.dumps(status), encoding="utf-8")
            with self.assertRaises(BuildError):
                build_report(results, root / "report", rank_results=rank_results)
            self.assertFalse((root / "report").exists())

    def test_refuses_missing_re200_force_cadence(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            results = create_results(root)
            rank_results = create_rank_results(root, results)
            target = results / "cylinder_m010_laminar_re200" / "forces.csv"
            rows = list(csv.reader(target.open(newline="", encoding="utf-8")))
            with target.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.writer(handle)
                writer.writerows(rows[:100] + rows[101:])
            with self.assertRaisesRegex(BuildError, "one accepted sample for every physical step"):
                build_report(results, root / "report", rank_results=rank_results)

    def test_refuses_short_steady_history(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            results = create_results(root)
            rank_results = create_rank_results(root, results)
            case = results / "naca0012_m015_inviscid"
            for name in ("residuals.csv", "forces.csv"):
                target = case / name
                rows = list(csv.reader(target.open(newline="", encoding="utf-8")))
                with target.open("w", newline="", encoding="utf-8") as handle:
                    csv.writer(handle).writerows(rows[:3])
            status_path = case / "run_status.json"
            status = json.loads(status_path.read_text(encoding="utf-8"))
            status["final_step"] = 2
            status_path.write_text(json.dumps(status), encoding="utf-8")
            with self.assertRaisesRegex(BuildError, "steady production evidence needs"):
                build_report(results, root / "report", rank_results=rank_results)

    def test_refuses_final_force_time_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            results = create_results(root)
            rank_results = create_rank_results(root, results)
            target = results / "cylinder_m010_laminar_re200" / "forces.csv"
            rows = list(csv.reader(target.open(newline="", encoding="utf-8")))
            rows[-1][1] = "299.99"
            with target.open("w", newline="", encoding="utf-8") as handle:
                csv.writer(handle).writerows(rows)
            with self.assertRaisesRegex(BuildError, "physical_time does not match"):
                build_report(results, root / "report", rank_results=rank_results)


if __name__ == "__main__":
    unittest.main()
