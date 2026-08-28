import json
import unittest
from pathlib import Path

from cfdeval import query
from cfdeval.expenses import cost_for, estimate_by_model, model_cost


ROOT = Path(__file__).resolve().parent
PRICE_FILE = ROOT / "config" / "cost_metadata.json"


class CostMetadataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.meta = json.loads(PRICE_FILE.read_text())

    def test_audited_provider_rates(self):
        expected = {
            "gpt-5.5": (5.0, 0.5, 30.0),
            "blsc/glm-5.2": (1.14, 0.29, 4.0),
            "blsc/minimax-m3": (0.6, 0.12, 2.4),
            "blsc/kimi-k3": (3.0, 0.3, 15.0),
            "kimi-code/k3": (3.0, 0.3, 15.0),
            "blsc/deepseek-v4-flash": (0.14, 0.03, 0.29),
            "blsc/deepseek-v4-pro": (1.71, 0.14, 3.43),
            "deepseek/deepseek-v4-flash": (0.22, 0.007, 0.66),
            "deepseek/deepseek-v4-pro": (0.66, 0.022, 1.98),
            "minimax/minimax-m3": (0.3, 0.06, 1.2),
            "claude-opus-5": (5.0, 0.5, 25.0),
            "claude-opus-4-6": (5.0, 0.5, 25.0),
            "claude-sonnet-5": (2.0, 0.2, 10.0),
            "claude-sonnet-4-6": (3.0, 0.3, 15.0),
            "vllm/qwen3.8-27b-int8-w8a16-mtp": (0.4, 0.04, 3.0),
            "xiaomi-mimo/mimo-v2.5": (0.14, 0.0028, 0.28),
            "xiaomi-mimo/mimo-v2.5-pro": (0.435, 0.0036, 0.87),
            "blsc/deepseek-v4-pro-0813": (0.43, 0.0036, 0.86),
            "internal_eccn/nvidia-moonshotai-eccn-kimi-k3": (1.45, 0.19, 7.25),
        }
        for key, rates in expected.items():
            with self.subTest(model=key):
                entry = self.meta["models"][key]
                self.assertEqual(
                    (entry["input_per_mtok"], entry["cached_input_per_mtok"],
                     entry["output_per_mtok"]), rates)

    def test_provider_case_and_routed_gpt55_aliases_resolve(self):
        blsc, defaults = model_cost(self.meta, "BLSC/DeepSeek-V4-Flash")
        self.assertFalse(defaults)
        self.assertEqual(blsc["input_per_mtok"], 0.14)

        routed, defaults = model_cost(
            self.meta, "internal_openai/openai/openai/gpt-5.5")
        self.assertFalse(defaults)
        self.assertEqual(routed["cached_input_per_mtok"], 0.5)

        claude, defaults = model_cost(
            self.meta,
            "internal_anth_eccn/us-aws-anthropic-eccn-claude-opus-5")
        self.assertFalse(defaults)
        self.assertEqual(claude["cache_write_per_mtok"], 6.25)

    def test_deepseek_cache_hit_miss_write_and_output_math(self):
        price, _ = model_cost(self.meta, "deepseek/deepseek-v4-flash")
        # 1M total input = 700k ordinary miss + 200k cache hit + 100k write.
        # Writes are cache misses for DeepSeek and therefore use $0.22/M.
        cost = cost_for(
            price, input_t=1_000_000, cached_t=200_000,
            cache_write_t=100_000, output_t=100_000)
        self.assertAlmostEqual(cost, 0.2434)

    def test_unresolved_models_do_not_fall_back_to_seed_defaults(self):
        price, used_defaults = model_cost(self.meta, "codex-auto-review")
        self.assertIsNone(price)
        self.assertFalse(used_defaults)
        estimate = estimate_by_model(self.meta, {
            "codex-auto-review": {"input": 100, "output": 20, "total": 120},
        })
        self.assertIsNone(estimate["total"])
        self.assertEqual(estimate["unpriced_tokens"], 120)
        self.assertEqual(
            estimate["by_model"]["codex-auto-review"]["pricing"], "unresolved")

    def test_dashboard_ignores_stale_snapshot_cost(self):
        expenses = {
            "tokens": {"by_model": {"gpt-5.5": {
                "input": 1_000_000, "cached_input": 900_000,
                "output": 100_000, "total": 1_100_000,
            }}},
            "cost_estimate_usd": {"total": 999.0},
        }
        estimate = query.current_cost_estimate(expenses)
        self.assertEqual(estimate["total"], 3.95)
        self.assertTrue(estimate["dashboard_current"])
        self.assertEqual(len(estimate["metadata_sha256"]), 64)


if __name__ == "__main__":
    unittest.main()
