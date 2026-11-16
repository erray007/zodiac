#!/usr/bin/env python3
"""Generates Zodiac's Unicode tables and NFC test vectors from pinned Unicode Character Database 16.0.0 files.

Outputs (committed): src/core/types/unicode_tables.hpp and unicode_tables.cpp (two-level lookup tables for simple case
mapping and normalization, plus the White_Space ranges) and tests/fixtures/unicode/nfc_vectors.txt (NormalizationTest.txt
reduced to the NFC columns). The UCD files are downloaded once into .tools/unicode/16.0.0 and refused unless their SHA-256
matches the pin below.

    python -I tools/gen_unicode.py           regenerate the outputs
    python -I tools/gen_unicode.py --check   exit 1 when a committed output differs from a fresh generation

Exit codes: 0 success, 1 --check found a difference, 2 a download failed or did not match its pinned SHA-256.
"""

import argparse
import hashlib
import sys
import time
import urllib.request
from pathlib import Path

UNICODE_VERSION = "16.0.0"
BASE_URL = f"https://www.unicode.org/Public/{UNICODE_VERSION}/ucd/"
# NormalizationTest.txt only feeds the test vectors; DerivedNormalizationProps.txt only cross-checks the derived
# composition exclusions and quick-check values.
PINNED = {
    "UnicodeData.txt": "ff58e5823bd095166564a006e47d111130813dcf8bf234ef79fa51a870edb48f",
    "CompositionExclusions.txt": "89e83cf9cc8bef6c1f8bf77e42cf6f0341dfa42e66261f4dbe9b492e7a23c8ee",
    "PropList.txt": "53d614508e2a0b2305a8aa21cd60d993de9326cdf65993660dfcce4503548583",
    "DerivedNormalizationProps.txt": "4d4c03892dea9146d674b686e495df2d55a28d071ac474041d73518f887abddc",
    "NormalizationTest.txt": "d811971453e7075e1ad56fb1b301eece5aa80757b81f6156e74a1bfb3ae5ceb1",
}
ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DATA_DIR = ROOT / ".tools" / "unicode" / UNICODE_VERSION
OUTPUTS = (
    "src/core/types/unicode_tables.hpp",
    "src/core/types/unicode_tables.cpp",
    "tests/fixtures/unicode/nfc_vectors.txt",
)
DOWNLOAD_ATTEMPTS = 3
MAX_CODE_POINT = 0x10FFFF

HANGUL_S_BASE = 0xAC00
HANGUL_L_BASE = 0x1100
HANGUL_V_BASE = 0x1161
HANGUL_T_BASE = 0x11A7
HANGUL_L_COUNT = 19
HANGUL_V_COUNT = 21
HANGUL_T_COUNT = 28
HANGUL_S_COUNT = HANGUL_L_COUNT * HANGUL_V_COUNT * HANGUL_T_COUNT

# Normalization record flags; the C++ side reads them through the generated constants.
QC_NO = 1
QC_MAYBE = 2
DECOMPOSITION_LENGTH_BITS = 3
COMPOSITION_COUNT_BITS = 6
CCC_SHIFT = 24


class ChecksumError(Exception):
    """Raised when a UCD file does not match its pinned SHA-256."""


class DownloadError(Exception):
    """Raised when a UCD file cannot be downloaded."""


def say(message: str) -> None:
    print(message, flush=True)


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, dest: Path) -> None:
    """Download url to dest, retrying transient failures."""
    for attempt in range(1, DOWNLOAD_ATTEMPTS + 1):
        try:
            with urllib.request.urlopen(url, timeout=60) as response, open(dest, "wb") as out:
                while True:
                    chunk = response.read(1 << 20)
                    if not chunk:
                        return
                    out.write(chunk)
        except OSError as error:
            if attempt == DOWNLOAD_ATTEMPTS:
                raise DownloadError(f"Could not download {url}: {error}") from error
            time.sleep(attempt)


def fetch(name: str, data_dir: Path) -> Path:
    """Return the cached UCD file, downloading it when missing or not matching its pin; refuse a mismatching download."""
    path = data_dir / name
    expected = PINNED[name].lower()
    if path.is_file() and sha256_of(path) == expected:
        return path
    data_dir.mkdir(parents=True, exist_ok=True)
    partial = path.with_name(name + ".part")
    path.unlink(missing_ok=True)
    say(f"Downloading {BASE_URL + name}")
    try:
        download(BASE_URL + name, partial)
        actual = sha256_of(partial)
        if actual != expected:
            raise ChecksumError(f"Checksum mismatch for {name}: expected {expected}, got {actual}")
        partial.replace(path)
    finally:
        partial.unlink(missing_ok=True)
    return path


