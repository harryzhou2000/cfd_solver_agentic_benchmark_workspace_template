#!/usr/bin/env python3
"""Deterministic, dependency-free postprocessing for canonical CFD results.

The script reads solver outputs from ``results/`` and writes only below
``report/``.  Figures are deterministic SVG vectors with matching vector-PDF
compile companions.  Field figures draw the original VTU cells as filled
polygons; no point scatter or interpolated visualization grid is used.
"""

from __future__ import annotations

import argparse
import cmath
import csv
import hashlib
import html
import json
import math
import statistics
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, Sequence


SCRIPT = Path(__file__).resolve()
SOLVER = SCRIPT.parents[1]
DEFAULT_RESULTS = SOLVER / "results"
DEFAULT_REPORT = SOLVER / "report"
RE200_ID = "cylinder_m010_laminar_re200"
FIGURE_COLUMNS = [
    "figure_file", "case_id", "figure_type", "variable", "source_file", "caption"
]
CASE_FILES = [
    "metadata.json", "run_status.json", "residuals.csv", "forces.csv",
    "surface.csv", "field_final.vtu", "partition_diagnostics.csv",
]
CANONICAL_VALUE_FILES = ["results_summary.json", "results_summary.csv", "sanity_checks.json"]
EXPECTED_CASES = {
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    RE200_ID,
}

# Color-blind-friendly scholarly palette.
INK = "#1b2838"
BLUE = "#0072b2"
ORANGE = "#d55e00"
GREEN = "#009e73"
PURPLE = "#7b3294"
GRID = "#d7dde5"
BG = "#ffffff"


def pdf_number(value: float) -> str:
    """Return a compact, deterministic PDF coordinate."""
    rendered = f"{value:.4f}".rstrip("0").rstrip(".")
    return "0" if rendered in ("", "-0") else rendered


def pdf_color(value: str) -> str:
    """Convert a #RRGGBB color to deterministic PDF color operands."""
    if not value.startswith("#") or len(value) != 7:
        raise ValueError(f"PDF export requires #RRGGBB colors, got {value!r}")
    return " ".join(pdf_number(int(value[index:index + 2], 16) / 255.0)
                    for index in (1, 3, 5))


def pdf_text(value: str) -> str:
    """Map plot labels to PDF's built-in WinAnsi-safe text repertoire."""
    value = value.replace("θ", "theta").replace("ω", "omega").replace("–", "-")
    value = value.encode("ascii", "replace").decode("ascii")
    return value.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


def pdf_text_width(value: str, size: int) -> float:
    """Estimate Helvetica label width for SVG-style text anchoring."""
    widths = {" ": 0.278, "i": 0.222, "l": 0.222, "I": 0.278,
              "m": 0.833, "w": 0.722, "M": 0.833, "W": 0.944}
    return size * sum(widths.get(character, 0.556) for character in value)


def write_vector_pdf(path: Path, width: int, height: int, commands: Sequence[str]) -> None:
    """Write one dependency-free, deterministic, single-page vector PDF."""
    stream = ("\n".join(commands) + "\n").encode("ascii")
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        (f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {width} {height}] "
         "/Resources << /Font << /F1 5 0 R /F2 6 0 R >> >> /Contents 4 0 R >>").encode("ascii"),
        b"<< /Length " + str(len(stream)).encode("ascii") + b" >>\nstream\n" + stream + b"endstream",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>",
    ]
    document = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = [0]
    for number, body in enumerate(objects, 1):
        offsets.append(len(document))
        document.extend(f"{number} 0 obj\n".encode("ascii"))
        document.extend(body)
        document.extend(b"\nendobj\n")
    xref = len(document)
    document.extend(f"xref\n0 {len(objects) + 1}\n".encode("ascii"))
    document.extend(b"0000000000 65535 f \n")
    for offset in offsets[1:]:
        document.extend(f"{offset:010d} 00000 n \n".encode("ascii"))
    document.extend(
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode("ascii")
    )
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(document)
    temporary.replace(path)


def read_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def floats(rows: Sequence[dict[str, str]], key: str) -> list[float]:
    return [float(row[key]) for row in rows]


def mean(values: Sequence[float]) -> float:
    return math.fsum(values) / len(values)


def rms_about(values: Sequence[float], center: float | None = None) -> float:
    center = mean(values) if center is None else center
    return math.sqrt(math.fsum((value - center) ** 2 for value in values) / len(values))


def quantile(values: Sequence[float], probability: float) -> float:
    ordered = sorted(values)
    if not ordered:
        raise ValueError("quantile of empty sequence")
    position = (len(ordered) - 1) * probability
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_result_hashes(results: Path) -> dict[str, str]:
    """Validate and return all 80 official canonical result hashes."""
    canonical_manifest = results / "canonicalization_manifest.json"
    if not canonical_manifest.is_file():
        raise RuntimeError(f"missing official canonical manifest: {canonical_manifest}")
    document = read_json(canonical_manifest)
    cases = document.get("cases", {})
    if set(cases) != EXPECTED_CASES:
        raise RuntimeError("official canonical manifest case set differs from expected cases")
    hashes: dict[str, str] = {}
    for case_id in sorted(cases):
        for filename, record in sorted(cases[case_id].get("files", {}).items()):
            path = results / case_id / filename
            expected = record.get("canonical_sha256")
            if not path.is_file() or not expected:
                raise RuntimeError(f"incomplete canonical hash record: {path}")
            actual = sha256(path)
            if actual != expected:
                raise RuntimeError(f"canonical result hash differs from official mapping: {path}")
            hashes[str(path.relative_to(results))] = actual
    if len(hashes) != 80:
        raise RuntimeError(f"official canonical hash count is {len(hashes)}, expected 80")
    return hashes


def atomic_text(path: Path, text: str) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    temporary.replace(path)


