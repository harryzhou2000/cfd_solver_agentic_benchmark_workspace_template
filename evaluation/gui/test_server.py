import json
import io
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import server
from cfdeval import codex_data, query, validation
from cfdeval.sessions import CodexThreadEvents


class ReportPdfDiscoveryTests(unittest.TestCase):
    def test_prefers_report_pdf_and_skips_build_and_venv(self):
        with tempfile.TemporaryDirectory() as raw:
            workspace = Path(raw)
            (workspace / "solver" / "report").mkdir(parents=True)
            (workspace / "solver" / "report" / "report.pdf").write_bytes(b"%PDF-report")
            (workspace / "build").mkdir()
            (workspace / "build" / "report.pdf").write_bytes(b"%PDF-build")
            (workspace / ".venv").mkdir()
            (workspace / ".venv" / "report.pdf").write_bytes(b"%PDF-venv")
            found = server.find_report_pdf(workspace)
            self.assertEqual(found["relative_path"], "solver/report/report.pdf")

    def test_workspace_identity_cannot_escape_workspace_root(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "workspace"
            valid = root / "codex" / "model" / "01"
            valid.mkdir(parents=True)
            with patch.object(server, "WORKSPACE_ROOT", root):
                self.assertEqual(
                    server.workspace_for_snapshot({
                        "workspace": str(valid),
                        "initial_branch": "codex/model/init",
                        "operator_number": "01",
                    }),
                    valid.resolve(),
                )
                self.assertIsNone(server.workspace_for_snapshot({
                    "workspace": str(Path(raw).parent),
                    "initial_branch": "../../escape/init",
                    "operator_number": "01",
                }))


class SnapshotProtocolTests(unittest.TestCase):
    def test_stale_needs_input_status_is_complete_when_no_questions_remain(self):
        self.assertEqual(query.effective_metadata_status({
            "status": "needs_user_input", "questions": []}), "complete")
        self.assertEqual(query.effective_metadata_status({
            "status": "needs_user_input", "questions": [{"answer": None}]}),
            "needs_user_input")

    def test_dashboard_reprices_snapshot_tokens_with_current_metadata(self):
        with tempfile.TemporaryDirectory() as raw:
            price_file = Path(raw) / "prices.json"
            price_file.write_text(json.dumps({
                "defaults": {"input_per_mtok": 1, "cached_input_per_mtok": 0.25,
                             "output_per_mtok": 4, "input_share": 0.75},
                "models": {"model-a": {"input_per_mtok": 2,
                                         "cached_input_per_mtok": 0.5,
                                         "output_per_mtok": 8}},
            }))
            expenses = {"tokens": {"by_model": {"model-a": {
                "input": 1_000_000, "cached_input": 800_000,
                "output": 100_000, "total": 1_100_000,
            }}}}
            with patch.object(query, "COST_METADATA", price_file):
                estimate = query.current_cost_estimate(expenses)
            self.assertEqual(estimate["total"], 1.6)
            self.assertTrue(estimate["dashboard_current"])
            self.assertEqual(len(estimate["metadata_sha256"]), 64)

    def test_dashboard_reprices_selected_opencode_tree_when_persisted_cost_is_zero(self):
        with tempfile.TemporaryDirectory() as raw:
            price_file = Path(raw) / "prices.json"
            price_file.write_text(json.dumps({
                "defaults": {"input_per_mtok": 1, "cached_input_per_mtok": 0.25,
                             "output_per_mtok": 4, "input_share": 0.75},
                "models": {"kimi-for-coding/k3": {"input_per_mtok": 3,
                                                       "cached_input_per_mtok": 0.3,
                                                       "output_per_mtok": 15}},
            }))
            metadata = {"harness": {"harness": "opencode"}, "opencode": {"sessions": [
                {"session_id": "root", "parent_id": None, "model": "k3",
                 "provider": "kimi-for-coding", "variant": None,
                 "tokens_input": 2_108_555, "tokens_cache_read": 132_839_894,
                 "tokens_cache_write": 0, "tokens_output": 339_703,
                 "tokens_reasoning": 0, "cost": 0.0},
                {"session_id": "excluded", "parent_id": None, "model": "k3",
                 "provider": "kimi-for-coding", "variant": None,
                 "tokens_input": 9_000_000, "tokens_output": 9_000_000,
                 "tokens_reasoning": 0, "cost": 0.0},
            ]}}
            scores = {"session_selection": {"roots": ["root"]}}
            with patch.object(query, "COST_METADATA", price_file):
                estimate = query.current_snapshot_cost({}, metadata, scores)
            self.assertEqual(estimate["total"], 51.2732)
            self.assertEqual(estimate["unpriced_tokens"], 0)
            self.assertEqual(estimate["source"],
                             "selected OpenCode session-tree token decomposition")

    def test_environment_phase_respects_explicit_capture_not_opt_symlink(self):
        self.assertEqual(query.environment_capture_phase({
            "capture_phase": "post_run",
            "workspace": {"external_symlink": "/opt/external"},
        }), "post_run")
        self.assertEqual(query.environment_capture_phase({
            "version": "1.0",
            "workspace": {"external_symlink": "/opt/external"},
        }), "pre_run")
        self.assertEqual(query.environment_capture_phase({
            "provenance": {"pre_run_authority": False},
            "workspace": {"external_symlink": "/opt/external"},
        }), "post_run")
        self.assertIsNone(query.environment_capture_phase({
            "version": "1.1",
            "workspace": {"external_symlink": "/opt/external"},
        }))

    def test_codex_model_decomposition_keys_model_plus_single_observed_effort(self):
        with tempfile.TemporaryDirectory() as raw:
            price_file = Path(raw) / "prices.json"
            price_file.write_text(json.dumps({
                "defaults": {"input_per_mtok": 1, "cached_input_per_mtok": 0.25,
                             "output_per_mtok": 4, "input_share": 0.75},
                "models": {"model-a": {"input_per_mtok": 2,
                                         "cached_input_per_mtok": 0.5,
                                         "output_per_mtok": 8}},
            }))
            expenses = {"tokens": {"by_thread": {
                "t1": {"tokens": {"model-a": {"input": 1_000_000,
                    "cached": 800_000, "output": 100_000,
                    "reasoning_output": 40_000, "total": 1_100_000}}},
                "t2": {"tokens": {"model-a": {"input": 500_000,
                    "cached": 0, "output": 50_000,
                    "reasoning_output": 10_000, "total": 550_000}}},
            }}}
            metadata = {"harness": {"harness": "codex"}, "threads": {
                "t1": {"entry_reasoning_effort": "ultra", "reasoning_effort": ["ultra"]},
                "t2": {"entry_reasoning_effort": "medium", "reasoning_effort": ["medium"]},
            }}
            with patch.object(query, "COST_METADATA", price_file):
                result = query.current_model_decomposition(expenses, metadata)
            self.assertEqual([r["key"] for r in result["rows"]],
                             ["model-a + medium", "model-a + ultra"])
            self.assertEqual(result["total_tokens"], 1_650_000)
            ultra = next(r for r in result["rows"] if r["reasoning"] == "ultra")
            self.assertEqual(ultra["current_cost_usd"], 1.6)
            self.assertIsNone(ultra["persisted_cost_usd"])

    def test_codex_model_decomposition_does_not_assign_mixed_thread_to_entry_effort(self):
        expenses = {"tokens": {"by_thread": {"t": {"tokens": {"model-a": {
            "input": 10, "output": 2, "reasoning_output": 1, "total": 12,
        }}}}}}
        metadata = {"harness": {"harness": "codex"}, "threads": {"t": {
            "entry_reasoning_effort": "ultra", "reasoning_effort": ["ultra", "high"],
        }}}
        result = query.current_model_decomposition(expenses, metadata)
        self.assertEqual(result["rows"][0]["key"], "model-a + mixed(high,ultra)")
        self.assertIn("split unavailable", result["rows"][0]["attribution_basis"])

    def test_legacy_total_only_model_decomposition_uses_blended_price(self):
        with tempfile.TemporaryDirectory() as raw:
            price_file = Path(raw) / "prices.json"
            price_file.write_text(json.dumps({
                "defaults": {"input_per_mtok": 1, "cached_input_per_mtok": 0.25,
                             "output_per_mtok": 4, "input_share": 0.75},
                "models": {"model-a": {"input_per_mtok": 2,
                                         "cached_input_per_mtok": 0.5,
                                         "output_per_mtok": 8}},
            }))
            expenses = {"tokens": {"by_thread": {"t": {
                "source": "threads_fallback",
                "tokens": {"model-a": {"input": 0, "cached": 0, "output": 0,
                                          "reasoning_output": 0, "total": 1_000_000}},
            }}}}
            metadata = {"harness": {"harness": "codex"}, "threads": {"t": {
                "reasoning_effort": ["medium"],
            }}}
            with patch.object(query, "COST_METADATA", price_file):
                result = query.current_model_decomposition(expenses, metadata)
            row = result["rows"][0]
            self.assertEqual(row["total"], 1_000_000)
            self.assertEqual(row["current_cost_usd"], 3.5)
            self.assertIsNone(row["input"])
            self.assertIsNone(row["cached_input"])
            self.assertFalse(row["token_split_available"])
            self.assertIn("total-only token fallback", row["attribution_basis"])

    def test_opencode_model_decomposition_filters_selected_tree(self):
        with tempfile.TemporaryDirectory() as raw:
            price_file = Path(raw) / "prices.json"
            price_file.write_text(json.dumps({
                "defaults": {"input_per_mtok": 1, "cached_input_per_mtok": 0.25,
                             "output_per_mtok": 4, "input_share": 0.75},
                "models": {"deepseek/model-a": {"input_per_mtok": 2,
                                                  "cached_input_per_mtok": 0.5,
                                                  "output_per_mtok": 8}},
            }))
            sessions = [
                {"session_id": "root", "parent_id": None, "model": "model-a",
                 "provider": "deepseek", "variant": "max", "tokens_input": 100,
                 "tokens_output": 20, "tokens_reasoning": 5, "cost": 1.0},
                {"session_id": "child", "parent_id": "root", "model": "model-a",
                 "provider": "deepseek", "variant": "max", "tokens_input": 50,
                 "tokens_output": 10, "tokens_reasoning": 2, "cost": 0.5},
                {"session_id": "other", "parent_id": None, "model": "model-a",
                 "provider": "deepseek", "variant": "max", "tokens_input": 999,
                 "tokens_output": 999, "tokens_reasoning": 999, "cost": 99},
            ]
            metadata = {"harness": {"harness": "opencode"},
                        "opencode": {"sessions": sessions}}
            scores = {"session_selection": {"roots": ["root"]}}
            with patch.object(query, "COST_METADATA", price_file):
                result = query.current_model_decomposition({}, metadata, scores)
            self.assertEqual(len(result["rows"]), 1)
            row = result["rows"][0]
            self.assertEqual(row["key"], "model-a + max")
            self.assertEqual((row["input"], row["output"], row["reasoning_output"], row["total"]),
                             (150, 30, 7, 187))
            self.assertEqual(row["persisted_cost_usd"], 1.5)

    def test_json_responses_disable_browser_cache(self):
        handler = object.__new__(server.Handler)
        handler.wfile = io.BytesIO()
        headers = []
        handler.send_response = lambda status: None
        handler.send_header = lambda name, value: headers.append((name, value))
        handler.end_headers = lambda: None

        handler._json({"status": "complete"})

        self.assertIn(("Cache-Control", "no-store"), headers)

    def test_discovery_only_includes_canonical_hashed_run_ids(self):
        with tempfile.TemporaryDirectory() as raw:
            outputs = Path(raw)
            names = (
                "codex_gpt56_01_cb349f",
                "codex_dsv4_flash_01_0c1996",
                "codex_gpt56_01",
                "codex_gpt56_01_CB349F",
                "codex_gpt56_01_cb349",
                "codex_gpt56_01_cb349ff",
            )
            for name in names:
                folder = outputs / name
                folder.mkdir()
                (folder / "index.json").write_text("{}")

            discovered = [folder.name for folder in query.result_folders(outputs)]

            self.assertEqual(discovered, [
                "codex_dsv4_flash_01_0c1996",
                "codex_gpt56_01_cb349f",
            ])

    def test_discovery_still_requires_index_manifest(self):
        with tempfile.TemporaryDirectory() as raw:
            outputs = Path(raw)
            (outputs / "codex_gpt56_01_cb349f").mkdir()

            self.assertEqual(query.result_folders(outputs), [])

    def test_detail_exposes_identity_final_response_and_pdf_metadata(self):
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            workspace_root = base / "workspace"
            workspace = workspace_root / "codex" / "model" / "01"
            (workspace / "report").mkdir(parents=True)
            (workspace / "report" / "report.pdf").write_bytes(b"%PDF-test")
            folder = base / "outputs" / "codex_model_01_deadbe"
            folder.mkdir(parents=True)
            (folder / "summary.json").write_text(json.dumps({"contestant": {}}))
            (folder / "metadata.json").write_text(json.dumps({"models": {}}))
            (folder / "index.json").write_text("{}")
            identity = {
                "run_id": folder.name,
                "workspace": str(workspace),
                "initial_branch": "codex/model/init",
                "operator_number": "01",
            }
            (folder / "run_identity.json").write_text(json.dumps(identity))
            (folder / "contestant_final_response.md").write_text("final prose\n")
            with patch.object(server, "WORKSPACE_ROOT", workspace_root):
                detail = server.snapshot_detail(folder)
            self.assertEqual(detail["run_identity"]["run_id"], folder.name)
            self.assertEqual(detail["metadata"], {"models": {}})
            self.assertEqual(detail["model_decomposition"]["rows"], [])
            self.assertEqual(detail["markdown"]["contestant_final_response.md"], "final prose\n")
            self.assertEqual(detail["report_pdf"]["relative_path"], "report/report.pdf")

    def test_table_uses_agent_scores_and_legacy_post_run_protocol(self):
        with tempfile.TemporaryDirectory() as raw:
            folder = Path(raw)
            summary = {
                "snapshot": {"env_snapshot_captured": True},
                "code_review": {"overall_score": 1.0},
                "cfd_review": {"overall_score": 1.0},
                "result_review": {"overall_score": 1.0},
            }
            (folder / "agent_scores.json").write_text(json.dumps({
                "session_selection": {"execution_date": "2026-07-31"},
                "scores": {
                    "code_review": {"overall_score": 3.2},
                    "cfd_review": {"overall_score": 3.3},
                    "result_review": {"overall_score": 1.4},
                },
                "rubric": {"total_scored": 58},
                "case_scores": {
                    "cylinder_m010_laminar_re20": {"score": 4.5, "notes": "credible"},
                    "naca0012_m200_inviscid": {"score": 2, "notes": "weak shock"},
                },
                "disqualification": {"triggered": True},
            }))
            (folder / "run_identity.json").write_text(json.dumps({"run_id": "codex_model_01_deadbe"}))
            (folder / "env_snapshot.json").write_text(json.dumps({
                "provenance": {"capture_kind": "post-run reconstruction", "pre_run_authority": False},
            }))
            row = server.query.row_for(folder, summary)
            self.assertEqual(row["run_id"], "codex_model_01_deadbe")
            self.assertEqual((row["code_score"], row["cfd_score"], row["result_score"]),
                             (3.2, 3.3, 1.4))
            self.assertEqual(row["execution_date"], "2026-07-31")
            self.assertTrue(row["disqualified"])
            self.assertEqual(row["env_capture_phase"], "post_run")
            self.assertEqual(row["case_cyl_re20"], 4.5)
            self.assertEqual(row["case_m200_inv"], 2)
            self.assertIsNone(row["case_cyl_re200"])

    def test_case_scores_schema_enforces_zero_to_five(self):
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "agent_scores.json"
            cases = {
                case_id: {"score": 3, "notes": "case evidence"}
                for case_id in query.CASE_SCORE_COLUMNS.values()
            }
            doc = {
                "schema": "agent_scores", "contestant": "test",
                "evaluated_at": "2026-08-14T00:00:00Z", "scores": {},
                "rubric": {"total_possible": 100, "total_scored": None,
                           "sections": []},
                "case_scores": cases,
            }
            path.write_text(json.dumps(doc))
            schema = Path(server.__file__).resolve().parents[1] / "schemas" / "agent_scores.schema.json"
            valid, errors = validation.validate_file(path, schema)
            self.assertTrue(valid, errors)
            legacy = dict(doc)
            legacy.pop("case_scores")
            path.write_text(json.dumps(legacy))
            valid, errors = validation.validate_file(path, schema)
            self.assertTrue(valid, errors)
            path.write_text(json.dumps(doc))
            removed = cases.pop("cylinder_m010_laminar_re200")
            path.write_text(json.dumps(doc))
            valid, _errors = validation.validate_file(path, schema)
            self.assertFalse(valid)
            cases["cylinder_m010_laminar_re200"] = removed
            cases["unexpected"] = {"score": 3, "notes": "not a case"}
            path.write_text(json.dumps(doc))
            valid, _errors = validation.validate_file(path, schema)
            self.assertFalse(valid)
            cases.pop("unexpected")
            cases["cylinder_m010_laminar_re20"]["score"] = 5.1
            path.write_text(json.dumps(doc))
            valid, _errors = validation.validate_file(path, schema)
            self.assertFalse(valid)

    def test_agent_scaffold_contains_all_case_scores_in_task_order(self):
        with tempfile.TemporaryDirectory() as raw:
            out = Path(raw) / "snapshot"
            out.mkdir()
            tool = Path(server.__file__).resolve().parents[1] / "tools" / "generate_agent_report.py"
            subprocess.run([sys.executable, str(tool), "--out", str(out)], check=True,
                           capture_output=True, text=True)
            scores = json.loads((out / "agent_scores.json").read_text())
            self.assertEqual(list(scores["case_scores"]), list(query.CASE_SCORE_COLUMNS.values()))
            self.assertTrue(all(item == {"score": None, "notes": None}
                                for item in scores["case_scores"].values()))

    def test_incomplete_recording_preserves_case_scores_but_fails_gate(self):
        with tempfile.TemporaryDirectory() as raw:
            folder = Path(raw) / "snapshot"
            folder.mkdir()
            cases = {
                case_id: {"score": 4, "notes": "case evidence"}
                for case_id in query.CASE_SCORE_COLUMNS.values()
            }
            scores = {
                "schema": "agent_scores", "contestant": "test",
                "evaluated_at": "2026-08-14T00:00:00Z",
                "scores": {
                    "code_review": {"points": [], "overall_score": 1},
                    "cfd_review": {"points": [], "overall_score": 2},
                    "result_review": {"points": [], "overall_score": 3},
                },
                "rubric": {"total_possible": 100, "total_scored": None,
                           "sections": [{"id": "all", "title": "All",
                                         "max_points": 100, "score": 77,
                                         "notes": "rubric evidence"}]},
                "case_scores": cases,
            }
            (folder / "agent_scores.json").write_text(json.dumps(scores))
            tool = Path(server.__file__).resolve().parents[1] / "tools" / "record_agent_results.py"
            result = subprocess.run([sys.executable, str(tool), "--folder", str(folder)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 3)
            self.assertIn("completion gate failed", result.stderr)
            recorded = json.loads((folder / "agent_scores.json").read_text())
            self.assertEqual(recorded["case_scores"], cases)
            self.assertEqual(recorded["rubric"]["total_scored"], 77)
            self.assertEqual([
                recorded["scores"][area]["overall_score"]
                for area in ("code_review", "cfd_review", "result_review")
            ], [None, None, None])

    def test_opencode_primary_model_and_persisted_tree_cost(self):
        with tempfile.TemporaryDirectory() as raw:
            folder = Path(raw)
            summary = {"expenses": {"cost_estimate_usd": {"total": 0.0}}}
            root = "root-session"
            (folder / "metadata.json").write_text(json.dumps({
                "harness": {"harness": "opencode"},
                "opencode": {"sessions": [
                    {"session_id": root, "parent_id": None,
                     "model": "deepseek-v4-pro", "variant": "max", "cost": 5.9},
                    {"session_id": "child", "parent_id": root,
                     "model": "deepseek-v4-flash", "variant": "max", "cost": 0.8},
                    {"session_id": "unrelated", "parent_id": None,
                     "model": "other", "variant": "low", "cost": 99},
                ]},
            }))
            (folder / "agent_scores.json").write_text(json.dumps({
                "session_selection": {"roots": [root]},
            }))
            row = query.row_for(folder, summary)
            self.assertEqual(row["primary_model_effort"], "deepseek-v4-pro max")
            self.assertEqual(row["cost_usd"], 6.7)

    def test_hidden_attribute_beats_component_display_rules(self):
        css = (Path(__file__).parent / "static" / "styles.css").read_text()
        self.assertIn("[hidden] { display: none !important; }", css)

    def test_list_view_is_visible_before_and_during_initial_routing(self):
        static = Path(server.__file__).resolve().parent / "static"
        html = (static / "index.html").read_text()
        app = (static / "app.js").read_text()
        self.assertIn('<section id="view-list" class="view view-list">', html)
        self.assertIn('const initial = parseHash();', app)
        self.assertIn('route();\n    if (initial.view === "list") loadSnapshots();', app)
        self.assertIn('{ key: "disqualified",   label: "DQ",           type: "dq"', app)
        self.assertEqual(re.findall(r'\{ key: "(case_[^"]+)"', app), [
            "case_m015_inv", "case_m080_inv", "case_m200_inv",
            "case_m015_re5k", "case_m080_re5k", "case_m200_re5k",
            "case_cyl_re20", "case_cyl_re200",
        ])
        self.assertIn('case "case-score":  return caseScoreCell(v);', app)
        self.assertIn('caseTag: "0012"', app)
        self.assertIn('caseTag: "Cyl"', app)
        self.assertIn('class="case-head"', app)
        self.assertIn('class="case-score case-score-${bucket}"', app)
        self.assertIn('data-tab="questions"', html)
        self.assertIn('data-tab="models"', html)
        self.assertIn('function renderQuestionsTab(detail)', app)
        self.assertIn('function renderModelsTab(detail)', app)
        css = (static / "styles.css").read_text()
        self.assertIn('table.snapshots th.case-col', css)
        self.assertIn('width: 68px; min-width: 68px; max-width: 68px;', css)
        for score in range(6):
            self.assertIn(f'.case-score-{score}', css)
        self.assertIn('pill pill-status-blocked">yes</span>', app)
        self.assertIn('/static/app.js?v=', html)
        self.assertIn('/static/styles.css?v=', html)
        self.assertNotIn('pill-status-needs_user_input">needs user input</span>', html)
        self.assertIn('Status badges in the table apply only to their own run row.', html)


class RolloutUsageTests(unittest.TestCase):
    def test_rollout_usage_and_context_window_are_persisted(self):
        with tempfile.TemporaryDirectory() as raw:
            rollout = Path(raw) / "rollout.jsonl"
            rows = [
                {"timestamp": "2026-01-01T00:00:00Z", "type": "event_msg", "payload": {
                    "type": "token_count", "info": {
                        "total_token_usage": {"input_tokens": 100, "cached_input_tokens": 80,
                            "output_tokens": 10, "reasoning_output_tokens": 4, "total_tokens": 110},
                        "last_token_usage": {"input_tokens": 100},
                        "model_context_window": 258400,
                    }}},
                {"timestamp": "2026-01-01T00:01:00Z", "type": "event_msg", "payload": {
                    "type": "token_count", "info": {
                        "total_token_usage": {"input_tokens": 160, "cached_input_tokens": 128,
                            "output_tokens": 20, "reasoning_output_tokens": 6, "total_tokens": 180},
                        "last_token_usage": {"input_tokens": 60},
                        "model_context_window": 258400,
                    }}},
            ]
            rollout.write_text("\n".join(json.dumps(row) for row in rows) + "\n")
            facts = codex_data.rollout_usage_facts(str(rollout))
            self.assertEqual(facts["input_tokens"], 160)
            self.assertEqual(facts["cached_input_tokens"], 128)
            self.assertEqual(facts["non_cached_input_tokens"], 32)
            self.assertEqual(facts["output_tokens"], 20)
            self.assertEqual(facts["model_context_window"], 258400)
            self.assertEqual(facts["max_prompt_input_tokens"], 100)
            stream = CodexThreadEvents("root", str(rollout))
            stream.load()
            self.assertEqual(stream.cumulative_usage["total_tokens"], 180)
            self.assertEqual(stream.model_context_window, 258400)

    def test_entry_settings_use_first_applied_thread_settings(self):
        with tempfile.TemporaryDirectory() as raw:
            rollout = Path(raw) / "rollout.jsonl"
            rows = [
                {"type": "event_msg", "payload": {"type": "thread_settings_applied",
                    "thread_settings": {"model": "gpt-5.6-sol", "reasoning_effort": "ultra"}}},
                {"type": "event_msg", "payload": {"type": "thread_settings_applied",
                    "thread_settings": {"model": "gpt-5.6-terra", "reasoning_effort": "medium"}}},
            ]
            rollout.write_text("\n".join(json.dumps(row) for row in rows) + "\n")
            self.assertEqual(codex_data.rollout_entry_settings(str(rollout)), {
                "model": "gpt-5.6-sol", "reasoning_effort": "ultra"})


if __name__ == "__main__":
    unittest.main()
