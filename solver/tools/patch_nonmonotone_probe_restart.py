#!/usr/bin/env python3
"""Upgrade a copied CFDRST11 checkpoint for a bounded nonmonotone probe."""

from __future__ import annotations

import argparse
import csv
import hashlib
from pathlib import Path
import re
import struct


ACCEPTED = re.compile(
    r"\bstep=(\d+)\b.*\baccepted=yes\b.*\bresidual=([^ ]+)\b"
    r".*\breconstruction_blend=1(?:\s|$)"
)


def read_u64(data: bytes | bytearray, offset: int) -> tuple[int, int]:
    return struct.unpack_from("<Q", data, offset)[0], offset + 8


def skip_string(data: bytes | bytearray, offset: int) -> tuple[int, int, int]:
    size, offset = read_u64(data, offset)
    start = offset
    end = start + size
    if end > len(data):
        raise ValueError("truncated restart string")
    return start, end, end


def continuation_total_attempts_offset(continuation: bytearray) -> int:
    offset = 2 * 8
    offset += struct.calcsize("<QiidQdd")
    string_size, offset = read_u64(continuation, offset)
    offset += string_size
    offset += 2  # transient-target and history-complete flags
    offset += 8  # rollbacks
    if offset + 8 > len(continuation):
        raise ValueError("truncated continuation before total-attempted count")
    return offset


def accepted_window(
    log_path: Path, residuals_path: Path, checkpoint_step: int
) -> list[float]:
    full_order_steps: set[int] = set()
    with log_path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            match = ACCEPTED.search(line)
            if match is None or int(match.group(1)) > checkpoint_step:
                continue
            residual = float(match.group(2))
            if residual < 0.0:
                raise ValueError("accepted residual is negative")
            full_order_steps.add(int(match.group(1)))

    samples: list[float] = []
    with residuals_path.open("r", encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream):
            step = int(row["step"])
            if step not in full_order_steps:
                continue
            residual = float(row["residual_l2"])
            if residual < 0.0:
                raise ValueError("accepted residual is negative")
            samples.append(residual)
    if len(samples) < 10:
        raise ValueError(
            "source log/residual CSV have fewer than ten accepted full-order residuals"
        )
    return samples[-10:]


def patch_restart(
    source: Path,
    destination: Path,
    executable: Path,
    log_path: Path,
    residuals_path: Path,
    total_attempts: int,
) -> None:
    data = bytearray(source.read_bytes())
    if data[:8] != b"CFDRST11" or struct.unpack_from("<I", data, 8)[0] != 11:
        raise ValueError("source must be a CFDRST11 version-11 checkpoint")

    offset = 12
    _, _, offset = skip_string(data, offset)  # compatibility fingerprint
    _, _, offset = skip_string(data, offset)  # case ID
    executable_start, executable_end, offset = skip_string(data, offset)
    if executable_end - executable_start != 64:
        raise ValueError("checkpoint executable hash is not SHA-256 sized")
    executable_hash = hashlib.sha256(executable.read_bytes()).hexdigest().encode()
    data[executable_start:executable_end] = executable_hash

    _, offset = read_u64(data, offset)  # cell count
    checkpoint_step, offset = read_u64(data, offset)
    offset += 8  # physical time
    continuation_size_offset = offset
    continuation_size, offset = read_u64(data, offset)
    continuation_start = offset
    continuation_end = continuation_start + continuation_size
    if continuation_end > len(data):
        raise ValueError("truncated continuation payload")
    continuation = bytearray(data[continuation_start:continuation_end])
    if not continuation or continuation[-1] != 1:
        raise ValueError("version-11 original-residual semantic marker is missing")
    if not checkpoint_step <= total_attempts:
        raise ValueError("total attempts cannot precede checkpoint step")
    struct.pack_into(
        "<Q", continuation,
        continuation_total_attempts_offset(continuation), total_attempts,
    )

    window = accepted_window(log_path, residuals_path, checkpoint_step)
    extension = bytearray(struct.pack("<Q", len(window)))
    extension.extend(struct.pack(f"<{len(window)}d", *window))
    extension.extend(
        struct.pack(
            "<QBBQQdQQ",
            3,  # strict-decrease stagnation streak: activation threshold
            0,  # bridge becomes active only after a qualifying accepted trial
            0,  # not watchdog-disabled
            0,  # accepted steps since strict best
            0,  # cumulative nonmonotone accepts
            0.0,
            0,  # strict-best improvements
            0,  # watchdog resets
        )
    )
    continuation.extend(extension)
    continuation.extend(struct.pack("<QQQdd", 0, 0, 0, -1.0, 1.0))
    continuation.extend(struct.pack("<dQd", -1.0, 0, 0.0))
    continuation.extend(
        struct.pack(
            "<BBddQQQQdddQQQ",
            0, 0, 0.1, -1.0, 0, 0, 0, 0, -1.0, -1.0, 0.0, 0, 0, 0,
        )
    )

    patched = bytearray(data[:continuation_size_offset])
    patched.extend(struct.pack("<Q", len(continuation)))
    patched.extend(continuation)
    patched.extend(data[continuation_end:])
    patched[:8] = b"CFDRST15"
    struct.pack_into("<I", patched, 8, 15)
    destination.write_bytes(patched)

    print(f"checkpoint_step={checkpoint_step}")
    print(f"patched_total_attempts={total_attempts}")
    print("window=" + ",".join(f"{value:.17g}" for value in window))
    print(f"window_reference={max(window):.17g}")
    print(f"executable_sha256={executable_hash.decode()}")


