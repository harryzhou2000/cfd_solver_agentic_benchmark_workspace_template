#!/usr/bin/env bash
# Build and run the standalone CGNS mesh probe.
#
#   ./build_probe.sh                 # build, then probe both benchmark meshes
#   ./build_probe.sh mesh.cgns ...   # build, then probe the given meshes
#
# Output goes to probe_out/raw_report.md (Markdown).
set -euo pipefail

CFD_EXT="${CFD_EXT:-/opt/external/cfd_externals/install}"
PROBE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$PROBE_DIR/probe_out"
MESH_DIR="/workspace/cfd_solver_agentic_benchmark/inputs/meshes"

mkdir -p "$OUT_DIR"

g++ -O2 -std=c++17 -Wall -Wextra \
    -I"$CFD_EXT/include" \
    "$PROBE_DIR/probe_cgns.cpp" -o "$PROBE_DIR/probe_cgns" \
    -L"$CFD_EXT/lib" -lcgns -lhdf5 -lz \
    -Wl,-rpath,"$CFD_EXT/lib"

echo "built $PROBE_DIR/probe_cgns"

if [ "$#" -gt 0 ]; then
  MESHES=("$@")
else
  MESHES=("$MESH_DIR/NACA0012_H2.cgns" "$MESH_DIR/CylinderB1.cgns")
fi

"$PROBE_DIR/probe_cgns" "${MESHES[@]}" > "$OUT_DIR/raw_report.md"
echo "wrote $OUT_DIR/raw_report.md"