def write_json(path: Path, value: object) -> None:
    atomic_text(path, json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n")


def write_csv(path: Path, fieldnames: Sequence[str], rows: Iterable[dict]) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    temporary.replace(path)


def source_name(path: Path) -> str:
    return str(path.resolve())


class Svg:
    """Small deterministic SVG writer used because no plotting package is installed."""

    def __init__(self, width: int = 1600, height: int = 1000) -> None:
        self.width = width
        self.height = height
        self.pdf_items: list[str] = []
        self.items = [
            '<?xml version="1.0" encoding="UTF-8"?>',
            (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
             f'viewBox="0 0 {width} {height}" data-raster-equivalent-dpi="300">'),
            '<rect width="100%" height="100%" fill="#ffffff"/>',
        ]

    def line(self, x1: float, y1: float, x2: float, y2: float, color: str = INK,
             width: float = 2.0, dash: str | None = None) -> None:
        extra = f' stroke-dasharray="{dash}"' if dash else ""
        self.items.append(
            f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" '
            f'stroke="{color}" stroke-width="{width:.2f}"{extra}/>'
        )
        dash_command = f"[{dash.replace(',', ' ')}] 0 d" if dash else "[] 0 d"
        self.pdf_items.append(
            f"q {pdf_color(color)} RG {pdf_number(width)} w {dash_command} "
            f"{pdf_number(x1)} {pdf_number(self.height - y1)} m "
            f"{pdf_number(x2)} {pdf_number(self.height - y2)} l S Q"
        )

    def rect(self, x: float, y: float, width: float, height: float, fill: str,
             stroke: str = "none", stroke_width: float = 0.0) -> None:
        self.items.append(
            f'<rect x="{x:.2f}" y="{y:.2f}" width="{width:.2f}" height="{height:.2f}" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width:.2f}"/>'
        )
        paint = "f"
        operands = ["q"]
        if fill != "none":
            operands.append(f"{pdf_color(fill)} rg")
        if stroke != "none":
            operands.extend((f"{pdf_color(stroke)} RG", f"{pdf_number(stroke_width)} w"))
            paint = "B" if fill != "none" else "S"
        operands.append(
            f"{pdf_number(x)} {pdf_number(self.height - y - height)} "
            f"{pdf_number(width)} {pdf_number(height)} re {paint} Q"
        )
        self.pdf_items.append(" ".join(operands))

    def text(self, x: float, y: float, value: str, size: int = 24, anchor: str = "middle",
             color: str = INK, weight: str = "normal", rotate: float | None = None) -> None:
        transform = f' transform="rotate({rotate:.1f} {x:.2f} {y:.2f})"' if rotate else ""
        self.items.append(
            f'<text x="{x:.2f}" y="{y:.2f}" text-anchor="{anchor}" fill="{color}" '
            f'font-family="DejaVu Sans,Arial,sans-serif" font-size="{size}" '
            f'font-weight="{weight}"{transform}>{html.escape(value)}</text>'
        )
        safe_value = pdf_text(value)
        anchor_offset = 0.0
        if anchor == "middle":
            anchor_offset = -0.5 * pdf_text_width(safe_value, size)
        elif anchor == "end":
            anchor_offset = -pdf_text_width(safe_value, size)
        elif anchor != "start":
            raise ValueError(f"unsupported text anchor for PDF export: {anchor}")
        angle = math.radians(-rotate if rotate is not None else 0.0)
        cosine, sine = math.cos(angle), math.sin(angle)
        font = "F2" if weight == "bold" else "F1"
        self.pdf_items.append(
            f"BT /{font} {size} Tf {pdf_color(color)} rg "
            f"{pdf_number(cosine)} {pdf_number(sine)} {pdf_number(-sine)} {pdf_number(cosine)} "
            f"{pdf_number(x)} {pdf_number(self.height - y)} Tm "
            f"{pdf_number(anchor_offset)} 0 Td ({safe_value}) Tj ET"
        )

    def path(self, points: Sequence[tuple[float, float]], color: str, width: float = 3.0,
             fill: str = "none") -> None:
        if not points:
            return
        commands = " ".join(
            ("M" if index == 0 else "L") + f"{x:.2f},{y:.2f}"
            for index, (x, y) in enumerate(points)
        )
        self.items.append(
            f'<path d="{commands}" fill="{fill}" stroke="{color}" '
            f'stroke-width="{width:.2f}" stroke-linejoin="round" stroke-linecap="round"/>'
        )
        pdf_points = [f"{pdf_number(x)} {pdf_number(self.height - y)}" for x, y in points]
        commands = [f"{pdf_points[0]} m"] + [f"{point} l" for point in pdf_points[1:]]
        paint = "S" if fill == "none" else "B"
        fill_command = "" if fill == "none" else f" {pdf_color(fill)} rg"
        self.pdf_items.append(
            f"q {pdf_color(color)} RG{fill_command} {pdf_number(width)} w "
            + " ".join(commands) + f" {paint} Q"
        )

    def polygon(self, points: Sequence[tuple[float, float]], fill: str,
                stroke: str = "none", stroke_width: float = 0.0) -> None:
        coords = " ".join(f"{x:.2f},{y:.2f}" for x, y in points)
        self.items.append(
            f'<polygon points="{coords}" fill="{fill}" stroke="{stroke}" '
            f'stroke-width="{stroke_width:.2f}"/>'
        )
        pdf_points = [f"{pdf_number(x)} {pdf_number(self.height - y)}" for x, y in points]
        commands = [f"{pdf_points[0]} m"] + [f"{point} l" for point in pdf_points[1:]] + ["h"]
        operands = ["q", f"{pdf_color(fill)} rg"]
        paint = "f"
        if stroke != "none":
            operands.extend((f"{pdf_color(stroke)} RG", f"{pdf_number(stroke_width)} w"))
            paint = "B"
        self.pdf_items.append(" ".join(operands + commands + [paint, "Q"]))

    def finish(self, path: Path) -> None:
        self.items.append("</svg>")
        atomic_text(path, "\n".join(self.items) + "\n")
        write_vector_pdf(path.with_suffix(".pdf"), self.width, self.height, self.pdf_items)


def nice_ticks(low: float, high: float, count: int = 6) -> list[float]:
    if high <= low:
        return [low]
    raw = (high - low) / max(1, count - 1)
    exponent = 10.0 ** math.floor(math.log10(raw))
    fraction = raw / exponent
    step = (1 if fraction <= 1 else 2 if fraction <= 2 else 2.5 if fraction <= 2.5 else 5 if fraction <= 5 else 10) * exponent
    start = math.ceil(low / step) * step
    ticks = []
    value = start
    while value <= high + step * 1e-9:
        ticks.append(value)
        value += step
    return ticks


def fmt_tick(value: float) -> str:
    if value == 0:
        return "0"
    if abs(value) >= 1000 or abs(value) < 0.001:
        return f"{value:.1e}"
    return f"{value:.4g}"


def axes(svg: Svg, box: tuple[float, float, float, float], xlim: tuple[float, float],
         ylim: tuple[float, float], xlabel: str, ylabel: str, log_y: bool = False,
         title: str | None = None) -> tuple:
    left, top, width, height = box
    right, bottom = left + width, top + height
    if log_y:
        y0, y1 = math.log10(ylim[0]), math.log10(ylim[1])
        yticks = [10.0 ** exponent for exponent in range(math.floor(y0), math.ceil(y1) + 1)]
    else:
        y0, y1 = ylim
        yticks = nice_ticks(*ylim)
    x0, x1 = xlim
    xticks = nice_ticks(*xlim)

    def sx(value: float) -> float:
        return left + width * (value - x0) / (x1 - x0)

    def sy(value: float) -> float:
        transformed = math.log10(max(value, ylim[0])) if log_y else value
        return bottom - height * (transformed - y0) / (y1 - y0)

    for tick in xticks:
        pixel = sx(tick)
        svg.line(pixel, top, pixel, bottom, GRID, 1)
        svg.text(pixel, bottom + 31, fmt_tick(tick), 18)
    for tick in yticks:
        if ylim[0] <= tick <= ylim[1]:
            pixel = sy(tick)
            svg.line(left, pixel, right, pixel, GRID, 1)
            svg.text(left - 14, pixel + 6, fmt_tick(tick), 18, "end")
    svg.line(left, bottom, right, bottom, INK, 2)
    svg.line(left, top, left, bottom, INK, 2)
    svg.text(left + width / 2, bottom + 68, xlabel, 23)
    svg.text(left - 92, top + height / 2, ylabel, 23, rotate=-90)
    if title:
        svg.text(left + width / 2, top - 24, title, 25, weight="bold")
    return sx, sy


def padded_range(values: Sequence[float], fraction: float = 0.08) -> tuple[float, float]:
    low, high = min(values), max(values)
    if math.isclose(low, high):
        delta = max(1e-9, abs(low) * 0.1, 0.1)
    else:
        delta = (high - low) * fraction
    return low - delta, high + delta


def line_figure(path: Path, case_id: str, x: Sequence[float], series: Sequence[tuple[str, Sequence[float], str]],
                xlabel: str, ylabel: str, title: str, log_y: bool = False,
                horizontal: Sequence[tuple[str, float, str]] = ()) -> None:
    svg = Svg()
    svg.text(800, 55, title, 31, weight="bold")
    yvalues = [value for _, values, _ in series for value in values if value > 0 or not log_y]
    yvalues.extend(value for _, value, _ in horizontal if value > 0 or not log_y)
    if log_y:
        positive = [value for value in yvalues if value > 0]
        ylim = (10 ** math.floor(math.log10(min(positive))), 10 ** math.ceil(math.log10(max(positive))))
    else:
        ylim = padded_range(yvalues)
    xlim = (min(x), max(x))
    sx, sy = axes(svg, (175, 115, 1310, 730), xlim, ylim, xlabel, ylabel, log_y)
    for _, value, color in horizontal:
        svg.line(175, sy(value), 1485, sy(value), color, 2, "10,8")
    for _, values, color in series:
        points = [(sx(xv), sy(yv)) for xv, yv in zip(x, values) if not log_y or yv > 0]
        svg.path(points, color, 3)
    legend_x = 205
    for label, _, color in list(series) + list(horizontal):
        svg.line(legend_x, 900, legend_x + 48, 900, color, 4)
        svg.text(legend_x + 58, 907, label, 19, "start")
        legend_x += max(190, len(label) * 13 + 90)
    svg.text(1480, 970, f"Case: {case_id}", 18, "end", color="#506070")
    svg.finish(path)


def re200_force_figure(path: Path, case_id: str, rows: Sequence[dict[str, str]]) -> None:
    time = floats(rows, "physical_time")
    cd = floats(rows, "cd")
    cl = floats(rows, "cl")
    svg = Svg(1700, 1100)
    svg.text(850, 54, "Cylinder Re=200 force history", 32, weight="bold")
    sx1, sy1 = axes(svg, (165, 115, 1450, 330), (time[0], time[-1]),
                    padded_range(cd + cl), "Physical time", "Coefficient", False,
                    "Full-history overview")
    svg.path([(sx1(t), sy1(v)) for t, v in zip(time, cd)], BLUE, 2.2)
    svg.path([(sx1(t), sy1(v)) for t, v in zip(time, cl)], ORANGE, 2.2)
    selected = [(t, d, l) for t, d, l in zip(time, cd, cl) if t >= 200.0 - 1e-9]
    tx = [row[0] for row in selected]
    dy = [row[1] for row in selected]
    ly = [row[2] for row in selected]
    sx2, sy2 = axes(svg, (165, 575, 1450, 330), (200.0, 300.0),
                    padded_range(dy + ly), "Physical time", "Coefficient", False,
                    "Post-transient window")
    svg.path([(sx2(t), sy2(v)) for t, v in zip(tx, dy)], BLUE, 2.5)
    svg.path([(sx2(t), sy2(v)) for t, v in zip(tx, ly)], ORANGE, 2.5)
    svg.line(230, 1010, 285, 1010, BLUE, 4)
    svg.text(300, 1017, "C_D", 22, "start")
    svg.line(410, 1010, 465, 1010, ORANGE, 4)
    svg.text(480, 1017, "C_L", 22, "start")
    svg.text(1610, 1040, f"Case: {case_id}", 18, "end", color="#506070")
    svg.finish(path)


def surface_plot_data(case_id: str, surface: Sequence[dict[str, str]], path: Path) -> list[dict]:
    output = []
    if case_id.startswith("naca"):
        for row in surface:
            y = float(row["y"])
            branch = "upper" if y >= 0.0 else "lower"
            output.append({
                "coordinate": float(row["x"]), "coordinate_name": "x_over_c",
                "surface_branch": branch, "x": float(row["x"]), "y": y,
                "cp": float(row["cp"]), "cf": float(row["cf"]),
            })
        output.sort(key=lambda row: (row["surface_branch"], row["coordinate"]))
    else:
        for row in surface:
            x, y = float(row["x"]), float(row["y"])
            theta = math.degrees(math.atan2(y, x)) % 360.0
            output.append({
                "coordinate": theta, "coordinate_name": "theta_degrees_from_downstream_x_ccw",
                "surface_branch": "cylinder", "x": x, "y": y,
                "cp": float(row["cp"]), "cf": float(row["cf"]),
            })
        output.sort(key=lambda row: row["coordinate"])
    write_csv(path, ["coordinate", "coordinate_name", "surface_branch", "x", "y", "cp", "cf"], output)
    return output


def surface_figure(path: Path, case_id: str, data: Sequence[dict], variable: str) -> None:
    is_naca = case_id.startswith("naca")
    x = [row["coordinate"] for row in data]
    y = [row[variable] for row in data]
    svg = Svg()
    symbol = "C_p" if variable == "cp" else "C_f"
    title = f"{case_id}: surface {symbol}"
    svg.text(800, 55, title, 31, weight="bold")
    sx, sy = axes(svg, (175, 115, 1310, 730), (min(x), max(x)), padded_range(y),
                  "x/c" if is_naca else "θ (deg, from downstream +x, CCW)", symbol)
    if is_naca:
        for branch, color in (("upper", BLUE), ("lower", ORANGE)):
            rows = [row for row in data if row["surface_branch"] == branch]
            svg.path([(sx(row["coordinate"]), sy(row[variable])) for row in rows], color, 3)
        labels = [("Upper surface", BLUE), ("Lower surface", ORANGE)]
    else:
        svg.path([(sx(row["coordinate"]), sy(row[variable])) for row in data], PURPLE, 3)
        labels = [("Cylinder wall", PURPLE)]
    cursor = 220
    for label, color in labels:
        svg.line(cursor, 905, cursor + 50, 905, color, 4)
        svg.text(cursor + 64, 912, label, 20, "start")
        cursor += 280
    svg.text(1480, 970, "Boundary-state surface output", 18, "end", color="#506070")
    svg.finish(path)


def parse_vtu(path: Path) -> dict:
    root = ET.parse(path).getroot()
    piece = root.find(".//Piece")
    if piece is None:
        raise ValueError(f"missing VTU Piece in {path}")
    points_node = piece.find("Points/DataArray")
    cells_node = piece.find("Cells")
    data_node = piece.find("CellData")
    if points_node is None or cells_node is None or data_node is None:
        raise ValueError(f"incomplete VTU structure in {path}")
    raw_points = [float(value) for value in (points_node.text or "").split()]
    points = [(raw_points[i], raw_points[i + 1]) for i in range(0, len(raw_points), 3)]
    cell_arrays = {node.get("Name"): (node.text or "").split() for node in cells_node.findall("DataArray")}
    connectivity = [int(value) for value in cell_arrays["connectivity"]]
    offsets = [int(value) for value in cell_arrays["offsets"]]
    cells = []
    start = 0
    for offset in offsets:
        cells.append([points[index] for index in connectivity[start:offset]])
        start = offset
    arrays = {}
    for node in data_node.findall("DataArray"):
        name = node.get("Name")
        if name:
            converter = int if node.get("type", "").startswith(("Int", "UInt")) else float
            arrays[name] = [converter(value) for value in (node.text or "").split()]
    field = {}
    for node in root.findall(".//FieldData/DataArray"):
        name = node.get("Name")
        if name:
            field[name] = float((node.text or "0").split()[0])
    if len(cells) != int(piece.get("NumberOfCells", "-1")):
        raise ValueError(f"VTU cell count mismatch in {path}")
    return {"cells": cells, "arrays": arrays, "field": field}


def centroid(cell: Sequence[tuple[float, float]]) -> tuple[float, float]:
    # Standard polygon centroid, falling back to the vertex mean for degeneracy.
    twice_area = 0.0
    cx = cy = 0.0
    for index, point in enumerate(cell):
        nxt = cell[(index + 1) % len(cell)]
        cross = point[0] * nxt[1] - nxt[0] * point[1]
        twice_area += cross
        cx += (point[0] + nxt[0]) * cross
        cy += (point[1] + nxt[1]) * cross
    if abs(twice_area) < 1e-30:
        return mean([p[0] for p in cell]), mean([p[1] for p in cell])
    return cx / (3.0 * twice_area), cy / (3.0 * twice_area)


VIRIDIS = [
    (0.00, (68, 1, 84)), (0.13, (71, 44, 122)), (0.25, (59, 82, 139)),
    (0.38, (44, 113, 142)), (0.50, (33, 145, 140)), (0.63, (39, 173, 129)),
    (0.75, (92, 200, 99)), (0.88, (170, 220, 50)), (1.00, (253, 231, 37)),
]
COOLWARM = [
    (0.00, (49, 54, 149)), (0.20, (69, 117, 180)), (0.40, (170, 204, 227)),
    (0.50, (245, 245, 245)), (0.60, (244, 165, 130)), (0.80, (214, 79, 64)),
    (1.00, (165, 0, 38)),
]


def colormap(value: float, low: float, high: float, divergent: bool = False) -> str:
    scale = COOLWARM if divergent else VIRIDIS
    fraction = min(1.0, max(0.0, (value - low) / (high - low)))
    for index in range(1, len(scale)):
        if fraction <= scale[index][0]:
            p0, c0 = scale[index - 1]
            p1, c1 = scale[index]
            alpha = (fraction - p0) / (p1 - p0)
            color = tuple(round(c0[k] + alpha * (c1[k] - c0[k])) for k in range(3))
            return "#%02x%02x%02x" % color
    return "#%02x%02x%02x" % scale[-1][1]


def body_outline(case_id: str, surface: Sequence[dict[str, str]]) -> list[tuple[float, float]]:
    points = [(float(row["x"]), float(row["y"])) for row in surface]
    if case_id.startswith("naca"):
        upper = sorted((point for point in points if point[1] >= 0), key=lambda p: p[0])
        lower = sorted((point for point in points if point[1] < 0), key=lambda p: p[0], reverse=True)
        return upper + lower
    return sorted(points, key=lambda p: math.atan2(p[1], p[0]))


def field_figure(path: Path, case_id: str, vtu: dict, surface: Sequence[dict[str, str]],
                 variable: str, clip: tuple[float, float] | None = None) -> tuple[float, float, int]:
    is_naca = case_id.startswith("naca")
    viewport = (-0.15, 1.15, -0.25, 0.25) if is_naca else (-1.0, 8.0, -3.0, 3.0)
    xmin, xmax, ymin, ymax = viewport
    cells = vtu["cells"]
    values = vtu["arrays"][variable]
    visible = []
    for index, cell in enumerate(cells):
        xs, ys = [p[0] for p in cell], [p[1] for p in cell]
        if max(xs) >= xmin and min(xs) <= xmax and max(ys) >= ymin and min(ys) <= ymax:
            visible.append(index)
    view_values = [values[index] for index in visible]
    if clip is None:
        low, high = quantile(view_values, 0.01), quantile(view_values, 0.99)
        if math.isclose(low, high):
            low, high = min(view_values), max(view_values)
    else:
        low, high = clip
    if math.isclose(low, high):
        high = low + 1e-12

    svg = Svg(1800, 1100)
    title_variable = {
        "mach": "Mach number", "pressure": "Pressure", "velocity_magnitude": "Velocity magnitude",
        "vorticity": "Vorticity ω_z",
    }[variable]
    svg.text(900, 54, f"{case_id}: {title_variable}", 32, weight="bold")
    plot_left, plot_top, plot_width, plot_height = 130.0, 120.0, 1430.0, 820.0
    xscale = plot_width / (xmax - xmin)
    yscale = plot_height / (ymax - ymin)
    common = min(xscale, yscale)
    used_width, used_height = common * (xmax - xmin), common * (ymax - ymin)
    left = plot_left + (plot_width - used_width) / 2
    top = plot_top + (plot_height - used_height) / 2

    def transform(point: tuple[float, float]) -> tuple[float, float]:
        return left + (point[0] - xmin) * common, top + used_height - (point[1] - ymin) * common

    divergent = variable == "vorticity"
    for index in visible:
        svg.polygon([transform(point) for point in cells[index]],
                    colormap(values[index], low, high, divergent))
    outline = body_outline(case_id, surface)
    svg.polygon([transform(point) for point in outline], BG, INK, 2.5)
    # Axes after polygons.
    svg.line(left, top + used_height, left + used_width, top + used_height, INK, 2)
    svg.line(left, top, left, top + used_height, INK, 2)
    for tick in nice_ticks(xmin, xmax, 7):
        pixel, base = transform((tick, ymin))
        svg.line(pixel, base, pixel, base + 9, INK, 2)
        svg.text(pixel, base + 35, fmt_tick(tick), 18)
    for tick in nice_ticks(ymin, ymax, 7):
        base, pixel = transform((xmin, tick))
        svg.line(base - 9, pixel, base, pixel, INK, 2)
        svg.text(base - 16, pixel + 6, fmt_tick(tick), 18, "end")
    svg.text(left + used_width / 2, top + used_height + 78, "x / L_ref", 23)
    svg.text(left - 88, top + used_height / 2, "y / L_ref", 23, rotate=-90)
    # Colorbar.
    bar_x, bar_y, bar_w, bar_h = 1630.0, 185.0, 48.0, 650.0
    for step in range(100):
        fraction = step / 99
        value = low + fraction * (high - low)
        svg.rect(bar_x, bar_y + (99 - step) * bar_h / 100, bar_w, bar_h / 100 + 1,
                 colormap(value, low, high, divergent))
    svg.rect(bar_x, bar_y, bar_w, bar_h, "none", INK, 1.5)
    for tick in [low + index * (high - low) / 5 for index in range(6)]:
        pixel = bar_y + bar_h * (high - tick) / (high - low)
        svg.line(bar_x + bar_w, pixel, bar_x + bar_w + 9, pixel, INK, 1.5)
        svg.text(bar_x + bar_w + 16, pixel + 6, fmt_tick(tick), 18, "start")
    label = {"mach": "Mach number, M", "pressure": "Pressure, p",
             "velocity_magnitude": "|u|", "vorticity": "ω_z (clipped)"}[variable]
    svg.text(bar_x + 22, bar_y - 30, label, 20)
    clipping = "fixed clip" if clip else "1st–99th percentile clip"
    svg.text(900, 1030, f"Actual unstructured cell polygons; cell-constant values; {clipping} [{low:.5g}, {high:.5g}]",
             18, color="#506070")
    svg.finish(path)
    return low, high, len(visible)


def edge_key(first: tuple[float, float], second: tuple[float, float]) -> tuple:
    a = (round(first[0], 12), round(first[1], 12))
    b = (round(second[0], 12), round(second[1], 12))
    return (a, b) if a <= b else (b, a)


def derive_vorticity(vtu: dict, path: Path) -> tuple[list[float], list[tuple[float, float]], list[int]]:
    cells = vtu["cells"]
    centers = [centroid(cell) for cell in cells]
    edge_cells: dict[tuple, list[int]] = defaultdict(list)
    for cell_id, cell in enumerate(cells):
        for index, first in enumerate(cell):
            edge_cells[edge_key(first, cell[(index + 1) % len(cell)])].append(cell_id)
    neighbors = [set() for _ in cells]
    for attached in edge_cells.values():
        if len(attached) == 2:
            first, second = attached
            neighbors[first].add(second)
            neighbors[second].add(first)
    u = vtu["arrays"]["u"]
    v = vtu["arrays"]["v"]
    omega: list[float] = []
    valid: list[int] = []
    rows = []
    for cell_id, (x, y) in enumerate(centers):
        stencil = set(neighbors[cell_id])

        def fit(component: Sequence[float], stencil_ids: set[int]) -> tuple[float, float, float]:
            a11 = a12 = a22 = b1 = b2 = 0.0
            for other in stencil_ids:
                dx, dy = centers[other][0] - x, centers[other][1] - y
                distance2 = dx * dx + dy * dy
                if distance2 <= 0:
                    continue
                weight = 1.0 / distance2
                delta = component[other] - component[cell_id]
                a11 += weight * dx * dx
                a12 += weight * dx * dy
                a22 += weight * dy * dy
                b1 += weight * dx * delta
                b2 += weight * dy * delta
            determinant = a11 * a22 - a12 * a12
            if abs(determinant) <= 1e-14 * max(1.0, a11 * a22):
                return 0.0, 0.0, determinant
            return ((b1 * a22 - b2 * a12) / determinant,
                    (a11 * b2 - a12 * b1) / determinant, determinant)

        gux, guy, determinant = fit(u, stencil)
        gvx, gvy, _ = fit(v, stencil)
        expanded = 0
        if abs(determinant) <= 1e-14:
            expanded = 1
            for neighbor in tuple(stencil):
                stencil.update(neighbors[neighbor])
            stencil.discard(cell_id)
            gux, guy, determinant = fit(u, stencil)
            gvx, gvy, _ = fit(v, stencil)
        is_valid = int(abs(determinant) > 1e-14)
        value = gvx - guy if is_valid else 0.0
        omega.append(value)
        valid.append(is_valid)
        rows.append({
            "cell_id": cell_id, "global_id": cell_id, "x": f"{x:.17g}", "y": f"{y:.17g}",
            "u": f"{u[cell_id]:.17g}", "v": f"{v[cell_id]:.17g}",
            "vorticity": f"{value:.17g}", "edge_neighbor_count": len(neighbors[cell_id]),
            "least_squares_stencil_count": len(stencil), "second_ring_expanded": expanded,
            "gradient_valid": is_valid,
        })
    write_csv(path, ["cell_id", "global_id", "x", "y", "u", "v", "vorticity",
                     "edge_neighbor_count", "least_squares_stencil_count",
                     "second_ring_expanded", "gradient_valid"], rows)
    return omega, centers, valid


def fft(values: Sequence[complex]) -> list[complex]:
    output = list(values)
    size = len(output)
    if size == 0 or size & (size - 1):
        raise ValueError("FFT size must be a power of two")
    j = 0
    for i in range(1, size):
        bit = size >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            output[i], output[j] = output[j], output[i]
    length = 2
    while length <= size:
        root = cmath.exp(-2j * math.pi / length)
        for start in range(0, size, length):
            factor = 1 + 0j
            half = length // 2
            for offset in range(half):
                even = output[start + offset]
                odd = output[start + offset + half] * factor
                output[start + offset] = even + odd
                output[start + offset + half] = even - odd
                factor *= root
        length *= 2
    return output


def re200_metrics(rows: Sequence[dict[str, str]], spectrum_path: Path,
                  surface: Sequence[dict[str, str]], vtu: dict) -> dict:
    selected = [row for row in rows if 200.0 - 1e-9 <= float(row["physical_time"]) <= 300.0 + 1e-9]
    time = floats(selected, "physical_time")
    cd = floats(selected, "cd")
    cl = floats(selected, "cl")
    cl_mean, cd_mean = mean(cl), mean(cd)
    cl_rms = rms_about(cl, cl_mean)
    dt = statistics.median([time[index] - time[index - 1] for index in range(1, len(time))])
    crossings = []
    for index in range(1, len(cl)):
        if cl[index - 1] <= cl_mean < cl[index]:
            fraction = (cl_mean - cl[index - 1]) / (cl[index] - cl[index - 1])
            crossings.append(time[index - 1] + fraction * (time[index] - time[index - 1]))
    periods = [crossings[index] - crossings[index - 1] for index in range(1, len(crossings))]
    crossing_frequency = 1.0 / mean(periods)
    period_cv = statistics.pstdev(periods) / mean(periods)

    nfft = 1
    while nfft < len(cl):
        nfft *= 2
    window = [0.5 - 0.5 * math.cos(2 * math.pi * index / (len(cl) - 1)) for index in range(len(cl))]
    window_sum = math.fsum(window)
    signal = [complex((value - cl_mean) * window[index], 0.0) for index, value in enumerate(cl)]
    signal.extend([0j] * (nfft - len(signal)))
    transformed = fft(signal)
    spectral_rows = []
    amplitudes = []
    for index in range(nfft // 2 + 1):
        frequency = index / (nfft * dt)
        amplitude = abs(transformed[index]) / window_sum * (1.0 if index in (0, nfft // 2) else 2.0)
        power = amplitude * amplitude
        amplitudes.append(amplitude)
        spectral_rows.append({"frequency": f"{frequency:.17g}", "amplitude": f"{amplitude:.17g}",
                              "power": f"{power:.17g}", "nfft": nfft, "dt": f"{dt:.17g}"})
    write_csv(spectrum_path, ["frequency", "amplitude", "power", "nfft", "dt"], spectral_rows)
    dominant_index = max(range(1, len(amplitudes)), key=amplitudes.__getitem__)
    fft_frequency = dominant_index / (nfft * dt)

    midpoint = 250.0
    first = [row for row in selected if float(row["physical_time"]) <= midpoint]
    second = [row for row in selected if float(row["physical_time"]) > midpoint]
    first_cd, second_cd = floats(first, "cd"), floats(second, "cd")
    first_cl, second_cl = floats(first, "cl"), floats(second, "cl")
    drag_drift = abs(mean(second_cd) - mean(first_cd)) / abs(cd_mean)
    rms_first, rms_second = rms_about(first_cl), rms_about(second_cl)
    lift_rms_drift = abs(rms_second - rms_first) / cl_rms
    first_crossings = sum(1 for value in crossings if 200.0 <= value <= 250.0)
    second_crossings = sum(1 for value in crossings if 250.0 < value <= 300.0)

    radii = [math.hypot(float(row["x"]), float(row["y"])) for row in surface]
    diameter = 2.0 * mean(radii)
    centers = [centroid(cell) for cell in vtu["cells"]]
    radius = [math.hypot(x, y) for x, y in centers]
    cutoff = quantile(radius, 0.95)
    outer_u = [vtu["arrays"]["u"][index] for index, value in enumerate(radius) if value >= cutoff]
    reference_velocity = statistics.median(outer_u)
    return {
        "window_start": 200.0, "window_end": 300.0, "samples": len(selected), "dt": dt,
        "mean_cd": cd_mean, "mean_cl": cl_mean, "cl_rms": cl_rms,
        "cl_half_peak_to_peak_amplitude": 0.5 * (max(cl) - min(cl)),
        "fft_dominant_frequency": fft_frequency,
        "fft_dominant_amplitude": amplitudes[dominant_index],
        "mean_upcrossing_frequency": crossing_frequency,
        "strouhal_fft": fft_frequency * diameter / reference_velocity,
        "strouhal_crossing": crossing_frequency * diameter / reference_velocity,
        "reference_diameter_from_surface": diameter,
        "reference_velocity_outer_field_median": reference_velocity,
        "mean_period": mean(periods), "period_coefficient_of_variation": period_cv,
        "upcrossings_total": len(crossings), "first_half_upcrossings": first_crossings,
        "second_half_upcrossings": second_crossings,
        "relative_drag_mean_drift": drag_drift, "relative_lift_rms_drift": lift_rms_drift,
        "periodic_pass": (first_crossings >= 3 and second_crossings >= 3 and period_cv <= 0.15
                          and drag_drift <= 0.05 and lift_rms_drift <= 0.10 and cl_rms >= 1e-4),
        "fft_method": "Hann-windowed mean-removed radix-2 FFT; all t=200..300 samples zero-padded",
        "crossing_method": "linear interpolation of upward crossings of the t=200..300 mean lift",
    }


def check_record(passed: bool, criterion: str, **evidence: object) -> dict:
    return {"pass": bool(passed), "mandatory": True, "criterion": criterion, "evidence": evidence}


def analyze_case(case_dir: Path, report: Path, figure_rows: list[dict], derived_rows: list[dict],
                 field_cache: dict[str, dict]) -> tuple[dict, dict]:
    case_id = case_dir.name
    metadata = read_json(case_dir / "metadata.json")
    status = read_json(case_dir / "run_status.json")
    residuals = read_csv(case_dir / "residuals.csv")
    forces = read_csv(case_dir / "forces.csv")
    surface = read_csv(case_dir / "surface.csv")
    partitions = read_csv(case_dir / "partition_diagnostics.csv")
    vtu = parse_vtu(case_dir / "field_final.vtu")
    field_cache[case_id] = vtu
    figures = report / "figures"
    data_dir = report / "data"

    # Residual history.
    residual_file = figures / f"{case_id}_residual_history.svg"
    x = floats(residuals, "step")
    residual_l2 = floats(residuals, "residual_l2")
    horizontal = []
    if status["final_physical_time"] == 0 and float(status.get("steady_initial_residual_scale", -1)) > 0:
        baseline = float(status["steady_initial_residual_scale"])
        horizontal.append(("Global initial baseline", baseline, GREEN))
        target_orders = float(metadata.get("residual_reduction_target", -1))
        if target_orders > 0:
            horizontal.append((f"Target ({target_orders:g} orders)", baseline * 10 ** (-target_orders), PURPLE))
    line_figure(residual_file, case_id, x, [("Global residual L2", residual_l2, BLUE)],
                "Physical step", "Global residual L2", f"{case_id}: residual convergence", True, horizontal)
    figure_rows.append({"figure_file": residual_file.name, "case_id": case_id,
                        "figure_type": "residual_history", "variable": "global_residual_l2",
                        "source_file": source_name(case_dir / "residuals.csv"),
                        "caption": f"Global residual L2 history for {case_id}; steady metadata baseline and target are shown when available."})

    # Force history.
    force_file = figures / f"{case_id}_force_history.svg"
    if case_id == RE200_ID:
        re200_force_figure(force_file, case_id, forces)
    else:
        line_figure(force_file, case_id, floats(forces, "step"),
                    [("C_D", floats(forces, "cd"), BLUE), ("C_L", floats(forces, "cl"), ORANGE)],
                    "Physical step", "Force coefficient", f"{case_id}: force history")
    figure_rows.append({"figure_file": force_file.name, "case_id": case_id,
                        "figure_type": "force_history", "variable": "cd_and_cl",
                        "source_file": source_name(case_dir / "forces.csv"),
                        "caption": (f"Drag and lift coefficient histories for {case_id}. "
                                    + ("The lower panel isolates t=200–300 while the upper panel retains startup."
                                       if case_id == RE200_ID else ""))})

    # Surface transformations and distributions.
    transformed = data_dir / f"{case_id}_surface_plot.csv"
    surface_data = surface_plot_data(case_id, surface, transformed)
    derived_rows.append({"derived_file": source_name(transformed), "source_file": source_name(case_dir / "surface.csv"),
                         "method": "NACA y-sign upper/lower pairing or cylinder atan2 angular coordinate",
                         "row_count": len(surface_data), "sha256": sha256(transformed)})
    cp_file = figures / f"{case_id}_surface_cp.svg"
    surface_figure(cp_file, case_id, surface_data, "cp")
    figure_rows.append({"figure_file": cp_file.name, "case_id": case_id,
                        "figure_type": "surface_distribution", "variable": "cp",
                        "source_file": source_name(transformed),
                        "caption": f"Surface pressure coefficient C_p for {case_id}; NACA sides and cylinder angle are explicitly distinguished."})
    viscous = "inviscid" not in case_id
    if viscous:
        cf_file = figures / f"{case_id}_surface_cf.svg"
        surface_figure(cf_file, case_id, surface_data, "cf")
        figure_rows.append({"figure_file": cf_file.name, "case_id": case_id,
                            "figure_type": "surface_distribution", "variable": "cf",
                            "source_file": source_name(transformed),
                            "caption": f"Surface skin-friction coefficient C_f for viscous case {case_id}."})

    field_clips = {}
    for variable in ("mach", "pressure"):
        field_file = figures / f"{case_id}_{variable}.svg"
        low, high, count = field_figure(field_file, case_id, vtu, surface, variable)
        field_clips[variable] = {"clip_min": low, "clip_max": high, "visible_cells": count}
        figure_rows.append({"figure_file": field_file.name, "case_id": case_id,
                            "figure_type": "unstructured_cell_field", "variable": variable,
                            "source_file": source_name(case_dir / "field_final.vtu"),
                            "caption": f"{variable.capitalize()} field for {case_id}, rendered as actual VTU cell polygons with cell-constant values and a labeled percentile-clipped colorbar."})

    # Force statistic is final for steady cases and t=200..300 mean for Re200.
    force_sample = [forces[-1]]
    force_statistic = "final"
    if case_id == RE200_ID:
        force_sample = [row for row in forces
                        if 200.0 - 1e-9 <= float(row["physical_time"]) <= 300.0 + 1e-9]
        force_statistic = "mean_t200_300"
    force_values = {key: mean(floats(force_sample, key)) for key in
                    ("cl", "cd", "cmz", "pressure_drag", "viscous_drag", "pressure_lift", "viscous_lift")}
    field_rho = vtu["arrays"]["rho"]
    field_pressure = vtu["arrays"]["pressure"]
    field_mach = vtu["arrays"]["mach"]
    surface_cp = floats(surface, "cp")
    surface_cf = floats(surface, "cf")
    surface_speed = [math.hypot(float(row["u"]), float(row["v"])) for row in surface]
    normal_speed = [abs(float(row["u"]) * float(row["nx"]) + float(row["v"]) * float(row["ny"])) for row in surface]
    closure_drag = max(abs(float(row["cd"]) - float(row["pressure_drag"]) - float(row["viscous_drag"])) for row in forces)
    closure_lift = max(abs(float(row["cl"]) - float(row["pressure_lift"]) - float(row["viscous_lift"])) for row in forces)
    final_step = int(status["final_step"])
    final_time = float(status["final_physical_time"])
    final_consistent = (int(float(residuals[-1]["step"])) == final_step
                        and int(float(forces[-1]["step"])) == final_step
                        and int(vtu["field"]["step"]) == final_step
                        and abs(float(residuals[-1]["physical_time"]) - final_time) <= 1e-10
                        and abs(float(forces[-1]["physical_time"]) - final_time) <= 1e-10
                        and abs(vtu["field"].get("time", final_time) - final_time) <= 1e-10)

    checks = {
        "positive_density_pressure": check_record(
            min(field_rho) > 0 and min(field_pressure) > 0
            and min(floats(surface, "rho")) > 0 and min(floats(surface, "pressure")) > 0,
            "minimum field and surface density and pressure are strictly positive",
            min_field_rho=min(field_rho), min_field_pressure=min(field_pressure),
            min_surface_rho=min(floats(surface, "rho")), min_surface_pressure=min(floats(surface, "pressure"))),
        "nontrivial_surface_and_field": check_record(
            max(surface_cp) - min(surface_cp) > 1e-6 and max(field_mach) - min(field_mach) > 1e-6
            and max(field_pressure) - min(field_pressure) > 1e-6,
            "surface Cp, field Mach, and field pressure ranges each exceed 1e-6",
            cp_range=max(surface_cp) - min(surface_cp), mach_range=max(field_mach) - min(field_mach),
            pressure_range=max(field_pressure) - min(field_pressure)),
        "force_split_closure": check_record(
            closure_drag <= 1e-10 and closure_lift <= 1e-10,
            "max absolute drag/lift split closure error is <=1e-10",
            max_drag_closure_error=closure_drag, max_lift_closure_error=closure_lift),
        "final_step_consistency": check_record(
            final_consistent,
            "last residual/force and VTU field step/time match run_status",
            residual_step=int(float(residuals[-1]["step"])), force_step=int(float(forces[-1]["step"])),
            vtu_step=int(vtu["field"]["step"]), status_step=final_step,
            residual_time=float(residuals[-1]["physical_time"]), force_time=float(forces[-1]["physical_time"]),
            vtu_time=vtu["field"].get("time"), status_time=final_time),
    }
    if case_id.startswith("naca"):
        checks["naca_lift_symmetry"] = check_record(
            abs(force_values["cl"]) <= 0.01, "absolute zero-incidence NACA lift coefficient is <=0.01",
            cl=force_values["cl"], absolute_cl=abs(force_values["cl"]), threshold=0.01)
    else:
        checks["cylinder_positive_drag"] = check_record(
            force_values["cd"] > 0, "cylinder final/post-transient mean drag is positive",
            cd=force_values["cd"], statistic=force_statistic)
    if viscous:
        checks["no_slip_wall"] = check_record(
            max(surface_speed) <= 1e-12 and max(surface_cf) - min(surface_cf) > 1e-6,
            "reported no-slip wall maximum speed <=1e-12 and Cf is nontrivial",
            max_wall_speed=max(surface_speed), cf_range=max(surface_cf) - min(surface_cf))
    else:
        max_viscous_force = max(max(abs(float(row["viscous_drag"])), abs(float(row["viscous_lift"]))) for row in forces)
        checks["slip_wall_and_zero_viscous_force"] = check_record(
            max(normal_speed) <= 1e-10 and max_viscous_force <= 1e-12,
            "slip-wall normal speed <=1e-10 and viscous force magnitude <=1e-12",
            max_normal_speed=max(normal_speed), max_viscous_force=max_viscous_force)

    owned = [int(row["num_cells_owned"]) for row in partitions]
    ghosts = [int(row["num_cells_ghost"]) for row in partitions]
    neighbors = [int(row["num_neighbor_ranks"]) for row in partitions]
    limiter = metadata.get("limiter_diagnostics", {})
    summary = {
        "case_id": case_id, "status": status["convergence_status"], "completed": metadata["completed"],
        "mpi_ranks": int(metadata["mpi_ranks"]), "final_step": final_step, "final_physical_time": final_time,
        "wall_time_seconds": float(status["wall_time_seconds"]),
        "residual_reduction_orders": float(status["residual_reduction_orders"]),
        "full_order_residual_reduction_orders": float(status.get("full_order_residual_reduction_orders", 0.0)),
        "residual_initial_recorded_l2": residual_l2[0], "residual_final_l2": residual_l2[-1],
        "force_statistic": force_statistic, **force_values,
        "final_forces": {key: float(forces[-1][key]) for key in
                         ("cl", "cd", "cmz", "pressure_drag", "viscous_drag", "pressure_lift", "viscous_lift")},
        "min_field_rho": min(field_rho), "min_field_pressure": min(field_pressure),
        "min_surface_rho": min(floats(surface, "rho")), "min_surface_pressure": min(floats(surface, "pressure")),
        "surface_cp_min": min(surface_cp), "surface_cp_max": max(surface_cp),
        "surface_cf_min": min(surface_cf), "surface_cf_max": max(surface_cf),
        "max_wall_speed": max(surface_speed), "field_ranges": {
            "rho": [min(field_rho), max(field_rho)], "pressure": [min(field_pressure), max(field_pressure)],
            "mach": [min(field_mach), max(field_mach)],
        }, "figure_clips": field_clips,
        "limiter": metadata["limiter"], "limiter_diagnostics": limiter,
        "partition": {"partitioner": metadata["partitioner"], "edge_cut": int(metadata["partition_edge_cut"]),
                      "owned_min": min(owned), "owned_mean": mean(owned), "owned_max": max(owned),
                      "load_balance_ratio": max(owned) / mean(owned), "total_ghost_cells": sum(ghosts),
                      "ghost_min": min(ghosts), "ghost_max": max(ghosts),
                      "neighbor_ranks_min": min(neighbors), "neighbor_ranks_max": max(neighbors)},
        "numerics": {"spatial_order_claimed": metadata["spatial_order_claimed"],
                     "reconstruction": metadata["reconstruction"], "time_integrator": metadata["time_integrator"],
                     "implicit_solver": metadata["implicit_solver"], "observed_min_inner_iterations": metadata.get("observed_min_inner_iterations"),
                     "observed_max_inner_iterations": metadata.get("observed_max_inner_iterations"),
                     "typical_inner_iterations": metadata.get("typical_inner_iterations"),
                     "inner_target_converged_fraction": metadata.get("inner_target_converged_fraction")},
    }
    return summary, checks


def flatten_summary(summary: dict) -> dict:
    partition = summary["partition"]
    limiter = summary["limiter_diagnostics"]
    return {
        "case_id": summary["case_id"], "status": summary["status"], "completed": summary["completed"],
        "mpi_ranks": summary["mpi_ranks"], "final_step": summary["final_step"],
        "final_physical_time": summary["final_physical_time"], "wall_time_seconds": summary["wall_time_seconds"],
        "residual_reduction_orders": summary["residual_reduction_orders"],
        "full_order_residual_reduction_orders": summary["full_order_residual_reduction_orders"],
        "residual_final_l2": summary["residual_final_l2"], "force_statistic": summary["force_statistic"],
        "cl": summary["cl"], "cd": summary["cd"], "cmz": summary["cmz"],
        "pressure_drag": summary["pressure_drag"], "viscous_drag": summary["viscous_drag"],
        "pressure_lift": summary["pressure_lift"], "viscous_lift": summary["viscous_lift"],
        "min_field_rho": summary["min_field_rho"], "min_field_pressure": summary["min_field_pressure"],
        "min_surface_rho": summary["min_surface_rho"], "min_surface_pressure": summary["min_surface_pressure"],
        "surface_cp_min": summary["surface_cp_min"], "surface_cp_max": summary["surface_cp_max"],
        "surface_cf_min": summary["surface_cf_min"], "surface_cf_max": summary["surface_cf_max"],
        "max_wall_speed": summary["max_wall_speed"], "limiter": summary["limiter"],
        "limited_face_components": limiter.get("venkatakrishnan_limited_face_components", 0),
        "shock_fallback_cells": limiter.get("shock_fallback_cells", 0),
        "positivity_barth_fallbacks": limiter.get("positivity_barth_fallbacks", 0),
        "positivity_scaled_faces": limiter.get("positivity_scaled_faces", 0),
        "first_order_fallback_faces": limiter.get("first_order_fallback_faces", 0),
        "partitioner": partition["partitioner"], "partition_edge_cut": partition["edge_cut"],
        "owned_cells_min": partition["owned_min"], "owned_cells_mean": partition["owned_mean"],
        "owned_cells_max": partition["owned_max"], "load_balance_ratio": partition["load_balance_ratio"],
        "total_ghost_cells": partition["total_ghost_cells"],
    }


def generate(results: Path, report: Path) -> dict:
    canonical_before = canonical_result_hashes(results)
    value_before = {
        name: sha256(report / name)
        for name in CANONICAL_VALUE_FILES if (report / name).is_file()
    }
    cases = sorted(path for path in results.iterdir() if path.is_dir() and (path / "metadata.json").exists())
    case_ids = {path.name for path in cases}
    if case_ids != EXPECTED_CASES:
        raise RuntimeError(f"canonical case set mismatch: missing={sorted(EXPECTED_CASES-case_ids)}, extra={sorted(case_ids-EXPECTED_CASES)}")
    figures, data_dir = report / "figures", report / "data"
    figures.mkdir(parents=True, exist_ok=True)
    data_dir.mkdir(parents=True, exist_ok=True)
    # These directories are owned by this pipeline; remove stale generated files.
    for directory in (figures, data_dir):
        for path in directory.iterdir():
            if path.is_file():
                path.unlink()

    figure_rows: list[dict] = []
    derived_rows: list[dict] = []
    summaries: dict[str, dict] = {}
    sanity_cases: dict[str, dict] = {}
    field_cache: dict[str, dict] = {}
    for case_dir in cases:
        summary, checks = analyze_case(case_dir, report, figure_rows, derived_rows, field_cache)
        summaries[case_dir.name] = summary
        sanity_cases[case_dir.name] = {"checks": checks, "mandatory_pass": all(item["pass"] for item in checks.values())}

    # Re200 independent analysis and wake figures.
    re_dir = results / RE200_ID
    re_forces = read_csv(re_dir / "forces.csv")
    re_surface = read_csv(re_dir / "surface.csv")
    spectrum_path = data_dir / "cylinder_m010_laminar_re200_lift_spectrum.csv"
    metrics = re200_metrics(re_forces, spectrum_path, re_surface, field_cache[RE200_ID])
    derived_rows.append({"derived_file": source_name(spectrum_path), "source_file": source_name(re_dir / "forces.csv"),
                         "method": metrics["fft_method"], "row_count": sum(1 for _ in spectrum_path.open()) - 1,
                         "sha256": sha256(spectrum_path)})
    summaries[RE200_ID]["re200_metrics"] = metrics
    periodic_check = check_record(bool(metrics["periodic_pass"]),
                                  "t=200..300 lift/drag satisfy crossing, period-CV, and half-window stationarity thresholds",
                                  **{key: metrics[key] for key in ("cl_rms", "first_half_upcrossings", "second_half_upcrossings",
                                                                  "period_coefficient_of_variation", "relative_drag_mean_drift",
                                                                  "relative_lift_rms_drift", "periodic_pass")})
    sanity_cases[RE200_ID]["checks"]["post_transient_periodicity"] = periodic_check
    sanity_cases[RE200_ID]["mandatory_pass"] = all(item["pass"] for item in sanity_cases[RE200_ID]["checks"].values())

    vorticity_path = data_dir / "cylinder_m010_laminar_re200_cell_vorticity.csv"
    omega, centers, valid = derive_vorticity(field_cache[RE200_ID], vorticity_path)
    derived_rows.append({"derived_file": source_name(vorticity_path), "source_file": source_name(re_dir / "field_final.vtu"),
                         "method": "edge-neighbor inverse-distance weighted cell-centroid least-squares gradients; omega=dv/dx-du/dy",
                         "row_count": len(omega), "sha256": sha256(vorticity_path)})
    summaries[RE200_ID]["vorticity_derivation"] = {
        "method": "For each VTU cell, shared-edge neighbors define a weighted least-squares fit of u and v differences at polygon centroids; weights are inverse squared centroid distance. A second edge-neighbor ring is used only if the 2x2 normal matrix is singular. omega_z=dv/dx-du/dy. Cell/global id is the zero-based VTU cell order, which the solver writes sorted by global id.",
        "cells": len(omega), "valid_gradient_cells": sum(valid), "invalid_gradient_cells": len(valid) - sum(valid),
        "vorticity_min": min(omega), "vorticity_max": max(omega), "visualization_clip": [-5.0, 5.0],
    }
    vtu = field_cache[RE200_ID]
    vtu["arrays"]["vorticity"] = omega
    velocity = [math.hypot(u, v) for u, v in zip(vtu["arrays"]["u"], vtu["arrays"]["v"])]
    vtu["arrays"]["velocity_magnitude"] = velocity
    for variable, clip, source in (
        ("velocity_magnitude", None, re_dir / "field_final.vtu"),
        ("vorticity", (-5.0, 5.0), vorticity_path),
    ):
        path = figures / f"{RE200_ID}_{variable}.svg"
        field_figure(path, RE200_ID, vtu, re_surface, variable, clip)
        figure_rows.append({"figure_file": path.name, "case_id": RE200_ID,
                            "figure_type": "unstructured_cell_wake", "variable": variable,
                            "source_file": source_name(source),
                            "caption": (f"Cylinder Re=200 {variable.replace('_', ' ')} wake field rendered on actual VTU cells. "
                                        + ("Vorticity is clipped to [-5,5] and derived by documented cell-neighbor least squares."
                                           if variable == "vorticity" else "Cell velocity magnitude is computed directly from VTU u and v."))})

    overall_pass = all(case["mandatory_pass"] for case in sanity_cases.values())
    sanity = {
        "schema_version": 1, "overall_pass": overall_pass,
        "rule": "overall_pass is true only when every mandatory per-case check passes",
        "cases": sanity_cases,
        "figure_variable_coverage": {
            "pass": all(sum(1 for row in figure_rows if row["case_id"] == case_id and row["variable"] == variable) == 1
                        for case_id in EXPECTED_CASES for variable in ("mach", "pressure")),
            "mandatory": True,
            "evidence": {"mach_figures": sum(row["variable"] == "mach" for row in figure_rows),
                         "pressure_figures": sum(row["variable"] == "pressure" for row in figure_rows)},
        },
    }
    sanity["overall_pass"] = sanity["overall_pass"] and sanity["figure_variable_coverage"]["pass"]

    summary_document = {
        "schema_version": 1,
        "force_convention": "steady cases use final force row; Re200 uses arithmetic mean over inclusive t=200..300",
        "cases": summaries,
    }
    write_json(report / "results_summary.json", summary_document)
    flattened = [flatten_summary(summaries[case_id]) for case_id in sorted(summaries)]
    write_csv(report / "results_summary.csv", list(flattened[0]), flattened)
    write_csv(report / "re200_metrics.csv", list(metrics), [metrics])
    write_json(report / "sanity_checks.json", sanity)
    write_csv(report / "figure_manifest.csv", FIGURE_COLUMNS, figure_rows)
    write_csv(report / "derived_data_manifest.csv",
              ["derived_file", "source_file", "method", "row_count", "sha256"], derived_rows)

    partition_rows = []
    for case_dir in cases:
        for row in read_csv(case_dir / "partition_diagnostics.csv"):
            partition_rows.append({"case_id": case_dir.name, **row})
    write_csv(report / "partition_summary.csv", ["case_id"] + list(partition_rows[0].keys())[1:], partition_rows)

    canonical_after = canonical_result_hashes(results)
    if canonical_after != canonical_before:
        raise RuntimeError("generation altered canonical result files")
    value_after = {name: sha256(report / name) for name in CANONICAL_VALUE_FILES}
    changed_values = sorted(name for name, digest in value_before.items()
                            if value_after.get(name) != digest)
    if changed_values:
        raise RuntimeError(f"generation altered canonical sanity/summary values: {changed_values}")

    methodology = f"""# Postprocessing methodology

Generated by `tools/postprocess_results.py` using only the Python standard library.

Run from `/workspace/solver` with `python3 tools/postprocess_results.py`, then validate
without rewriting with `python3 tools/postprocess_results.py --check`.

- **Figures:** SVG vector graphics are the official source/variable mapping. Each SVG has a deterministic, dependency-free vector-PDF compile companion with the same basename. Both use fixed fonts/colors/layouts and tight fixed viewports.
- **Fields:** Mach, pressure, velocity magnitude, and vorticity use filled polygons from the actual VTU unstructured cells. Values remain cell-constant. No scatter display, remeshing, interpolation, or fabricated samples are used. NACA views are near-body; cylinder views include the body and wake. Mach/pressure/velocity limits are the visible-cell 1st–99th percentiles; captions show limits. Vorticity uses the required fixed `[-5,5]` range.
- **Surface coordinates:** NACA rows are paired by the sign of `y` into upper/lower surfaces and sorted by `x/c`. Cylinder rows use `atan2(y,x)` in degrees modulo 360, measured counter-clockwise from downstream `+x`. Transformed rows are saved in `report/data`.
- **Vorticity:** {summaries[RE200_ID]['vorticity_derivation']['method']}
- **Re200 spectrum:** {metrics['fft_method']}. {metrics['crossing_method']}. Strouhal uses the cylinder diameter measured from surface coordinates and outer-field median streamwise velocity, both independently derived from canonical outputs.
- **Traceability:** `postprocessing_manifest.json` hashes every canonical source and generated artifact; `--check` detects any source change and never rewrites files.
"""
    atomic_text(report / "postprocessing_methodology.md", methodology)

    source_paths = [case / name for case in cases for name in CASE_FILES]
    generated_paths = [report / "results_summary.json", report / "results_summary.csv",
                       report / "re200_metrics.csv", report / "sanity_checks.json",
                       report / "figure_manifest.csv", report / "derived_data_manifest.csv",
                       report / "partition_summary.csv", report / "postprocessing_methodology.md"]
    generated_paths += sorted(figures.iterdir()) + sorted(data_dir.iterdir())
    manifest = {
        "schema_version": 1, "generator": source_name(SCRIPT), "generator_sha256": sha256(SCRIPT),
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "results_directory": source_name(results), "report_directory": source_name(report),
        "source_files": {source_name(path): {"sha256": sha256(path), "bytes": path.stat().st_size}
                         for path in source_paths},
        "generated_files": {source_name(path): {"sha256": sha256(path), "bytes": path.stat().st_size}
                             for path in generated_paths},
        "canonical_results": {"file_count": len(canonical_after), "files": canonical_after},
        "canonical_value_files": {
            source_name(report / name): {"sha256": value_after[name], "bytes": (report / name).stat().st_size}
            for name in CANONICAL_VALUE_FILES
        },
        "figure_count": len(figure_rows), "sanity_overall_pass": sanity["overall_pass"],
        "warnings": [
            "SVG figures are the official source/variable mapping; same-basename PDFs are vector compile companions.",
            "Vorticity is a cell-neighbor least-squares derivative of cell-centered velocities and is less accurate at one-sided boundary stencils.",
        ],
    }
    write_json(report / "postprocessing_manifest.json", manifest)
    return {"figure_count": len(figure_rows), "pdf_count": len(list(figures.glob("*.pdf"))),
            "sanity_overall_pass": sanity["overall_pass"],
            "re200_metrics": metrics, "warnings": manifest["warnings"]}


def validate(results: Path, report: Path) -> dict:
    """Read-only validation of generated outputs against current canonical sources."""
    errors: list[str] = []
    warnings: list[str] = []
    manifest_path = report / "postprocessing_manifest.json"
    if not manifest_path.exists():
        raise RuntimeError(f"missing {manifest_path}; run generation first")
    manifest = read_json(manifest_path)
    if manifest.get("generator_sha256") != sha256(SCRIPT):
        errors.append("pipeline script hash differs from generation manifest")
    for name, expected in manifest.get("source_files", {}).items():
        path = Path(name)
        if not path.exists():
            errors.append(f"missing canonical source: {path}")
        elif path.stat().st_size != expected["bytes"] or sha256(path) != expected["sha256"]:
            errors.append(f"canonical source changed: {path}")
    for name, expected in manifest.get("generated_files", {}).items():
        path = Path(name)
        if not path.exists():
            errors.append(f"missing generated artifact: {path}")
        elif path.stat().st_size != expected["bytes"] or sha256(path) != expected["sha256"]:
            errors.append(f"generated artifact changed: {path}")

    try:
        canonical_hashes = canonical_result_hashes(results)
        recorded = manifest.get("canonical_results", {})
        if recorded.get("file_count") != 80 or recorded.get("files") != canonical_hashes:
            errors.append("canonical 80-hash snapshot differs from generation manifest")
    except RuntimeError as error:
        errors.append(str(error))
    for name, expected in manifest.get("canonical_value_files", {}).items():
        path = Path(name)
        if not path.is_file():
            errors.append(f"missing canonical sanity/summary artifact: {path}")
        elif path.stat().st_size != expected.get("bytes") or sha256(path) != expected.get("sha256"):
            errors.append(f"canonical sanity/summary values changed: {path}")

    figure_manifest = read_csv(report / "figure_manifest.csv")
    if not figure_manifest or list(figure_manifest[0]) != FIGURE_COLUMNS:
        errors.append("figure_manifest.csv columns are not exact")
    seen = set()
    for row in figure_manifest:
        figure = report / "figures" / row["figure_file"]
        pdf = figure.with_suffix(".pdf")
        source = Path(row["source_file"])
        if not figure.exists():
            errors.append(f"manifest figure missing: {figure}")
        elif not figure.read_text(encoding="utf-8", errors="replace").startswith("<?xml"):
            errors.append(f"figure is not readable SVG: {figure}")
        if not pdf.is_file() or pdf.stat().st_size == 0:
            errors.append(f"manifest SVG has no nonempty PDF companion: {figure} -> {pdf}")
        if not source.exists():
            errors.append(f"manifest source missing: {source}")
        if row["variable"] in ("mach", "pressure") and row["variable"] not in row["figure_file"]:
            errors.append(f"field filename/variable mismatch: {row['figure_file']} -> {row['variable']}")
        key = (row["case_id"], row["variable"])
        if row["variable"] in ("mach", "pressure"):
            if key in seen:
                errors.append(f"duplicate required field figure: {key}")
            seen.add(key)
    expected_coverage = {(case_id, variable) for case_id in EXPECTED_CASES for variable in ("mach", "pressure")}
    missing = expected_coverage - seen
    if missing:
        errors.append(f"missing field coverage: {sorted(missing)}")
    disk_figures = list((report / "figures").glob("*.svg"))
    if len(disk_figures) != len(figure_manifest):
        errors.append(f"figure count differs: disk={len(disk_figures)}, manifest={len(figure_manifest)}")
    disk_pdfs = list((report / "figures").glob("*.pdf"))
    expected_pdf_names = {Path(row["figure_file"]).with_suffix(".pdf").name for row in figure_manifest}
    if len(disk_pdfs) != len(figure_manifest) or {path.name for path in disk_pdfs} != expected_pdf_names:
        errors.append(f"PDF companion set differs: disk={len(disk_pdfs)}, manifest={len(figure_manifest)}")
    sanity = read_json(report / "sanity_checks.json")
    if not sanity.get("overall_pass"):
        errors.append("sanity_checks.json overall_pass is false")
    for case_id in EXPECTED_CASES:
        source_status = read_json(results / case_id / "run_status.json")["convergence_status"]
        summary_status = read_json(report / "results_summary.json")["cases"][case_id]["status"]
        if source_status != summary_status:
            errors.append(f"status mismatch for {case_id}: source={source_status}, summary={summary_status}")
    metrics = read_json(report / "results_summary.json")["cases"][RE200_ID]["re200_metrics"]
    if abs(metrics["fft_dominant_frequency"] - metrics["mean_upcrossing_frequency"]) > 0.03:
        warnings.append("Re200 FFT and crossing frequencies differ by more than 0.03")
    warnings.extend(manifest.get("warnings", []))
    result = {"pass": not errors, "figure_count": len(figure_manifest), "pdf_count": len(disk_pdfs),
              "sanity_overall_pass": bool(sanity.get("overall_pass")),
              "re200_metrics": metrics, "errors": errors, "warnings": warnings}
    if errors:
        raise RuntimeError(json.dumps(result, indent=2, sort_keys=True))
    return result


def print_result(label: str, result: dict) -> None:
    metrics = result["re200_metrics"]
    print(f"{label}: figures={result['figure_count']} pdfs={result['pdf_count']} "
          f"sanity_overall_pass={str(result['sanity_overall_pass']).lower()}")
    print("Re200 t=200..300: "
          f"mean_Cd={metrics['mean_cd']:.9g} Cl_rms={metrics['cl_rms']:.9g} "
          f"Cl_amp={metrics['cl_half_peak_to_peak_amplitude']:.9g} "
          f"f_fft={metrics['fft_dominant_frequency']:.9g} "
          f"f_cross={metrics['mean_upcrossing_frequency']:.9g} "
          f"St_fft={metrics['strouhal_fft']:.9g} periodic={str(metrics['periodic_pass']).lower()}")
    for warning in result.get("warnings", []):
        print(f"WARNING: {warning}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=DEFAULT_RESULTS,
                        help="canonical results directory (default: solver/results)")
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT,
                        help="report output directory (default: solver/report)")
    parser.add_argument("--check", action="store_true",
                        help="validate existing report artifacts without rewriting")
    args = parser.parse_args()
    results, report = args.results.resolve(), args.report.resolve()
    if not results.is_relative_to(DEFAULT_RESULTS.resolve()):
        parser.error("results path must remain under /workspace/solver/results")
    if not report.is_relative_to(DEFAULT_REPORT.resolve()):
        parser.error("all generated outputs must remain under /workspace/solver/report")
    if args.check:
        result = validate(results, report)
        print_result("CHECK PASS", result)
    else:
        result = generate(results, report)
        print_result("GENERATION COMPLETE", result)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # concise CLI failure with nonzero status
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
