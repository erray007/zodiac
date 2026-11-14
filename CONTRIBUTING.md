# Contributing to Zodiac

Zodiac is community owned and every change arrives as a pull request on GitHub. This guide is plain text with simple headings and lists so it works well with a screen reader.

## Set up

Requirements: Python 3.9 or newer. On Linux you also need clang 18 or newer with libc++ from your package manager, for example: sudo apt install clang-18 libc++-18-dev libc++abi-18-dev

1. Install the pinned toolchain into the .tools folder:
   `python bootstrap.py`
2. Build and run every test:
   - Windows (Command Prompt or PowerShell): `.tools\cmake\bin\cmake --workflow --preset windows`
   - Linux: `.tools/cmake/bin/cmake --workflow --preset linux`
3. If you change bootstrap.py, run its unit tests too (CI runs them on both systems):
   `python -I -m unittest discover -v -s tools -p "test_bootstrap.py"`

Windows commands use backslashes, because Command Prompt does not accept forward slashes in a program path. The build output goes to build\windows-debug on Windows and build/linux-debug on Linux. On Linux the build uses `clang` and `clang++` from your PATH, or the compilers named by the CC and CXX environment variables if you set them, and links libc++ statically.

## Build one target

Build a single target, for example the example module:

- Windows: `.tools\cmake\bin\cmake --build --preset windows-debug --target zodiac_mod_example`
- Linux: `.tools/cmake/bin/cmake --build --preset linux-debug --target zodiac_mod_example`

## Run one test

Run only the tests whose names match a pattern, for example the host tests:

- Windows: `.tools\cmake\bin\ctest --preset windows-debug -R test_host`
- Linux: `.tools/cmake/bin/ctest --preset linux-debug -R test_host`

## Try the command line tool

- `zodiac new <dir>` creates a project.
- `zodiac run [dir]` runs a project.
- `zodiac modules` lists the loaded modules.
- `zodiac --version` prints the version.
- `zodiac help` prints the help text.
- Options: `--log-file <path>` and `--module-dir <dir>`.

## Code style

- Write everything in English: code, comments, messages and documentation.
- Do not indent C, C++ or CMake code. Python and YAML keep the indentation their syntax requires.
- Only plain C types may cross the plugin ABI. Never put C++ types such as std::string in plugin.h.
- plugin.h is append-only. Never reorder, remove or change existing entries, only add new ones at the end.
- Every message the program prints or speaks is one plain line, so a screen reader reads it cleanly.
- Platform specific code lives only in src/platform/<os>/.

## Pull requests

- One topic per pull request.
- Every behavior change comes with a test.
- CI must be green on both Windows and Linux.
- A maintainer reviews every pull request before it is merged.
- Describe what changed and why in plain sentences.

## Write a plugin

A plugin is a shared library that registers functions for scripts. Include plugin.h for the stable C interface and plugin.hpp for the C++ helper that registers a plain function in one line. The helper supports int32_t, int64_t, float, double, bool and std::string_view parameters, and void, numeric, bool, std::string and std::vector<std::string> results. An exception thrown by your function becomes a script exception.

This is a trimmed copy of modules/example/example.cpp:

```cpp
#include <zodiac/plugin.h>
#include <zodiac/plugin.hpp>
#include <string>
#include <string_view>

namespace {
std::string echo(std::string_view text)
{
return std::string(text);
}

int register_module(zodiac_host* host)
{
return zodiac::plugin::function<&echo>(host,"string example_echo(const string &in)");
}
}

extern "C" const zodiac_module_info zodiac_module_info_example={ZODIAC_ABI_VERSION,sizeof(zodiac_module_info),"example","0.1.0","Zlib",nullptr,0,&register_module,nullptr};
ZODIAC_DEFINE_MODULE(zodiac_module_info_example)
```

The script declaration string must match the C++ function signature. For a hot path with only primitive parameters, register a plain `extern "C"` function with `host->register_native_function` as modules/example/example.cpp does for `example_add`.

Copy modules/example to start your own module, and add a test next to it.
