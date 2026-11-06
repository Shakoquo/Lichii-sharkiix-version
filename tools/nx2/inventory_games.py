#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Inventory a game library read-only, without opening key files or launching games."""

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import struct
import sys


def read_at(stream, offset, size, length):
    if offset < 0 or size < 0 or offset + size > length:
        raise ValueError("metadata extent outside file")
    stream.seek(offset)
    data = stream.read(size)
    if len(data) != size:
        raise ValueError("truncated metadata")
    return data


def npdm_info(path):
    data = path.read_bytes()
    if len(data) < 128 or data[:4] != b"META":
        raise ValueError("invalid NPDM header")
    aci, size = struct.unpack_from("<II", data, 0x70)
    if size < 24 or aci + size > len(data) or data[aci:aci + 4] != b"ACI0":
        raise ValueError("invalid NPDM ACI0 extent")
    return {"path": str(path), "title_id": f"{struct.unpack_from('<Q', data, aci + 16)[0]:016X}",
            "process_name": data[32:48].split(b"\0")[0].decode("utf-8", errors="replace")}


def nup_info(path):
    length = path.stat().st_size
    with path.open("rb") as stream:
        header = read_at(stream, 0, 128, length)
        if header[:4] != b"NUP\x01":
            raise ValueError("invalid NUP magic")
        toc_offset, toc_size = struct.unpack_from("<QQ", header, 8)
        toc = read_at(stream, toc_offset, 48, length)
        magic, count = struct.unpack_from("<II", toc)
        if magic != 0x01434F54 or count > 1024 or 48 + count * 64 > toc_size:
            raise ValueError("invalid NUP package table")
        packages = []
        for index in range(count):
            entry = read_at(stream, toc_offset + 48 + index * 64, 64, length)
            title_id, = struct.unpack_from("<Q", entry)
            version, = struct.unpack_from("<I", entry, 20)
            meta_offset, meta_size = struct.unpack_from("<QI", entry, 32)
            package = {"title_id": f"{title_id:016X}", "version": version, "kind": entry[24]}
            cursor = meta_offset
            end = meta_offset + meta_size
            if end > length:
                raise ValueError("invalid NUP metadata extent")
            while cursor < end:
                tag, reserved, size = struct.unpack("<HHI", read_at(stream, cursor, 8, end))
                cursor += 8
                if cursor + size > end:
                    raise ValueError("invalid NUP metadata field")
                if tag in (4, 5) and size <= 65536:
                    text = read_at(stream, cursor, size, end).rstrip(b"\0").decode("utf-8", errors="replace")
                    package["display_version" if tag == 4 else "title_name"] = text
                cursor += size
            packages.append(package)
    return {"path": str(path), "packages": packages}


def inventory(root):
    games = []
    for directory in sorted(root.iterdir()):
        if directory.is_file() and directory.suffix.lower() == ".nup":
            game = {"directory": str(directory), "name": directory.name, "programs": [],
                    "packages": [], "candidates": [], "candidate_extensions": {}, "errors": []}
            try:
                game["packages"].append(nup_info(directory))
            except (OSError, ValueError, struct.error) as exc:
                game["errors"].append({"path": str(directory), "error": str(exc)})
            games.append(game)
            continue
        if not directory.is_dir():
            continue
        game = {"directory": str(directory), "name": directory.name, "programs": [],
                "packages": [], "candidates": [], "errors": []}
        extensions = Counter()
        print(f"Scanning {directory.name}", file=sys.stderr, flush=True)
        for parent, directories, files in os.walk(directory):
            directories.sort()
            shader_directory = "shader" in os.path.relpath(parent, directory).lower()
            for filename in sorted(files):
                name = filename.lower()
                if name.startswith(".") or name in ("prod.keys", "title.keys", "dev.keys", "keys.rar"):
                    continue
                path = Path(parent, filename)
                try:
                    if name == "main.npdm":
                        game["programs"].append(npdm_info(path))
                    elif name.endswith(".nup"):
                        game["packages"].append(nup_info(path))
                    if (shader_directory or "shader" in name or
                            name.endswith((".bfsha", ".bfsha.zs", ".bnsh", ".shdr", ".zip", ".pak", ".utoc"))):
                        extensions[path.suffix.lower()] += 1
                        game["candidates"].append({"path": str(path), "size": path.stat().st_size})
                except (OSError, ValueError, struct.error) as exc:
                    game["errors"].append({"path": str(path), "error": str(exc)})
        game["candidate_extensions"] = dict(extensions)
        games.append(game)
    return games


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.output.resolve()
    if output == root or root in output.parents:
        parser.error("output must be outside the read-only library")
    result = inventory(root)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    for game in result:
        ids = sorted({p["title_id"] for p in game["programs"]} |
                     {p["title_id"] for archive in game["packages"] for p in archive["packages"]})
        print(json.dumps({"name": game["name"], "ids": ids,
                          "candidates": len(game["candidates"]),
                          "extensions": game["candidate_extensions"], "errors": game["errors"]}))


if __name__ == "__main__":
    main()
