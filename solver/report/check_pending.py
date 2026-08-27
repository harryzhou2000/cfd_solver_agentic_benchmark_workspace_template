"""List macros that are still pending AND referenced in body prose.

A pending macro is fine in a table, where an empty row plainly means "not yet".
It is a problem in a sentence, because the sentence reads as though a value should
be there.  This separates the two so the remaining gaps can be judged.

    ../.venv/bin/python check_pending.py
"""

import glob
import re

REPORT_DIR = "/workspace/solver/report/"

# Macros are pre-defined to the pending marker and then overwritten by a later
# \def once the run has finished, so only the LAST definition of each name is the
# effective one.  Taking the first would report every pre-defined macro as
# pending and produce false alarms.
auto = open(REPORT_DIR + "numbers_auto.tex").read()
effective = {}
for name, value in re.findall(r"\\def\\(cns\w+)\{(.*)\}", auto):
    effective[name] = value
pending = {n for n, v in effective.items() if v.strip() == r"\pending"}

body_files = sorted(glob.glob(REPORT_DIR + "sec_*.tex")) + [REPORT_DIR + "report.tex"]
hits = {}
for path in body_files:
    text = open(path).read()
    for lineno, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("%"):
            continue
        for macro in re.findall(r"\\(cns\w+)", line):
            if macro in pending:
                hits.setdefault(macro, []).append((path.split("/")[-1], lineno))

print("pending macros defined:      %d" % len(pending))
print("pending AND used in prose:   %d" % len(hits))
for macro in sorted(hits):
    where = ", ".join("%s:%d" % w for w in hits[macro][:3])
    print("  %-44s %s" % (macro, where))