def fetch_all(data_dir: Path) -> dict:
    return {name: fetch(name, Path(data_dir)) for name in PINNED}


def data_lines(text: str):
    """Yield the semicolon-separated fields of every non-comment line, stripped."""
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            yield [field.strip() for field in line.split(";")]


def code_point_range(field: str):
    first, _, last = field.partition("..")
    return int(first, 16), int(last or first, 16)


def property_ranges(text: str, name: str, value=None) -> list:
    ranges = []
    for fields in data_lines(text):
        if fields[1] == name and (value is None or fields[2] == value):
            ranges.append(code_point_range(fields[0]))
    return sorted(ranges)


def range_set(ranges) -> set:
    return {code_point for first, last in ranges for code_point in range(first, last + 1)}


def parse_unicode_data(text: str) -> dict:
    """Map each explicitly listed code point to its fields. Ranges (<CJK Ideograph, First> ...) carry no mappings and ccc 0."""
    return {int(fields[0], 16): fields for fields in (line.split(";") for line in text.splitlines() if line)}


def int_type(maximum: int, signed: bool = False) -> tuple:
    for bits in (8, 16, 32):
        if signed and -(1 << (bits - 1)) <= maximum < (1 << (bits - 1)):
            return f"std::int{bits}_t", bits // 8
        if not signed and maximum < (1 << bits):
            return f"std::uint{bits}_t", bits // 8
    raise ValueError(f"{maximum} does not fit in 32 bits")


class TwoLevel:
    """values[c] == stage2[(stage1[c >> shift] << shift) | (c & mask)] for every c < limit."""

    def __init__(self, shift: int, stage1: list, stage2: list):
        self.shift = shift
        self.stage1 = stage1
        self.stage2 = stage2
        self.limit = len(stage1) << shift
        self.stage1_type, stage1_size = int_type(max(stage1))
        self.stage2_type, stage2_size = int_type(max(stage2))
        self.size = len(stage1) * stage1_size + len(stage2) * stage2_size


def two_level(values: list) -> TwoLevel:
    """The smallest two-level table (in bytes) for values; identical blocks are stored once. Ties keep the smaller shift."""
    best = None
    for shift in range(2, 13):
        block_size = 1 << shift
        padded = values + [0] * (-len(values) % block_size)
        blocks = {}
        stage1 = []
        stage2 = []
        for start in range(0, len(padded), block_size):
            block = tuple(padded[start:start + block_size])
            index = blocks.get(block)
            if index is None:
                index = blocks[block] = len(blocks)
                stage2.extend(block)
            stage1.append(index)
        table = TwoLevel(shift, stage1, stage2)
        if best is None or table.size < best.size:
            best = table
    return best


def values_up_to_last(mapping: dict) -> list:
    """Dense list of mapping[c] (0 when absent) from 0 to the last code point with a non-zero value."""
    last = max((code_point for code_point, value in mapping.items() if value), default=-1)
    return [mapping.get(code_point, 0) for code_point in range(last + 1)]


def build_case(unicode_data: dict) -> dict:
    deltas = {(0, 0): 0}
    indices = {}
    for code_point, fields in sorted(unicode_data.items()):
        upper = int(fields[12], 16) - code_point if fields[12] else 0
        lower = int(fields[13], 16) - code_point if fields[13] else 0
        if upper or lower:
            indices[code_point] = deltas.setdefault((lower, upper), len(deltas))
    for code_point in range(0x80):
        lower, upper = next(pair for pair, index in deltas.items() if index == indices.get(code_point, 0))
        assert lower == (32 if 0x41 <= code_point <= 0x5A else 0) and upper == (-32 if 0x61 <= code_point <= 0x7A else 0), \
            "ASCII case mapping no longer matches the C++ fast path"
    # The Turkic functions special-case I/İ/ı/i only; the simple mappings must already give İ->i and ı->I.
    assert int(unicode_data[0x130][13], 16) == 0x69 and int(unicode_data[0x131][12], 16) == 0x49
    return {"table": two_level(values_up_to_last(indices)), "deltas": sorted(deltas, key=deltas.get)}


