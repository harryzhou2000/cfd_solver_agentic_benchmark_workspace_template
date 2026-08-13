"""Focused unit tests for post-processing history-window selection."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest

import numpy as np


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "postprocess.py"
SPEC = importlib.util.spec_from_file_location("postprocess", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
postprocess = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = postprocess
SPEC.loader.exec_module(postprocess)


class ForceTailSliceTests(unittest.TestCase):
    def test_steady_cases_use_final_200_samples(self) -> None:
        tail = postprocess.force_tail_slice("naca0012_m015_inviscid", 1_000)
        self.assertEqual((tail.start, tail.stop), (800, None))

    def test_short_steady_history_uses_all_samples(self) -> None:
        tail = postprocess.force_tail_slice("cylinder_m010_laminar_re20", 199)
        self.assertEqual((tail.start, tail.stop), (0, None))

    def test_re200_retains_final_40_percent_window(self) -> None:
        tail = postprocess.force_tail_slice("cylinder_m010_laminar_re200", 1_000)
        self.assertEqual((tail.start, tail.stop), (600, None))

    def test_steady_caption_declares_full_and_terminal_views(self) -> None:
        caption = postprocess.force_history_caption("naca0012_m200_laminar_re5000", 1_000)
        self.assertIn("Full", caption)
        self.assertIn("terminal 200-sample", caption)

    def test_re200_caption_remains_physical_time_history(self) -> None:
        caption = postprocess.force_history_caption("cylinder_m010_laminar_re200", 1_000)
        self.assertEqual(caption, "Physical-time lift and drag coefficient history.")


class DominantFrequencyTests(unittest.TestCase):
    def test_sinusoid_with_linear_drift_is_recovered_after_detrending(self) -> None:
        dt = 0.05
        times = np.arange(0.0, 120.0 + 0.5 * dt, dt)
        expected_frequency = 0.125
        lift = 0.32 * np.sin(2.0 * np.pi * expected_frequency * times + 0.4)
        lift += 0.018 * times + 0.7

        estimate = postprocess.dominant_frequency_estimate(
            times, lift, reference_length=2.0, freestream_velocity=4.0
        )

        self.assertTrue(estimate["frequency_estimation_valid"])
        self.assertAlmostEqual(float(estimate["dominant_frequency"]), expected_frequency,
                               delta=2.0e-3)
        self.assertAlmostEqual(float(estimate["strouhal_number"]),
                               0.5 * expected_frequency, delta=1.0e-3)
        self.assertGreaterEqual(float(estimate["frequency_peak_cycles"]),
                                postprocess.FREQUENCY_MIN_CYCLES)
        self.assertGreater(float(estimate["frequency_peak_power_fraction"]),
                           postprocess.FREQUENCY_MIN_PEAK_POWER_FRACTION)

    def test_pure_linear_drift_is_rejected(self) -> None:
        times = np.linspace(0.0, 100.0, 2_001)
        lift = 0.3 + 0.02 * times

        estimate = postprocess.dominant_frequency_estimate(times, lift)

        self.assertFalse(estimate["frequency_estimation_valid"])
        self.assertEqual(estimate["frequency_estimation_reason"],
                         "linear_trend_or_negligible_fluctuation")
        self.assertNotIn("dominant_frequency", estimate)

    def test_dominant_signal_with_fewer_than_required_cycles_is_rejected(self) -> None:
        dt = 0.05
        times = np.arange(0.0, 20.0 + 0.5 * dt, dt)
        lift = 0.25 * np.sin(2.0 * np.pi * 0.1 * times) + 0.01 * times

        estimate = postprocess.dominant_frequency_estimate(times, lift)

        self.assertFalse(estimate["frequency_estimation_valid"])
        self.assertEqual(estimate["frequency_estimation_reason"],
                         "dominant_signal_has_insufficient_cycles")

    def test_strouhal_scales_are_loaded_from_input_case_config(self) -> None:
        length, velocity, source = postprocess.strouhal_reference_scales(
            "cylinder_m010_laminar_re200", {}
        )
        self.assertEqual((length, velocity, source), (1.0, 1.0, "input_case_config"))


if __name__ == "__main__":
    unittest.main()
