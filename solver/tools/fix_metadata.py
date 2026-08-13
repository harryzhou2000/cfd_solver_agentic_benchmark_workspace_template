#!/usr/bin/env python3
"""Repair metadata.json written by the intermediate binary (field shift in the
wall-pressure-smoothing metadata patch). The final binary writes correct JSON;
this repairs files produced by runs launched with the intermediate build."""
import json
import re
import sys


def repair(path):
    txt = open(path).read()
    if '"true_bdf2_inner_loop": "' in txt or '"true_bdf2_inner_loop": true' in txt:
        return False  # already the fixed format
    m = re.search(r'"true_bdf2_inner_loop":\s*(\w+)', txt)
    if not m:
        return False
    smoothing_raw = m.group(1)  # the value that landed in the bdf2 slot
    # The three preceding fields are shifted by one position.
    def field(name):
        mm = re.search(r'"%s":\s*("(?:[^"\\]|\\.)*"|[-+0-9.eE]+)' % name, txt)
        return mm.group(1) if mm else None
    order_used = field("spatial_order_used")      # actually bdf2 flag
    fallback = field("first_order_fallback")      # actually order_used
    smoothing = field("wall_pressure_smoothing")  # actually fallback text
    bdf2 = order_used == '"true"'
    # Replace the shifted block with the correct one.
    new_block = (
        f'  "spatial_order_used": {fallback},\n'
        f'  "first_order_fallback": {smoothing},\n'
        f'  "wall_pressure_smoothing": {json.dumps(smoothing_raw)},\n'
        f'  "true_bdf2_inner_loop": {"true" if bdf2 else "false"},'
    )
    pat = re.compile(
        r'  "spatial_order_used": .*?\n'
        r'  "first_order_fallback": .*?\n'
        r'  "wall_pressure_smoothing": .*?\n'
        r'  "true_bdf2_inner_loop": \w+,',
        re.S)
    txt2, n = pat.subn(new_block, txt, count=1)
    if n != 1:
        return False
    open(path, "w").write(txt2)
    json.loads(txt2)  # validate
    return True


if __name__ == "__main__":
    for p in sys.argv[1:]:
        try:
            print(p, "repaired" if repair(p) else "unchanged/ok")
        except Exception as e:
            print(p, "FAILED", e)
