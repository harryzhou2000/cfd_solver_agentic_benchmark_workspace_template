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

mapfile -t SPECS < <(jq -r '.plugins[] | .spec' "$MANIFEST")
if [ "${#SPECS[@]}" -eq 0 ]; then
  echo "ERROR: manifest $MANIFEST has no .plugins[] entries" >&2
  exit 1
fi

# NOTE: `opencode plugin -g` reads stdin (it is an interactive TUI), so it
# would swallow the remaining specs if they were piped via a here-string.
# Iterate an array instead and give each install an empty stdin.
for spec in "${SPECS[@]}"; do
  echo "== opencode plugin -g $spec"
  opencode plugin -g "$spec" </dev/null
done

# opencode's plugin CLI can exit 0 without persisting the plugin (observed
# with npm specs on a flaky first build). Verify every spec is recorded in
# the global config and fail loudly if any is missing, so a partial install
# can never be baked into a cached layer.
CONFIG_FILE="$HOME/.config/opencode/opencode.jsonc"
echo "== verifying plugins recorded in $CONFIG_FILE =="
MISSING=0
for spec in "${SPECS[@]}"; do
  if [ -f "$CONFIG_FILE" ] && jq -e --arg s "$spec" '.plugin | index($s)' \
      "$CONFIG_FILE" >/dev/null 2>&1; then
    echo "OK   $spec"
  else
    echo "MISSING from global config: $spec" >&2
    MISSING=1
  fi
done
if [ "$MISSING" != "0" ]; then
  echo "ERROR: plugin install incomplete; inspect $CONFIG_FILE" >&2
  exit 1
fi

echo "== plugins installed from manifest $MANIFEST"
jq -r '.plugins[] | "- " + .id + "  (" + .spec + ")"' "$MANIFEST"
