"""Focused regression tests for evidence-grounded report prose."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "generate_report.py"
SPEC = importlib.util.spec_from_file_location("generate_report", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
generate_report = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = generate_report
SPEC.loader.exec_module(generate_report)


def case_mach(case_id: str) -> float:
    if "m015" in case_id:
        return 0.15
    if "m080" in case_id:
        return 0.8
    if "m200" in case_id and case_id.startswith("naca"):
        return 2.0
    return 0.1


def synthetic_rank_evidence(case_id: str) -> list[dict[str, object]]:
    return [
        {
            "ranks": 1,
            "residuals": [{"residual_l2": "1"}, {"residual_l2": "0.01"}],
            "force": {"cd": 0.21, "cl": 0.002},
            "status": {"wall_time_seconds": 12.0, "convergence_status": "converged"},
            "partitions": [{
                "rank": "0", "num_cells_owned": "100", "num_cells_ghost": "0",
                "num_neighbor_ranks": "0", "send_cells": "0", "recv_cells": "0",
            }],
        },
        {
            "ranks": 8,
            "residuals": [{"residual_l2": "1"}, {"residual_l2": "0.001"}],
            "force": {"cd": 0.20, "cl": 0.001},
            "status": {"wall_time_seconds": 8.0, "convergence_status": "converged"},
            "partitions": [
                {
                    "rank": "0", "num_cells_owned": "49", "num_cells_ghost": "5",
                    "num_neighbor_ranks": "2", "send_cells": "5", "recv_cells": "5",
                },
                {
                    "rank": "1", "num_cells_owned": "51", "num_cells_ghost": "5",
                    "num_neighbor_ranks": "2", "send_cells": "5", "recv_cells": "5",
                },
            ],
        },
    ]


class ReportTextTests(unittest.TestCase):
    def fixture(self, incomplete_case: str | None = None) -> dict[str, object]:
        configs: dict[str, object] = {}
        statistics: dict[str, object] = {}
        metadata: dict[str, object] = {}
        statuses: dict[str, object] = {}
        partitions: dict[str, object] = {}
        forces: dict[str, object] = {}
        sanity_cases: list[dict[str, object]] = []
        for index, case_id in enumerate(generate_report.REQUIRED_CASES):
            transient = case_id == generate_report.TRANSIENT_CASE
            laminar = "laminar" in case_id
            physics: dict[str, object] = {"mode": "laminar" if laminar else "inviscid"}
            if laminar:
                physics["reynolds"] = 200 if transient else (20 if "re20" in case_id else 5000)
            configs[case_id] = {
                "physics": physics,
                "freestream": {"mach": case_mach(case_id)},
                "run_control": ({
                    "type": "transient", "time_step": 0.01, "final_time": 300.0,
                    "min_inner_iterations": 5, "max_inner_iterations": 1000,
                    "inner_residual_reduction_target": 1.0e-3, "cfl_initial": 1.0,
                    "rusanov_dissipation_scale": 1.0,
                } if transient else {
                    "type": "steady", "max_steps": 20_000, "cfl_initial": 1.0,
                    "cfl_max": 50.0, "pseudo_cfl_ramp_steps": 1000,
                    "min_inner_iterations": 3, "max_inner_iterations": 50,
                    "inner_residual_reduction_target": 0.01,
                }),
                "boundary_conditions": {"farfield": "farfield", "wall": (
                    "no_slip_adiabatic_wall" if laminar else "slip_wall")},
            }
            expected_status = "statistically_periodic" if transient else "converged"
            status = "failed" if case_id == incomplete_case else expected_status
            statuses[case_id] = {
                "convergence_status": status, "final_step": 30_000 if transient else 1000,
                "final_physical_time": 300.0 if transient else 0.0,
                "wall_time_seconds": 10.0 + index,
            }
            statistics[case_id] = {
                "case_id": case_id, "convergence_status": status, "mpi_ranks": 8,
                "final_step": 30_000 if transient else 1000,
                "final_physical_time": 300.0 if transient else 0.0,
                "wall_time_seconds": 10.0 + index,
                "residual_reduction_orders_measured": 4.0,
                "final_cd": 0.1 + 0.01 * index, "final_cl": 1.0e-4 * index,
                "mean_cd_tail": 1.4 if transient else 0.2,
                "mean_cl_tail": 0.0, "lift_amplitude_tail": 0.4,
                "surface_cp_range": 1.2, "field_mach_relative_span": 0.3,
                "field_pressure_relative_span": 0.4,
                "dominant_frequency": 0.2, "strouhal_number": 0.2,
                "frequency_estimation_method": generate_report.FREQUENCY_ESTIMATION_METHOD,
                "frequency_estimation_valid": True, "frequency_estimation_reason": "accepted",
                "frequency_peak_cycles": 10.0, "frequency_peak_power_fraction": 0.8,
                "frequency_peak_prominence": 20.0, "tail_physical_time_span": 50.0,
                "strouhal_reference_length": 1.0, "strouhal_freestream_velocity": 1.0,
                "strouhal_reference_source": "input_case_config",
            }
            metadata[case_id] = {
                "completed": status != "failed", "convergence_status": status,
                "mpi_ranks": 8, "mesh_file": "mesh.cgns", "num_cells_global": 100,
                "num_faces_global": 200, "partitioner": "metis_kway",
                "halo_exchange": "neighbor_isend_irecv",
                "full_mesh_replication_during_iterations": False,
                "full_state_replication_during_iterations": False,
                "partition_edge_cut": 10, "reconstruction": "weighted_least_squares",
                "limiter": "barth_jespersen_active", "positivity_preservation": "scaling",
                "spatial_order_claimed": 2, "inviscid_flux": "rusanov",
                "entropy_fix": "not_applicable", "viscous_flux": "corrected_gradient",
                "time_integrator": "bdf2_dual_time" if transient else "implicit_local_pseudo_time",
                "implicit_solver": "matrix_free_gmres", "typical_inner_iterations": 5,
                "observed_min_inner_iterations": 5, "observed_max_inner_iterations": 10,
                "observed_mean_inner_iterations": 6.0,
                "inner_residual_reduction_target": 1.0e-3,
                "inner_target_misses": 2 if transient else 0,
                "inner_target_converged_fraction": 0.99,
                "last_inner_residual_ratio": 8.0e-4,
                "true_bdf2_inner_loop": transient,
            }
            partitions[case_id] = [{
                "rank": "0", "num_cells_owned": "100", "num_cells_ghost": "0",
                "num_neighbor_ranks": "0", "send_cells": "0", "recv_cells": "0",
            }]
            forces[case_id] = {
                "basis": "tail mean" if transient else "final row", "cd": 0.1,
                "cl": 0.0, "cmz": 0.0, "pressure_drag": 0.06,
                "viscous_drag": 0.04 if laminar else 0.0,
                "pressure_lift": 0.0, "viscous_lift": 0.0,
            }
            sanity_cases.append({
                "case_id": case_id, "passed": status != "failed",
                "checks": {
                    "near_symmetric_lift": True, "no_slip_wall_velocity": True,
                    "skin_friction_finite": True, "nonzero_skin_friction_evidence": True,
                    "positive_tail_mean_drag": True,
                },
                "metrics": {"max_wall_speed": 0.0,
                            "max_abs_skin_friction_coefficient": 0.02},
            })
        return {
            "configs": configs, "statistics": statistics, "metadata_by_case": metadata,
            "status_by_case": statuses, "partitions": partitions,
            "force_summaries": forces,
            "sanity": {"all_passed": incomplete_case is None, "cases": sanity_cases},
            "rank_comparisons": {
                "naca0012_m015_inviscid": synthetic_rank_evidence(
                    "naca0012_m015_inviscid"),
                "cylinder_m010_laminar_re20": synthetic_rank_evidence(
                    "cylinder_m010_laminar_re20"),
            },
        }

    def render(self, incomplete_case: str | None = None) -> str:
        fixture = self.fixture(incomplete_case)
        return generate_report.report_text(
            Path("."), Path("report"), Path("results"),
            fixture["configs"], fixture["statistics"], [], [],
            fixture["metadata_by_case"], fixture["status_by_case"],
            fixture["partitions"], fixture["force_summaries"], fixture["sanity"],
            fixture["rank_comparisons"], [],
        )

    def test_abstract_claims_all_cases_only_when_every_status_qualifies(self) -> None:
        complete = self.render()
        self.assertIn("All eight required primary cases completed successfully", complete)

        incomplete = self.render("naca0012_m080_inviscid")
        self.assertIn("not every required case completed successfully", incomplete)
        self.assertNotIn("All eight required primary cases completed successfully", incomplete)

    def test_abstract_rejects_disagreeing_metadata_completion(self) -> None:
        fixture = self.fixture()
        fixture["metadata_by_case"]["naca0012_m015_inviscid"]["completed"] = False
        report = generate_report.report_text(
            Path("."), Path("report"), Path("results"),
            fixture["configs"], fixture["statistics"], [], [],
            fixture["metadata_by_case"], fixture["status_by_case"],
            fixture["partitions"], fixture["force_summaries"], fixture["sanity"],
            fixture["rank_comparisons"], [],
        )
        self.assertIn("Only 7 of 8 required primary cases", report)
        self.assertNotIn("All eight required primary cases completed successfully", report)

    def test_required_method_reproducibility_and_limitation_detail_is_rendered(self) -> None:
        report = self.render()
        for expected in (
            r"p_{ref}=\rho_\infty U_\infty^2",
            r"TRI\_3/QUAD\_4",
            "non-orthogonal correction",
            r"\Delta\tau_i=\mathrm{CFL}",
            "200-sample window",
            "compact LocalMesh payload",
            r"report/run\_manifest.csv",
            "Rusanov/local Lax--Friedrichs is robust but diffusive",
            "general equation of state",
        ):
            self.assertIn(expected, report)

    def test_parallel_section_quantifies_residual_and_partition_differences(self) -> None:
        report = self.render()
        self.assertIn(r"|\Delta\log_{10}(R_0/R_f)|_8", report)
        self.assertIn("summed receive-cell count divided by summed owned cells", report)
        self.assertIn("no MPI profiler isolates communication overhead", report)
        self.assertNotIn("measured communication overhead is", report)

    def test_dense_tables_use_compact_labels_and_partition_extrema(self) -> None:
        report = self.render()
        self.assertIn("NACA .15 inv.", report)
        self.assertIn("Cylinder Re200", report)
        self.assertIn("owned min--max", report)
        self.assertIn("complete per-rank rows remain", report)
        self.assertNotIn(r"\begin{tabular}{p{0.23\linewidth}r r r r r r}", report)

    def test_caption_math_allowlist_preserves_math_and_escapes_other_text(self) -> None:
        rendered = generate_report.caption_tex(
            r"Near-body $|\mathbf{u}|/U_\infty$; 50% & safe")
        self.assertIn(r"$|\mathbf{u}|/U_\infty$", rendered)
        self.assertIn(r"50\% \& safe", rendered)


if __name__ == "__main__":
    unittest.main()