def canonical_order(sequence: list, ccc: dict) -> list:
    """Stable sort of every run of non-starters by combining class."""
    result = list(sequence)
    for i in range(1, len(result)):
        j = i
        while j > 0 and ccc.get(result[j], 0) != 0 and ccc.get(result[j - 1], 0) > ccc.get(result[j], 0):
            result[j - 1], result[j] = result[j], result[j - 1]
            j -= 1
    return result


def build_normalization(unicode_data: dict, exclusions_text: str, derived_text: str) -> dict:
    ccc = {code_point: int(fields[3]) for code_point, fields in unicode_data.items() if int(fields[3])}
    mappings = {}
    for code_point, fields in unicode_data.items():
        if fields[5] and not fields[5].startswith("<"):
            mappings[code_point] = [int(part, 16) for part in fields[5].split()]

    def full_decomposition(code_point: int) -> list:
        if code_point not in mappings:
            return [code_point]
        return [part for item in mappings[code_point] for part in full_decomposition(item)]

    excluded = range_set(code_point_range(fields[0]) for fields in data_lines(exclusions_text))
    singletons = {code_point for code_point, mapping in mappings.items() if len(mapping) == 1}
    non_starters = {code_point for code_point, mapping in mappings.items() if ccc.get(code_point, 0) or ccc.get(mapping[0], 0)}
    full_exclusion = excluded | singletons | non_starters
    assert full_exclusion == range_set(property_ranges(derived_text, "Full_Composition_Exclusion")), \
        "derived Full_Composition_Exclusion differs from DerivedNormalizationProps.txt"

    compositions = {}
    for code_point, mapping in mappings.items():
        if code_point not in full_exclusion:
            assert len(mapping) == 2 and ccc.get(code_point, 0) == 0, f"U+{code_point:04X} is an unexpected primary composite"
            compositions.setdefault(mapping[0], []).append((mapping[1], code_point))
    seconds = {second for pairs in compositions.values() for second, _ in pairs}
    hangul_vt = set(range(HANGUL_V_BASE, HANGUL_V_BASE + HANGUL_V_COUNT)) | set(range(HANGUL_T_BASE + 1, HANGUL_T_BASE + HANGUL_T_COUNT))
    maybe = seconds | hangul_vt
    # A character whose decomposition starts with one that combines backward combines backward too (Unicode 16 added
    # composites such as U+16121 = U+1611E U+1611E, where U+1611E is itself a second).
    while True:
        grown = maybe | {code_point for code_point in mappings if code_point not in full_exclusion and full_decomposition(code_point)[0] in maybe}
        if grown == maybe:
            break
        maybe = grown
    assert full_exclusion == range_set(property_ranges(derived_text, "NFC_QC", "N")), "NFC_QC=No differs from DerivedNormalizationProps.txt"
    assert maybe == range_set(property_ranges(derived_text, "NFC_QC", "M")), "NFC_QC=Maybe differs from DerivedNormalizationProps.txt"
    assert not (set(compositions) & set(range(HANGUL_S_BASE, HANGUL_S_BASE + HANGUL_S_COUNT))), "Hangul syllables are composed algorithmically"

    def flags_of(code_point: int) -> int:
        return (QC_NO if code_point in full_exclusion else 0) | (QC_MAYBE if code_point in maybe else 0)

    # nfc() restarts normalization at a starter that passes the quick check (ccc 0, NFC_QC=Yes). That is only safe when such
    # a character's decomposition starts with a starter that cannot combine with what precedes it.
    for code_point, mapping in mappings.items():
        if ccc.get(code_point, 0) == 0 and not flags_of(code_point):
            first = full_decomposition(code_point)[0]
            assert ccc.get(first, 0) == 0 and not flags_of(first) & QC_MAYBE, f"U+{code_point:04X} is not a safe restart point"

    # Two tables: properties (ccc and quick-check flags, all the quick check reads) and mappings (decomposition and composition).
    pool = []
    pool_index = {}
    composition_seconds = []
    composition_results = []
    properties = {(0, 0): 0}
    property_indices = {}
    for code_point in sorted(set(ccc) | full_exclusion | maybe):
        property_indices[code_point] = properties.setdefault((ccc.get(code_point, 0), flags_of(code_point)), len(properties))
    records = {(0, 0): 0}
    indices = {}
    for code_point in sorted(set(mappings) | set(compositions)):
        decomposition = 0
        if code_point in mappings:
            sequence = tuple(canonical_order(full_decomposition(code_point), ccc))
            offset = pool_index.get(sequence)
            if offset is None:
                offset = pool_index[sequence] = len(pool)
                pool.extend(part | ccc.get(part, 0) << CCC_SHIFT for part in sequence)
            assert len(sequence) < (1 << DECOMPOSITION_LENGTH_BITS) and offset < (1 << (16 - DECOMPOSITION_LENGTH_BITS))
            decomposition = offset << DECOMPOSITION_LENGTH_BITS | len(sequence)
        composition = 0
        if code_point in compositions:
            pairs = sorted(compositions[code_point])
            offset = len(composition_seconds)
            composition_seconds.extend(second for second, _ in pairs)
            composition_results.extend(result for _, result in pairs)
            assert len(pairs) < (1 << COMPOSITION_COUNT_BITS) and offset < (1 << (16 - COMPOSITION_COUNT_BITS))
            composition = offset << COMPOSITION_COUNT_BITS | len(pairs)
        indices[code_point] = records.setdefault((decomposition, composition), len(records))
    return {
        "property_table": two_level(values_up_to_last(property_indices)),
        "properties": sorted(properties, key=properties.get),
        "mapping_table": two_level(values_up_to_last(indices)),
        "mappings": sorted(records, key=records.get),
        "pool": pool,
        "composition_seconds": composition_seconds,
        "composition_results": composition_results,
        "quick_check_limit": min(property_indices),
    }


