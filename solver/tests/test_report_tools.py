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

from build_report import (BuildError, NACA_ZERO_AOA_LIFT_LIMIT, REQUIRED_CASES,
                          _load_case_dir, _native_primitive_fields,
                          _sanity_for_case, _validate_force_evidence,
                          build_report, read_field)


RESIDUAL_HEADER = ["step", "physical_time", "inner_iter", "cfl", "dt", "rho", "rhou", "rhov", "rhoE", "residual_l2", "residual_linf"]
FORCE_HEADER = ["step", "physical_time", "cl", "cd", "cmz", "pressure_drag", "viscous_drag", "pressure_lift", "viscous_lift"]
SURFACE_HEADER = ["x", "y", "nx", "ny", "pressure", "cp", "cf", "rho", "u", "v", "mach", "tag"]


def _write_csv(path: Path, header: list[str], rows: list[list[object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(header)
        writer.writerows(rows)


def _write_field(path: Path, case_input: dict) -> None:
    # A real, tiny, unstructured legacy VTK field—not a plotted-image fixture.
    rho = float(case_input["freestream"]["rho"])
    pressure = float(case_input["freestream"]["pressure"])
    velocity = float(case_input["freestream"]["velocity_magnitude"])
    mach = float(case_input["freestream"]["mach"])
    path.write_text(
        f"""# vtk DataFile Version 3.0
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
{rho} {rho} {rho} {rho}
SCALARS pressure float 1
LOOKUP_TABLE default
{pressure} {0.995 * pressure} {1.005 * pressure} {pressure}
SCALARS mach float 1
LOOKUP_TABLE default
{mach} {mach} {mach} {mach}
VECTORS velocity float
{velocity} 0 0  {velocity} 0 0  {velocity} 0 0  {velocity} 0 0
""",
        encoding="utf-8",
    )


def _write_bad_cell_field(path: Path, case_input: dict) -> None:
    rho = float(case_input["freestream"]["rho"])
    pressure = float(case_input["freestream"]["pressure"])
    velocity = float(case_input["freestream"]["velocity_magnitude"])
    mach = float(case_input["freestream"]["mach"])
    path.write_text(
        f"""# vtk DataFile Version 3.0
two native cell tuples with one numerical-vacuum state
ASCII
DATASET UNSTRUCTURED_GRID
POINTS 8 float
0 0 0  1 0 0  1 1 0  0 1 0  2 0 0  3 0 0  3 1 0  2 1 0
CELLS 2 10
4 0 1 2 3
4 4 5 6 7
CELL_TYPES 2
9 9
CELL_DATA 2
SCALARS density float 1
LOOKUP_TABLE default
{rho} {0.01 * rho}
SCALARS pressure float 1
LOOKUP_TABLE default
{pressure} {0.01 * pressure}
SCALARS mach float 1
LOOKUP_TABLE default
{mach} {mach}
VECTORS velocity float
{velocity} 0 0  {velocity} 0 0
""",
        encoding="utf-8",
    )


def create_results(root: Path) -> Path:
    results = root / "results"
    for case_id in REQUIRED_CASES:
        case_input = json.loads(
            (ROOT.parent / "cfd_solver_agentic_benchmark" / "inputs" / "cases" /
             f"{case_id}.json").read_text(encoding="utf-8")
        )
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
                "observed_max_inner_iterations": 5,
                "observed_mean_inner_iterations": 5.0,
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
                residual = 1.0e-4
                residual_rows.append([step, time, 5, 1, 0.01, residual, residual,
                                      residual, residual, residual, 2.0 * residual])
                cd = 1.1 + 0.01 * math.cos(2.0 * math.pi * 0.4 * time)
                force_rows.append([step, time, cl, cd, 0.0, cd - viscous, viscous, cl, 0.0])
        else:
            residual_rows = [[step, 0.0, 3, 1.0, 0.0, 1.0, 1.0, 1.0, 1.0,
                              10.0 ** (-0.1 * step), 2.0 * 10.0 ** (-0.1 * step)]
                             for step in range(1, 61)]
            force_rows = []
            for step in range(1, 61):
                cd = 1.1 + 1.0e-4 * math.sin(step)
                force_rows.append([step, 0.0, cl_final, cd, 0.0, cd - viscous,
                                   viscous, cl_final, 0.0])
        _write_csv(case / "residuals.csv", RESIDUAL_HEADER, residual_rows)
        _write_csv(case / "forces.csv", FORCE_HEADER, force_rows)
        cf = 0.0 if inviscid else 0.01
        wall_tag = next(tag for tag, kind in case_input["boundary_conditions"].items()
                        if "wall" in kind)
        _write_csv(case / "surface.csv", SURFACE_HEADER,
                   [[0, -0.1, 0, -1, 1.0, -1.0, cf, 1, 0, 0, 0, wall_tag],
                    [1, 0.1, 0, 1, 1.1, 1.0, cf, 1, 0, 0, 0, wall_tag]])
        _write_csv(case / "partition_diagnostics.csv",
                   ["rank", "num_cells_owned", "num_cells_ghost", "num_boundary_faces",
                    "num_neighbor_ranks", "neighbor_ranks", "send_cells", "recv_cells"],
                   [[0, 1, 0, 4, 0, "", "", ""]])
        _write_field(case / "field_final.vtk", case_input)
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
            rows = [[rank, 1 if rank == 0 else 0, 1 if ranks > 1 else 0, 1,
                     1 if ranks > 1 else 0,
                     str((rank + 1) % ranks) if ranks > 1 else "",
                     "0" if ranks > 1 else "", "0" if ranks > 1 else ""]
                    for rank in range(ranks)]
            _write_csv(target / "partition_diagnostics.csv",
                       ["rank", "num_cells_owned", "num_cells_ghost", "num_boundary_faces",
                        "num_neighbor_ranks", "neighbor_ranks", "send_cells", "recv_cells"],
                       rows)
            directories.append(target)
    return tuple(directories)


class ReportAutomationTests(unittest.TestCase):
    def test_force_split_tolerance_accounts_for_printed_component_cancellation(self) -> None:
        case_input = {"physics": {"mode": "laminar"}}
        row = {
            "cd": -0.0722753,
            "cl": 0.024872,
            "pressure_drag": -7.68148,
            "viscous_drag": 7.60921,
            "pressure_lift": 0.00578125,
            "viscous_lift": 0.0190907,
        }
        _validate_force_evidence("cancellation_fixture", case_input, [row])

        inconsistent = dict(row)
        inconsistent["cd"] = -0.0722
        with self.assertRaisesRegex(BuildError, "inconsistent drag split"):
            _validate_force_evidence("cancellation_fixture", case_input,
                                     [inconsistent])

    def test_zero_aoa_lift_tolerance_has_a_strict_boundary(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            results = create_results(root)
            case_id = "naca0012_m080_inviscid"
            case_dir = results / case_id
            case_input = json.loads(
                (ROOT.parent / "cfd_solver_agentic_benchmark" / "inputs" /
                 "cases" / f"{case_id}.json").read_text(encoding="utf-8")
            )

            def set_final_lift(value: float) -> None:
                path = case_dir / "forces.csv"
                with path.open(newline="", encoding="utf-8") as handle:
                    rows = list(csv.DictReader(handle))
                rows[-1]["cl"] = str(value)
                rows[-1]["pressure_lift"] = str(value)
                _write_csv(path, FORCE_HEADER,
                           [[row[name] for name in FORCE_HEADER] for row in rows])

            set_final_lift(NACA_ZERO_AOA_LIFT_LIMIT)
            passing = _sanity_for_case(
                _load_case_dir(case_dir, case_id, case_input))
            self.assertTrue(passing["symmetric_lift_near_zero"])

            set_final_lift(math.nextafter(NACA_ZERO_AOA_LIFT_LIMIT, math.inf))
            failing = _sanity_for_case(
                _load_case_dir(case_dir, case_id, case_input))
            self.assertFalse(failing["symmetric_lift_near_zero"])

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
            spectrum_entry = next(
                entry for entry in entries
                if entry["case_id"] == "cylinder_m010_laminar_re200"
                and entry["variable"] == "lift_spectrum"
            )
            self.assertIn("FFT was evaluated through the Nyquist frequency",
                          spectrum_entry["caption"])
            sanity = json.loads((report / "sanity_checks.json").read_text(encoding="utf-8"))
            self.assertEqual(len(sanity["cases"]), 8)
            self.assertIn("submitted solver output only", sanity["generated_from"])
            tex = (report / "report.tex").read_text(encoding="utf-8")
            self.assertIn("Final force coefficients", tex)
            self.assertIn("cylinder\\_m010\\_laminar\\_re200", tex)
            self.assertIn("Concise labels are used for readability", tex)
            self.assertIn("$4\\times4$ block Jacobi", tex)
            self.assertIn("\\[\nR_i=\\sum_", tex)
            self.assertIn(
                "\\begin{table}[htbp]\\centering\\scriptsize\n"
                "\\caption{Submitted run status.",
                tex,
            )

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

    def test_uses_native_cell_tuples_for_physics_checks(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "field.vtk"
            case_input = json.loads(
                (ROOT.parent / "cfd_solver_agentic_benchmark" / "inputs" / "cases" /
                 "naca0012_m200_inviscid.json").read_text(encoding="utf-8")
            )
            _write_bad_cell_field(path, case_input)
            density, pressure, u, v, association = _native_primitive_fields(read_field(path))
            self.assertEqual(association, "cell_data")
            self.assertEqual(len(density), 2)
            self.assertAlmostEqual(float(density[1]), 0.01 * case_input["freestream"]["rho"])
            self.assertAlmostEqual(float(pressure[1]), 0.01 * case_input["freestream"]["pressure"])
            self.assertEqual(len(u), len(v))

    def test_refuses_re200_pseudo_cfl_override(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            results = create_results(root)
            rank_results = create_rank_results(root, results)
            target = results / "cylinder_m010_laminar_re200" / "residuals.csv"
            rows = list(csv.reader(target.open(newline="", encoding="utf-8")))
            rows[1][3] = "10"
            with target.open("w", newline="", encoding="utf-8") as handle:
                csv.writer(handle).writerows(rows)
            with self.assertRaisesRegex(BuildError, "pseudo-time CFL"):
                build_report(results, root / "report", rank_results=rank_results)

    def test_refuses_rank_dependent_force_solution(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            results = create_results(root)
            rank_results = create_rank_results(root, results)
            target_dir = next(path for path in rank_results
                              if path.name == "naca0012_m015_inviscid_np8")
            target = target_dir / "forces.csv"
            rows = list(csv.reader(target.open(newline="", encoding="utf-8")))
            rows[-1][3] = str(float(rows[-1][3]) + 0.1)
            rows[-1][5] = str(float(rows[-1][5]) + 0.1)
            with target.open("w", newline="", encoding="utf-8") as handle:
                csv.writer(handle).writerows(rows)
            with self.assertRaisesRegex(BuildError, "rank-validation drag"):
                build_report(results, root / "report", rank_results=rank_results)


if __name__ == "__main__":
    unittest.main()
