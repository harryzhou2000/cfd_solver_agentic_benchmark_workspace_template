#!/usr/bin/env bash
# Install the pinned opencode plugins for the benchmark image.
#
# Uses opencode's official plugin installer (`opencode plugin -g <spec>`),
# which downloads the package from its official source (npm or GitHub),
# installs it into the global plugin store (~/.cache/opencode/packages) and
# records it in the global config (~/.config/opencode/opencode.jsonc).
#
# Every plugin is pinned in the manifest (docker/opencode-plugins.json) to a
# version or commit. Nothing is read or copied from the host.
set -euo pipefail

MANIFEST="/opt/opencode-plugins.json"
while [ $# -gt 0 ]; do
  case "$1" in
    --manifest) MANIFEST="$2"; shift 2 ;;
    -h|--help)
      echo "usage: install-opencode-plugins [--manifest FILE]" >&2
      exit 0
      ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

if [ ! -f "$MANIFEST" ]; then
  echo "ERROR: plugin manifest not found: $MANIFEST" >&2
  exit 1
fi

SPECS="$(jq -r '.plugins[] | .spec' "$MANIFEST")"
if [ -z "$SPECS" ]; then
  echo "ERROR: manifest $MANIFEST has no .plugins[] entries" >&2
  exit 1
fi

while IFS= read -r spec; do
  echo "== opencode plugin -g $spec"
  opencode plugin -g "$spec"
done <<< "$SPECS"

echo "== plugins installed from manifest $MANIFEST"
jq -r '.plugins[] | "- " + .id + "  (" + .spec + ")"' "$MANIFEST"
