#!/usr/bin/env python3
"""Which macros end up as \pending, and does the document actually use them?

numbers_auto.tex defines fallbacks first and overrides them with harvested
values, so only the LAST definition of each macro matters.  A pending macro is
harmless if nothing references it; it is a visible placeholder if anything does.
Read-only.
"""
import re
import sys
from pathlib import Path

REPORT = Path("report")

final = {}
for path in ("numbers.tex", "numbers_auto.tex"):
    p = REPORT / path
    if not p.exists():
        continue
    for line in p.read_text().splitlines():
        m = re.match(r"\\(?:def|newcommand|renewcommand)\\?\{?\\(cns[A-Za-z]+)\}?\{(.*)\}\s*$", line)
        if m:
            final[m.group(1)] = m.group(2)

pending = sorted(k for k, v in final.items() if "pending" in v)
print(f"macros with a final definition: {len(final)}")
print(f"still pending after overrides : {len(pending)}")

# every macro reference in the document, excluding the definition files
used = {}
for tex in sorted(REPORT.glob("*.tex")):
    if tex.name in ("numbers.tex", "numbers_auto.tex", "report_requirements.tex"):
        continue
    txt = tex.read_text()
    for m in re.finditer(r"\\(cns[A-Za-z]+)", txt):
        used.setdefault(m.group(1), []).append(tex.name)

print(f"distinct macros referenced in the document: {len(used)}")
print()
bad = [k for k in pending if k in used]
if bad:
    print("PENDING MACROS THAT ARE ACTUALLY USED -> visible placeholders:")
    for k in bad:
        print(f"   {k}  in {sorted(set(used[k]))}")
else:
    print("no pending macro is referenced anywhere in the document: no placeholder can render")
print()
print("the pending macros (all unused):")
for k in pending:
    print("   " + k)

undefined = sorted(k for k in used if k not in final)
print()
print(f"referenced but never defined: {len(undefined)}")
for k in undefined:
    print("   " + k, sorted(set(used[k])))
sys.exit(1 if bad or undefined else 0)
