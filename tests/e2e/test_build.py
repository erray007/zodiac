"""End-to-end tests for zodiac build. Windows only: release builds link with the kit's ld.lld and llvm-mingw CRT subset.
Needs ZODIAC_EXE, ZODIAC_MODULE_DIR, ZODIAC_TEST_MODULE_DIR and ZODIAC_ECHO_ARGS in the environment (CTest sets them, see tests/CMakeLists.txt)."""
import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

EXE = os.environ.get("ZODIAC_EXE", "")
MODULE_DIR = os.environ.get("ZODIAC_MODULE_DIR", "")
TEST_MODULE_DIR = os.environ.get("ZODIAC_TEST_MODULE_DIR", "")
ECHO_ARGS = os.environ.get("ZODIAC_ECHO_ARGS", "")
SCRIPT_FIXTURES = Path(__file__).resolve().parent.parent / "fixtures" / "scripts"
# Fixtures that need test-only modules from ZODIAC_TEST_MODULE_DIR: a release loads its modules from the kit (static) or its own folder (dynamic),
# and the kit ships no test modules, so these cannot be built. Every other fixture takes part in the identity test.
FIXTURES_NEEDING_TEST_MODULES = {"holds_plugin_object"}
# The fixtures that build; the identity test fails if this set changes, so a fixture can never drop out of it silently.
BUILDABLE_FIXTURES = {"int_main", "non_ascii_include", "runtime_error", "uses_module", "void_main"}
SIZE_LIMIT = 3145728


def zodiac(*args, cwd=None, errors="strict"):
    result = subprocess.run([EXE, *args], capture_output=True, cwd=cwd, timeout=300)
    return result.returncode, result.stdout.decode("utf-8", errors), result.stderr.decode("utf-8", errors)


def run_exe(path, cwd=None, errors="strict"):
    result = subprocess.run([str(path)], capture_output=True, cwd=cwd, timeout=60)
    return result.returncode, result.stdout.decode("utf-8", errors), result.stderr.decode("utf-8", errors)


def write(path, text):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(text.encode("utf-8"))


def toml_literal(text):
    """A TOML literal string: no escapes, so Windows paths stay as they are."""
    assert "'" not in text
    return "'" + text + "'"


def pe_imports(path):
    """The DLL names a PE image imports (import and delay-import directories), lower-case."""
    data = Path(path).read_bytes()
    if data[:2] != b"MZ":
        raise ValueError("not a PE image: %s" % path)
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("no PE signature: %s" % path)
    sections_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    directories = optional + (112 if magic == 0x20B else 96)
    sections = []
    table = optional + optional_size
    for index in range(sections_count):
        entry = table + index * 40
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, entry + 8)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_pointer))

    def offset(rva):
        for virtual_address, size, raw_pointer in sections:
            if virtual_address <= rva < virtual_address + size:
                return rva - virtual_address + raw_pointer
        raise ValueError("RVA 0x%x is outside every section" % rva)

    def c_string(rva):
        start = offset(rva)
        return data[start:data.index(b"\0", start)].decode("ascii").lower()

    names = []
    import_rva = struct.unpack_from("<I", data, directories + 8)[0]
    if import_rva:
        at = offset(import_rva)
        while True:
            fields = struct.unpack_from("<IIIII", data, at)
            if not any(fields):
                break
            names.append(c_string(fields[3]))
            at += 20
    delay_rva = struct.unpack_from("<I", data, directories + 13 * 8)[0]
    if delay_rva:
        at = offset(delay_rva)
        while True:
            fields = struct.unpack_from("<IIIIIIII", data, at)
            if not any(fields):
                break
            names.append(c_string(fields[1]))
            at += 32
    return names


