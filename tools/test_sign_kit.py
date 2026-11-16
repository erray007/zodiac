import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import sign_kit


class SignKitTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)
        self.kit = self.tmp / "Zodiac Kit"
        for name in ("bin/zodiac.exe", "bin/zodiacw.exe", "bin/ld.lld.exe", "bin/libLLVM-23.dll", "bin/libc++.dll", "modules/zodiac_mod_example.dll",
                "modules/static/zodiac_mod_example.a", "lib/release/libzodiac_core_release.a", "README.txt"):
            path = self.kit / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("x")

    def test_sign_command_expands_like_inno_setup(self):
        file = Path("C:/kit dir/bin/zodiac.exe")
        command = sign_kit.sign_command('cmd.exe /d /s /c ""C:\\t\\sign.cmd" $f" $p $$x $qa$q $z', file)
        self.assertEqual(command, 'cmd.exe /d /s /c ""C:\\t\\sign.cmd" "' + str(file) + '""  $x "a" $z')

    def test_kit_files_are_the_executables_and_dlls_only(self):
        names = [path.relative_to(self.kit).as_posix() for path in sign_kit.kit_files(self.kit)]
        self.assertEqual(sorted(names), sorted(["bin/zodiac.exe", "bin/zodiacw.exe", "bin/ld.lld.exe", "bin/libLLVM-23.dll", "bin/libc++.dll", "modules/zodiac_mod_example.dll"]))

    def test_sign_kit_runs_the_command_once_per_file(self):
        with mock.patch.object(sign_kit.subprocess, "run", return_value=mock.Mock(returncode=0)) as ran, mock.patch.object(sign_kit, "say"):
            self.assertEqual(sign_kit.sign_kit(self.kit, "signtool sign $f"), 6)
        commands = [call.args[0] for call in ran.call_args_list]
        self.assertIn('signtool sign "' + str(self.kit / "bin" / "zodiac.exe") + '"', commands)
        self.assertFalse(any(".a\"" in command for command in commands))

    def test_sign_kit_does_nothing_without_a_sign_tool(self):
        with mock.patch.object(sign_kit.subprocess, "run") as ran, mock.patch.object(sign_kit, "say"):
            self.assertEqual(sign_kit.sign_kit(self.kit, None), 0)
        ran.assert_not_called()

    def test_sign_kit_stops_at_a_failed_signature(self):
        with mock.patch.object(sign_kit.subprocess, "run", return_value=mock.Mock(returncode=1)) as ran, mock.patch.object(sign_kit, "say"):
            with self.assertRaisesRegex(sign_kit.SignError, "exit code 1"):
                sign_kit.sign_kit(self.kit, "signtool sign $f")
        self.assertEqual(ran.call_count, 1)


if __name__ == "__main__":
    unittest.main()
