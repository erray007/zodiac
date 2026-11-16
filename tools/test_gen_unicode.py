import contextlib
import io
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import gen_unicode


def quiet(function, *args):
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return function(*args)


class GenUnicodeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Downloads the pinned UCD files once into the shared cache (.tools/unicode/<version>) when they are missing.
        cls.sources = quiet(gen_unicode.fetch_all, gen_unicode.DEFAULT_DATA_DIR)

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def copy_outputs(self):
        for relative in gen_unicode.OUTPUTS:
            target = self.tmp / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(gen_unicode.ROOT / relative, target)

    def test_check_passes_on_the_committed_files(self):
        self.assertEqual(quiet(gen_unicode.main, ["--check"]), 0)

    def test_check_fails_when_a_committed_file_differs(self):
        self.copy_outputs()
        tables = self.tmp / gen_unicode.OUTPUTS[1]
        tables.write_bytes(tables.read_bytes().replace(b"case_stage1", b"case_stageX", 1))
        self.assertEqual(quiet(gen_unicode.main, ["--check", "--root", str(self.tmp)]), 1)

    def test_check_fails_when_a_committed_file_is_missing(self):
        self.copy_outputs()
        (self.tmp / gen_unicode.OUTPUTS[2]).unlink()
        self.assertEqual(quiet(gen_unicode.main, ["--check", "--root", str(self.tmp)]), 1)

    def test_check_ignores_crlf_checkouts(self):
        self.copy_outputs()
        for relative in gen_unicode.OUTPUTS:
            path = self.tmp / relative
            path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        self.assertEqual(quiet(gen_unicode.main, ["--check", "--root", str(self.tmp)]), 0)

    def test_generation_is_deterministic(self):
        self.assertEqual(gen_unicode.generate(self.sources), gen_unicode.generate(self.sources))

    def test_generate_writes_every_output(self):
        self.assertEqual(quiet(gen_unicode.main, ["--root", str(self.tmp)]), 0)
        for relative in gen_unicode.OUTPUTS:
            self.assertEqual((self.tmp / relative).read_bytes(), (gen_unicode.ROOT / relative).read_bytes().replace(b"\r\n", b"\n"))

    def test_a_corrupted_pinned_hash_makes_the_generator_refuse(self):
        data = self.tmp / "data"
        data.mkdir()
        for name in gen_unicode.PINNED:
            shutil.copyfile(self.sources[name], data / name)
        pinned = dict(gen_unicode.PINNED)
        pinned["UnicodeData.txt"] = "0" * 64

        def fake_download(url, dest):
            # The server hands back the genuine file, which does not match the corrupted pin.
            shutil.copyfile(self.sources[url.rsplit("/", 1)[1]], dest)

        out = self.tmp / "out"
        with mock.patch.object(gen_unicode, "PINNED", pinned), mock.patch.object(gen_unicode, "download", side_effect=fake_download) as downloaded:
            code = quiet(gen_unicode.main, ["--data", str(data), "--root", str(out)])
        self.assertEqual(code, 2)
        self.assertEqual(downloaded.call_count, 1)
        self.assertFalse((data / "UnicodeData.txt").exists())
        self.assertFalse(out.exists())

    def test_fetch_downloads_a_missing_file_and_keeps_a_matching_one(self):
        data = self.tmp / "data"

        def fake_download(url, dest):
            shutil.copyfile(self.sources[url.rsplit("/", 1)[1]], dest)

        with mock.patch.object(gen_unicode, "download", side_effect=fake_download) as downloaded:
            quiet(gen_unicode.fetch_all, data)
            self.assertEqual(downloaded.call_count, len(gen_unicode.PINNED))
            self.assertEqual(downloaded.call_args_list[0].args[0], gen_unicode.BASE_URL + next(iter(gen_unicode.PINNED)))
            quiet(gen_unicode.fetch_all, data)
            self.assertEqual(downloaded.call_count, len(gen_unicode.PINNED))

    def test_two_level_tables_round_trip(self):
        values = [0] * 300 + [5, 6, 7] * 100 + [0] * 1000 + [70000]
        table = gen_unicode.two_level(values)
        for index, value in enumerate(values):
            block = table.stage1[index >> table.shift]
            self.assertEqual(table.stage2[(block << table.shift) | (index & ((1 << table.shift) - 1))], value)
        self.assertEqual(table.limit, len(table.stage1) << table.shift)
        self.assertGreaterEqual(table.limit, len(values))


if __name__ == "__main__":
    unittest.main()
