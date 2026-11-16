# Zodiac Kit Usability Changes — Short Design

Status: approved by the owner (2026-11-16), including the display name "Zodiac Game Toolkit". Amends `docs/superpowers/specs/2026-11-16-zodiac-release-and-kit-design.md` (plan 3) after the owner's first hands-on test of the installed kit with NVDA.

## Intent

The owner tried the kit: right-click "Build with Zodiac" on a lone `.zs` file stopped with `zodiac.toml not found`, and a script error on double-click vanished with the closing console. Building and error reporting must work for someone who never opens a terminal.

## Decisions (owner)

1. **Build without a project file (A + B together).** `zodiac build <file.zs>` with no `zodiac.toml` in the file's folder or its parents no longer fails. It builds like `zodiac run` does for a lone script: every installed module linked statically, the file as main script, the root is the file's folder, the game name is the file name without extension. The result gets the warning line `warning: no zodiac.toml; every installed module was linked in (create a project file to choose modules)`.
2. **"Create project file" button.** The build result window shows a `Create project file` button when the build ran without a project file. It writes `zodiac.toml` next to the script, in the same format `zodiac new` writes, with `main` set to that script and `static` listing the installed modules that were linked, so the developer only deletes what the game does not use. It never overwrites an existing file. The button reports the outcome in the window's text (`Created <path>` or the error).
3. **Window rule.** Without `--no-window`:
   - `zodiac build` always shows the result window, on success and failure (as now).
   - `zodiac run` shows the same accessible window only when the run ends with an error (compile error, module error, uncaught exception, exit code 1 or 2 from the engine). A clean exit shows nothing. Title `Zodiac run - <game name>`, the first line `Run failed: <N> errors`, no output folder button.
   - With `--no-window`, nothing is shown and everything goes to the console. When no interactive desktop exists, both commands behave as with `--no-window`.
4. **`--config <file>` flag.** `zodiac run` and `zodiac build` accept `--config <file>` (also `--config=<file>`) naming the project file explicitly, with any file name or extension (`.toml`, `.txt`, ...). The content is the same TOML as `zodiac.toml`. The project root becomes the config file's folder; every relative path in it resolves against that folder. A positional `.zs` argument together with `--config` overrides `main`; a positional folder together with `--config` is a usage error (exit 2). A missing or unreadable config file is `error: config file "<path>" does not exist` (exit 2).
5. **Name in Windows "Apps / Programs and Features".** The setup's display name becomes `Zodiac Game Toolkit` (Inno `AppName` and the uninstall entry's `DisplayName`). The Start menu folder uses the same name.

## Left to the builder

- Where the window code is shared between build and run (one function for both).
- How `run` collects its diagnostics for the window (the same `BuildReport` style sink).
- Tests: e2e for the lone-file build, `--config` (both spellings, `.txt` name, relative paths, conflicts), the run-error window path through `ZODIAC_FORCE_NO_DESKTOP` (prints the summary) and a unit test of the generated `zodiac.toml` text; CI's setup checks updated for the new display name.
