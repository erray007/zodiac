import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import build_installer


class BuildInstallerTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)
        self.iscc = self.tmp / "innosetup" / "ISCC.exe"
        self.iscc.parent.mkdir()
        self.iscc.write_text("iscc")
        self.script = self.tmp / "zodiac.iss"
        self.script.write_text("[Setup]")
        self.kit = self.tmp / "dist" / "Zodiac"
        (self.kit / "bin").mkdir(parents=True)
        (self.kit / "bin" / "zodiac.exe").write_text("exe")
        self.output = self.tmp / "dist"

    def test_unsigned_command_defines_version_and_kit_only(self):
        command = build_installer.iscc_command(self.iscc, self.script, "1.2.3", self.kit, self.output, None)
        self.assertEqual(command, [str(self.iscc), "/DZodiacVersion=1.2.3", "/DKitDir=" + str(self.kit.resolve()), "/O" + str(self.output.resolve()), str(self.script)])

    def test_signed_command_defines_sign_and_the_sign_tool_with_quotes_as_dollar_q(self):
        command = build_installer.iscc_command(self.iscc, self.script, "1.2.3", self.kit, self.output, '"C:\\sign tool\\signtool.exe" sign /f "a b.pfx" $f')
        self.assertIn("/DSign=1", command)
        self.assertIn("/Szodiacsign=$qC:\\sign tool\\signtool.exe$q sign /f $qa b.pfx$q $f", command)
        self.assertEqual(command[-1], str(self.script))

    def test_build_runs_iscc_and_returns_the_setup(self):
        def fake_run(command, **kwargs):
            (self.output / "zodiac-setup-1.2.3.exe").write_text("setup")
            return mock.Mock(returncode=0)

        with mock.patch.object(build_installer.subprocess, "run", side_effect=fake_run) as ran, mock.patch.object(build_installer, "say"):
            setup = build_installer.build(self.iscc, self.script, "1.2.3", self.kit, self.output, None)
        self.assertEqual(setup, self.output / "zodiac-setup-1.2.3.exe")
        self.assertNotIn("/DSign=1", ran.call_args.args[0])

    def test_build_removes_a_stale_setup_and_fails_when_iscc_makes_none(self):
        (self.output / "zodiac-setup-1.2.3.exe").write_text("old")
        with mock.patch.object(build_installer.subprocess, "run", return_value=mock.Mock(returncode=0)), mock.patch.object(build_installer, "say"):
            with self.assertRaises(build_installer.InstallerBuildError):
                build_installer.build(self.iscc, self.script, "1.2.3", self.kit, self.output, None)
        self.assertFalse((self.output / "zodiac-setup-1.2.3.exe").exists())

    def test_build_reports_a_failed_compile(self):
        with mock.patch.object(build_installer.subprocess, "run", return_value=mock.Mock(returncode=2)), mock.patch.object(build_installer, "say"):
            with self.assertRaisesRegex(build_installer.InstallerBuildError, "exit code 2"):
                build_installer.build(self.iscc, self.script, "1.2.3", self.kit, self.output, None)

    def test_missing_inputs_say_what_to_do(self):
        with self.assertRaisesRegex(build_installer.InstallerBuildError, "bootstrap.py"):
            build_installer.check_inputs(self.tmp / "missing" / "ISCC.exe", self.script, "1.2.3", self.kit)
        with self.assertRaisesRegex(build_installer.InstallerBuildError, "zodiac_zip"):
            build_installer.check_inputs(self.iscc, self.script, "1.2.3", self.tmp / "empty")
        with self.assertRaisesRegex(build_installer.InstallerBuildError, "1.2.3"):
            build_installer.check_inputs(self.iscc, self.script, "1.2", self.kit)


if __name__ == "__main__":
    unittest.main()
