#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the NUP reader in scratch using a configured Windows build read-only."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--exporter-source", required=True, type=Path)
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("run with Windows Python after loading the configured clang-cl environment")
    build, source, work = (p.resolve() for p in (args.build_dir, args.source_dir, args.work_dir))
    if work == build or build in work.parents or work == source or source in work.parents:
        parser.error("work directory must be outside the configured source and build trees")
    commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    reference = next(row["command"] for row in commands
                     if row["file"].replace("\\", "/").endswith("/nup_container.cpp"))
    prefix = reference.rsplit(" -c -- ", 1)[0]
    compiler = re.match(r'"([^"]+)"', reference).group(1)
    libraries = None
    lines = (build / "build.ninja").read_text(encoding="utf-8").splitlines()
    for index, line in enumerate(lines):
        if line.startswith("build bin/eden-cli.exe:"):
            libraries = next(line.split(" = ", 1)[1] for line in lines[index + 1:]
                             if line.startswith("  LINK_LIBRARIES = ")).split()
            break
    if libraries is None:
        raise RuntimeError("configured eden-cli link dependencies were not found")
    libraries = [str(build / library) if "/" in library and not Path(library).is_absolute()
                 else library for library in libraries]
    work.mkdir(parents=True, exist_ok=True)
    sources = [args.exporter_source.resolve()] + [source / "src/core/file_sys" / path for path in (
        "nup_container.cpp", "vfs/vfs.cpp", "vfs/vfs_real.cpp", "vfs/vfs_vector.cpp", "vfs/vfs_offset.cpp")]
    objects = [work / f"source-{index}.obj" for index in range(len(sources))]

    def compile_one(index):
        command = re.sub(r"/Fo\S+", lambda _: "/Fo" + objects[index].as_posix(), prefix)
        command = re.sub(r"/Fd\S+", lambda _: "/Fd" + (work / f"source-{index}.pdb").as_posix(), command)
        command += ' /utf-8 -c -- "' + str(sources[index]) + '"'
        process = subprocess.run(command, cwd=work, capture_output=True, text=True)
        (work / f"compile-{index}.log").write_text(process.stdout + process.stderr, encoding="utf-8")
        if process.returncode:
            raise RuntimeError(f"compile failed for {sources[index].name}; see compile-{index}.log")
        print(f"Compiled {sources[index].name}", flush=True)

    with ThreadPoolExecutor(max_workers=12) as workers:
        list(workers.map(compile_one, range(len(sources))))
    executable = work / "nup-shader-export.exe"
    command = [compiler, *map(str, objects), *libraries, "/link", "/SUBSYSTEM:CONSOLE",
               "/MACHINE:X64", "/OUT:" + str(executable), "/PDB:" + str(work / "exporter.pdb"),
               "/ILK:" + str(work / "exporter.ilk")]
    process = subprocess.run(command, cwd=work, capture_output=True, text=True)
    (work / "link.log").write_text(process.stdout + process.stderr, encoding="utf-8")
    if process.returncode:
        raise RuntimeError("link failed; see link.log")
    print(executable)


if __name__ == "__main__":
    main()
