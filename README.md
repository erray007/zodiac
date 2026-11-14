# Zodiac

Zodiac is an open-source audio game development kit written in C++ with AngelScript scripting. It is designed so that blind developers can build, test and ship audio games using only a keyboard and a screen reader.

## Quick start

Requirements: Python 3.9 or newer.

1. Download the pinned toolchain (CMake, Ninja and, on Windows, llvm-mingw) into the .tools folder:
   `python bootstrap.py`
2. Build and test on Windows (use the preset name shown by the bootstrap script on other systems):
   `.tools/cmake/bin/cmake --workflow --preset windows`
3. Create a new game project:
   `build/windows-debug/bin/zodiac new mygame`
4. Run it:
   `build/windows-debug/bin/zodiac run mygame`

On Linux, replace `windows` with `linux` in the preset and in the build folder name.

To contribute, read CONTRIBUTING.md.

## Notes

- The bootstrap script verifies every download against a pinned SHA-256 checksum.
- Running the bootstrap script again does nothing if the tools are already installed.
- On Linux, clang 17 or newer must be installed through your package manager.

## License

Zodiac is released under the zlib license. See the LICENSE file.
