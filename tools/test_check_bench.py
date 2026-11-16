"""Tests for tools/check_bench.py, the gate over bench_types' JSON lines (spec 8.4).

Contract under test: ratios above 1.05 fail with exit code 1 and name every such operation, everything at or below passes
with exit code 0, and any input that is not a complete set of well-formed result lines fails with exit code 2."""

import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_bench


def line(op, ratio, new_ns=10.0, addon_ns=10.0):
    return json.dumps({"op": op, "new_ns": new_ns, "addon_ns": addon_ns, "ratio": ratio})


class CheckBenchTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.path = Path(self._tmp.name) / "bench.json"

    def run_main(self, text):
        self.path.write_text(text, encoding="utf-8")
        out = io.StringIO()
        err = io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = check_bench.main([str(self.path)])
        return code, out.getvalue(), err.getvalue()

    def test_all_ratios_at_or_below_the_limit_pass(self):
        code, out, err = self.run_main("\n".join([line("string_find", 0.5), line("array_sort_int", 1.0), line("dict_insert_10", 1.05)]) + "\n")
        self.assertEqual(code, 0, err)
        for op in ("string_find", "array_sort_int", "dict_insert_10"):
            self.assertIn(op, out)
        self.assertEqual(err, "")

    def test_ratios_above_the_limit_fail_naming_every_such_op(self):
        code, out, err = self.run_main("\n".join([line("string_find", 1.2), line("array_sort_int", 0.9), line("dict_lookup_1000", 1.0501)]) + "\n")
        self.assertEqual(code, 1)
        self.assertIn("string_find", err)
        self.assertIn("dict_lookup_1000", err)
        self.assertNotIn("array_sort_int", err)

    def test_the_limit_is_five_percent(self):
        self.assertEqual(check_bench.LIMIT, 1.05)

    def test_malformed_json_fails(self):
        code, _, err = self.run_main(line("string_find", 0.5) + "\n{\"op\": \"array_find_int\", \"ratio\":\n")
        self.assertEqual(code, 2)
        self.assertIn("line 2", err)

    def test_missing_or_wrong_fields_fail(self):
        for bad in ('{"op": "x", "new_ns": 1, "addon_ns": 1}', '{"op": "x", "new_ns": 1, "addon_ns": 1, "ratio": "0.5"}', '{"op": "", "new_ns": 1, "addon_ns": 1, "ratio": 0.5}',
                '{"op": "x", "new_ns": 1, "addon_ns": 1, "ratio": NaN}', '{"op": "x", "new_ns": -1, "addon_ns": 1, "ratio": 0.5}', '{"op": "x", "new_ns": 1, "addon_ns": 1, "ratio": true}', '[1, 2]'):
            with self.subTest(bad=bad):
                code, _, err = self.run_main(bad + "\n")
                self.assertEqual(code, 2)
                self.assertIn("line 1", err)

    def test_a_repeated_op_fails(self):
        code, _, err = self.run_main(line("string_find", 0.5) + "\n" + line("string_find", 0.6) + "\n")
        self.assertEqual(code, 2)
        self.assertIn("string_find", err)

    def test_no_results_fail(self):
        code, _, err = self.run_main("\n\n")
        self.assertEqual(code, 2)
        self.assertIn("no results", err)

    def test_a_missing_file_fails(self):
        out = io.StringIO()
        err = io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = check_bench.main([str(self.path)])
        self.assertEqual(code, 2)
        self.assertIn(str(self.path), err.getvalue())


if __name__ == "__main__":
    unittest.main()
