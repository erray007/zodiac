#!/usr/bin/env python3
"""Signs the executables and DLLs of an installed kit folder: bin/*.exe, bin/*.dll and modules/*.dll.

Called by the CMake target zodiac_zip after "cmake --install" and before the zip and the setup are made, so both packages hold the signed files.
It signs only when the environment variable ZODIAC_SIGN_TOOL holds a sign command, the same one tools/build_installer.py gives Inno Setup:
it runs once per file, with $f standing for the quoted file name, $q for a double quote and $$ for a dollar sign. Static archives are not signed."""

import argparse
import os
import subprocess
import sys
from pathlib import Path

SIGN_TOOL_VARIABLE = "ZODIAC_SIGN_TOOL"
SIGNED_PATTERNS = (("bin", "*.exe"), ("bin", "*.dll"), ("modules", "*.dll"))


class SignError(Exception):
    """Raised when a file cannot be signed."""


def say(message: str) -> None:
    print(message, flush=True)


def sign_command(template: str, file: Path) -> str:
    """The command line for one file: Inno Setup's sign tool expansion of template ($f, $q, $$; $p is empty, other $ sequences are kept)."""
    out = []
    i = 0
    while i < len(template):
        char = template[i]
        following = template[i + 1] if i + 1 < len(template) else ""
        if char == "$" and following in ("f", "F"):
            out.append('"' + str(file) + '"')
        elif char == "$" and following in ("q", "Q"):
            out.append('"')
        elif char == "$" and following in ("p", "P"):
            pass
        elif char == "$" and following == "$":
            out.append("$")
        else:
            out.append(char)
            i += 1
            continue
        i += 2
    return "".join(out)


def kit_files(kit: Path) -> list:
    """The files of the kit that are signed, sorted: the executables and DLLs in bin and the module DLLs (not modules/static)."""
    files = set()
    for folder, pattern in SIGNED_PATTERNS:
        files.update(path for path in (Path(kit) / folder).glob(pattern) if path.is_file())
    return sorted(files)


def sign_kit(kit: Path, sign_tool) -> int:
    """Sign every kit file with sign_tool; returns how many files were signed (0 when sign_tool is None)."""
    if not sign_tool:
        say(f"Not signing the kit files ({SIGN_TOOL_VARIABLE} is not set).")
        return 0
    if not (Path(kit) / "bin" / "zodiac.exe").is_file():
        raise SignError(f"{kit} does not hold an installed kit (bin\\zodiac.exe is missing).")
    files = kit_files(kit)
    for file in files:
        say(f"Signing {file}")
        result = subprocess.run(sign_command(sign_tool, file), check=False)
        if result.returncode != 0:
            raise SignError(f"Signing {file} failed with exit code {result.returncode}.")
    return len(files)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Sign the executables and DLLs of an installed Zodiac kit.")
    parser.add_argument("--kit", required=True, type=Path, help="the installed kit folder (dist/Zodiac)")
    args = parser.parse_args(argv)
    try:
        count = sign_kit(args.kit, os.environ.get(SIGN_TOOL_VARIABLE) or None)
    except (SignError, OSError) as error:
        say(f"Error: {error}")
        return 1
    if count:
        say(f"Signed {count} files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
