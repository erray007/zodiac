"""End-to-end tests for the zodiac command line. Needs ZODIAC_EXE and ZODIAC_MODULE_DIR in the environment (CTest sets them)."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

EXE = os.environ["ZODIAC_EXE"]
MODULE_DIR = os.environ["ZODIAC_MODULE_DIR"]
VERSION = os.environ.get("ZODIAC_VERSION", "")


def zodiac(*args, cwd=None):
    result = subprocess.run([EXE, *args], capture_output=True, cwd=cwd, timeout=60)
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

    def test_missing_module_exit_2(self):
        project = self.root / "game"
        self.make_project(project, ["no_such_module"], "void main()\n{\n}\n")
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR)
        self.assertEqual(code, 2)
        self.assertEqual(out, "")
        self.assertEqual(len(err.splitlines()), 1)
        self.assertIn('module "no_such_module" not found', err)

    def test_main_return_value_is_exit_code(self):
        project = self.root / "game"
        self.make_project(project, [], "int main()\n{\nreturn 7;\n}\n")
        code, out, err = zodiac("run", str(project), "--module-dir", MODULE_DIR)
        self.assertEqual((code, out, err), (7, "", ""))

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

    def test_project_error_exit_2(self):
        project = self.root / "broken"
        project.mkdir()
        write(project / "zodiac.toml", "[game\n")
        code, out, err = zodiac("run", str(project))
        self.assertEqual(code, 2)
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
        for command in ("new", "run", "modules", "help"):
            self.assertTrue(any(command in line for line in out.splitlines()), command)


if __name__ == "__main__":
    unittest.main()
