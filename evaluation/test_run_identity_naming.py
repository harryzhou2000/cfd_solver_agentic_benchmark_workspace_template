import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


check_branch = load_module(
    "check_upstream_branch",
    ROOT / ".codex/skills/cfd-benchmark-evaluation/scripts/check_upstream_branch.py",
)
derive_id = load_module(
    "derive_run_id",
    ROOT / ".codex/skills/cfd-benchmark-evaluation/scripts/derive_run_id.py",
)
env_snapshot = load_module(
    "env_snapshot",
    ROOT / "evaluation/tools/env_snapshot.py",
)


class RunLabelTests(unittest.TestCase):
    def test_non_numeric_labels_are_preserved_in_branch_and_run_id_base(self):
        for label in ("03", "trial-a", "r2_rc.1"):
            with self.subTest(label=label):
                self.assertEqual(check_branch.validate_run_label(label), label)
                self.assertEqual(
                    derive_id.parse_initial_branch("codex/generic/init", label),
                    (f"codex/generic/{label}", f"codex_generic_{label}"),
                )

    def test_unsafe_or_non_component_labels_are_rejected(self):
        for label in ("", "trial/a", ".hidden", "a..b", "trailing.", "x.lock", "has space"):
            with self.subTest(label=label), self.assertRaises(SystemExit):
                check_branch.validate_run_label(label)

    def test_collision_helper_derives_from_snapshotted_initial_branch(self):
        with tempfile.TemporaryDirectory() as raw:
            snapshot = Path(raw) / "env_snapshot.json"
            snapshot.write_text(json.dumps({
                "workspace": {"branch": "codex/generic/init", "commit": "a" * 40}
            }))
            self.assertEqual(
                check_branch.result_branch(snapshot, "trial-a"),
                ("codex/generic/init", "a" * 40, "codex/generic/trial-a"),
            )

    def test_collision_helper_accepts_label_and_reports_initial_upstream_match(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            repo = root / "workspace"
            upstream = root / "upstream.git"
            repo.mkdir()
            subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
            subprocess.run(["git", "config", "user.email", "test@example.invalid"],
                           cwd=repo, check=True)
            subprocess.run(["git", "config", "user.name", "Test"],
                           cwd=repo, check=True)
            (repo / "tracked.txt").write_text("initial\n")
            subprocess.run(["git", "add", "tracked.txt"], cwd=repo, check=True)
            subprocess.run(["git", "commit", "-q", "-m", "initial"], cwd=repo, check=True)
            subprocess.run(["git", "branch", "-M", "codex/generic/init"],
                           cwd=repo, check=True)
            commit = subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=repo, text=True,
            ).strip()
            (repo / ".eval").mkdir()
            (repo / ".eval/env_snapshot.json").write_text(json.dumps({
                "workspace": {"branch": "codex/generic/init", "commit": commit}
            }))
            subprocess.run(["git", "clone", "-q", "--bare", str(repo), str(upstream)],
                           check=True)

            result = subprocess.run([
                "python3", str(ROOT / ".codex/skills/cfd-benchmark-evaluation/scripts/check_upstream_branch.py"),
                "--workspace", str(repo), "--number", "trial-a",
                "--upstream", str(upstream),
            ], check=True, capture_output=True, text=True)
            payload = json.loads(result.stdout)

            self.assertEqual(payload["result_branch"], "codex/generic/trial-a")
            self.assertEqual(payload["operator_number"], "trial-a")
            self.assertTrue(payload["initial_upstream_matches_snapshot"])
            self.assertTrue(payload["available"])


class InitialBranchDetectionTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.repo = Path(self.tempdir.name)
        self.git("init", "-q")
        self.git("config", "user.email", "test@example.invalid")
        self.git("config", "user.name", "Test")
        (self.repo / "tracked.txt").write_text("initial\n")
        self.git("add", "tracked.txt")
        self.git("commit", "-q", "-m", "initial")
        self.git("branch", "-M", "codex/generic/init")
        self.commit = self.git("rev-parse", "HEAD").stdout.strip()

    def tearDown(self):
        self.tempdir.cleanup()

    def git(self, *args):
        return subprocess.run(
            ["git", "-C", str(self.repo), *args],
            check=True, capture_output=True, text=True,
        )

    def test_prefers_symbolic_local_init_branch(self):
        detected = env_snapshot.detect_initial_branch(self.repo)
        self.assertEqual(detected["canonical_branch"], "codex/generic/init")
        self.assertEqual(detected["commit"], self.commit)
        self.assertEqual(detected["selected_ref"], "refs/heads/codex/generic/init")
        self.assertEqual(detected["source"], "local")

    def test_detects_remote_tracking_init_branch_at_detached_head(self):
        self.git("update-ref", "refs/remotes/origin/codex/generic/init", self.commit)
        self.git("checkout", "-q", "--detach", self.commit)
        self.git("branch", "-D", "codex/generic/init")

        detected = env_snapshot.detect_initial_branch(self.repo)

        self.assertEqual(detected["canonical_branch"], "codex/generic/init")
        self.assertEqual(detected["source"], "remote_tracking")
        self.assertEqual(
            detected["selected_ref"], "refs/remotes/origin/codex/generic/init"
        )

    def test_requires_disambiguation_for_multiple_names_at_detached_head(self):
        self.git("update-ref", "refs/remotes/origin/codex/generic/init", self.commit)
        self.git("update-ref", "refs/remotes/origin/codex/other/init", self.commit)
        self.git("checkout", "-q", "--detach", self.commit)
        self.git("branch", "-D", "codex/generic/init")

        with self.assertRaises(SystemExit):
            env_snapshot.detect_initial_branch(self.repo)
        detected = env_snapshot.detect_initial_branch(
            self.repo, "refs/remotes/origin/codex/generic/init"
        )
        self.assertEqual(detected["canonical_branch"], "codex/generic/init")
        detected_short = env_snapshot.detect_initial_branch(
            self.repo, "origin/codex/generic/init"
        )
        self.assertEqual(detected_short["canonical_branch"], "codex/generic/init")


if __name__ == "__main__":
    unittest.main()
