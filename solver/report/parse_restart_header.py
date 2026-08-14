#!/usr/bin/env python3
"""Parse the CFDRST restart framing header (magic, version, fingerprint,
case ID, executable SHA-256, cell count, step, time, continuation size)
and report sha256 of the file and payload framing checks."""

import hashlib
import struct
import sys


def read_string(f, label):
    (length,) = struct.unpack("<Q", f.read(8))
    if length > 1024 * 1024:
        raise RuntimeError(f"{label} too large: {length}")
    value = f.read(length).decode("utf-8")
    return value


def parse(path):
    with open(path, "rb") as f:
        data = f.read()
    f = open(path, "rb")
    magic = f.read(8)
    (version,) = struct.unpack("<I", f.read(4))
    fingerprint = read_string(f, "fingerprint")
    case_id = read_string(f, "case ID")
    executable_sha256 = read_string(f, "executable SHA-256")
    (count,) = struct.unpack("<Q", f.read(8))
    (step,) = struct.unpack("<Q", f.read(8))
    (time,) = struct.unpack("<d", f.read(8))
    (continuation_size,) = struct.unpack("<Q", f.read(8))
    f.read(continuation_size)
    rest = f.read()
    f.close()
    return {
        "magic": magic.decode("utf-8", "replace"),
        "version": version,
        "fingerprint": fingerprint,
        "case_id": case_id,
        "executable_sha256": executable_sha256,
        "cell_count": count,
        "step": step,
        "physical_time": time,
        "continuation_size": continuation_size,
        "trailing_bytes": len(rest),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


if __name__ == "__main__":
    for path in sys.argv[1:]:
        info = parse(path)
        print(path)
        for k, v in info.items():
            print(f"  {k} = {v}")
