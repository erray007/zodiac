# Zodiac Kit Usability Changes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Building a lone `.zs` file works, the result window offers to create a project file, `zodiac run` errors appear in the accessible window, `--config` names the project file, and the setup is listed as "Zodiac Game Toolkit".

**Architecture:** Extends plan 3's CLI (`src/cli/build_command.*`, `src/cli/main.cpp`, `src/cli/build_report.*`), project loading (`src/core/project_file.*`), the result window (`src/platform/*/result_window.*`) and the installer (`packaging/zodiac.iss`, `.github/workflows/ci.yml`).

**Tech Stack:** C++23, clang (llvm-mingw 20261104), CMake + Ninja, doctest, Python e2e tests, Inno Setup 6.7.4.

**Spec:** `docs/superpowers/specs/2026-11-16-zodiac-kit-usability-design.md` (this change), amending `docs/superpowers/specs/2026-11-16-zodiac-release-and-kit-design.md`.

## Global Constraints

- All Global Constraints of `docs/superpowers/plans/2026-11-16-zodiac-release-kit.md` still apply (style without indentation in C++/CMake, English identifiers, one-line diagnostics, exit codes 0/1/2, identity rule, paths with spaces and non-ASCII, Linux CI green, Windows-only release builds).
- Exact texts: warning `warning: no zodiac.toml; every installed module was linked in (create a project file to choose modules)`; button `Create project file`; window outcome lines `Created <path>` or the error line; run window title `Zodiac run - <game name>`; run summary `Run failed: <N> errors` (`1 error`); config error `error: config file "<path>" does not exist`; setup display name `Zodiac Game Toolkit`.
- The result window stays standard Win32 controls only, labeled, keyboard-operable, no word wrap (plan 3 Task 9 rules).
- Never use heredocs or shell redirection to create or edit files (owner's rule).

## Review Focus

1. A lone `.zs` in a folder whose parent has an unrelated `zodiac.toml`: the parent project is used (upward search), not the lone-file path — must stay as today.
2. `Create project file` when `zodiac.toml` already appeared meanwhile: never overwritten, error line shown.
3. `--config` with a relative path from a different working directory: resolved against the cwd at launch, before `enter_root`.
4. `zodiac run` of a game that exits with code 1 from its own `int main()` (not an engine error): no window — only engine-reported errors open it. Decision: the window opens when at least one ERROR-level diagnostic was reported during the run.
5. Upgrade from a kit installed under the old display name: same AppId, so it upgrades in place and the entry is renamed.

---

### Task 1: Lone-file build, generated project file text and `--config`

**Files:**
- Modify: `src/core/project_file.hpp`, `src/core/project_file.cpp`, `src/cli/main.cpp`, `src/cli/build_command.hpp`, `src/cli/build_command.cpp`
- Test: `tests/test_project_file.cpp`, `tests/e2e/test_build.py`, `tests/e2e/test_cli.py`

**Interfaces:**
- Produces in `project_file.hpp`:
  - `std::expected<Project,std::string> load_project_file(const std::filesystem::path& config_file);` — like `load_project(dir)` but reads the given file (any name/extension); root = the file's folder (absolute, normalized). Missing file: `error: config file "<path>" does not exist`. `load_project(dir)` becomes `load_project_file(dir/"zodiac.toml")` with its existing messages unchanged.
  - `std::string project_file_text(std::string_view name,std::string_view main_relative,std::span<const std::string> static_modules);` — the exact `zodiac new` format (see `src/cli/templates.hpp`) with `main` and `static` filled in, TOML-escaped.
- CLI: `run` and `build` accept `--config <file>` and `--config=<file>`; positional `.zs` + `--config` overrides `main`; positional folder + `--config` → `error: --config cannot be combined with a project folder` (exit 2). The config path is made absolute against the launch cwd before anything else.
- `build_project`: when no project root is found for a `.zs` target and no `--config` is given, build with `load_project_for_script` semantics (installed modules, static) and report the exact warning line. `BuildOptions` gains `std::filesystem::path config;` and the build result exposes `bool used_lone_file` plus the linked module names so Task 2 can generate the file.

- [ ] **Step 1: Write the failing tests** — unit: `load_project_file` on `my settings.txt` in a folder with a space returns root = that folder and resolves `main` relative to it; missing file gives the exact error; `project_file_text("oyun","menu.zs",{"example"})` round-trips through `load_project_file` (write to temp, load, compare name/main/static). e2e: lone `.zs` build succeeds with the warning line and `Build succeeded:`; the exe runs; `build --config=<abs>\ayar.txt` and `run --config <rel>` from another cwd work; folder + `--config` exits 2 with the exact message; Review Focus 1 (parent project wins).
- [ ] **Step 2: Run to verify they fail** (`.tools/cmake/bin/ctest --preset windows-debug -R "test_project_file|build_e2e|cli_e2e"`).
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run tests to verify they pass**, full suite once (Windows; Linux via CI).
- [ ] **Step 5: Commit** `git commit -m "Build a lone .zs file and name the project file with --config"`

### Task 2: Create-project-file button and run error window

**Files:**
- Modify: `src/platform/result_window.hpp`, `src/platform/windows/result_window.cpp`, `src/platform/posix/result_window.cpp`, `src/cli/build_report.hpp`, `src/cli/build_report.cpp`, `src/cli/build_command.cpp`, `src/cli/main.cpp`
- Test: `tests/test_build_report.cpp`, `tests/e2e/test_build.py`, `tests/e2e/test_cli.py`

**Interfaces:**
- Consumes: Task 1's `project_file_text`, `used_lone_file`, linked module names.
- `show_result_window` gains an optional action: `struct ResultAction{std::wstring_view label;std::function<std::string()> run;};` passed as `const ResultAction*` (nullptr = no button). The button `Create project file` sits after `Copy all`; pressing it runs the action and appends its returned line (`Created <path>` or the error) to the edit, moves the caret to that line, and disables the button. One window function serves build and run.
- `BuildReport` generalizes to `ResultReport` with a verb (`Build`/`Run`): `Build succeeded: <exe>` / `Build failed: <N> errors` as today; `Run failed: <N> errors` for run (run has no success line because no window opens on success).
- `zodiac run` without `--no-window`, with an interactive desktop, after the game ends: if at least one ERROR-level diagnostic was reported (Review Focus 4), show the window titled `Zodiac run - <game name>` with the summary and lines, no output-folder button, no action. Exit code unchanged.
- The create action writes `<script folder>/zodiac.toml` with `project_file_text(<stem>, <script file name>, <linked modules>)`, refusing if the file exists (`error: "<path>" already exists`).

- [ ] **Step 1: Write the failing tests** — unit: `ResultReport` run summary singular/plural; the create action writes the expected text and refuses an existing file. e2e with `ZODIAC_FORCE_NO_DESKTOP=1`: a failing `run` prints `Run failed: 1 error` as the last stdout line; a clean `run` prints no summary; `--no-window` never prints the run summary.
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement.** Manual window check like plan 3 Task 9: drive the window through Win32, press `Create project file`, confirm the appended line, focus and caret, and that the button is disabled after use; open a run-error window and close it with Esc.
- [ ] **Step 4: Run tests to verify they pass**, full suite once.
- [ ] **Step 5: Commit** `git commit -m "Offer to create the project file and show run errors in the result window"`

### Task 3: Setup display name

**Files:**
- Modify: `packaging/zodiac.iss`, `.github/workflows/ci.yml`, `README.md`

**Interfaces:**
- `AppName=Zodiac Game Toolkit`, `UninstallDisplayName=Zodiac Game Toolkit`, Start menu group `Zodiac Game Toolkit`; `AppId` unchanged (upgrade in place, Review Focus 5). Shortcut names keep "Zodiac manual" / "Zodiac examples".
- CI `kit-clean-machine`: assert the uninstall entry's `DisplayName` is `Zodiac Game Toolkit` and the Start menu folder path uses the new name.

- [ ] **Step 1:** Update the CI assertions first (they fail against the old setup).
- [ ] **Step 2:** Update the `.iss` and README.
- [ ] **Step 3:** Local silent per-user install/uninstall check; push the branch; `kit-windows` and `kit-clean-machine` green.
- [ ] **Step 4: Commit** `git commit -m "List the setup as Zodiac Game Toolkit"`
