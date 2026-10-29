#!/usr/bin/env python3
"""Summarize selected rows of a submitted force CSV without judging convergence.

The default is the final ten percent of data rows, not a ten-percent physical
time window. Use --start-time for a physical-time window. The result is a
diagnostic to review alongside the case input, report, and plots.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from pathlib import Path
from typing import Any


def _number(value: str | None, name: str, row: int) -> float:
    try:
        result = float(value)  # type: ignore[arg-type]
    except (TypeError, ValueError) as exc:
        raise ValueError(f"row {row}: {name} is not a number") from exc
    if not math.isfinite(result):
        raise ValueError(f"row {row}: {name} is not finite")
    return result


def summarize(path: Path, columns: list[str], tail_fraction: float | None,
              start_time: float | None) -> dict[str, Any]:
    if not columns or len(set(columns)) != len(columns):
        raise ValueError("select at least one distinct numeric column")
    if (tail_fraction is None) == (start_time is None):
        raise ValueError("select exactly one of tail fraction or start time")
    if tail_fraction is not None and (not math.isfinite(tail_fraction) or not 0 < tail_fraction <= 1):
        raise ValueError("tail fraction must be finite and in (0, 1]")
    if start_time is not None and not math.isfinite(start_time):
        raise ValueError("start time must be finite")

    with path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        headers = reader.fieldnames or []
        if len(set(headers)) != len(headers):
            raise ValueError("duplicate CSV column names")
        missing = [name for name in columns if name not in headers]
        if start_time is not None and "physical_time" not in headers:
            missing.append("physical_time")
        if missing:
            raise ValueError("missing CSV columns: " + ", ".join(missing))
        rows = []
        times = []
        for row_number, row in enumerate(reader, start=2):
            if None in row:
                raise ValueError(f"row {row_number}: extra CSV fields")
            values = {name: _number(row.get(name), name, row_number) for name in columns}
            if "physical_time" in headers:
                times.append(_number(row.get("physical_time"), "physical_time", row_number))
            rows.append(values)
    if not rows:
        raise ValueError("CSV contains no data rows")
    if times and any(b < a for a, b in zip(times, times[1:])):
        raise ValueError("physical_time is not nondecreasing")
    if start_time is not None:
        indices = [i for i, t in enumerate(times) if t >= start_time]
        selection = {"mode": "physical_time", "start_time_inclusive": start_time}
    else:
        count = max(1, math.ceil(len(rows) * tail_fraction))  # type: ignore[operator]
        indices = list(range(len(rows) - count, len(rows)))
        selection = {"mode": "tail_rows", "fraction": tail_fraction}
    if not indices:
        raise ValueError("selection contains no data rows")

    metrics = {}
    for name in columns:
        values = [rows[i][name] for i in indices]
        metrics[name] = {"first": values[0], "last": values[-1],
                         "min": min(values), "max": max(values),
                         "mean": statistics.fmean(values)}
    return {
        "source": str(path), "rows_total": len(rows), "rows_selected": len(indices),
        "first_data_row_1based": indices[0] + 1,
        "last_data_row_1based": indices[-1] + 1,
        "selection": selection,
        "physical_time_first": times[indices[0]] if times else None,
        "physical_time_last": times[indices[-1]] if times else None,
        "columns": metrics,
        "interpretation": "Descriptive values only; convergence and physical validity are not assessed.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_path", type=Path)
    parser.add_argument("--columns", nargs="+", default=["cd", "cl"])
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--tail-fraction", type=float, help="fraction of final rows, default 0.1")
    group.add_argument("--start-time", type=float, help="include physical_time >= this value")
    args = parser.parse_args()
    tail = args.tail_fraction if args.tail_fraction is not None else (None if args.start_time is not None else 0.1)
    try:
        result = summarize(args.csv_path, args.columns, tail, args.start_time)
    except (OSError, UnicodeError, csv.Error, ValueError) as exc:
        parser.exit(2, f"error: {exc}\n")
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