def build_whitespace(proplist_text: str) -> dict:
    ranges = property_ranges(proplist_text, "White_Space")
    ascii_mask = 0
    other = []
    for first, last in ranges:
        for code_point in range(first, min(last, 0x7F) + 1):
            assert code_point < 64, "ASCII White_Space above U+003F no longer fits the 64-bit mask"
            ascii_mask |= 1 << code_point
        if last >= 0x80:
            other.append((max(first, 0x80), last))
    return {"ascii_mask": ascii_mask, "ranges": other}


def number_lines(numbers, width: int = 160) -> str:
    lines = []
    line = ""
    for number in numbers:
        item = f"{number},"
        if line and len(line) + len(item) > width:
            lines.append(line)
            line = ""
        line += item
    if line:
        lines.append(line)
    return "\n".join(lines)


HEADER_COMMENT = f"// Generated by tools/gen_unicode.py from the Unicode Character Database {UNICODE_VERSION}; do not edit.\n"


def render_tables_hpp(case: dict, norm: dict, whitespace: dict) -> str:
    ct = case["table"]
    pt = norm["property_table"]
    mt = norm["mapping_table"]
    return (
        HEADER_COMMENT
        + "// Internal tables behind core/types/unicode.hpp. A two-level table maps code point c below its limit to\n"
        "// stage2[(stage1[c>>shift]<<shift)|(c&((1<<shift)-1))]; identical blocks of 1<<shift values are stored once.\n"
        "#pragma once\n"
        "#include <cstdint>\n\n"
        "namespace zodiac::unicode::tables {\n"
        "// Simple case mapping: case_stage2 holds indices into case_deltas, whose entries are added to the code point. Index 0 maps to itself.\n"
        "struct CaseDelta {\n"
        "std::int32_t lower;\n"
        "std::int32_t upper;\n"
        "};\n"
        f"inline constexpr char32_t case_limit={ct.limit};\n"
        f"inline constexpr unsigned case_shift={ct.shift};\n"
        f"extern const {ct.stage1_type} case_stage1[{len(ct.stage1)}];\n"
        f"extern const {ct.stage2_type} case_stage2[{len(ct.stage2)}];\n"
        f"extern const CaseDelta case_deltas[{len(case['deltas'])}];\n"
        "// Normalization properties: property_stage2 holds indices into norm_properties (index 0: ccc 0, NFC_QC=Yes).\n"
        "struct NormProperties {\n"
        "std::uint8_t ccc;\n"
        "std::uint8_t flags;\n"
        "};\n"
        f"inline constexpr std::uint8_t qc_no={QC_NO};\n"
        f"inline constexpr std::uint8_t qc_maybe={QC_MAYBE};\n"
        "// Every code point below quick_check_limit has ccc 0 and NFC_QC=Yes.\n"
        f"inline constexpr char32_t quick_check_limit={norm['quick_check_limit']};\n"
        f"inline constexpr char32_t property_limit={pt.limit};\n"
        f"inline constexpr unsigned property_shift={pt.shift};\n"
        f"extern const {pt.stage1_type} property_stage1[{len(pt.stage1)}];\n"
        f"extern const {pt.stage2_type} property_stage2[{len(pt.stage2)}];\n"
        f"extern const NormProperties norm_properties[{len(norm['properties'])}];\n"
        "// Normalization mappings: mapping_stage2 holds indices into norm_mappings (index 0: no decomposition, composes with nothing).\n"
        "// decomposition is offset<<decomposition_length_bits|length into decomposition_pool, the full canonical decomposition in canonical\n"
        "// order, each entry a code point with its combining class in the bits from ccc_shift up. composition is offset<<composition_count_bits|count\n"
        "// into composition_seconds (ascending) and composition_results: the primary composites this code point starts. Hangul is algorithmic.\n"
        "struct NormMapping {\n"
        "std::uint16_t decomposition;\n"
        "std::uint16_t composition;\n"
        "};\n"
        f"inline constexpr unsigned decomposition_length_bits={DECOMPOSITION_LENGTH_BITS};\n"
        f"inline constexpr unsigned composition_count_bits={COMPOSITION_COUNT_BITS};\n"
        f"inline constexpr unsigned ccc_shift={CCC_SHIFT};\n"
        f"inline constexpr char32_t mapping_limit={mt.limit};\n"
        f"inline constexpr unsigned mapping_shift={mt.shift};\n"
        f"extern const {mt.stage1_type} mapping_stage1[{len(mt.stage1)}];\n"
        f"extern const {mt.stage2_type} mapping_stage2[{len(mt.stage2)}];\n"
        f"extern const NormMapping norm_mappings[{len(norm['mappings'])}];\n"
        f"extern const char32_t decomposition_pool[{len(norm['pool'])}];\n"
        f"extern const char32_t composition_seconds[{len(norm['composition_seconds'])}];\n"
        f"extern const char32_t composition_results[{len(norm['composition_results'])}];\n"
        "// White_Space: bit c of whitespace_ascii_mask for c below 64, then the inclusive ranges above U+007F in ascending order.\n"
        "struct CodePointRange {\n"
        "char32_t first;\n"
        "char32_t last;\n"
        "};\n"
        f"inline constexpr std::uint64_t whitespace_ascii_mask={whitespace['ascii_mask']:#x}u;\n"
        f"inline constexpr CodePointRange whitespace_ranges[]={{{','.join(f'{{{first:#x},{last:#x}}}' for first, last in whitespace['ranges'])}}};\n"
        "}\n"
    )


