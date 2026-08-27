"""Crude PDF text probe: confirm key phrases actually reached the rendered PDF."""
import re
import sys
import zlib

data = open(sys.argv[1], "rb").read()
chunks = []
for match in re.finditer(rb"stream\r?\n(.*?)endstream", data, re.S):
    try:
        chunks.append(zlib.decompress(match.group(1)).decode("latin-1"))
    except Exception:
        pass
text = " ".join(chunks)
shown = " ".join(re.findall(r"\((.*?)\)", text)).lower()
for probe in sys.argv[2:]:
    print(probe.ljust(20), "FOUND" if probe.lower() in shown else "MISSING")
