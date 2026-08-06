#!/usr/bin/env bash
# Regenerate docker/configs from the live host environment, redacting secrets.
# Run from the repo root. Never commit unredacted credentials.
#
# Optional --env-mode: instead of leaving apiKeys as REDACTED, rewrite them to
# environment-variable references so the vendored stack can authenticate when
# the vars are exported at runtime (or supplied by start.sh --host-credentials):
#   - opencode  : "{env:OPENCODE_API_KEY_<PROVIDER>}" (opencode's native
#                 placeholder, per provider — e.g. OPENCODE_API_KEY_DEEPSEEK)
#   - opencodex : "$OPENCODEX_<PROVIDER>_API_KEY" (opencodex resolves $VAR /
#                 ${VAR} apiKeys from the environment on every request)
# Codex auth remains file-based (auth.json) or a custom provider's env_key.
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
  echo "== converting opencode apiKeys to per-provider {env:OPENCODE_API_KEY_<PROVIDER>} placeholders =="
  find "$D/opencode" -type f \( -name '*.json' -o -name '*.jsonc' \) \
    -exec python3 -c '
import re, sys
from pathlib import Path

KEY = re.compile(r"^(\s*\")([A-Za-z0-9_.-]+)(\"\s*:\s*\{)")
APIKEY = re.compile(r"^(\s*\"apiKey\"\s*:\s*\")REDACTED(\")")

def stem(name):
    return re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_").upper()

for arg in sys.argv[1:]:
    path = Path(arg)
    src = path.read_text()
    depth = 0
    in_providers = False
    provider_depth = 0
    provider = None
    out = []
    changed = []
    for line in src.splitlines(keepends=True):
        stripped = line.lstrip()
        if stripped.startswith("//"):
            out.append(line)
            continue
        m = KEY.match(line)
        if m:
            name = m.group(2)
            if in_providers and depth == provider_depth:
                provider = name
            if not in_providers and depth == 1 and name in ("provider", "providers"):
                in_providers = True
                provider_depth = depth + line.count("{") - line.count("}")
            out.append(line)
            depth += line.count("{") - line.count("}")
            if in_providers and depth < provider_depth:
                in_providers = False
                provider = None
            continue
        m = APIKEY.match(line)
        if m and provider:
            env = "OPENCODE_API_KEY_" + stem(provider)
            line = line[:m.end(1)] + "{env:" + env + "}" + line[m.end(2):]
            changed.append(f"{provider}.apiKey -> {env}")
        out.append(line)
        depth += line.count("{") - line.count("}")
        if in_providers and depth < provider_depth:
            in_providers = False
            provider = None
    path.write_text("".join(out))
    if changed:
        try:
            rel = path.relative_to(Path.cwd())
        except ValueError:
            rel = path
        print(f"  {rel}: " + ", ".join(changed))
' {} +

  echo "== converting opencodex provider apiKeys to env references =="
  python3 - "$D/opencodex/config.json" <<'EOF'
import json, re, sys
from pathlib import Path

path = Path(sys.argv[1])
cfg = json.loads(path.read_text())
changed = []

def env_ref(provider_id):
    stem = re.sub(r"[^A-Za-z0-9]+", "_", provider_id).strip("_").upper()
    return f"$OPENCODEX_{stem}_API_KEY"

def rewrite(value, provider_id):
    if isinstance(value, str) and value in ("REDACTED", "sk-REDACTED"):
        return env_ref(provider_id)
    return value

for pid, prov in (cfg.get("providers") or {}).items():
    if not isinstance(prov, dict):
        continue
    if "apiKey" in prov:
        nv = rewrite(prov["apiKey"], pid)
        if nv != prov["apiKey"]:
            prov["apiKey"] = nv
            changed.append(f"{pid}.apiKey")
    for i, entry in enumerate(prov.get("apiKeyPool") or []):
        if isinstance(entry, dict) and "key" in entry:
            nv = rewrite(entry["key"], pid)
            if nv != entry["key"]:
                entry["key"] = nv
                changed.append(f"{pid}.apiKeyPool[{i}]")

if changed:
    path.write_text(json.dumps(cfg, indent=2, ensure_ascii=False) + "\n")
    print("  rewrote:", ", ".join(changed))
else:
    print("  (no provider apiKeys to rewrite)")
EOF
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
        if val != "REDACTED" and not (val.startswith("{env:") or val.startswith("$")):
            bad.append(f"{p}: apiKey={val[:24]}...")
    if "opencodex" in p.parts:
        for m in key.finditer(text):
            val = text[m.end():].split('"', 1)[0]
            if val != "REDACTED" and not (val.startswith("{env:") or val.startswith("$")):
                bad.append(f"{p}: key={val[:24]}...")

if bad:
    print("ERROR: unredacted secrets found:", file=sys.stderr)
    for line in bad:
        print("  " + line, file=sys.stderr)
    sys.exit(1)
print("OK: configs redacted and committed-safe")
EOF
echo "OK: configs redacted and committed-safe"