def carry_forward_rejected_attempts(
    source: Path, destination: Path, total_attempts: int
) -> None:
    """Copy a v12-v15 checkpoint while advancing its attempted-step count.

    This is used after an externally timed-out probe only when every attempt
    after the checkpoint was rejected, so the checkpoint solution is still the
    live solution. It prevents repeated work from exceeding the probe budget.
    """
    data = bytearray(source.read_bytes())
    version = struct.unpack_from("<I", data, 8)[0]
    if (data[:8], version) not in {
        (b"CFDRST12", 12),
        (b"CFDRST13", 13),
        (b"CFDRST14", 14),
        (b"CFDRST15", 15),
    }:
        raise ValueError("carry-forward source must be CFDRST12/13/14/15")
    offset = 12
    _, _, offset = skip_string(data, offset)
    _, _, offset = skip_string(data, offset)
    _, _, offset = skip_string(data, offset)
    _, offset = read_u64(data, offset)
    checkpoint_step, offset = read_u64(data, offset)
    offset += 8
    continuation_size, offset = read_u64(data, offset)
    continuation_end = offset + continuation_size
    if continuation_end > len(data):
        raise ValueError("truncated continuation payload")
    if total_attempts < checkpoint_step:
        raise ValueError("total attempts cannot precede checkpoint step")
    continuation = bytearray(data[offset:continuation_end])
    struct.pack_into(
        "<Q",
        continuation,
        continuation_total_attempts_offset(continuation),
        total_attempts,
    )
    if version == 12:
        continuation.extend(struct.pack("<QQQdd", 0, 0, 0, -1.0, 1.0))
    if version <= 13:
        continuation.extend(struct.pack("<dQd", -1.0, 0, 0.0))
    if version <= 14:
        continuation.extend(
            struct.pack(
                "<BBddQQQQdddQQQ",
                0, 0, 0.1, -1.0, 0, 0, 0, 0, -1.0, -1.0, 0.0, 0, 0, 0,
            )
        )
        data[:8] = b"CFDRST15"
        struct.pack_into("<I", data, 8, 15)
    data = (
        data[: offset - 8]
        + bytearray(struct.pack("<Q", len(continuation)))
        + continuation
        + data[continuation_end:]
    )
    destination.write_bytes(data)
    print(f"checkpoint_step={checkpoint_step}")
    print(f"carried_total_attempts={total_attempts}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--residuals", type=Path)
    parser.add_argument("--total-attempts", required=True, type=int)
    parser.add_argument("--carry-forward-rejected-attempts", action="store_true")
    args = parser.parse_args()
    if args.carry_forward_rejected_attempts:
        carry_forward_rejected_attempts(
            args.source, args.destination, args.total_attempts
        )
        return
    if args.executable is None or args.log is None or args.residuals is None:
        parser.error("v11 migration requires --executable, --log, and --residuals")
    patch_restart(
        args.source,
        args.destination,
        args.executable,
        args.log,
        args.residuals,
        args.total_attempts,
    )


if __name__ == "__main__":
    main()
