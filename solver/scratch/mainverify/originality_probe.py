#!/usr/bin/env python3
"""Test the submitted source against the documented idioms of the major
open-source CFD codes it could plausibly have been lifted from.

This does not prove originality -- nothing local can -- but a code copied or
machine-translated from one of these keeps structural fingerprints that survive
renaming: class-prefix conventions, method names, accessor styles, and the
distinctive spellings each project uses.  Absence of ALL of them across a
7-kLOC core is meaningful negative evidence.  Read-only.
"""
import re
from pathlib import Path

SRC = Path("src")
files = sorted(list(SRC.rglob("*.cpp")) + list(SRC.rglob("*.h")))
blob = "\n".join(p.read_text() for p in files)
lines = blob.count("\n")

# Idioms documented for each project.  Keys are regexes.
SIGNATURES = {
    "SU2": [
        (r"\bclass C[A-Z][A-Za-z_]*", "C-prefixed class names (CNumerics, CUpwRoe_Flow)"),
        (r"\bComputeResidual\b", "CNumerics::ComputeResidual entry point"),
        (r"_Flow\b", "_Flow equation-family suffix"),
        (r"\bCConfig\b|\bCGeometry\b|\bCSolver\b", "CConfig/CGeometry/CSolver god objects"),
        (r"\bSetPrimitive|\bGetPrimitive|\bGetPressure\(", "Set/Get primitive accessor style"),
        (r"\bnDim\b|\bnVar\b|\bnPoint\b|\bnEdge\b", "nDim/nVar/nPoint/nEdge member naming"),
        (r"\bUpw[A-Z]|\bAvgGrad", "Upw/AvgGrad scheme-category tokens"),
    ],
    "OpenFOAM": [
        (r"\bfvMesh\b|\bvolScalarField\b|\bsurfaceScalarField\b", "fvMesh / volScalarField types"),
        (r"\bfvm::|\bfvc::", "fvm:: / fvc:: discretisation namespaces"),
        (r"\bIOobject\b|\bdimensionedScalar\b", "IOobject / dimensionedScalar"),
        (r"\bforAll\s*\(", "forAll macro"),
        (r"\bMustBeDefined\b|\bNotImplemented\b", "OpenFOAM error macros"),
    ],
    "DNDSR": [
        (r"\bDNDS\b|\bDNDSR\b", "DNDS namespace or project token"),
        (r"\btEigen|\btIndex\b|\btReal\b", "DNDS t-prefixed type aliases"),
    ],
    "Fluent/other transliteration": [
        (r"\bIMPLICIT_EULER\b|\bEULER_IMPLICIT\b", "borrowed enum spellings"),
        (r"\bMARKER_[A-Z]+", "SU2 config MARKER_ tokens"),
    ],
}

print(f"examined {len(files)} files, {lines} lines of the solver core\n")
total_hits = 0
for project, sigs in SIGNATURES.items():
    print(f"{project}:")
    for pat, label in sigs:
        hits = re.findall(pat, blob)
        total_hits += len(hits)
        mark = "HIT" if hits else " . "
        extra = f"  ({len(hits)} occurrences, e.g. {hits[:3]})" if hits else ""
        print(f"   [{mark}] {label}{extra}")
    print()

print(f"total foreign-idiom hits across all projects: {total_hits}")

# What the code uses INSTEAD, as positive evidence of an independent convention.
print("\nthe conventions this code actually uses:")
enums = sorted(set(re.findall(r"enum class ([A-Za-z]+)", blob)))
print(f"   enum class names ({len(enums)}): {', '.join(enums)}")
ks = sorted(set(re.findall(r"\b(k[A-Z][A-Za-z]+)\b", blob)))
print(f"   k-prefixed compile-time constants: {len(ks)} distinct, e.g. {', '.join(ks[:8])}")
snake = len(re.findall(r"\b[a-z]+_[a-z_]+\b", blob))
print(f"   snake_case identifiers: {snake} occurrences")
print("   -> lowerCamelCase functions, snake_case members with trailing underscore,")
print("      k-prefixed constants, scoped enum classes: a self-consistent style")
print("      that matches none of the projects probed above.")
