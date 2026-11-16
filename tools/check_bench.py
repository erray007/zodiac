#!/usr/bin/env python3
"""Checks bench_types' results against the speed gate of spec 8.4: no Zodiac type may be more than 5 % slower than its sample add-on.

    python -I tools/check_bench.py <results.json>

The file holds one JSON object per line, {"op": name, "new_ns": n, "addon_ns": n, "ratio": r}, as bench_types writes it: new_ns and
addon_ns are the median nanoseconds per iteration of Zodiac's types and of the add-ons, ratio the median of the per-round ratios new/addon.
Blank lines are ignored. Prints a table of every operation to stdout.

Exit codes: 0 every ratio is at most LIMIT; 1 some ratio is above LIMIT (each such operation is named on stderr); 2 the file is missing or
unreadable, a line is not such an object (malformed JSON, a missing or non-numeric field, a negative or non-finite number, an empty op name),
an operation appears twice, or the file holds no results.
"""

import argparse
import json
import math
import sys
from pathlib import Path

LIMIT = 1.05
FIELDS = ("new_ns", "addon_ns", "ratio")


class BenchFormatError(Exception):
    """Raised when the results are not a complete set of well-formed lines."""


def _reject_constant(name: str):
    raise ValueError(f"{name} is not a number")


def _number(record: dict, field: str, number: int) -> float:
    value = record.get(field)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise BenchFormatError(f"line {number}: \"{field}\" is missing or not a number")
    value = float(value)
    if not math.isfinite(value) or value < 0:
        raise BenchFormatError(f"line {number}: \"{field}\" is {value}, not a finite number of at least 0")
    return value


def parse(text: str) -> list:
    """The (op, new_ns, addon_ns, ratio) tuples of text, in order. Raises BenchFormatError."""
    results = []
    seen = set()
    for number, raw in enumerate(text.splitlines(), start=1):
        if not raw.strip():
            continue
        try:
            record = json.loads(raw, parse_constant=_reject_constant)
        except ValueError as error:
            raise BenchFormatError(f"line {number}: malformed JSON ({error})") from None
        if not isinstance(record, dict):
            raise BenchFormatError(f"line {number}: not a JSON object")
        op = record.get("op")
        if not isinstance(op, str) or not op:
            raise BenchFormatError(f"line {number}: \"op\" is missing or not a name")
        if op in seen:
            raise BenchFormatError(f"line {number}: {op} appears twice")
        seen.add(op)
        results.append((op,) + tuple(_number(record, field, number) for field in FIELDS))
    if not results:
        raise BenchFormatError("no results")
    return results


def table(results: list) -> str:
    width = max(len("op"), max(len(op) for op, *_ in results))
    lines = [f"{'op':<{width}}  {'new_ns':>12}  {'addon_ns':>12}  {'ratio':>6}"]
    for op, new_ns, addon_ns, ratio in results:
        mark = "  FAIL" if ratio > LIMIT else ""
        lines.append(f"{op:<{width}}  {new_ns:>12.2f}  {addon_ns:>12.2f}  {ratio:>6.3f}{mark}")
    return "\n".join(lines)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Fail when a Zodiac type is more than 5 % slower than its sample add-on.")
    parser.add_argument("results", type=Path, help="bench_types' JSON lines")
    args = parser.parse_args(argv)
    try:
        text = args.results.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        print(f"check_bench: cannot read {args.results}: {error}", file=sys.stderr)
        return 2
    try:
        results = parse(text)
    except BenchFormatError as error:
        print(f"check_bench: {args.results}: {error}", file=sys.stderr)
        return 2
    print(table(results), flush=True)
    slower = [(op, ratio) for op, _, _, ratio in results if ratio > LIMIT]
    if slower:
        print(f"check_bench: {len(slower)} operation(s) more than {round((LIMIT - 1) * 100)} % slower than the sample add-ons:", file=sys.stderr)
        for op, ratio in slower:
            print(f"  {op}: ratio {ratio:.3f}", file=sys.stderr)
        return 1
    print(f"check_bench: all {len(results)} operations within the {LIMIT} limit.", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
