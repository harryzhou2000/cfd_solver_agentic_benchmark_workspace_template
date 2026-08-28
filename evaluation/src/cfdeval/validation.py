"""Draft-07 JSON-schema subset validator used for format-checking evaluation
artifacts. Stdlib only; supports the schema features cfdeval uses (type
incl. unions, enum, numeric bounds, required, properties,
additionalProperties, items, $ref)."""

from __future__ import annotations

import json
import math
from pathlib import Path


def _type_ok(value, t) -> bool:
    if t == "object":
        return isinstance(value, dict)
    if t == "array":
        return isinstance(value, list)
    if t == "string":
        return isinstance(value, str)
    if t == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if t == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if t == "boolean":
        return isinstance(value, bool)
    if t == "null":
        return value is None
    return True


def validate(instance, schema, path: str = "$", errors: list[str] | None = None,
             root: dict | None = None) -> list[str]:
    """Validate `instance` against `schema`; return a list of error strings."""
    if errors is None:
        errors = []
    root = root if root is not None else schema
    if "$ref" in schema:
        ref = schema["$ref"].lstrip("#/")
        target = root
        for part in ref.split("/"):
            target = target.get(part)
        if target is None:
            errors.append(f"{path}: unresolvable $ref {schema['$ref']}")
            return errors
        validate(instance, target, path, errors, root)
        return errors
    if "type" in schema:
        types = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
        if not any(_type_ok(instance, t) for t in types):
            errors.append(f"{path}: expected type {schema['type']}, got "
                          f"{type(instance).__name__}")
            return errors
    if "enum" in schema and instance not in schema["enum"]:
        errors.append(f"{path}: value {instance!r} not in enum {schema['enum']}")
    if isinstance(instance, (int, float)) and not isinstance(instance, bool):
        if not math.isfinite(instance):
            errors.append(f"{path}: number must be finite")
        if "minimum" in schema and instance < schema["minimum"]:
            errors.append(f"{path}: value {instance!r} is below minimum {schema['minimum']}")
        if "maximum" in schema and instance > schema["maximum"]:
            errors.append(f"{path}: value {instance!r} exceeds maximum {schema['maximum']}")
    if isinstance(instance, dict):
        if "required" in schema:
            for key in schema["required"]:
                if key not in instance:
                    errors.append(f"{path}: missing required property {key!r}")
        props = schema.get("properties", {})
        for key, value in instance.items():
            if key in props:
                validate(value, props[key], f"{path}.{key}", errors, root)
            elif schema.get("additionalProperties") is False:
                errors.append(f"{path}: unexpected property {key!r}")
    if isinstance(instance, list) and "items" in schema:
        for i, item in enumerate(instance):
            validate(item, schema["items"], f"{path}[{i}]", errors, root)
    return errors


def validate_file(instance_path, schema_path) -> tuple[bool, list[str]]:
    """Validate a JSON artifact file against a schema file."""
    instance = json.loads(Path(instance_path).read_text())
    schema = json.loads(Path(schema_path).read_text())
    errors = validate(instance, schema)
    return (not errors, errors)


def check_cli(argv: list[str] | None = None) -> int:
    """`cfdeval check <folder>`: validate every artifact in a result folder
    against its schema and the index.json sha256 manifest."""
    import argparse
    import hashlib
    ap = argparse.ArgumentParser(description="Format-check an evaluation result folder")
    ap.add_argument("folder")
    ap.add_argument("--schemas", default=str(Path(__file__).resolve().parents[2] / "schemas"))
    args = ap.parse_args(argv)
    folder = Path(args.folder)
    schema_dir = Path(args.schemas)
    if not (folder / "index.json").exists():
        print(f"ERROR: {folder} is not a contract result folder (no index.json)")
        return 1
    index = json.loads((folder / "index.json").read_text())
    ok = True
    for name, meta in sorted(index.get("artifacts", {}).items()):
        from pathlib import PurePosixPath
        rel = PurePosixPath(name)
        if (not name or rel.is_absolute() or ".." in rel.parts
                or len(rel.parts) != 1 or rel.as_posix() != name):
            print(f"FAIL  {name}: artifact name is not a safe top-level filename")
            ok = False
            continue
        f = folder / name
        if not f.exists() or not f.is_file() or f.is_symlink():
            print(f"FAIL  {name}: artifact missing")
            ok = False
            continue
        if meta.get("bytes") is not None and meta.get("bytes") != f.stat().st_size:
            print(f"FAIL  {name}: byte count mismatch")
            ok = False
            continue
        digest = hashlib.sha256(f.read_bytes()).hexdigest()
        if meta.get("sha256") and digest != meta["sha256"]:
            print(f"FAIL  {name}: sha256 mismatch")
            ok = False
            continue
        kind = meta.get("artifact_kind", "json")
        schema_name = meta.get("schema")
        if kind == "binary":
            if schema_name is not None:
                print(f"FAIL  {name}: binary artifact must not declare a JSON schema")
                ok = False
                continue
            media_type = meta.get("media_type")
            if media_type != "application/pdf":
                print(f"FAIL  {name}: schema-less artifact has unsupported media type")
                ok = False
                continue
            from cfdeval.report_pdf import validate_pdf_bytes
            pdf_errors = validate_pdf_bytes(f.read_bytes())
            if pdf_errors:
                print(f"FAIL  {name} (application/pdf): {'; '.join(pdf_errors)}")
                ok = False
            else:
                print(f"OK    {name} (application/pdf)")
            continue
        if kind != "json":
            print(f"FAIL  {name}: unsupported artifact kind {kind!r}")
            ok = False
            continue
        if not isinstance(schema_name, str) or not schema_name:
            print(f"FAIL  {name}: JSON artifact must declare a schema")
            ok = False
            continue
        schema = schema_dir / schema_name
        if not schema.exists():
            print(f"FAIL  {name}: schema {schema_name} not found")
            ok = False
            continue
        valid, errors = validate_file(f, schema)
        if valid:
            print(f"OK    {name} ({meta.get('schema')})")
        else:
            ok = False
            print(f"FAIL  {name} ({meta.get('schema')}):")
            for e in errors[:10]:
                print(f"        {e}")
    return 0 if ok else 1
