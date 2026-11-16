#!/usr/bin/env python3
"""Builds the Zodiac setup wizard, <output>/zodiac-setup-<version>.exe, from an installed kit folder with Inno Setup's command line compiler.

Called by the CMake target zodiac_installer. The setup is signed only when the environment variable ZODIAC_SIGN_TOOL holds a sign command:
Inno Setup runs it once per file it signs, with $f standing for the file. Double quotes in it are passed to Inno Setup as $q."""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

SIGN_TOOL_VARIABLE = "ZODIAC_SIGN_TOOL"
SIGN_TOOL_NAME = "zodiacsign"


class InstallerBuildError(Exception):
    """Raised when the setup cannot be built; the message says what to do."""


def say(message: str) -> None:
    print(message, flush=True)


def iscc_command(iscc: Path, script: Path, version: str, kit: Path, output: Path, sign_tool) -> list:
    """The ISCC command line for packaging/zodiac.iss. sign_tool is the sign command, or None for an unsigned setup."""
    command = [str(iscc), f"/DZodiacVersion={version}", f"/DKitDir={Path(kit).resolve()}", f"/O{Path(output).resolve()}"]
    if sign_tool:
        command += ["/DSign=1", "/S" + SIGN_TOOL_NAME + "=" + sign_tool.replace('"', "$q")]
    command.append(str(script))
    return command


def check_inputs(iscc: Path, script: Path, version: str, kit: Path) -> None:
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise InstallerBuildError(f"The version must look like 1.2.3, but it is {version!r}.")
    if not Path(iscc).is_file():
        raise InstallerBuildError(f"Inno Setup was not found at {iscc}. Run python bootstrap.py first.")
    if not Path(script).is_file():
        raise InstallerBuildError(f"The setup script {script} is missing.")
    if not (Path(kit) / "bin" / "zodiac.exe").is_file():
        raise InstallerBuildError(f"{kit} does not hold an installed kit (bin\\zodiac.exe is missing). Build the target zodiac_zip first.")


def build(iscc: Path, script: Path, version: str, kit: Path, output: Path, sign_tool) -> Path:
    """Run ISCC and return the setup it made."""
    check_inputs(iscc, script, version, kit)
    Path(output).mkdir(parents=True, exist_ok=True)
    setup = Path(output) / f"zodiac-setup-{version}.exe"
    setup.unlink(missing_ok=True)
    say("Signing the setup with " + SIGN_TOOL_VARIABLE + "." if sign_tool else "Building an unsigned setup (" + SIGN_TOOL_VARIABLE + " is not set).")
    result = subprocess.run(iscc_command(iscc, script, version, kit, output, sign_tool), check=False)
    if result.returncode != 0:
        raise InstallerBuildError(f"Inno Setup failed with exit code {result.returncode}.")
    if not setup.is_file():
        raise InstallerBuildError(f"Inno Setup finished, but {setup} was not made.")
    return setup


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Build the Zodiac setup wizard with Inno Setup.")
    parser.add_argument("--iscc", required=True, type=Path, help="ISCC.exe, Inno Setup's command line compiler")
    parser.add_argument("--script", required=True, type=Path, help="packaging/zodiac.iss")
    parser.add_argument("--version", required=True, help="the kit version, such as 1.2.3")
    parser.add_argument("--kit", required=True, type=Path, help="the installed kit folder (dist/Zodiac)")
    parser.add_argument("--output", required=True, type=Path, help="the folder the setup is written to")
    args = parser.parse_args(argv)
    try:
        setup = build(args.iscc, args.script, args.version, args.kit, args.output, os.environ.get(SIGN_TOOL_VARIABLE) or None)
    except (InstallerBuildError, OSError) as error:
        say(f"Error: {error}")
        return 1
    say(f"Built {setup} ({setup.stat().st_size} bytes).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
