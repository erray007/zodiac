// The build result window of zodiac build (release-and-kit spec 8.2a), implemented per platform in src/platform/<os>/result_window.cpp
// and linked into the command line front end only (games never get it).
//
// Windows: a window titled title, built from standard Win32 controls only so screen readers (NVDA, JAWS) read it: a static label "Build output"
// followed (in z-order and tab order) by a read-only multi-line EDIT control (ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP) holding text,
// focused when the window opens with the caret at position 0, then the buttons "Copy all", "Open output folder" (only when output_folder is
// non-null) and "Close" (the default button; Esc and the window's close box also close). Keyboard handling is the dialog manager's
// (IsDialogMessageW): Tab and Shift+Tab move between controls, Enter in the edit presses Close. Controls use the system message font for the
// window's DPI. Copy all puts the whole text on the clipboard as CF_UNICODETEXT; Open output folder runs ShellExecuteW "open" on the folder.
// A failed copy or open beeps (MB_ICONERROR) and leaves the window open.
// Other hosts: no desktop is reported and show_result_window does nothing (until a Linux host plan adds a window).
#pragma once
#include <filesystem>
#include <string_view>

namespace zodiac::platform {
// True when a window can be shown to a user: Windows, the process's window station is visible (WSF_VISIBLE; a service session's is not).
// The documented test hook ZODIAC_FORCE_NO_DESKTOP=1 (exactly "1") in the environment makes it false. Always false on other hosts.
bool has_interactive_desktop() noexcept;
// Shows the result window and blocks until the user closes it. title and text are UTF-8; text's lines are separated by "\n" (shown with "\r\n").
// output_folder, when non-null, is the folder "Open output folder" opens. Call from the main thread with no other window open; it runs its own
// message loop. Failures to create the window are silent: the caller has already put the result on the console.
void show_result_window(std::string_view title,std::string_view text,const std::filesystem::path* output_folder);
}
