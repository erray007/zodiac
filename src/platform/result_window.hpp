// The result window of zodiac build and of a failed zodiac run (release-and-kit spec 8.2a, kit usability spec decisions 2 and 3), implemented per
// platform in src/platform/<os>/result_window.cpp and linked into the command line front end only (games never get it).
//
// Windows: a window titled title, built from standard Win32 controls only so screen readers (NVDA, JAWS) read it: a static label output_label
// ("&Build output" or "&Run output", the & marking its Alt mnemonic) followed (in z-order and tab order) by a read-only multi-line EDIT control
// (ES_MULTILINE|ES_READONLY|ES_AUTOHSCROLL|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP) holding text without word wrap (each line of text is one edit line,
// so each arrow press reads one whole message), focused when the window opens with the caret at position 0, then the buttons "Copy all", the
// action's button (only when action is non-null, labeled action->label), "Open output folder" (only when output_folder is non-null) and "Close"
// (the default button; Esc and the window's close box also close). Keyboard handling is the dialog manager's (IsDialogMessageW): Tab and
// Shift+Tab move between controls, Enter in the edit presses Close. Controls use the system message font for the window's DPI. Copy all puts the
// whole text (with any action line appended) on the clipboard as CF_UNICODETEXT; Open output folder runs ShellExecuteW "open" on the folder.
// A failed copy or open beeps (MB_ICONERROR) and leaves the window open.
// Pressing the action's button calls action->run() once, on the window's thread (the window does not answer until it returns), and appends the
// one UTF-8 line it returns to the edit as a new last line. The caret moves to the start of that line, focus moves to the edit (so a screen
// reader reads the line), and only then is the button disabled, so focus is never lost. A run() that throws beeps (MB_ICONERROR) and leaves the
// button enabled.
// The window asks for the foreground (SetForegroundWindow). When Windows refuses (the foreground lock, e.g. the user kept typing in Explorer
// during the build), it plays MessageBeep(MB_OK) when succeeded or MessageBeep(MB_ICONERROR) otherwise and flashes its taskbar button
// (FlashWindowEx, until it is activated), so it can be found. A WM_QUIT already pending on the thread when the window opens (left behind by a
// game's own window in zodiac run) is discarded, so it cannot close the window at once; one that arrives while the window is open closes it and
// is posted again for an outer loop.
// Other hosts: no desktop is reported and show_result_window does nothing (until a Linux host plan adds a window).
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace zodiac::platform {
// An extra button of the result window. label is the button's text with its & mnemonic and must outlive the window; run performs the action and
// returns the one line (UTF-8, no line break) that reports its outcome.
struct ResultAction{
std::wstring_view label;
std::function<std::string()> run;
};
// True when a window can be shown to a user: Windows, the process's window station is visible (WSF_VISIBLE; a service session's is not).
// The documented test hook ZODIAC_FORCE_NO_DESKTOP=1 (exactly "1") in the environment makes it false. Always false on other hosts.
bool has_interactive_desktop() noexcept;
// Shows the result window and blocks until the user closes it. title and text are UTF-8; text's lines are separated by "\n" (shown with "\r\n").
// succeeded picks the sound played when the window cannot take the foreground. output_folder, when non-null, is the folder "Open output folder"
// opens; action, when non-null, adds its button. Call from the main thread with no other window open; it runs its own message loop. Failures to
// create the window are silent: the caller has already put the result on the console.
void show_result_window(std::string_view title,std::wstring_view output_label,std::string_view text,bool succeeded,const std::filesystem::path* output_folder,const ResultAction* action);
}
