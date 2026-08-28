"""Semantic completion gate for a fully evaluated benchmark snapshot."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path

from cfdeval import validation
from cfdeval import report_pdf as report_pdf_tools


EVALUATION_ROOT = Path(__file__).resolve().parents[2]
SCHEMA_DIR = EVALUATION_ROOT / "schemas"
REQUIRED_INDEXED = {
    "summary.json", "metadata.json", "expenses.json", "measurements.json",
    "configs.json", "sessions.json", "env_snapshot.json", "agent_scores.json",
    "review_code.json", "review_cfd.json", "review_results.json",
    "report_pdf.json",
}
AREA_CONFIGS = {
    "code_review": "review_points_code.json",
    "cfd_review": "review_points_cfd.json",
    "result_review": "review_points_results.json",
}
REVIEW_FILES = {
    "code_review": "review_code.json",
    "cfd_review": "review_cfd.json",
    "result_review": "review_results.json",
}


def _load(path: Path, errors: list[str]) -> dict:
    if not path.is_file():
        errors.append(f"missing required file: {path.name}")
        return {}
    try:
        value = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"cannot read {path.name}: {exc}")
        return {}
    if not isinstance(value, dict):
        errors.append(f"{path.name}: expected JSON object")
        return {}
    return value


def _number(value) -> bool:
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value))


def _weighted(points: list[dict]) -> float | None:
    if not points or any(not _number(p.get("score")) for p in points):
        return None
    denom = sum(float(p.get("weight", 1.0)) for p in points)
    if not denom:
        return None
    return round(sum(float(p.get("weight", 1.0)) * float(p["score"])
                     for p in points) / denom, 2)


def completion_errors(folder: Path) -> list[str]:
    """Return every semantic defect that prevents declaring an evaluation done."""
    folder = Path(folder)
    errors: list[str] = []
    scores = _load(folder / "agent_scores.json", errors)
    index = _load(folder / "index.json", errors)
    metadata = _load(folder / "metadata.json", errors)
    report_pdf = _load(folder / "report_pdf.json", errors)
    run_identity = _load(folder / "run_identity.json", errors)

    for name in ("agent_report.md",):
        path = folder / name
        if not path.is_file() or path.stat().st_size == 0:
            errors.append(f"missing or empty required sidecar: {name}")
    final_response = folder / "contestant_final_response.md"
    selection = scores.get("session_selection") or {}
    if not final_response.is_file() or final_response.stat().st_size == 0:
        if selection.get("contestant_final_response_status") != "absent":
            errors.append("missing contestant_final_response.md without explicit absent status")
        if not str(selection.get("contestant_final_response_absence_reason") or "").strip():
            errors.append("missing final-response absence reason")

    artifacts = index.get("artifacts") or {}
    for name in sorted(REQUIRED_INDEXED):
        if name not in artifacts:
            errors.append(f"index.json does not index required artifact: {name}")
            continue
        path = folder / name
        if not path.is_file():
            errors.append(f"indexed artifact is missing: {name}")
            continue
        expected = artifacts[name].get("sha256")
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if expected != actual:
            errors.append(f"index digest mismatch: {name}")
        schema_name = artifacts[name].get("schema")
        schema = SCHEMA_DIR / str(schema_name)
        if not schema.is_file():
            errors.append(f"missing schema for {name}: {schema_name}")
        else:
            valid, schema_errors = validation.validate_file(path, schema)
            if not valid:
                errors.extend(f"{name}: {e}" for e in schema_errors[:10])

    report_status = report_pdf.get("status")
    if not str(report_pdf.get("evaluator") or "").strip():
        errors.append("report_pdf.json evaluator is empty")
    if not str(report_pdf.get("submission_commit") or "").strip():
        errors.append("report_pdf.json submission_commit is empty")
    pdf_path = folder / report_pdf_tools.REPORT_PDF
    pdf_index = artifacts.get(report_pdf_tools.REPORT_PDF)
    if report_status == "accepted":
        approval = report_pdf.get("appropriateness") or {}
        if (approval.get("approved") is not True
                or approval.get("visually_reviewed") is not True
                or approval.get("main_report_confirmed") is not True
                or approval.get("readable") is not True
                or not str(approval.get("notes") or "").strip()):
            errors.append("report_pdf.json: accepted PDF lacks evaluator approval notes")
        if report_pdf.get("submission_commit") != run_identity.get("submission_commit"):
            errors.append(
                "report_pdf.json submission_commit does not match run_identity.json")
        source = report_pdf.get("source")
        snap = report_pdf.get("snapshot")
        if not isinstance(source, dict) or not isinstance(snap, dict):
            errors.append("report_pdf.json accepted record requires source and snapshot objects")
            source, snap = {}, {}
        if source.get("mode") not in {"workspace_existing", "compiled_from_submission"}:
            errors.append("report_pdf.json source mode is invalid")
        if not str(source.get("report_tex") or "").strip():
            errors.append("report_pdf.json source report_tex is empty")
        else:
            try:
                report_pdf_tools.normalized_repo_path(source["report_tex"])
            except ValueError as exc:
                errors.append(f"report_pdf.json source report_tex is invalid: {exc}")
        if source.get("mode") == "workspace_existing":
            expected_pdf = str(Path(str(source.get("report_tex"))).with_suffix(".pdf"))
            if source.get("workspace_relative_pdf") != expected_pdf:
                errors.append(
                    "workspace-existing report PDF is not the sibling of report_tex")
            if source.get("build_command") is not None:
                errors.append("workspace-existing report PDF must not record build_command")
        if source.get("mode") == "compiled_from_submission" and not str(
                source.get("build_command") or "").strip():
            errors.append("compiled report PDF lacks its build command")
        if (source.get("mode") == "compiled_from_submission"
                and source.get("workspace_relative_pdf") is not None):
            errors.append("compiled report PDF must not claim a workspace-relative PDF")
        if snap.get("filename") != report_pdf_tools.REPORT_PDF:
            errors.append("report_pdf.json snapshot filename must be report.pdf")
        if snap.get("media_type") != report_pdf_tools.MEDIA_TYPE:
            errors.append("report_pdf.json snapshot media_type must be application/pdf")
        if snap.get("pdf_header_valid") is not True or snap.get("pdf_eof_valid") is not True:
            errors.append("report_pdf.json snapshot PDF structural flags must be true")
        if not pdf_path.is_file() or pdf_path.is_symlink():
            errors.append("accepted report PDF is missing or not a regular snapshot file")
        elif not isinstance(pdf_index, dict):
            errors.append("index.json does not index accepted artifact: report.pdf")
        else:
            data = pdf_path.read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            if pdf_index.get("sha256") != digest:
                errors.append("index digest mismatch: report.pdf")
            if pdf_index.get("bytes") != len(data):
                errors.append("index byte count mismatch: report.pdf")
            if pdf_index.get("artifact_kind") != "binary":
                errors.append("report.pdf index artifact_kind must be binary")
            if pdf_index.get("schema") is not None:
                errors.append("report.pdf must be indexed as a binary artifact")
            if pdf_index.get("media_type") != report_pdf_tools.MEDIA_TYPE:
                errors.append("report.pdf index media_type must be application/pdf")
            errors.extend(
                f"report.pdf: {error}"
                for error in report_pdf_tools.validate_pdf_bytes(data))
            if snap.get("sha256") != digest or source.get("sha256") != digest:
                errors.append("report_pdf.json digest does not match vendored report.pdf")
            if not re.fullmatch(r"[0-9a-f]{64}", str(snap.get("sha256") or "")):
                errors.append("report_pdf.json snapshot SHA-256 is malformed")
            if snap.get("bytes") != len(data) or source.get("bytes") != len(data):
                errors.append("report_pdf.json byte count does not match vendored report.pdf")
    elif report_status == "absent":
        approval = report_pdf.get("appropriateness") or {}
        if (approval.get("approved") is not False
                or approval.get("visually_reviewed") is not False
                or approval.get("main_report_confirmed") is not False
                or approval.get("readable") is not False
                or not str(approval.get("notes") or "").strip()):
            errors.append("report_pdf.json: absent report lacks an explicit reason")
        if pdf_path.exists() or pdf_path.is_symlink() or pdf_index is not None:
            errors.append("report_pdf.json says absent but report.pdf is present or indexed")
        if report_pdf.get("source") is not None or report_pdf.get("snapshot") is not None:
            errors.append("report_pdf.json absent record must have null source and snapshot")
        if report_pdf.get("submission_commit") != run_identity.get("submission_commit"):
            errors.append(
                "report_pdf.json submission_commit does not match run_identity.json")
    else:
        errors.append("report_pdf.json status must be accepted or absent")

    score_areas = scores.get("scores") or {}
    for area, config_name in AREA_CONFIGS.items():
        area_doc = score_areas.get(area) or {}
        points = area_doc.get("points") or []
        config = json.loads((EVALUATION_ROOT / "config" / config_name).read_text())
        expected_ids = [p["id"] for p in config["points"]]
        actual_ids = [p.get("id") for p in points]
        if actual_ids != expected_ids:
            errors.append(f"{area}: point ids/order do not match {config_name}")
        for point in points:
            pid, score = point.get("id", "?"), point.get("score")
            if not _number(score) or not 0 <= score <= 5:
                errors.append(f"{area}.{pid}: score must be complete and within 0-5")
            if not str(point.get("notes") or "").strip():
                errors.append(f"{area}.{pid}: evidence notes are empty")
        computed = _weighted(points)
        overall = area_doc.get("overall_score")
        if computed is None or not _number(overall):
            errors.append(f"{area}: overall_score is incomplete")
        elif abs(float(overall) - computed) > 0.011:
            errors.append(f"{area}: overall_score {overall} != weighted points {computed}")
        review = _load(folder / REVIEW_FILES[area], errors)
        if review and review.get("overall_score") != overall:
            errors.append(f"{REVIEW_FILES[area]} overall does not match agent_scores")

    rubric = scores.get("rubric") or {}
    sections = rubric.get("sections") or []
    if len(sections) != 10:
        errors.append(f"rubric: expected 10 sections, found {len(sections)}")
    rubric_sum = 0.0
    max_sum = 0.0
    for section in sections:
        sid = section.get("id", "?")
        score, maximum = section.get("score"), section.get("max_points")
        if not _number(score) or not _number(maximum) or not 0 <= score <= maximum:
            errors.append(f"rubric.{sid}: score must be complete and within 0..max_points")
        else:
            rubric_sum += float(score)
            max_sum += float(maximum)
        if not str(section.get("notes") or "").strip():
            errors.append(f"rubric.{sid}: evidence notes are empty")
    total_possible = rubric.get("total_possible")
    total_scored = rubric.get("total_scored")
    if total_possible != 100 or abs(max_sum - 100) > 1e-9:
        errors.append("rubric: total_possible and section maxima must equal 100")
    if not _number(total_scored) or abs(float(total_scored) - rubric_sum) > 0.011:
        errors.append(f"rubric: total_scored {total_scored!r} != section sum {rubric_sum:g}")

    case_scores = scores.get("case_scores") or {}
    case_schema = json.loads((SCHEMA_DIR / "agent_scores.schema.json").read_text())
    expected_cases = case_schema["properties"]["case_scores"]["required"]
    if list(case_scores) != expected_cases:
        errors.append("case_scores: missing, extra, or non-canonical case order")
    for case_id in expected_cases:
        item = case_scores.get(case_id) or {}
        value = item.get("score")
        if not _number(value) or not 0 <= value <= 5:
            errors.append(f"case_scores.{case_id}: score must be complete and within 0-5")
        if not str(item.get("notes") or "").strip():
            errors.append(f"case_scores.{case_id}: evidence/limitation note is empty")

    dq = scores.get("disqualification") or {}
    if not isinstance(dq.get("triggered"), bool):
        errors.append("disqualification.triggered must be explicitly true or false")
    if dq.get("triggered") is True:
        found = [f for f in (dq.get("flags") or []) if f.get("found") is True]
        if not found or any(not str(f.get("evidence") or "").strip() for f in found):
            errors.append("disqualification: triggered verdict requires found flag evidence")

    questions = metadata.get("questions") or []
    for question in questions:
        if not str(question.get("answer") or "").strip():
            errors.append(f"metadata question unanswered: {question.get('id', '?')}")
    if metadata.get("status") == "needs_user_input":
        errors.append("metadata status is still needs_user_input")

    report = folder / "agent_report.md"
    if report.is_file():
        text = report.read_text(encoding="utf-8", errors="replace")
        lower = text.lower()
        if len(text.strip()) < 200:
            errors.append("agent_report.md is too short to be a completed evaluation")
        for marker in ("> **Agent fills this in**", "TODO: complete evaluation"):
            if marker in text:
                errors.append(f"agent_report.md retains scaffold marker: {marker}")
        sidecars = [folder / "run_identity.json"]
        if final_response.is_file() and final_response.stat().st_size:
            sidecars.append(final_response)
        for sidecar in sidecars:
            digest = hashlib.sha256(sidecar.read_bytes()).hexdigest()
            if digest not in text:
                errors.append(
                    f"agent_report.md does not record {sidecar.name} SHA-256 {digest}")
        if sidecars and not ("unindexed" in lower and "cfdeval check" in lower):
            errors.append(
                "agent_report.md must state that unindexed sidecars are not validated by cfdeval check")
    return errors


def check_cli(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Fail unless an evaluation snapshot is semantically complete")
    parser.add_argument("folder")
    args = parser.parse_args(argv)
    errors = completion_errors(Path(args.folder))
    if errors:
        print(f"INCOMPLETE: {args.folder} ({len(errors)} defect(s))")
        for error in errors:
            print(f"  - {error}")
        return 1
    print(f"COMPLETE: {args.folder}")
    return 0


if __name__ == "__main__":
    raise SystemExit(check_cli())
