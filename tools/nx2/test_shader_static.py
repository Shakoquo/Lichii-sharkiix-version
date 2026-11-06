# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import unittest

from sweep_static_shaders import artifact_path, compute_format, output_path


class StaticSweepTests(unittest.TestCase):
    def test_output_is_new_and_outside_protected_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "library"
            library.mkdir()
            self.assertEqual(output_path(root / "output", [library]), root / "output")
            for path in (root, library, library / "output"):
                with self.assertRaises(ValueError):
                    output_path(path, [library])

    def test_manifest_paths_are_portable_and_cannot_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            expected = root / "programs" / "compute.bin"
            self.assertEqual(artifact_path(root, "programs/compute.bin"), expected)
            self.assertEqual(artifact_path(root, "programs\\compute.bin"), expected)
            for name in ("../outside.bin", str(root.parent / "outside.bin"), "."):
                with self.assertRaises(ValueError):
                    artifact_path(root, name)

    def test_corrupt_cache_and_empty_corpus_are_not_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = root / "inputs"
            inputs.mkdir()
            manifest = inputs / "manifest.json"
            manifest.write_text(json.dumps({"programs": [], "errors": []}))
            cache = root / "cache" / "0400000000000000"
            cache.mkdir(parents=True)
            (cache / "vulkan.fork-v49.bin").write_bytes(b"broken cache")
            output = root / "output"
            result = subprocess.run([sys.executable, str(Path(__file__).with_name("sweep_static_shaders.py")),
                                     str(manifest), "--compiler", sys.executable,
                                     "--read-only-root", str(inputs),
                                     "--spirv-val", sys.executable, "--cache-directory", str(cache.parent),
                                     "--output", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stderr)
            summary = json.loads(result.stdout)
            self.assertEqual(summary["cache_errors"], 1)
            errors = json.loads((output / "archive-errors.json").read_text())
            self.assertTrue(any("no extracted shader" in error["error"] for error in errors))

    def test_compute_envelope_requires_control_version_stage_and_bounded_size(self):
        code = bytes(256)
        control = bytearray(224)
        control[:8] = bytes.fromhex("34127698c9000000")
        struct.pack_into("<I", control, 56, len(code))
        struct.pack_into("<I", control, 84, 5 << 16)
        self.assertTrue(compute_format(code, control))
        self.assertTrue(compute_format(code + bytes(128), control))
        for invalid in (control[:103], bytes(len(control))):
            self.assertFalse(compute_format(code, invalid))
        self.assertFalse(compute_format(code[:-1], control))
        self.assertFalse(compute_format(code[:-16], control))
        struct.pack_into("<I", control, 84, 7 << 16)
        self.assertFalse(compute_format(code, control))


if __name__ == "__main__":
    unittest.main()
