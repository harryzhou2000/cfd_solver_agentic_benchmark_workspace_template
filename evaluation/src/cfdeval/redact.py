"""Credential redaction for captured configs and environment snapshots.

Redaction is conservative: anything that looks like a secret is replaced
with a marker, while sha256 digests, hex colors, and ordinary words survive.
"""

from __future__ import annotations

import json
import re


MARK = "***REDACTED***"

# Keys whose JSON values are secrets (matched case-insensitively on the key).
SECRET_KEYS = (
    "api_key", "apikey", "api-key", "access_token", "refresh_token",
    "auth_token", "token", "secret", "password", "passwd", "client_secret",
    "private_key", "credential", "credentials", "authorization",
)

_SECRET_KEY_VALUE = re.compile(
    r'("(?:[^"\\]|\\.)*?(?:' + "|".join(
        re.escape(k) for k in SECRET_KEYS
    ) + r')"\s*:\s*)(?:"(?:[^"\\]|\\.)*?"|[0-9]+|true|false|null)',
    re.IGNORECASE,
)
_BEARER = re.compile(r"(?i)\bBearer\s+[A-Za-z0-9._~+/=-]{8,}")
_SK_KEY = re.compile(r"\bsk-[A-Za-z0-9_\-]{8,}")
_ASSIGN = re.compile(
    r"(?i)\b(api[_-]?key|access[_-]?token|refresh[_-]?token|auth[_-]?token|"
    r"secret|password|passwd|token)\b\s*[:=]\s*[\"']?[A-Za-z0-9._~+/=-]{8,}",
)
_PROXY_CREDS = re.compile(r"(?i)(https?://)([^/\s:@]+):([^@/\s]+)@")
_LONG_B64 = re.compile(r"[A-Za-z0-9+/]{40,}={0,2}")


def redact_json_value(obj, key: str = "") -> object:
    """Recursively redact values whose key looks like a secret."""
    if isinstance(obj, dict):
        return {
            k: (MARK if k.lower() in SECRET_KEYS and isinstance(v, (str, int, float))
                else redact_json_value(v, k))
            for k, v in obj.items()
        }
    if isinstance(obj, list):
        return [redact_json_value(v, key) for v in obj]
    return obj


def redact_text(text: str) -> tuple[str, int]:
    """Redact secret-looking content in free text; return (text, hits)."""
    hits = 0

    def _sub(pat, repl):
        nonlocal hits
        text2, n = pat.subn(repl, text)
        hits += n
        return text2

    def _json_val(m):
        return m.group(1) + '"' + MARK + '"'

    text = _sub(_SECRET_KEY_VALUE, _json_val)
    text = _sub(_BEARER, "Bearer " + MARK)
    text = _sub(_SK_KEY, "sk-" + MARK)
    text = _sub(_ASSIGN, lambda m: m.group(1) + "=" + MARK)
    text = _sub(_PROXY_CREDS, lambda m: m.group(1) + m.group(2) + ":" + MARK + "@")
    text = _sub(_LONG_B64, MARK)
    return text, hits


def redact_document(text: str) -> dict:
    """Redact a config document, keeping JSON structure where possible.

    Returns {"content": str, "redacted": bool, "redaction_hits": int}.
    """
    stripped = text.strip()
    try:
        obj = json.loads(stripped)
    except json.JSONDecodeError:
        out, hits = redact_text(text)
        return {"content": out, "redacted": hits > 0, "redaction_hits": hits}
    redacted = redact_json_value(obj)
    dumped = json.dumps(redacted, indent=2)
    # JSON documents still need the text-level passes: secret-like values can
    # hide under arbitrary key names (e.g. apiKeyPool[].key = "sk-...").
    out, text_hits = redact_text(dumped)
    hits = out.count(MARK)
    return {"content": out, "redacted": hits > 0, "redaction_hits": hits}


def truncate(text: str, cap: int) -> tuple[str, bool]:
    if len(text) <= cap:
        return text, False
    return text[:cap] + f"\n... [truncated {len(text) - cap} bytes]", True
