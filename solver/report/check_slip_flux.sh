#!/bin/sh
# Build and run the slip-wall flux probe against the solver's own library.
#
# This links the shipped libcns2d_core.a, so the numbers it prints are a property
# of the code that produced the submitted results rather than of a
# reimplementation.  Writes only into a temporary directory.
set -e
cd "$(dirname "$0")"

PROBE_DIR="$(mktemp -d)"
trap 'rm -rf "$PROBE_DIR"' EXIT

g++ -std=c++17 -O2 -I../src slip_flux_probe.cpp \
    -o "$PROBE_DIR/slip_flux_probe" \
    ../build/libcns2d_core.a -lm

"$PROBE_DIR/slip_flux_probe"
