#!/usr/bin/env python3
"""Publish a contestant workspace as a regulated results/<slug> branch.

Usage:
  python3 evaluation/tools/publish_results.py --workspace <contestant-repo>
      [--branch results/<slug>] [--origin-url URL] [--rename-current]
      [--commit-msg MSG] [--exclude GLOB] [--push-remote NAME]
      [--push-url URL] [--push] [--no-fetch] [--dry-run]

Steps, in order (each action is printed; --dry-run only previews):
  1. Verify <workspace> is a git repository.
  2. Restore the origin remote if missing (or point it at --origin-url).
     Default URL: this manager repository's own origin, i.e. the workspace
     template repo the contestants are cloned from.
  3. Fetch full remote history: if the clone is shallow (common for these
     contestant workspaces), first git fetch --unshallow origin, then
     git fetch --all --tags --prune. Pushing from a shallow clone is
     rejected by remotes, so the full history is required.
  4. Move to a regulated branch named results/<slug> (default slug = the
     workspace directory name normalized to lowercase hyphens; --branch
     overrides). Creates the branch from the current branch by default;
     --rename-current renames the current branch instead (fails if the
     target already exists).
  5. Stage and commit the working tree on that branch (git add -A; skipped
     when nothing changed). Default message: "results: <slug>".
  6. Push -u to the target remote -- only when --push is given. Default
     remote is origin; --push-remote overrides.

The results branch therefore contains everything on the contestant's current
branch plus the committed run state (solver, reports, results, the `done`
marker, AGENTS.md/.codegraph if untracked, ...), so it is self-contained.
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import subprocess
import sys
from pathlib import Path


MANAGER_ROOT = Path(__file__).resolve().parents[2]
FALLBACK_ORIGIN_URL = (
    "https://github.com/harryzhou2000/"
    "cfd_solver_agentic_benchmark_workspace_template.git"
)
BRANCH_RE = re.compile(r"^results/[A-Za-z0-9][A-Za-z0-9._/-]*$")


def _git(cwd: Path, args: list[str], *, dry_run: bool = False,
         check: bool = True) -> subprocess.CompletedProcess | None:
    """Run git in cwd. In dry-run mode only print the command."""
    display = "git " + " ".join(args)
    if dry_run:
        print(f"[dry-run] {display}")
        return None
    print(f"+ {display}")
    r = subprocess.run(["git", "-C", str(cwd), *args], capture_output=True,
                       text=True)
    if check and r.returncode != 0:
        raise SystemExit(f"git {args[0]} failed in {cwd}:\n{r.stderr.strip()}")
    return r


def _git_out(cwd: Path, args: list[str]) -> str | None:
    r = subprocess.run(["git", "-C", str(cwd), *args], capture_output=True,
                       text=True)
    return r.stdout.strip() if r.returncode == 0 else None


def slugify(name: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9./-]+", "-", name).strip("-./").lower()
    if not slug or not BRANCH_RE.match("results/" + slug):
        raise SystemExit(f"cannot derive a valid results branch from {name!r}; "
                         "pass --branch results/<slug> explicitly")
    return slug


def manager_origin_url() -> str | None:
    return _git_out(MANAGER_ROOT, ["config", "--get", "remote.origin.url"])


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--workspace", required=True,
                   help="contestant repository to publish (required)")
    p.add_argument("--branch", help="target branch; must start with results/ "
                                    "(default: results/<normalized workspace name>)")
    p.add_argument("--origin-url", help="URL to (re)store origin as "
                                        "(default: this manager repo's origin)")
    p.add_argument("--rename-current", action="store_true",
                   help="rename the current branch to the target instead of "
                        "creating a new branch from it")
    p.add_argument("--commit-msg", help="commit message "
                                        "(default: 'results: <slug>')")
    p.add_argument("--exclude", action="append", default=[],
                   metavar="GLOB", help="do not commit paths matching GLOB "
                                        "(repeatable)")
    p.add_argument("--push-remote", default="origin",
                   help="remote to push to (default: origin)")
    p.add_argument("--push-url", help="URL to use when adding --push-remote")
    p.add_argument("--push", action="store_true",
                   help="actually run the push (default: prepare locally only)")
    p.add_argument("--no-fetch", action="store_true", help="skip the fetch step")
    p.add_argument("--dry-run", action="store_true",
                   help="print actions without changing anything")
    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    ws = Path(args.workspace).resolve()
    if not (ws / ".git").exists() and _git_out(ws, ["rev-parse", "--git-dir"]) is None:
        raise SystemExit(f"{ws} is not a git repository")

    target = args.branch or ("results/" + slugify(ws.name))
    if not BRANCH_RE.match(target):
        raise SystemExit(f"branch name {target!r} is not regulated; "
                         "must match results/<slug>")
    slug = target.split("/", 1)[1]
    commit_msg = args.commit_msg or f"results: {slug}"

    # 1. origin remote
    origin_url = args.origin_url or manager_origin_url() or FALLBACK_ORIGIN_URL
    have_origin = _git_out(ws, ["remote", "get-url", "origin"]) is not None
    if args.origin_url or not have_origin:
        if have_origin:
            _git(ws, ["remote", "set-url", "origin", origin_url],
                 dry_run=args.dry_run)
        else:
            _git(ws, ["remote", "add", "origin", origin_url],
                 dry_run=args.dry_run)
    else:
        print(f"origin already set: {have_origin}")

    # 2. fetch full remote history (unshallow first if needed)
    if not args.no_fetch:
        if _git_out(ws, ["rev-parse", "--is-shallow-repository"]) == "true":
            print("workspace is a shallow clone; unshallowing")
            _git(ws, ["fetch", "--unshallow", "origin"], dry_run=args.dry_run)
        _git(ws, ["fetch", "--all", "--tags", "--prune"], dry_run=args.dry_run)

    # 3. move to the regulated results branch
    current = _git_out(ws, ["symbolic-ref", "--short", "HEAD"])
    target_exists = (_git_out(ws, ["show-ref", "--verify", "--quiet",
                                   f"refs/heads/{target}"]) is not None)
    if current == target:
        print(f"already on {target}")
    elif target_exists:
        _git(ws, ["switch", target], dry_run=args.dry_run)
    elif args.rename_current:
        _git(ws, ["branch", "-m", target], dry_run=args.dry_run)
    else:
        _git(ws, ["switch", "-c", target], dry_run=args.dry_run)

    # 4. stage and commit the working tree
    _git(ws, ["add", "-A"], dry_run=args.dry_run)
    if args.exclude and not args.dry_run:
        staged = _git_out(ws, ["ls-files", "--cached", "-z"])
        if staged:
            paths = [p for p in staged.split("\0") if p and
                     any(fnmatch.fnmatch(p, g) for g in args.exclude)]
            if paths:
                _git(ws, ["reset", "-q", "--", *paths])
                print(f"excluded {len(paths)} path(s) from the commit")
    elif args.exclude:
        print("[dry-run] excluding paths matching: " + ", ".join(args.exclude))
    if args.dry_run:
        print(f"[dry-run] git commit -m {commit_msg!r}  (skipped if nothing staged)")
    else:
        has_staged = _git_out(ws, ["diff", "--cached", "--quiet"]) is None
        if has_staged:  # diff --cached --quiet exits 1 when changes are staged
            _git(ws, ["commit", "-m", commit_msg])
        else:
            print("nothing to commit; working tree already clean on " + target)

    # 5. push (explicit opt-in)
    remote = args.push_remote
    remote_url = _git_out(ws, ["remote", "get-url", remote])
    if remote_url is None and args.push_url:
        _git(ws, ["remote", "add", remote, args.push_url], dry_run=args.dry_run)
        remote_url = args.push_url
    if args.push:
        if remote_url is None:
            raise SystemExit(f"remote {remote!r} is not configured; pass "
                             "--push-url URL to add it")
        if (_git_out(ws, ["rev-parse", "--is-shallow-repository"]) == "true"
                and not args.dry_run):
            raise SystemExit("workspace is still shallow; remotes reject "
                             "shallow pushes. Run without --no-fetch so the "
                             "helper can git fetch --unshallow first.")
        _git(ws, ["push", "-u", remote, target], dry_run=args.dry_run)
    else:
        print(f"push skipped (pass --push); would run: "
              f"git push -u {remote} {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