def render_tables_cpp(case: dict, norm: dict) -> str:
    ct = case["table"]
    pt = norm["property_table"]
    mt = norm["mapping_table"]

    def array(declaration: str, numbers) -> str:
        return f"{declaration}={{\n{number_lines(numbers)}\n}};\n"

    def struct_array(declaration: str, rows) -> str:
        return f"{declaration}={{\n{number_lines('{' + ','.join(map(str, row)) + '}' for row in rows)}\n}};\n"

    case_size = ct.size + 8 * len(case["deltas"])
    property_size = pt.size + 2 * len(norm["properties"])
    mapping_size = mt.size + 4 * len(norm["mappings"])
    pool_size = 4 * len(norm["pool"])
    composition_size = 8 * len(norm["composition_seconds"])
    sizes = (
        f"// Sizes in bytes: case {case_size} (two-level {ct.size}, deltas {8 * len(case['deltas'])});"
        f" normalization {property_size + mapping_size + pool_size + composition_size}: properties {property_size} (two-level {pt.size}),"
        f" mappings {mapping_size} (two-level {mt.size}), decomposition pool {pool_size}, compositions {composition_size}.\n"
    )
    return (
        HEADER_COMMENT
        + sizes
        + "#include <core/types/unicode_tables.hpp>\n\n"
        "namespace zodiac::unicode::tables {\n"
        + array(f"const {ct.stage1_type} case_stage1[{len(ct.stage1)}]", ct.stage1)
        + array(f"const {ct.stage2_type} case_stage2[{len(ct.stage2)}]", ct.stage2)
        + struct_array(f"const CaseDelta case_deltas[{len(case['deltas'])}]", case["deltas"])
        + array(f"const {pt.stage1_type} property_stage1[{len(pt.stage1)}]", pt.stage1)
        + array(f"const {pt.stage2_type} property_stage2[{len(pt.stage2)}]", pt.stage2)
        + struct_array(f"const NormProperties norm_properties[{len(norm['properties'])}]", norm["properties"])
        + array(f"const {mt.stage1_type} mapping_stage1[{len(mt.stage1)}]", mt.stage1)
        + array(f"const {mt.stage2_type} mapping_stage2[{len(mt.stage2)}]", mt.stage2)
        + struct_array(f"const NormMapping norm_mappings[{len(norm['mappings'])}]", norm["mappings"])
        + array(f"const char32_t decomposition_pool[{len(norm['pool'])}]", norm["pool"])
        + array(f"const char32_t composition_seconds[{len(norm['composition_seconds'])}]", norm["composition_seconds"])
        + array(f"const char32_t composition_results[{len(norm['composition_results'])}]", norm["composition_results"])
        + "}\n"
    )


