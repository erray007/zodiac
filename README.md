# Zodiac

Zodiac is an open-source audio game development kit written in C++ with AngelScript scripting. It is designed so that blind developers can build, test and ship audio games using only a keyboard and a screen reader.

## Installing the kit (Windows)

Download the kit from the project's GitHub releases page. There are two ways to install it; both give the same kit.

- The setup wizard, `zodiac-setup-<version>.exe`. It uses only standard Windows controls, so it works with a screen reader and the keyboard alone. The first page asks whether to install for all users (needs administrator rights, installs into Program Files) or only for you (no administrator rights). The tasks page has four checkboxes, all ticked by default:
  - add Zodiac to the PATH, so the `zodiac` command works in every new terminal window;
  - open `.zs` files with Zodiac;
  - add "Build with Zodiac" to the right-click menu of `.zs` files;
  - Start menu shortcuts to the Zodiac manual and the examples.
  The setup is listed as "Zodiac Game Toolkit" in Settings, Apps, and the shortcuts are in the "Zodiac Game Toolkit" Start menu folder; the install folder is still named Zodiac.
  Installing a newer version over an older one replaces the kit in place and keeps the options you chose. The upgrade deletes the kit's bin, lib, modules and include folders before it copies the new ones, so anything you added there yourself is lost: keep your own files and modules outside the kit folder. Uninstalling (Settings, Apps) removes the kit folder and everything the setup added, and nothing else.
- The portable zip, `zodiac-<version>-windows-x86_64.zip`. Unzip the Zodiac folder anywhere; it changes nothing on your system. Its README.txt explains how to add its bin folder to your PATH yourself.

The setup is not signed yet, so Windows SmartScreen may warn that it is from an unknown publisher. To run it anyway, choose "More info", then "Run anyway".

### Using the kit

- `zodiac new mygame` creates a project folder, `zodiac run mygame` runs it while you work on it, and `zodiac build mygame` builds the finished game into the `mygame\release` folder, which runs on any Windows computer without the kit. Run `zodiac help` to see every command.
- Ship the whole release folder, not only `mygame.exe`: it also holds the DLLs of the modules your game lists as dynamic and the files your `[build] assets` patterns copy, and the game needs them next to the executable.
- Third-party DLLs: a dynamic module can need a closed library that exists only as a DLL, such as BASS or FMOD. Put that DLL in the same folder as the module's DLL (the kit's modules folder, or your project's plugins folder). The build reads which DLLs each module DLL loads, copies the ones found in that folder into the release folder (also the DLLs those need), signs them like the module DLLs when `[build.sign]` is set, and leaves out Windows' own DLLs. Any DLL found in the Windows system folder counts as Windows' own and is not shipped, including the Microsoft C++ runtime DLLs some libraries need (such as vcruntime140.dll or msvcp140.dll): ship the matching Microsoft Visual C++ Redistributable with your game, or put that DLL next to the module so the build copies it. A DLL that is neither next to the module nor part of Windows stops the build with `error: module "<name>" needs "<dll>", which is not next to "<module dll>"`. Shipping such a library is your responsibility: check its licence terms (BASS and FMOD, for example, have their own terms for commercial games) before you release the game.
- In a terminal, use `zodiac build --no-window mygame`: without it, the build also opens its result window when a desktop is available, and the command waits until you close it. `zodiac run` opens the same window only when the run reports an error (a compile, module or script error), titled "Zodiac run - <game>" with every message, one per line; `zodiac run --no-window` keeps everything on the console.
- Building a lone `.zs` file with no `zodiac.toml` links in every installed module. Its result window then has a "Create project file" button (Alt+P), which writes a `zodiac.toml` next to the script listing those modules, so you only delete the ones your game does not use. It never replaces an existing file; the window's last line says what happened.
- A build replaces the files it writes but never deletes anything else in the release folder, so files from earlier builds (an asset or a module you no longer use) stay there. Delete the release folder before building when you want a clean one.
- Double-clicking a `.zs` file (with the association option) runs it, exactly like `zodiac run` on that file; the console closes when the program ends, and when the run reported an error the result window stays open with the errors until you close it.
- Right-clicking a `.zs` file and choosing "Build with Zodiac" (with the right-click option) builds the project that file belongs to, then shows the result in a window: whether the build succeeded, where the game was written, and every error, one per line. On Windows 11 the entry is under "Show more options" (Shift+F10 opens that menu directly).

## Building Zodiac from source

Requirements: Python 3.9 or newer.

1. Download the pinned toolchain (CMake, Ninja and, on Windows, llvm-mingw and Inno Setup) into the .tools folder:
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

To build the kit packages on Windows, use the optimized release preset (the packaging targets refuse a Debug build):
`.tools\cmake\bin\cmake --workflow --preset windows-release`, then `.tools\cmake\bin\cmake --build build\windows-release --target zodiac_installer`.
This writes the kit folder `dist\Zodiac`, the zip and the setup into the dist folder; the target `zodiac_zip` makes only the folder and the zip.

To contribute, read CONTRIBUTING.md.

## Notes

- The bootstrap script verifies every download against a pinned SHA-256 checksum.
- Running the bootstrap script again does nothing if the tools are already installed.
- On Linux, clang 18 or newer with libc++ must be installed through your package manager, for example: sudo apt install clang-18 libc++-18-dev libc++abi-18-dev

## License

Zodiac is released under the zlib license. See the LICENSE file.
