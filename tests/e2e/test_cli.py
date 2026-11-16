"""End-to-end tests for the zodiac command line. Needs ZODIAC_EXE, ZODIAC_MODULE_DIR and ZODIAC_TEST_MODULE_DIR in the environment (CTest sets them)."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

EXE = os.environ["ZODIAC_EXE"]
MODULE_DIR = os.environ["ZODIAC_MODULE_DIR"]
TEST_MODULE_DIR = os.environ["ZODIAC_TEST_MODULE_DIR"]
VERSION = os.environ.get("ZODIAC_VERSION", "")
LONE_FILE_NOTE = "note: no zodiac.toml; only the core is available, so module functions are not declared (create a project file to add modules)"


def no_desktop_env():
    """The environment with the documented test hook ZODIAC_FORCE_NO_DESKTOP=1: has_interactive_desktop() reports no desktop, so a failed
    zodiac run prints its summary on the console instead of opening the result window, and no test can wait on a window."""
    env = dict(os.environ)
    env["ZODIAC_FORCE_NO_DESKTOP"] = "1"
    return env


def zodiac(*args, cwd=None):
    result = subprocess.run([EXE, *args], capture_output=True, cwd=cwd, env=no_desktop_env(), timeout=60)
    return result.returncode, result.stdout.decode("utf-8"), result.stderr.decode("utf-8")


def write(path, text):
    Path(path).write_bytes(text.encode("utf-8"))


class CliTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def make_project(self, directory, modules, script):
        code, _, err = zodiac("new", str(directory))
        self.assertEqual(code, 0, err)
        toml = directory / "zodiac.toml"
        text = toml.read_text(encoding="utf-8")
        self.assertIn("static = []", text)
        toml.write_bytes(text.replace("static = []", "static = [%s]" % ", ".join('"%s"' % m for m in modules)).encode("utf-8"))
        write(directory / "main.zs", script)

    def test_new_then_run_prints_echo(self):
        project = self.root / "game"
        self.make_project(project, ["example"], 'void main()\n{\nexample_print(example_echo("merhaba kanka"));\n}\n')
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR)
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(out.splitlines(), ["merhaba kanka"])

    def test_new_template_runs_without_modules(self):
        project = self.root / "plain"
        code, out, err = zodiac("new", str(project))
        self.assertEqual(code, 0, err)
        self.assertEqual(len(out.splitlines()), 1)
        self.assertIn('name = "plain"', (project / "zodiac.toml").read_text(encoding="utf-8"))
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR)
        self.assertEqual((code, out, err), (0, "", ""))

    def test_run_defaults_to_current_directory(self):
        project = self.root / "game"
        self.make_project(project, ["example"], 'void main()\n{\nexample_print("here");\n}\n')
        code, out, err = zodiac("run", "--module-dir", MODULE_DIR, cwd=project)
        self.assertEqual((code, out.splitlines(), err), (0, ["here"], ""))

    def test_run_in_unicode_dir(self):
        project = self.root / "Kanka şövalye oyunu"
        self.make_project(project, ["example"], 'void main()\n{\nexample_print("şövalye ğüşiöç");\n}\n')
        self.assertIn('name = "Kanka şövalye oyunu"', (project / "zodiac.toml").read_text(encoding="utf-8"))
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR)
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(out.splitlines(), ["şövalye ğüşiöç"])

    def test_run_sets_working_directory_to_project_root(self):
        project = self.root / "Kanka şövalye oyunu"
        # The global is initialised during the build, so the root must be the working directory before it (as in a release).
        self.make_project(project, ["example", "cwd_probe"], "string start_dir = cwd();\nvoid main()\n{\nexample_print(start_dir);\nexample_print(cwd());\n}\n")
        # Started from the build tree (the temp folder may be on another drive), with --module-dir values relative to it: they must keep
        # meaning that directory after zodiac enters the project root.
        elsewhere = Path(MODULE_DIR).parent
        module_dir = os.path.relpath(MODULE_DIR, elsewhere)
        test_module_dir = os.path.relpath(TEST_MODULE_DIR, elsewhere)
        code, out, err = zodiac("run", str(project), "--module-dir", module_dir, "--module-dir", test_module_dir, cwd=elsewhere)
        self.assertEqual((code, err), (0, ""))
        lines = out.splitlines()
        self.assertEqual(len(lines), 2, out)
        expected = os.path.normcase(os.path.realpath(project))
        for line in lines:
            self.assertEqual(os.path.normcase(os.path.realpath(line)), expected)

    def test_run_script_in_subfolder_uses_project(self):
        project = self.root / "game"
        self.make_project(project, ["example"], "void main()\n{\n}\n")
        (project / "includes").mkdir()
        write(project / "shared.zs", 'void shared()\n{\nexample_print("shared");\n}\n')
        write(project / "includes" / "menu.zs", '#include "../shared.zs"\nvoid main()\n{\nexample_print("menu");\nshared();\n}\n')
        code, out, err = zodiac("run", str(project / "includes" / "menu.zs"), "--module-dir", MODULE_DIR)
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(out.splitlines(), ["menu", "shared"])

    def test_run_lone_script_runs_with_the_core_only(self):
        folder = self.root / "tek klasör"
        folder.mkdir()
        # An exit value no error path uses, so only the script's own main can produce it.
        write(folder / "oyun.zs", "int main()\n{\nreturn 4;\n}\n")
        code, out, err = zodiac("run", str(folder / "oyun.zs"), "--module-dir", MODULE_DIR, "--module-dir", TEST_MODULE_DIR)
        # No installed module is loaded: the test module folder holds broken fixtures (abi_zero) whose loading would fail the run.
        self.assertEqual((code, out, err), (4, "", ""))

    def test_run_lone_script_calling_a_module_function_fails_with_the_note(self):
        folder = self.root / "lone"
        folder.mkdir()
        write(folder / "oyun.zs", 'void main()\n{\nexample_print("tek dosya");\n}\n')
        code, out, err = zodiac("run", str(folder / "oyun.zs"), "--module-dir", MODULE_DIR)
        self.assertEqual(code, 1, err)
        lines = err.splitlines()
        self.assertTrue(any("oyun.zs:3:" in line and "example_print" in line for line in lines), err)
        # The note follows the compiler's errors and is no error itself.
        self.assertEqual(lines[-1], LONE_FILE_NOTE)
        self.assertEqual(lines.count(LONE_FILE_NOTE), 1)
        self.assertEqual(out.splitlines(), ["Run failed: 1 error"])
        code, out, err = zodiac("run", str(folder / "oyun.zs"), "--no-window", "--module-dir", MODULE_DIR)
        self.assertEqual((code, out, err.splitlines()[-1]), (1, "", LONE_FILE_NOTE))

    def test_run_compile_error_in_a_project_has_no_note(self):
        project = self.root / "game"
        self.make_project(project, [], 'void main()\n{\nexample_print("x");\n}\n')
        code, out, err = zodiac("run", str(project / "main.zs"), "--module-dir", MODULE_DIR)
        self.assertEqual(code, 1, err)
        self.assertNotIn("note:", err)

    def test_run_script_extension_ignores_case(self):
        folder = self.root / "upper"
        folder.mkdir()
        write(folder / "MAIN.ZS", "int main()\n{\nreturn 5;\n}\n")
        code, out, err = zodiac("run", str(folder / "MAIN.ZS"), "--module-dir", MODULE_DIR)
        self.assertEqual((code, out, err), (5, "", ""))

    def test_run_lone_script_relative_include(self):
        folder = self.root / "lone"
        folder.mkdir()
        write(folder / "yardimci.zs", "int yardim()\n{\nreturn 9;\n}\n")
        write(folder / "oyun.zs", '#include "yardimci.zs"\nint main()\n{\nreturn yardim();\n}\n')
        code, out, err = zodiac("run", str(folder / "oyun.zs"), "--module-dir", MODULE_DIR)
        self.assertEqual((code, out, err), (9, "", ""))

    def test_run_config_file_relative_from_another_folder(self):
        project = self.root / "ayar klasörü"
        (project / "sahne").mkdir(parents=True)
        write(project / "sahne" / "menü.zs", 'string start_dir = cwd();\nvoid main()\n{\nexample_print("ayar");\nexample_print(start_dir);\n}\n')
        write(project / "ayar.txt", '[game]\nmain = "sahne/menü.zs"\n[modules]\nstatic = ["example", "cwd_probe"]\n')
        for flag in (["--config", os.path.join("ayar klasörü", "ayar.txt")], ["--config=" + os.path.join("ayar klasörü", "ayar.txt")]):
            with self.subTest(flag=flag[0]):
                code, out, err = zodiac("run", *flag, "--module-dir", MODULE_DIR, "--module-dir", TEST_MODULE_DIR, cwd=self.root)
                self.assertEqual((code, err), (0, ""))
                lines = out.splitlines()
                self.assertEqual(lines[0], "ayar")
                # The project root is the config file's folder, and it is the working directory while the game runs.
                self.assertEqual(os.path.normcase(os.path.realpath(lines[1])), os.path.normcase(os.path.realpath(project)))

    def test_run_config_with_script_overrides_main(self):
        project = self.root / "game"
        project.mkdir()
        # Exit values no error path uses, so only the chosen main can produce them.
        write(project / "main.zs", "int main()\n{\nreturn 5;\n}\n")
        write(project / "two.zs", "int main()\n{\nreturn 6;\n}\n")
        write(project / "settings.toml", '[game]\nmain = "main.zs"\n')
        code, out, err = zodiac("run", str(project / "two.zs"), "--config", str(project / "settings.toml"), "--module-dir", MODULE_DIR)
        self.assertEqual((code, out, err), (6, "", ""))
        code, out, err = zodiac("run", "--config", str(project / "settings.toml"), "--module-dir", MODULE_DIR)
        self.assertEqual((code, out, err), (5, "", ""))

    def test_run_config_errors(self):
        project = self.root / "game"
        project.mkdir()
        write(project / "main.zs", "void main()\n{\n}\n")
        write(project / "ayar.txt", '[game]\nmain = "main.zs"\n')
        code, out, err = zodiac("run", str(project), "--config", str(project / "ayar.txt"))
        self.assertEqual((code, out, err.splitlines()), (2, "", ["error: --config cannot be combined with a project folder"]))
        # The run starts with loading the project, so its errors are summed up; command line mistakes are not.
        code, out, err = zodiac("run", "--config", "yok.txt", cwd=project)
        self.assertEqual((code, out.splitlines(), err.splitlines()), (2, ["Run failed: 1 error"], ['error: config file "%s" does not exist' % (project / "yok.txt")]))
        write(project / "bozuk.txt", "[game\n")
        code, out, err = zodiac("run", "--config", str(project / "bozuk.txt"))
        self.assertEqual((code, out.splitlines()), (2, ["Run failed: 1 error"]))
        self.assertTrue(err.startswith("bozuk.txt:1:"), err)
        code, out, err = zodiac("run", "--config")
        self.assertEqual((code, err.splitlines()), (2, ["error: --config needs a value"]))
        code, out, err = zodiac("run", "--config", "--log-file", "x.log")
        self.assertEqual((code, err.splitlines()), (2, ["error: --config needs a value"]))
        code, out, err = zodiac("run", "--config=a.toml", "--config", "b.toml")
        self.assertEqual((code, out, err.splitlines()), (2, "", ["error: --config given twice"]))
        code, out, err = zodiac("modules", "--config", str(project / "ayar.txt"))
        self.assertEqual((code, err.splitlines()), (2, ["error: --config applies to run and build only; run zodiac help"]))

    def test_run_missing_script_exit_2(self):
        missing = self.root / "yok.zs"
        code, out, err = zodiac("run", str(missing), "--module-dir", MODULE_DIR)
        self.assertEqual((code, out.splitlines()), (2, ["Run failed: 1 error"]))
        self.assertEqual(err.splitlines(), ['error: script "%s" does not exist' % missing])

    def test_missing_module_exit_2(self):
        project = self.root / "game"
        self.make_project(project, ["no_such_module"], "void main()\n{\n}\n")
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR)
        self.assertEqual(code, 2)
        self.assertEqual(out.splitlines(), ["Run failed: 1 error"])
        self.assertEqual(len(err.splitlines()), 1)
        self.assertIn('module "no_such_module" not found', err)

    def test_failed_run_summary_is_the_last_stdout_line(self):
        project = self.root / "game"
        self.make_project(project, ["example"], 'void main()\n{\nexample_print("before");\nint zero = 0;\nint x = 1 / zero;\nexample_print("after");\n}\n')
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR)
        self.assertEqual(code, 1)
        self.assertIn("main.zs:5: runtime error:", err)
        self.assertEqual(out.splitlines(), ["before", "Run failed: 1 error"])

    def test_no_window_run_never_prints_the_summary(self):
        project = self.root / "game"
        self.make_project(project, ["no_such_module"], "void main()\n{\n}\n")
        code, out, err = zodiac("run", str(project), "--no-window", "--module-dir", MODULE_DIR)
        self.assertEqual((code, out), (2, ""))
        self.assertIn('module "no_such_module" not found', err)
        clean = self.root / "clean"
        self.make_project(clean, ["example"], 'void main()\n{\nexample_print("ok");\n}\n')
        code, out, err = zodiac("run", "--no-window", str(clean), "--module-dir", MODULE_DIR)
        self.assertEqual((code, out.splitlines(), err), (0, ["ok"], ""))
        code, out, err = zodiac("run", str(clean), "--output", str(self.root / "out"))
        self.assertEqual((code, out, err.splitlines()), (2, "", ["error: --output applies to build only; run zodiac help"]))

    def test_main_return_value_is_exit_code(self):
        project = self.root / "game"
        self.make_project(project, [], "int main()\n{\nreturn 7;\n}\n")
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR)
        self.assertEqual((code, out, err), (7, "", ""))

    def test_game_exit_code_1_without_an_engine_error_prints_no_summary(self):
        project = self.root / "game"
        self.make_project(project, [], "int main()\n{\nreturn 1;\n}\n")
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR)
        self.assertEqual((code, out, err), (1, "", ""))

    def test_main_return_value_out_of_range_warns_and_exits_1(self):
        project = self.root / "game"
        self.make_project(project, [], "int main()\n{\nreturn 256;\n}\n")
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR)
        self.assertEqual(code, 1)
        self.assertEqual(err.splitlines(), ["warning: main() returned 256; exit codes must be 0-255, using 1"])
        project = self.root / "game2"
        self.make_project(project, [], "int main()\n{\nreturn -1;\n}\n")
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR)
        self.assertEqual(code, 1)
        self.assertEqual(err.splitlines(), ["warning: main() returned -1; exit codes must be 0-255, using 1"])

    def test_compile_error_exit_1(self):
        project = self.root / "game"
        self.make_project(project, [], "void main()\n{\nint(zz);\n}\n")
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR)
        self.assertEqual(code, 1)
        self.assertIn("main.zs:3:", err)
        self.assertIn("error", err)
        self.assertRegex(out, r"^Run failed: [1-9][0-9]* errors?\r?\n$")

    def test_project_error_exit_2(self):
        project = self.root / "broken"
        project.mkdir()
        write(project / "zodiac.toml", "[game\n")
        code, out, err = zodiac("run", str(project))
        self.assertEqual((code, out.splitlines()), (2, ["Run failed: 1 error"]))
        self.assertTrue(err.startswith("zodiac.toml"), err)
        self.assertEqual(len(err.splitlines()), 1)

    def test_modules_lists_example(self):
        code, out, err = zodiac("modules", "--module-dir", MODULE_DIR)
        self.assertEqual(code, 0, err)
        lines = [line for line in out.splitlines() if line.startswith("example ")]
        self.assertEqual(len(lines), 1, out)
        self.assertTrue(lines[0].startswith("example 0.1.0 Zlib ("), lines[0])
        self.assertTrue(lines[0].endswith(")"))
        self.assertIn("zodiac_mod_example", lines[0])
        names = [line.split(" ")[0] for line in out.splitlines()]
        self.assertEqual(names, sorted(names))

    def test_modules_reports_unreadable_file_and_continues(self):
        directory = self.root / "mods"
        directory.mkdir()
        suffix = Path(next(Path(MODULE_DIR).glob("zodiac_mod_example.*"))).suffix
        (directory / ("zodiac_mod_corrupt" + suffix)).write_bytes(b"not a library")
        code, out, err = zodiac("modules", "--module-dir", str(directory), "--module-dir", MODULE_DIR)
        self.assertEqual(code, 0)
        self.assertEqual(len(err.splitlines()), 1, err)
        self.assertIn("zodiac_mod_corrupt", err)
        self.assertTrue(any(line.startswith("example ") for line in out.splitlines()))

    def test_unknown_command_exit_2(self):
        code, out, err = zodiac("frobnicate")
        self.assertEqual((code, out), (2, ""))
        self.assertEqual(err.splitlines(), ['error: unknown command "frobnicate"; run zodiac help'])

    def test_missing_option_value_exit_2(self):
        code, out, err = zodiac("run", "--log-file")
        self.assertEqual(code, 2)
        self.assertEqual(err.splitlines(), ["error: --log-file needs a value"])
        code, out, err = zodiac("modules", "--module-dir")
        self.assertEqual(code, 2)
        self.assertEqual(err.splitlines(), ["error: --module-dir needs a value"])

    def test_new_refuses_non_empty_dir(self):
        project = self.root / "taken"
        project.mkdir()
        write(project / "keep.txt", "x")
        code, out, err = zodiac("new", str(project))
        self.assertEqual(code, 2)
        self.assertEqual(err.splitlines(), ['error: directory "%s" is not empty' % project])
        self.assertEqual([p.name for p in project.iterdir()], ["keep.txt"])

    def test_new_accepts_existing_empty_dir(self):
        project = self.root / "empty"
        project.mkdir()
        code, out, err = zodiac("new", str(project))
        self.assertEqual(code, 0, err)
        self.assertTrue((project / "zodiac.toml").is_file())
        self.assertTrue((project / "main.zs").is_file())

    def test_new_needs_directory(self):
        code, out, err = zodiac("new")
        self.assertEqual(code, 2)
        self.assertEqual(len(err.splitlines()), 1)

    def test_log_file_flag_captures_module_error(self):
        project = self.root / "game"
        self.make_project(project, ["no_such_module"], "void main()\n{\n}\n")
        log = self.root / "logs" / "zodiac.log"
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR, "--log-file", str(log))
        self.assertEqual(code, 2)
        logged = log.read_bytes().decode("utf-8")
        self.assertEqual(len(err.splitlines()), 1)
        self.assertEqual(logged.splitlines(), err.splitlines())
        self.assertIn("no_such_module", logged)

    def test_log_file_flag_captures_project_error(self):
        project = self.root / "broken"
        project.mkdir()
        write(project / "zodiac.toml", "[game\n")
        log = self.root / "project.log"
        code, out, err = zodiac("run", str(project), "--log-file", str(log))
        self.assertEqual(code, 2)
        self.assertEqual(log.read_bytes().decode("utf-8").splitlines(), err.splitlines())

    def test_project_log_file_is_used(self):
        project = self.root / "game"
        self.make_project(project, ["no_such_module"], "void main()\n{\n}\n")
        toml = project / "zodiac.toml"
        write(toml, toml.read_text(encoding="utf-8") + '\n[diagnostics]\nfile = "game.log"\n')
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR)
        self.assertEqual(code, 2)
        self.assertIn("no_such_module", (project / "game.log").read_text(encoding="utf-8"))

    def test_version(self):
        code, out, err = zodiac("--version")
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(out.splitlines(), ["zodiac " + VERSION])

    def test_help_lists_commands(self):
        code, out, err = zodiac("help")
        self.assertEqual((code, err), (0, ""))
        for command in ("new", "run", "build", "modules", "help"):
            self.assertTrue(any(command in line for line in out.splitlines()), command)
        self.assertIn("zodiac build [project_dir|file.zs]: build a release of the game (default output: <project>/release)", out.splitlines())
        self.assertTrue(any("--config <file>" in line for line in out.splitlines()), out)

    @unittest.skipIf(os.name == "nt", "Windows builds releases; see test_build.py")
    def test_build_not_supported_on_this_host(self):
        project = self.root / "game"
        self.make_project(project, [], "void main()\n{\n}\n")
        code, out, err = zodiac("build", str(project), "--no-window")
        self.assertEqual(code, 2)
        self.assertEqual(err.splitlines(), ["error: release builds are not supported on this host yet"])
        self.assertFalse((project / "release").exists())


if __name__ == "__main__":
    unittest.main()