def hangul_decomposition(syllable: int) -> tuple:
    index = syllable - HANGUL_S_BASE
    parts = (HANGUL_L_BASE + index // (HANGUL_V_COUNT * HANGUL_T_COUNT), HANGUL_V_BASE + index % (HANGUL_V_COUNT * HANGUL_T_COUNT) // HANGUL_T_COUNT)
    if index % HANGUL_T_COUNT:
        parts += (HANGUL_T_BASE + index % HANGUL_T_COUNT,)
    return parts


def render_vectors(test_text: str) -> str:
    def hex_field(code_points) -> str:
        return " ".join(f"{code_point:X}" for code_point in code_points)

    lines = [
        f"# Generated by tools/gen_unicode.py from NormalizationTest-{UNICODE_VERSION}.txt; do not edit.",
        "# Each row: source;NFC;NFD as hex code points separated by spaces (columns 1-3 of NormalizationTest.txt). An empty field",
        "# repeats the field before it. \"@part N\" starts part N. \"@hangul FIRST;LAST\" stands for Part 1's rows of every Hangul",
        "# syllable in the range: source and NFC are the syllable, NFD its algorithmic decomposition.",
    ]
    hangul = []
    part = None
    for raw in test_text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("@Part"):
            part = int(line[5:])
            lines.append(f"@part {part}")
            continue
        source, composed, decomposed = (tuple(int(item, 16) for item in field.split()) for field in line.split(";")[:3])
        if part == 1 and len(source) == 1 and HANGUL_S_BASE <= source[0] < HANGUL_S_BASE + HANGUL_S_COUNT:
            assert composed == source and decomposed == hangul_decomposition(source[0]), "unexpected Hangul row"
            if not hangul:
                lines.append("@hangul")
            hangul.append(source[0])
            continue
        lines.append(";".join((hex_field(source), "" if composed == source else hex_field(composed), "" if decomposed == composed else hex_field(decomposed))))
    assert hangul == list(range(HANGUL_S_BASE, HANGUL_S_BASE + HANGUL_S_COUNT)), "Part 1 no longer lists every Hangul syllable in order"
    lines[lines.index("@hangul")] = f"@hangul {HANGUL_S_BASE:X};{HANGUL_S_BASE + HANGUL_S_COUNT - 1:X}"
    return "\n".join(lines) + "\n"


def generate(sources: dict) -> dict:
    """Map each output path (relative to the repository root) to its text."""
    def read(name: str) -> str:
        return Path(sources[name]).read_text(encoding="utf-8")

    unicode_data = parse_unicode_data(read("UnicodeData.txt"))
    case = build_case(unicode_data)
    norm = build_normalization(unicode_data, read("CompositionExclusions.txt"), read("DerivedNormalizationProps.txt"))
    whitespace = build_whitespace(read("PropList.txt"))
    return {
        OUTPUTS[0]: render_tables_hpp(case, norm, whitespace),
        OUTPUTS[1]: render_tables_cpp(case, norm),
        OUTPUTS[2]: render_vectors(read("NormalizationTest.txt")),
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="exit 1 when a committed output differs from a fresh generation")
    parser.add_argument("--data", type=Path, default=DEFAULT_DATA_DIR, help="UCD download cache (default .tools/unicode/16.0.0)")
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root the outputs live under")
    args = parser.parse_args(argv)
    try:
        sources = fetch_all(args.data)
    except (ChecksumError, DownloadError) as error:
        print(f"gen_unicode: {error}", file=sys.stderr, flush=True)
        return 2
    outputs = generate(sources)
    if args.check:
        stale = []
        for relative, text in outputs.items():
            path = args.root / relative
            if not path.is_file() or path.read_bytes().replace(b"\r\n", b"\n") != text.encode("utf-8"):
                stale.append(relative)
        for relative in stale:
            print(f"gen_unicode: {relative} is out of date; run python -I tools/gen_unicode.py", file=sys.stderr, flush=True)
        return 1 if stale else 0
    for relative, text in outputs.items():
        path = args.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        say(f"Wrote {relative}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
