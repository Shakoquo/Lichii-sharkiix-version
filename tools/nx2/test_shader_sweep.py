#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

import struct
import unittest

import shader_sweep as sweep


def environment(version, stage):
    code = bytes(88)
    counts = [len(code), 1, 1, 1]
    if version >= 47:
        counts.append(1)
    counts.append(1)
    header = struct.pack("<" + "Q" * len(counts) + "7I", *counts, 0, 2, 0, 0, 80, 1, stage)
    context = bytes(8 + 8 + 12 + (9 if version >= 47 else 0) + 12)
    tail = bytes(16 if stage == 5 else 80 + (32 if stage == 3 else 0))
    return header + code + context + tail


def pipeline_cache(version):
    data = b"yuzucach" + struct.pack("<I", version)
    data += struct.pack("<I", 3)
    data += environment(version, 0) + environment(version, 3) + environment(version, 4)
    data += bytes(sweep.GRAPHICS_KEY_SIZE)
    data += struct.pack("<I", 1) + environment(version, 5) + bytes(24)
    return data


class CacheTests(unittest.TestCase):
    def test_both_versions_preserve_records_and_stage(self):
        for version in (46, 47, 48, 49, 50):
            records = list(sweep.parse_cache(pipeline_cache(version)))
            self.assertEqual([r["stage"] for r in records], ["vertex", "geometry", "fragment", "compute"])
            for record, stage in zip(records, (0, 3, 4, 5)):
                self.assertEqual(record["bytes"], environment(version, stage))
                self.assertEqual(record["version"], version)
                self.assertEqual(len(record["key"]), 24 if stage == 5 else 1056)

    def test_truncation_is_not_an_empty_success(self):
        data = pipeline_cache(47)
        for length in (0, 7, 11, 13, 50, len(data) - 1):
            with self.subTest(length=length), self.assertRaises(sweep.CacheError):
                list(sweep.parse_cache(data[:length]))

    def test_unknown_version_and_environment_count_rejected(self):
        for data in (b"yuzucach" + struct.pack("<I", 51),
                     b"yuzucach" + struct.pack("<II", 47, 7)):
            with self.assertRaises(sweep.CacheError):
                list(sweep.parse_cache(data))

    def test_native_code_matching_excludes_header_scheduling_padding_and_map(self):
        words = [0] * 10 + [11, 12, 0, 13, 14, 15, 0, 16, 0, 80, 88, 2, sweep.MAP_MAGIC]
        code = struct.pack("<" + "Q" * len(words), *words)
        self.assertEqual(sweep.unpack_native_code(code), struct.pack("<6Q", 11, 12, 13, 14, 15, 16))

    def test_maxwell_without_address_map_does_not_match_native(self):
        self.assertIsNone(sweep.unpack_native_code(bytes(128)))


if __name__ == "__main__":
    unittest.main()
