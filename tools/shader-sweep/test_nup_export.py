# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


COMPRESSED_SHADER = bytes.fromhex(
    "28b52ffd8058c0912100f40000b073796e746865746963207368616465722062797465730100ceff339f4d"
    "4c000008790100fcff3910024c000008650100fcff3910024c000008730100fcff3910024c000008740100"
    "fcff3910024c0000086e0100fcff3910024c000008650100fcff3910024c000008200100fcff3910024c0000"
    "08610100fcff3910024c000008630100fcff3910024c000008680100fcff3910024c000008730100fcff3910"
    "024c000008790100fcff3910024c000008650100fcff3910024c000008730100fcff3910024c000008740100"
    "fcff3910024d0000086e0100bc91391002"
)
EXECUTABLE = None
WORK_DIR = None


def fixture(filename='Shader.bfsha', corrupt=False):
    payloads = [b'synthetic shader bytes' * 100000, b'not a candidate' * 200000, b'']
    names = ['root', 'Shaders', filename, 'readme.txt', 'empty.bnsh']
    pool = b''
    strings = []
    for name in names:
        encoded = name.encode()
        strings.append((len(pool), len(encoded)))
        pool += encoded
    packages = 3
    dirs = 2
    files = len(payloads)
    children = [1, dirs + 1, dirs, dirs + 2]
    toc_size = 0x30 + packages * 0x40 + dirs * 0x18 + files * 0x50 + len(children) * 4 + len(pool)
    blob = (0x80 + toc_size + 15) & ~15
    data = bytearray(blob)
    def put(offset, value, width=8):
        data[offset:offset+width] = value.to_bytes(width, 'little')
    data[:4] = b'NUP\x01'
    put(4, 0x80, 2)
    put(8, 0x80)
    put(16, toc_size)
    put(0x38, blob)
    put(0x48, packages, 4)
    data[0x80:0x84] = b'TOC\x01'
    put(0x84, packages, 4)
    put(0x88, dirs)
    put(0x90, files)
    put(0x98, toc_size - len(pool))
    put(0xa0, len(pool))
    pos = 0xb0 + packages * 0x40
    for i, (start, count) in enumerate([(0, 2), (2, 2)]):
        off, size = strings[i]
        put(pos, off, 4)
        put(pos + 4, size, 4)
        put(pos + 8, start, 4)
        put(pos + 12, count, 4)
        pos += 0x18
    for i, payload in enumerate(payloads):
        off, size = strings[i+2]
        put(pos, off, 4)
        put(pos + 4, size, 4)
        put(pos + 8, len(payload))
        encoded = (b'broken zstd' if corrupt else COMPRESSED_SHADER) if i == 0 else payload
        if payload:
            put(pos + 16, len(data))
            put(pos + 24, len(encoded))
            data += encoded
            data += b'\x00' * (-len(data) % 16)
        data[pos + 32:pos + 64] = hashlib.sha256(payload).digest()
        put(pos + 64, int(i == 0), 1)
        pos += 0x50
    for child in children:
        put(pos, child, 4)
        pos += 4
    data[pos:pos+len(pool)] = pool
    for i in range(packages):
        p = 0xb0 + i * 0x40
        put(p, 0x0400000000000000 + i)
        put(p + 12, 0xffffffff, 4)
        put(p + 16, 0 if i < 2 else 0xffffffff, 4)
        put(p + 20, i + 10, 4)
        put(p + 24, i, 1)
        metadata = b''
        for kind, value in [(5, f'Synthetic package {i}'.encode()), (4, b'1.2.3')]:
            metadata += struct.pack('<HHI', kind, 0, len(value)) + value
        put(p + 32, len(data))
        put(p + 40, len(metadata), 4)
        data += metadata
    put(0x40, len(data))
    data[0x18:0x38] = hashlib.sha256(data[0x80:0x80+toc_size]).digest()
    return bytes(data), payloads, 0x80 + toc_size + sum(8+len(f'Synthetic package {i}')+8+5 for i in range(packages))


