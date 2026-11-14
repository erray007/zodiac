# Zodiac

Zodiac is an open-source audio game development kit written in C++ with AngelScript scripting. It is designed so that blind developers can build, test and ship audio games using only a keyboard and a screen reader.

## Quick start

Requirements: Python 3.9 or newer.

1. Download the pinned toolchain (CMake, Ninja and, on Windows, llvm-mingw) into the .tools folder:
   `python bootstrap.py`
2. Build and test:
   - Windows (Command Prompt or PowerShell): `.tools\cmake\bin\cmake --workflow --preset windows`
   - Linux: `.tools/cmake/bin/cmake --workflow --preset linux`
3. Create a new game project:
   - Windows: `build\windows-debug\bin\zodiac new mygame`
   - Linux: `build/linux-debug/bin/zodiac new mygame`
4. Run it:
   - Windows: `build\windows-debug\bin\zodiac run mygame`
   - Linux: `build/linux-debug/bin/zodiac run mygame`

Windows commands use backslashes, because Command Prompt does not accept forward slashes in a program path.

To contribute, read CONTRIBUTING.md.

## Notes

- The bootstrap script verifies every download against a pinned SHA-256 checksum.
- Running the bootstrap script again does nothing if the tools are already installed.
- On Linux, clang 18 or newer with libc++ must be installed through your package manager, for example: sudo apt install clang-18 libc++-18-dev libc++abi-18-dev

## License

Zodiac is released under the zlib license. See the LICENSE file.
