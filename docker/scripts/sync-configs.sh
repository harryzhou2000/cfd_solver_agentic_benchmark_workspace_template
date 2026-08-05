#!/usr/bin/env bash
# Regenerate docker/configs from the live host environment, redacting secrets.
# Run from the repo root. Never commit unredacted credentials.
#
# Optional --env-mode: instead of leaving apiKeys as REDACTED, rewrite opencode
# config apiKeys to "{env:OPENCODE_API_KEY}" placeholders so the baked image
# can be used by exporting OPENCODE_API_KEY at runtime. Codex/opencodex keys
# stay mount-only (their configs have no env placeholder support).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

ENV_MODE=0
for arg in "$@"; do
  case "$arg" in
    --env-mode) ENV_MODE=1 ;;
    *) echo "unknown option: $arg" >&2; exit 1 ;;
  esac
done

D="$ROOT/docker/configs"
OCONF="$HOME/.config/opencode"
TOOLS="/mnt/ssd-SATARAID5/harry/tools/opencode_config/opencode"

mkdir -p "$D/opencode" "$D/codex" "$D/opencodex"

echo "== copying opencode configs =="
cp "$OCONF/opencode.jsonc"          "$D/opencode/opencode.jsonc"
cp "$OCONF/oh-my-opencode-slim.json" "$D/opencode/"
cp "$OCONF/tui.json"                "$D/opencode/"
cp "$OCONF/package.json" "$OCONF/package-lock.json" "$D/opencode/"
[ -e "$OCONF/.oh-my-opencode-slim" ] && cp -a "$OCONF/.oh-my-opencode-slim" "$D/opencode/"
for d in agents commands mcp rules skills themes; do
  [ -e "$TOOLS/$d" ] && cp -a "$TOOLS/$d" "$D/opencode/"
done
[ -e "$TOOLS/cost-guard.config.jsonc" ] && cp -a "$TOOLS/cost-guard.config.jsonc" "$D/opencode/"
[ -e "$TOOLS/tui.jsonc" ] && cp -a "$TOOLS/tui.jsonc" "$D/opencode/"
[ -e "$TOOLS/opencode.jsonc" ] && cp -a "$TOOLS/opencode.jsonc" "$D/opencode/opencode.jsonc.aux"

echo "== copying codex configs =="
for f in config.toml opencodex.config.toml ocx.config.toml opencodex-catalog.json AGENTS.md; do
  [ -e "$HOME/.codex/$f" ] && cp -a "$HOME/.codex/$f" "$D/codex/"
done

echo "== copying opencodex config =="
[ -e "$HOME/.opencodex/config.json" ] && cp -a "$HOME/.opencodex/config.json" "$D/opencodex/"

echo "== redacting secrets =="
find "$D" -type f \( -name '*.json' -o -name '*.jsonc' -o -name '*.toml' -o -name '*.md' -o -name '*.txt' \) \
  -exec sed -i -E \
    -e 's#("apiKey"[[:space:]]*:[[:space:]]*")[^"]*#\1REDACTED#g' \
    -e 's#("key"[[:space:]]*:[[:space:]]*")[^"]*#\1REDACTED#g' \
    -e 's#(sk-[A-Za-z0-9_-]{16,})#sk-REDACTED#g' \
    {} +

if [ "$ENV_MODE" = "1" ]; then
  echo "== converting opencode apiKeys to {env:OPENCODE_API_KEY} placeholders =="
  find "$D/opencode" -type f \( -name '*.json' -o -name '*.jsonc' \) \
    -exec sed -i -E \
      -e 's#("apiKey"[[:space:]]*:[[:space:]]*")REDACTED(")#\1{env:OPENCODE_API_KEY}\2#g' \
      {} +
fi

echo "== verifying no secrets remain =="
python3 - "$D" <<'EOF'
import re, sys
from pathlib import Path

root = Path(sys.argv[1])
bad = []
sk = re.compile(r"sk-[A-Za-z0-9_-]{16,}")
api = re.compile(r'"apiKey"\s*:\s*"')
key = re.compile(r'"key"\s*:\s*"')

for p in root.rglob("*"):
    if not p.is_file() or p.suffix not in (".json", ".jsonc", ".toml", ".md", ".txt"):
        continue
    text = p.read_text(errors="replace")
    for m in sk.finditer(text):
        bad.append(f"{p}: sk- key: {m.group(0)[:24]}...")
    for m in api.finditer(text):
        val = text[m.end():].split('"', 1)[0]
        if val != "REDACTED" and not val.startswith("{env:"):
            bad.append(f"{p}: apiKey={val[:24]}...")
    if "opencodex" in p.parts:
        for m in key.finditer(text):
            val = text[m.end():].split('"', 1)[0]
            if val != "REDACTED" and not val.startswith("{env:"):
                bad.append(f"{p}: key={val[:24]}...")

if bad:
    print("ERROR: unredacted secrets found:", file=sys.stderr)
    for line in bad:
        print("  " + line, file=sys.stderr)
    sys.exit(1)
print("OK: configs redacted and committed-safe")
EOF
echo "OK: configs redacted and committed-safe"