@unittest.skipUnless(os.name == "nt", "release builds are Windows only for now")
class BuildTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def new_project(self, directory, static=(), dynamic=(), script=None):
        code, _, err = zodiac("new", str(directory))
        self.assertEqual(code, 0, err)
        toml = directory / "zodiac.toml"
        text = toml.read_text(encoding="utf-8")
        text = text.replace("static = []", "static = [%s]" % ", ".join('"%s"' % m for m in static))
        text = text.replace("dynamic = []", "dynamic = [%s]" % ", ".join('"%s"' % m for m in dynamic))
        write(toml, text)
        if script is not None:
            write(directory / "main.zs", script)
        return directory

    def append_toml(self, directory, text):
        toml = directory / "zodiac.toml"
        write(toml, toml.read_text(encoding="utf-8") + text)

    def build(self, *args, cwd=None):
        return zodiac("build", *args, "--no-window", "--module-dir", MODULE_DIR, cwd=cwd)

    def assert_built(self, result, exe):
        code, out, err = result
        self.assertEqual((code, err), (0, ""), out)
        self.assertEqual(out.splitlines()[-1], "Build succeeded: %s" % exe)
        self.assertTrue(exe.is_file(), exe)

    def build_hello(self):
        project = self.new_project(self.root / "hello")
        exe = project / "release" / "hello.exe"
        self.assert_built(self.build(str(project)), exe)
        return exe

    def test_build_hello_runs(self):
        exe = self.build_hello()
        self.assertEqual(run_exe(exe), (0, "", ""))
        # Only the executable: no temporary object, response file or .tmp is left behind.
        self.assertEqual(sorted(p.name for p in exe.parent.iterdir()), ["hello.exe"])

    def test_build_static_example_module_prints(self):
        hello = self.build_hello()
        project = self.new_project(self.root / "game", static=["example"], script='void main()\n{\nexample_print(example_echo("merhaba kanka"));\n}\n')
        exe = project / "release" / "game.exe"
        self.assert_built(self.build(str(project)), exe)
        # Started with piped output, the GUI-subsystem release delivers the module's text on stdout, exactly.
        code, out, err = run_exe(exe)
        self.assertEqual((code, out.splitlines(), err), (0, ["merhaba kanka"], ""))
        self.assertEqual(sorted(p.name for p in exe.parent.iterdir()), ["game.exe"])
        # Undeclared modules stay out of the executable.
        self.assertNotIn(b"example_echo", hello.read_bytes())
        self.assertIn(b"example_echo", exe.read_bytes())

    def test_build_dynamic_module_ships_dll(self):
        project = self.new_project(self.root / "game", dynamic=["example"], script='int main()\n{\nexample_print("dinamik");\nreturn example_add(2,3);\n}\n')
        exe = project / "release" / "game.exe"
        self.assert_built(self.build(str(project)), exe)
        dll = exe.parent / "zodiac_mod_example.dll"
        self.assertTrue(dll.is_file())
        self.assertEqual(dll.read_bytes(), (Path(MODULE_DIR) / "zodiac_mod_example.dll").read_bytes())
        code, out, err = run_exe(exe)
        self.assertEqual((code, out.splitlines(), err), (5, ["dinamik"], ""))
        dll.unlink()
        code, out, err = run_exe(exe)
        self.assertEqual(code, 2)
        self.assertIn('module "example"', err)

    def test_identity_with_run(self):
        built = set()
        for fixture in sorted(p for p in SCRIPT_FIXTURES.iterdir() if p.is_dir()):
            if fixture.name in FIXTURES_NEEDING_TEST_MODULES:
                continue
            with self.subTest(fixture=fixture.name):
                # The system's reason in some messages (missing_include) is not always UTF-8 yet; both sides get the same bytes, so compare them escaped.
                run_code, run_out, run_err = zodiac("run", str(fixture), "--module-dir", MODULE_DIR, errors="backslashreplace")
                output = self.root / fixture.name
                build_code, build_out, build_err = zodiac("build", str(fixture), "--no-window", "--module-dir", MODULE_DIR, "--output", str(output), errors="backslashreplace")
                if build_code != 0:
                    # A fixture run refuses (compile, include, main or module errors) fails the build the same way.
                    self.assertEqual(build_code, run_code, build_err)
                    self.assertEqual(build_err.splitlines(), run_err.splitlines())
                    self.assertFalse((output / (fixture.name + ".exe")).exists())
                    continue
                built.add(fixture.name)
                exe_code, exe_out, exe_err = run_exe(output / (fixture.name + ".exe"), errors="backslashreplace")
                self.assertEqual(exe_code, run_code)
                self.assertEqual(exe_err.splitlines(), run_err.splitlines())
                self.assertEqual(exe_out, run_out)
        self.assertEqual(built, BUILDABLE_FIXTURES)

    def test_build_paths_with_space_and_unicode(self):
        project = self.new_project(self.root / "Kanka şövalye oyunu", static=["example"], script='int main()\n{\nexample_print("şövalye ğüşiöç");\nreturn 3;\n}\n')
        output = self.root / "çıktı klasörü"
        exe = output / "Kanka şövalye oyunu.exe"
        self.assert_built(self.build(str(project), "--output", str(output)), exe)
        code, out, err = run_exe(exe)
        self.assertEqual((code, out.splitlines(), err), (3, ["şövalye ğüşiöç"], ""))

    def test_build_from_script_file_uses_its_project(self):
        project = self.new_project(self.root / "game")
        write(project / "levels" / "one" / "level.zs", "void level()\n{\n}\n")
        exe = project / "release" / "game.exe"
        self.assert_built(self.build(str(project / "levels" / "one" / "level.zs")), exe)
        self.assertEqual(run_exe(exe), (0, "", ""))

    def test_build_locked_output(self):
        import ctypes
        from ctypes import wintypes
        exe = self.build_hello()
        before = exe.read_bytes()
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateFileW.restype = wintypes.HANDLE
        kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        generic_read = 0x80000000
        open_existing = 3
        handle = kernel32.CreateFileW(str(exe), generic_read, 0, None, open_existing, 0, None)
        self.assertNotEqual(handle, wintypes.HANDLE(-1).value, ctypes.get_last_error())
        try:
            code, out, err = self.build(str(exe.parent.parent))
        finally:
            kernel32.CloseHandle(handle)
        self.assertEqual(code, 2, err)
        self.assertIn('error: cannot replace "%s": it is in use (is the game still running?)' % exe, err.splitlines())
        self.assertEqual(out.splitlines()[-1], "Build failed: 1 error")
        self.assertEqual(sorted(p.name for p in exe.parent.iterdir()), ["hello.exe"])
        self.assertEqual(exe.read_bytes(), before)

    def test_build_compile_error_exit_1(self):
        project = self.new_project(self.root / "game", script="void main()\n{\nint(zz);\n}\n")
        code, out, err = self.build(str(project))
        self.assertEqual(code, 1)
        self.assertIn("main.zs:3:", err)
        errors = [line for line in err.splitlines() if not line.startswith("  ") and ("error: " in line)]
        self.assertTrue(errors, err)
        self.assertEqual(out.splitlines()[-1], "Build failed: %d %s" % (len(errors), "error" if len(errors) == 1 else "errors"))
        self.assertFalse((project / "release" / "game.exe").exists())

    def test_build_assets_copied(self):
        project = self.new_project(self.root / "game")
        self.append_toml(project, '\n[build]\nassets = ["sounds/**/*.ogg"]\n')
        write(project / "sounds" / "a.ogg", "a")
        write(project / "sounds" / "x" / "b.ogg", "b")
        write(project / "sounds" / "c.wav", "c")
        write(project / "music" / "d.ogg", "d")
        release = project / "release"
        self.assert_built(self.build(str(project)), release / "game.exe")
        files = sorted(p.relative_to(release).as_posix() for p in release.rglob("*") if p.is_file())
        self.assertEqual(files, ["game.exe", "sounds/a.ogg", "sounds/x/b.ogg"])
        self.assertEqual((release / "sounds" / "x" / "b.ogg").read_text(encoding="utf-8"), "b")

    def test_build_sign_command_runs(self):
        self.assertTrue(ECHO_ARGS and Path(ECHO_ARGS).is_file(), ECHO_ARGS)
        project = self.new_project(self.root / "game", dynamic=["example"])
        marker = self.root / "signed.txt"
        self.append_toml(project, "\n[build.sign]\ncommand = [%s, \"--marker\", %s, \"{file}\"]\n" % (toml_literal(ECHO_ARGS), toml_literal(str(marker))))
        release = project / "release"
        self.assert_built(self.build(str(project)), release / "game.exe")
        signed = marker.read_text(encoding="utf-8").splitlines()
        self.assertEqual(sorted(signed), sorted([str(release / "game.exe"), str(release / "zodiac_mod_example.dll")]))
        # echo_args without --marker exits with its argument count (1): the build fails naming the file.
        failing = self.new_project(self.root / "failing")
        self.append_toml(failing, "\n[build.sign]\ncommand = [%s, \"{file}\"]\n" % toml_literal(ECHO_ARGS))
        code, out, err = self.build(str(failing))
        self.assertEqual(code, 2, err)
        self.assertIn('error: signing "%s" failed' % (failing / "release" / "failing.exe"), err.splitlines())
        self.assertEqual(out.splitlines()[-1], "Build failed: 1 error")

    def test_build_without_project(self):
        alone = self.root / "alone"
        write(alone / "game.zs", "void main()\n{\n}\n")
        expected = ["error: zodiac.toml not found; create a project with zodiac new"]
        code, out, err = self.build(str(alone / "game.zs"))
        self.assertEqual((code, err.splitlines()), (2, expected))
        code, out, err = self.build(str(alone))
        self.assertEqual((code, err.splitlines()), (2, expected))
        self.assertFalse((alone / "release").exists())

    def test_build_refuses_module_that_forbids_static_linking(self):
        project = self.new_project(self.root / "game", static=["lgpl_dynamic"])
        code, out, err = self.build(str(project), "--module-dir", TEST_MODULE_DIR)
        self.assertEqual(code, 2, err)
        self.assertEqual(err.splitlines(), ['error: module "lgpl_dynamic" (LGPL-2.1-only) cannot be linked statically; list it under dynamic'])
        self.assertEqual(out.splitlines()[-1], "Build failed: 1 error")
        self.assertFalse((project / "release").exists())

    def test_build_ships_dynamic_module_from_module_dir(self):
        project = self.new_project(self.root / "game", dynamic=["lgpl_dynamic"])
        code, out, err = self.build(str(project), "--module-dir", TEST_MODULE_DIR)
        self.assertEqual(code, 0, err)
        release = project / "release"
        self.assertTrue((release / "game.exe").is_file())
        self.assertEqual((release / "zodiac_mod_lgpl_dynamic.dll").read_bytes(), (Path(TEST_MODULE_DIR) / "zodiac_mod_lgpl_dynamic.dll").read_bytes())

    def test_build_static_plugin_needs_library_and_toolchain_stamp(self):
        project = self.new_project(self.root / "game", static=["gpl_static"])
        warning = 'warning: module "gpl_static" is linked statically under license GPL-3.0-only; check its terms'
        code, out, err = self.build(str(project), "--module-dir", TEST_MODULE_DIR)
        self.assertEqual(code, 2, err)
        self.assertEqual(err.splitlines(), [warning, 'error: module "gpl_static" has no static library in the kit or in plugins/static; list it under dynamic'])
        static_dir = project / "plugins" / "static"
        static_dir.mkdir(parents=True)
        write(static_dir / "zodiac_mod_gpl_static.a", "!<arch>\n")
        stamp_error = 'error: plugin "gpl_static" was not built with this kit\'s toolchain; list it under dynamic'
        code, out, err = self.build(str(project), "--module-dir", TEST_MODULE_DIR)
        self.assertEqual((code, err.splitlines()), (2, [warning, stamp_error]))
        write(static_dir / "zodiac_mod_gpl_static.stamp", "some other toolchain")
        code, out, err = self.build(str(project), "--module-dir", TEST_MODULE_DIR)
        self.assertEqual((code, err.splitlines()), (2, [warning, stamp_error]))
        self.assertFalse((project / "release").exists())

    def test_hello_size_gate(self):
        exe = self.build_hello()
        size = exe.stat().st_size
        self.assertLessEqual(size, SIZE_LIMIT, "core-only hello release is %d bytes" % size)

    def test_hello_imports_no_toolchain_runtime(self):
        exe = self.build_hello()
        imports = pe_imports(exe)
        self.assertTrue(imports, "no imports found")
        for name in ("libc++.dll", "libunwind.dll", "libwinpthread-1.dll"):
            self.assertNotIn(name, imports)

    def test_build_usage_errors(self):
        code, out, err = zodiac("build", "a", "b")
        self.assertEqual((code, out), (2, ""))
        self.assertEqual(err.splitlines(), ["error: build takes at most one project directory or script; run zodiac help"])
        code, out, err = zodiac("build", "--output")
        self.assertEqual((code, err.splitlines()), (2, ["error: --output needs a value"]))
        code, out, err = zodiac("run", "--no-window")
        self.assertEqual((code, err.splitlines()), (2, ["error: --output and --no-window apply to build only; run zodiac help"]))


if __name__ == "__main__":
    unittest.main()
