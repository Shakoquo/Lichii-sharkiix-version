#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

"""Check that direct Vulkan command and submission calls have host timing scopes."""

import pathlib
import re
import sys


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    pattern = re.compile(
        r"\b(?:dld->|dld\.)(vkCmd\w+|vkBeginCommandBuffer|vkEndCommandBuffer|vkQueueSubmit2?)\("
    )
    count = 0
    missing = []
    for path in (root / "src/video_core").rglob("*"):
        if path.suffix not in (".h", ".cpp"):
            continue
        lines = path.read_text().splitlines()
        for index, line in enumerate(lines):
            match = pattern.search(line)
            if not match:
                continue
            count += 1
            phase = "Submit" if match[1].startswith("vkQueueSubmit") else "Encode"
            expected = f"VideoCommon::HostGpuProfile::Phase::{phase}}};"
            if index == 0 or lines[index - 1].strip() != expected:
                missing.append(f"{path.relative_to(root)}:{index + 1}: {match[1]}")
    if missing:
        print("Missing host timing scopes:\n" + "\n".join(missing), file=sys.stderr)
        return 1
    if count == 0:
        print("No Vulkan calls found", file=sys.stderr)
        return 1
    print(f"Verified {count} Vulkan recording/submission call sites")
    return 0


if __name__ == "__main__":
    sys.exit(main())
