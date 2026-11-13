import hashlib
import io
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import bootstrap


def make_zip(path, members):
    with zipfile.ZipFile(path, "w") as zf:
        for name, data in members.items():
            zf.writestr(name, data)


class BootstrapTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def test_download_rejects_bad_checksum(self):
        source = self.tmp / "source.bin"
        source.write_bytes(b"hello zodiac")
        dest = self.tmp / "out" / "file.bin"
        with self.assertRaises(bootstrap.ChecksumError):
            bootstrap.download(source.as_uri(), dest, "0" * 64)
        self.assertFalse(dest.exists())
        self.assertFalse(dest.with_name(dest.name + ".part").exists())

    def test_download_accepts_good_checksum(self):
        source = self.tmp / "source.bin"
        source.write_bytes(b"hello zodiac")
        dest = self.tmp / "file.bin"
        bootstrap.download(source.as_uri(), dest, hashlib.sha256(b"hello zodiac").hexdigest())
        self.assertEqual(dest.read_bytes(), b"hello zodiac")

    def test_safe_extract_rejects_parent_paths(self):
        archive = self.tmp / "evil.zip"
        make_zip(archive, {"../evil.txt": "x"})
        dest = self.tmp / "dest"
        with self.assertRaises(bootstrap.UnsafeArchiveError):
            bootstrap.safe_extract(archive, dest, "")
        self.assertFalse((self.tmp / "evil.txt").exists())
        self.assertEqual(list(dest.rglob("*")), [])

    def test_safe_extract_rejects_absolute_paths(self):
        archive = self.tmp / "abs.zip"
        make_zip(archive, {"/abs.txt": "x"})
        with self.assertRaises(bootstrap.UnsafeArchiveError):
            bootstrap.safe_extract(archive, self.tmp / "dest", "")

    def test_safe_extract_strips_prefix(self):
        archive = self.tmp / "cmake.zip"
        make_zip(archive, {"cmake-3.x/bin/cmake.exe": "exe", "cmake-3.x/share/readme.txt": "r"})
        dest = self.tmp / "dest"
        bootstrap.safe_extract(archive, dest, "cmake-3.x")
        self.assertEqual((dest / "bin" / "cmake.exe").read_text(), "exe")
        self.assertTrue((dest / "share" / "readme.txt").exists())

    def test_safe_extract_handles_tar_gz(self):
        archive = self.tmp / "cmake.tar.gz"
        data = b"tool"
        with tarfile.open(archive, "w:gz") as tf:
            info = tarfile.TarInfo("cmake-3.x/bin/cmake")
            info.size = len(data)
            info.mode = 0o755
            tf.addfile(info, io.BytesIO(data))
        dest = self.tmp / "dest"
        bootstrap.safe_extract(archive, dest, "cmake-3.x")
        self.assertEqual((dest / "bin" / "cmake").read_bytes(), data)

    def test_ensure_tool_is_idempotent(self):
        def fake_download(url, dest, sha256):
            make_zip(dest, {"tool-1/bin/tool.exe": "exe"})

        content = self.tmp / "seed.zip"
        make_zip(content, {"tool-1/bin/tool.exe": "exe"})
        spec = {
            "version": "1.0",
            "url": "https://example.invalid/tool-1.zip",
            "sha256": hashlib.sha256(content.read_bytes()).hexdigest(),
            "strip_prefix": "tool-1",
        }
        root = self.tmp / "root"
        with mock.patch.object(bootstrap, "download", side_effect=fake_download) as patched:
            self.assertTrue(bootstrap.ensure_tool("tool", spec, root))
            self.assertEqual(patched.call_count, 1)
            self.assertEqual((root / ".tools" / "tool" / ".stamp").read_text().strip(), "1.0")
            self.assertTrue((root / ".tools" / "tool" / "bin" / "tool.exe").exists())
            self.assertFalse(bootstrap.ensure_tool("tool", spec, root))
            self.assertEqual(patched.call_count, 1)


if __name__ == "__main__":
    unittest.main()