def snapshot(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return path.stat().st_size, path.stat().st_mtime_ns, digest.hexdigest()


class NupExportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="nup-export-", dir=WORK_DIR)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.library = self.root / "library"
        self.library.mkdir()
        self.data, self.payloads, self.catalog_bytes = fixture()
        self.source = self.library / "synthetic.nup"
        self.source.write_bytes(self.data)
        self.counter = 0

    def run_export(self, extra=(), success=True, source=None, output=None):
        source = source or self.source
        self.counter += 1
        output = output or self.root / f"out-{self.counter}"
        original = snapshot(source)
        result = subprocess.run(
            [str(EXECUTABLE), "--input", str(source), "--read-only-root", str(self.library),
             "--output", str(output), *extra],
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        self.assertEqual(snapshot(source), original, "Source size, mtime or SHA-256 changed")
        manifest = output / "manifest.json"
        return json.loads(manifest.read_text(encoding="utf-8")) if manifest.exists() else None, output

    def test_catalog_reads_only_metadata(self):
        manifest, output = self.run_export(["--list"])
        self.assertEqual(manifest["status"], "success")
        self.assertEqual(manifest["exported_files"], 0)
        self.assertEqual([p["kind"] for p in manifest["packages"]], ["base", "update", "addon"])
        self.assertEqual([p["version"] for p in manifest["packages"]], [10, 11, 12])
        self.assertEqual(manifest["packages"][0]["title_name"], "Synthetic package 0")
        self.assertEqual(manifest["packages"][0]["title_id"], "0400000000000000")
        self.assertEqual(manifest["packages"][0]["display_version"], "1.2.3")
        self.assertFalse(manifest["packages"][2]["has_romfs"])
        self.assertEqual(len(manifest["files"]), 6)
        self.assertEqual(manifest["catalog_source_bytes_read"], self.catalog_bytes)
        self.assertEqual(manifest["source_bytes_read"], self.catalog_bytes)
        self.assertEqual(manifest["source"], self.source.resolve().as_posix())
        self.assertFalse((output / "spill").exists())
        self.assertEqual(sorted(p.name for p in output.iterdir()), ["manifest.json"])

    def test_zstd_export_preserves_name_package_and_hash(self):
        manifest, output = self.run_export(["--path", "1:/Shaders/Shader.bfsha"])
        exported = [entry for entry in manifest["files"] if entry.get("exported")]
        self.assertEqual(len(exported), 1)
        self.assertEqual(exported[0]["package"], 1)
        self.assertEqual(exported[0]["path"], "/Shaders/Shader.bfsha")
        self.assertEqual(exported[0]["output"], "packages/1/romfs/Shaders/Shader.bfsha")
        self.assertEqual((output / exported[0]["output"]).read_bytes(), self.payloads[0])
        self.assertEqual(exported[0]["sha256"], hashlib.sha256(self.payloads[0]).hexdigest())
        self.assertEqual(list((output / "spill").iterdir()), [])

    def test_raw_explicit_path_and_candidate_selection(self):
        _, output = self.run_export(["--path", "0:/readme.txt"])
        self.assertEqual((output / "packages/0/romfs/readme.txt").read_bytes(), self.payloads[1])
        manifest, _ = self.run_export(["--candidates"])
        self.assertEqual(manifest["exported_files"], 4)
        self.assertEqual(
            sorted(entry["size"] for entry in manifest["files"] if entry.get("exported")),
            [0, 0, len(self.payloads[0]), len(self.payloads[0])])

    def test_missing_selection_and_export_limits_fail_before_export(self):
        for flags in (
            ["--path", "0:/missing.bin"],
            ["--path", "0:/Shaders/Shader.bfsha", "--max-file-bytes", "10"],
            ["--candidates", "--max-total-bytes", str(len(self.payloads[0]))],
        ):
            with self.subTest(flags=flags):
                manifest, output = self.run_export(flags, success=False)
                self.assertEqual(manifest["status"], "error")
                self.assertTrue(manifest["error"])
                self.assertFalse((output / "packages").exists())

    def test_library_output_and_existing_output_are_rejected(self):
        forbidden = self.library / "must-not-create"
        self.run_export(success=False, output=forbidden)
        self.assertFalse(forbidden.exists())
        _, listing = self.run_export()
        original = (listing / "manifest.json").read_bytes()
        self.run_export(success=False, output=listing)
        self.assertEqual((listing / "manifest.json").read_bytes(), original)

    def test_output_symlink_cannot_enter_library(self):
        link = self.root / "link-to-library"
        try:
            link.symlink_to(self.library, target_is_directory=True)
        except OSError as error:
            if getattr(error, "winerror", None) == 1314:
                self.skipTest("Windows symlink privilege is unavailable")
            raise
        self.run_export(success=False, output=link / "must-not-create")
        self.assertFalse((self.library / "must-not-create").exists())

    def test_input_outside_library_is_rejected(self):
        outside = self.root / "outside.nup"
        outside.write_bytes(self.data)
        self.run_export(success=False, source=outside)

    def test_unsafe_member_names_are_rejected(self):
        for index, name in enumerate([
            "../escape.bfsha", "bad:stream.bfsha", "CON.bfsha", "back\\\\slash.bfsha",
            "COM\u00b9.bfsha", "CONOUT$.bfsha",
        ]):
            with self.subTest(name=name):
                source = self.library / f"unsafe-{index}.nup"
                source.write_bytes(fixture(filename=name)[0])
                manifest, output = self.run_export(success=False, source=source)
                self.assertEqual(manifest["status"], "error")
                self.assertFalse((output / "packages").exists())
        self.assertFalse((self.root / "escape.bfsha").exists())

    def test_unicode_name_is_preserved(self):
        source = self.library / "unicode.nup"
        source.write_bytes(fixture(filename="シェーダー.bfsha")[0])
        _, output = self.run_export(["--path", "0:/Shaders/シェーダー.bfsha"], source=source)
        self.assertEqual((output / "packages/0/romfs/Shaders/シェーダー.bfsha").read_bytes(),
                         self.payloads[0])

    def test_corrupt_zstd_is_not_read_by_catalog_but_fails_export(self):
        source = self.library / "broken.nup"
        source.write_bytes(fixture(corrupt=True)[0])
        self.run_export(source=source)
        manifest, output = self.run_export(
            ["--path", "0:/Shaders/Shader.bfsha"], source=source, success=False)
        self.assertEqual(manifest["status"], "error")
        self.assertIn("decompression", manifest["error"])
        self.assertFalse((output / "packages/0/romfs/Shaders/Shader.bfsha").exists())
        self.assertEqual(list((output / "spill").iterdir()), [])

    def test_corrupt_toc_is_rejected(self):
        source = self.library / "bad-toc.nup"
        data = bytearray(self.data)
        data[0x18] ^= 1
        source.write_bytes(data)
        manifest, _ = self.run_export(source=source, success=False)
        self.assertIn("parse failed", manifest["error"])

    def test_metadata_allocation_limit(self):
        source = self.library / "oversized-toc.nup"
        header = bytearray(self.data[:0x80])
        size = 256 * 1024 * 1024 + 1
        blob = (0x80 + size + 15) & ~15
        struct.pack_into("<Q", header, 0x10, size)
        struct.pack_into("<Q", header, 0x38, blob)
        struct.pack_into("<Q", header, 0x40, blob)
        with source.open("wb") as stream:
            stream.write(header)
            stream.truncate(blob)
        manifest, _ = self.run_export(source=source, success=False)
        self.assertIn("metadata exceeds", manifest["error"])

    def test_no_candidate_matches_is_not_empty_success(self):
        source = self.library / "no-candidates.nup"
        data = bytearray(fixture(filename="payload.data")[0])
        toc_end = 0x80 + struct.unpack_from("<Q", data, 0x10)[0]
        toc = data[0x80:toc_end].replace(b"Shaders", b"AssetsX").replace(b"empty.bnsh", b"empty.data")
        data[0x80:toc_end] = toc
        data[0x18:0x38] = hashlib.sha256(toc).digest()
        source.write_bytes(data)
        manifest, _ = self.run_export(["--candidates"], source=source, success=False)
        self.assertIn("No files matched", manifest["error"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Synthetic NUP read-only/export regressions")
    parser.add_argument("executable", type=Path)
    parser.add_argument("--work-dir", type=Path, help="Existing parent for temporary test files")
    args, unittest_args = parser.parse_known_args()
    EXECUTABLE = args.executable.resolve(strict=True)
    WORK_DIR = args.work_dir.resolve(strict=True) if args.work_dir else None
    unittest.main(argv=[__file__, *unittest_args])
