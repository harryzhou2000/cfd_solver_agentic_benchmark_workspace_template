#!/usr/bin/env python3
"""Assemble rank-local VTK pieces named by ``field_final.pvtu`` into a VTU.

The rank files are parsed only for validation.  Their ``<Piece>`` elements are
then copied as their original XML text, rather than being serialized again, so
the field arrays and their formatting are not changed by this utility.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path, PurePosixPath
import sys
import xml.etree.ElementTree as ET


class AssembleError(RuntimeError):
    """A supplied PVTU manifest or rank piece is unsuitable for assembly."""


def local_name(tag: str) -> str:
    """Return an XML tag's local name without accepting a changed namespace."""
    return tag.rsplit("}", 1)[-1]


def read_xml(path: Path, label: str) -> tuple[ET.Element, str]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise AssembleError(f"cannot read {label} {path}: {exc}") from exc
    except UnicodeDecodeError as exc:
        raise AssembleError(f"{label} {path} is not UTF-8 XML") from exc
    try:
        return ET.fromstring(text), text
    except ET.ParseError as exc:
        raise AssembleError(f"invalid XML in {label} {path}: {exc}") from exc


def direct_children(element: ET.Element, name: str) -> list[ET.Element]:
    return [child for child in element if local_name(child.tag) == name]


def validate_vtk_root(root: ET.Element, vtk_type: str, label: str) -> ET.Element:
    if local_name(root.tag) != "VTKFile" or root.attrib.get("type") != vtk_type:
        raise AssembleError(f"{label} is not a VTKFile of type {vtk_type}")
    grids = direct_children(root, vtk_type)
    if len(grids) != 1:
        raise AssembleError(f"{label} must contain exactly one {vtk_type} element")
    return grids[0]


def safe_piece_path(output_dir: Path, source: str) -> Path:
    """Resolve one PVTU ``Source`` without allowing a path escape."""
    if not source or "\x00" in source or "\\" in source:
        raise AssembleError(f"unsafe Piece Source {source!r}")
    posix_path = PurePosixPath(source)
    if (source != str(posix_path) or posix_path.is_absolute() or
            any(part in {"", ".", ".."} for part in posix_path.parts)):
        raise AssembleError(f"unsafe Piece Source {source!r}")
    candidate = (output_dir / Path(*posix_path.parts)).resolve()
    try:
        candidate.relative_to(output_dir)
    except ValueError as exc:
        raise AssembleError(f"Piece Source escapes the output directory: {source!r}") from exc
    if not candidate.is_file():
        raise AssembleError(f"missing Piece Source {source!r}")
    return candidate


def xml_tokens(text: str):
    """Yield lexical XML tags while respecting quoted attributes and comments."""
    position = 0
    while True:
        start = text.find("<", position)
        if start < 0:
            return
        if text.startswith("<!--", start):
            end = text.find("-->", start + 4)
            if end < 0:
                raise AssembleError("unterminated XML comment")
            position = end + 3
            continue
        if text.startswith("<![CDATA[", start):
            end = text.find("]]>", start + 9)
            if end < 0:
                raise AssembleError("unterminated XML CDATA")
            position = end + 3
            continue
        quote: str | None = None
        index = start + 1
        while index < len(text):
            character = text[index]
            if quote is not None:
                if character == quote:
                    quote = None
            elif character in {"'", '"'}:
                quote = character
            elif character == ">":
                break
            index += 1
        if index == len(text):
            raise AssembleError("unterminated XML tag")
        body = text[start + 1:index].strip()
        position = index + 1
        if not body or body[0] in {"?", "!"}:
            continue
        closing = body.startswith("/")
        if closing:
            body = body[1:].lstrip()
        self_closing = body.rstrip().endswith("/")
        name = body.rstrip("/").split(None, 1)[0]
        yield start, index + 1, name, closing, self_closing


def raw_piece_xml(piece_path: Path) -> str:
    root, text = read_xml(piece_path, "rank piece")
    grid = validate_vtk_root(root, "UnstructuredGrid", f"rank piece {piece_path}")
    if len(direct_children(grid, "Piece")) != 1:
        raise AssembleError(f"rank piece {piece_path} must contain exactly one direct Piece")

    starts: list[tuple[int, int]] = []
    ends: list[tuple[int, int]] = []
    for start, end, name, closing, self_closing in xml_tokens(text):
        if name != "Piece":
            continue
        if self_closing:
            raise AssembleError(f"rank piece {piece_path} has a self-closing Piece")
        if closing:
            ends.append((start, end))
        else:
            starts.append((start, end))
    if len(starts) != 1 or len(ends) != 1 or starts[0][0] >= ends[0][0]:
        raise AssembleError(f"rank piece {piece_path} does not have one extractable Piece XML block")
    return text[starts[0][0]:ends[0][1]]


def assemble(output_dir: Path, output: Path) -> int:
    source_dir = output_dir.resolve()
    if not source_dir.is_dir():
        raise AssembleError(f"output directory does not exist: {output_dir}")
    pvtu_path = source_dir / "field_final.pvtu"
    pvtu_root, _ = read_xml(pvtu_path, "PVTU manifest")
    grid = validate_vtk_root(pvtu_root, "PUnstructuredGrid", f"PVTU manifest {pvtu_path}")
    manifest_pieces = direct_children(grid, "Piece")
    if not manifest_pieces:
        raise AssembleError(f"PVTU manifest {pvtu_path} has no Piece entries")

    source_paths: list[Path] = []
    for piece in manifest_pieces:
        if set(piece.attrib) != {"Source"} or list(piece):
            raise AssembleError("each PVTU Piece must be an empty element with only a Source attribute")
        source_paths.append(safe_piece_path(source_dir, piece.attrib["Source"]))
    if len(set(source_paths)) != len(source_paths):
        raise AssembleError("PVTU manifest names the same rank piece more than once")

    target = output.expanduser().resolve()
    if target.suffix.lower() != ".vtu":
        raise AssembleError(f"output must have a .vtu suffix: {target}")
    if not target.parent.is_dir():
        raise AssembleError(f"output parent directory does not exist: {target.parent}")
    if target == pvtu_path or target in source_paths:
        raise AssembleError("output path would overwrite an input XML file")
    if target.exists() or target.is_symlink():
        raise AssembleError(f"refusing to overwrite existing output: {target}")

    pieces = [raw_piece_xml(path) for path in source_paths]
    try:
        with target.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write('<?xml version="1.0"?>\n<VTKFile type="UnstructuredGrid" version="0.1" byte_order="LittleEndian">\n')
            stream.write("<UnstructuredGrid>\n")
            for piece in pieces:
                stream.write(piece)
                stream.write("\n")
            stream.write("</UnstructuredGrid>\n</VTKFile>\n")
    except OSError as exc:
        try:
            target.unlink(missing_ok=True)
        except OSError:
            pass
        raise AssembleError(f"cannot write output {target}: {exc}") from exc
    print(f"assembled {len(pieces)} rank pieces into {target}")
    return len(pieces)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_directory", type=Path,
                        help="directory containing field_final.pvtu and its rank-local VTU files")
    parser.add_argument("--output", type=Path, default=None,
                        help="destination VTU (default: <output_directory>/field_final.vtu; must not already exist)")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    output = args.output if args.output is not None else args.output_directory / "field_final.vtu"
    try:
        assemble(args.output_directory, output)
    except AssembleError as exc:
        print(f"assemble_pvtu: error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
