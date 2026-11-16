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

    def test_manifest_pins_inno_setup_for_windows(self):
        manifest = bootstrap.load_manifest(Path(__file__).resolve().parent / "toolchain.json")
        spec = manifest["hosts"]["windows-x86_64"]["innosetup"]
        self.assertRegex(spec["version"], r"^6\.\d+\.\d+$")
        self.assertTrue(spec["url"].startswith("https://"))
        self.assertTrue(spec["url"].endswith(".exe"))
        self.assertRegex(spec["sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(spec["kind"], "installer")
        self.assertNotIn("innosetup", manifest["hosts"]["linux-x86_64"])

    def test_installer_command_installs_silently_for_the_current_user(self):
        dest = self.tmp / "root" / ".tools" / "innosetup.new"
        command = bootstrap.installer_command(self.tmp / "innosetup.exe", dest)
        self.assertEqual(command[0], str(self.tmp / "innosetup.exe"))
        for flag in ("/VERYSILENT", "/SUPPRESSMSGBOXES", "/CURRENTUSER", "/NORESTART", "/PORTABLE=1"):
            self.assertIn(flag, command)
        self.assertIn("/DIR=" + str(dest.resolve()), command)
        self.assertTrue(Path(command[-1].removeprefix("/DIR=")).is_absolute())

    def test_ensure_tool_runs_an_installer_once(self):
        payload = b"setup"
        spec = {
            "version": "6.0.0",
            "url": "https://example.invalid/setup-6.0.0.exe",
            "sha256": hashlib.sha256(payload).hexdigest(),
            "kind": "installer",
        }
        root = self.tmp / "root"

        def fake_download(url, dest, sha256):
            Path(dest).write_bytes(payload)

        def fake_run(command, **kwargs):
            target = Path(command[-1].removeprefix("/DIR="))
            target.mkdir(parents=True, exist_ok=True)
            (target / "ISCC.exe").write_text("iscc")
            return mock.Mock(returncode=0)

        with mock.patch.object(bootstrap, "download", side_effect=fake_download), \
                mock.patch.object(bootstrap.subprocess, "run", side_effect=fake_run) as ran:
            self.assertTrue(bootstrap.ensure_tool("innosetup", spec, root))
            self.assertEqual(ran.call_count, 1)
            self.assertIn("/CURRENTUSER", ran.call_args.args[0])
            self.assertEqual((root / ".tools" / "innosetup" / ".stamp").read_text().strip(), "6.0.0")
            self.assertTrue((root / ".tools" / "innosetup" / "ISCC.exe").exists())
            self.assertFalse((root / ".tools" / "innosetup.new").exists())
            self.assertFalse(bootstrap.ensure_tool("innosetup", spec, root))
            self.assertEqual(ran.call_count, 1)

    def test_ensure_tool_reports_a_failed_installer(self):
        payload = b"setup"
        spec = {
            "version": "6.0.0",
            "url": "https://example.invalid/setup-6.0.0.exe",
            "sha256": hashlib.sha256(payload).hexdigest(),
            "kind": "installer",
        }
        root = self.tmp / "root"
        with mock.patch.object(bootstrap, "download", side_effect=lambda url, dest, sha256: Path(dest).write_bytes(payload)), \
                mock.patch.object(bootstrap.subprocess, "run", return_value=mock.Mock(returncode=5)):
            with self.assertRaises(bootstrap.InstallerError):
                bootstrap.ensure_tool("innosetup", spec, root)
        self.assertFalse((root / ".tools" / "innosetup").exists())
        self.assertFalse((root / ".tools" / "innosetup.new").exists())

    def test_next_step_uses_the_host_path_separator(self):
        self.assertEqual(bootstrap.next_step("windows-x86_64"), r"Setup complete. Next: .tools\cmake\bin\cmake --workflow --preset windows")
        self.assertEqual(bootstrap.next_step("linux-x86_64"), "Setup complete. Next: .tools/cmake/bin/cmake --workflow --preset linux")

    def test_compiler_program_takes_the_first_word(self):
        self.assertEqual(bootstrap.compiler_program("clang++-18"), "clang++-18")
        self.assertEqual(bootstrap.compiler_program("'/opt/llvm 18/bin/clang++' -v"), "/opt/llvm 18/bin/clang++")
        self.assertIsNone(bootstrap.compiler_program(""))
        self.assertIsNone(bootstrap.compiler_program("'unterminated"))

    def test_check_linux_clang_checks_cxx_and_cc(self):
        versions = {"clang++-18": 18, "clang-17": 17}
        with mock.patch.dict(bootstrap.os.environ, {"CXX": "clang++-18 -v", "CC": "clang-17"}), \
                mock.patch.object(bootstrap, "clang_major_version", side_effect=lambda command: versions.get(bootstrap.compiler_program(command))) as patched, \
                mock.patch.object(bootstrap, "say") as said:
            self.assertFalse(bootstrap.check_linux_clang())
        self.assertEqual([call.args[0] for call in patched.call_args_list], ["clang++-18 -v", "clang-17"])
        lines = [call.args[0] for call in said.call_args_list]
        self.assertEqual(len(lines), 2)
        self.assertIn("clang-17 (CC) is version 17", lines[0])
        self.assertIn("libc++-18-dev libc++abi-18-dev", lines[1])

    def test_check_linux_clang_defaults_to_clang_and_accepts_18(self):
        with mock.patch.dict(bootstrap.os.environ, {"CXX": "", "CC": ""}), \
                mock.patch.object(bootstrap, "clang_major_version", return_value=18) as patched, \
                mock.patch.object(bootstrap, "say") as said:
            self.assertTrue(bootstrap.check_linux_clang())
        self.assertEqual([call.args[0] for call in patched.call_args_list], ["clang++", "clang"])
        said.assert_not_called()


if __name__ == "__main__":
    unittest.main()
