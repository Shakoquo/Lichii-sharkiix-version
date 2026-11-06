#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile


def serialize_environment(env):
    code = b"".join(struct.pack("<Q", int(word, 16)) for word in env["code"])
    fields = ("texture_types", "texture_formats", "cbuf_values", "storage_pointers", "replacements")
    data = struct.pack("<6Q7I", len(code), *(len(env[field]) for field in fields), *env["parameters"])
    data += code
    for field, fmt in zip(fields, ("<II", "<II", "<QI", "<QB", "<QI")):
        data += b"".join(struct.pack(fmt, *entry) for entry in env[field])
    return data + bytes.fromhex(env["tail"])


def main():
    parser = argparse.ArgumentParser(description="Compile and validate captured shader regression fixtures")
    parser.add_argument("compiler", type=Path)
    parser.add_argument("--fixture", default="*", help="fixture filename stem or glob")
    parser.add_argument("--spirv-val", default="spirv-val")
    args = parser.parse_args()
    fixtures = sorted((Path(__file__).parent / "fixtures").glob(args.fixture + ".json"))
    if not fixtures:
        parser.error("no matching captured fixtures")
    for path in fixtures:
        fixture = json.loads(path.read_text())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            env = root / "shader.env"
            key = root / "pipeline.key"
            output = root / "shader.spv"
            env.write_bytes(serialize_environment(fixture["environment"]))
            command = [str(args.compiler.resolve()), "--environment", str(env),
                       "--cache-version", str(fixture["cache_version"]), "--output", str(output)]
            if "pipeline_key" in fixture:
                key.write_bytes(bytes.fromhex(fixture["pipeline_key"]))
                command += ["--pipeline-key", str(key)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
            if compiled.returncode:
                raise RuntimeError(f"{path.stem}: compiler exit {compiled.returncode}\n"
                                   f"{compiled.stdout}{compiled.stderr}")
            result = json.loads(compiled.stdout)
            if result["status"] != "ok":
                raise RuntimeError(f"{path.stem}: {result}")
            validated = subprocess.run([args.spirv_val, "--target-env", "vulkan1.2", str(output)],
                                       capture_output=True, text=True, timeout=30)
            if validated.returncode:
                raise RuntimeError(f"{path.stem}: {validated.stdout}{validated.stderr}")
            print(f"PASS {path.stem}: {result['spirv_words']} SPIR-V words")


if __name__ == "__main__":
    main()
