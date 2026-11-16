#include <core/text.hpp>
#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

TEST_CASE("is_one_line tells whether one_line would change the text")
{
CHECK(zodiac::is_one_line(""));
CHECK(zodiac::is_one_line("plain \xC5\x9F text\\"));
CHECK_FALSE(zodiac::is_one_line("a\nb"));
CHECK_FALSE(zodiac::is_one_line(std::string_view("a\0b",3)));
CHECK_FALSE(zodiac::is_one_line("x\x7F"));
}

TEST_CASE("error_text has no trailing line break or full stop")
{
const std::string text=zodiac::error_text(std::make_error_code(std::errc::no_such_file_or_directory));
CHECK_FALSE(text.empty());
CHECK(zodiac::is_one_line(text));
CHECK_FALSE(text.ends_with('.'));
CHECK_FALSE(text.ends_with(' '));
}

#ifdef _WIN32
TEST_CASE("error_text names a Win32 code the system has no text for")
{
CHECK(zodiac::error_text(std::error_code(0x2FFFFFFF,std::system_category()))=="error 805306367");
}

TEST_CASE("error_text describes a Win32 error with the system's own text in UTF-8")
{
// ERROR_ACCESS_DENIED: the same text FormatMessageW gives, converted to UTF-8 (on Turkish Windows "Erişim engellendi").
const std::string text=zodiac::error_text(std::error_code(ERROR_ACCESS_DENIED,std::system_category()));
CHECK_FALSE(text.empty());
CHECK(zodiac::is_one_line(text));
CHECK_FALSE(text.ends_with('.'));
CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)>0);
wchar_t* raw=nullptr;
const DWORD length=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,ERROR_ACCESS_DENIED,0,reinterpret_cast<LPWSTR>(&raw),0,nullptr);
REQUIRE(length>0);
std::wstring wide(raw,length);
LocalFree(raw);
while(!wide.empty()&&(wide.back()==L'\r'||wide.back()==L'\n'||wide.back()==L' '||wide.back()==L'\t'||wide.back()==L'.'))wide.pop_back();
std::string expected(static_cast<std::size_t>(WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr)),'\0');
WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),expected.data(),static_cast<int>(expected.size()),nullptr,nullptr);
CHECK(text==expected);
}
#endif

TEST_CASE("has_extension compares the extension ignoring ASCII case")
{
CHECK(zodiac::has_extension("main.zs",".zs"));
CHECK(zodiac::has_extension("MAIN.ZS",".zs"));
CHECK(zodiac::has_extension("dir/Menu.Zs",".zs"));
CHECK(zodiac::has_extension(std::filesystem::path(u8"şövalye.zS"),".zs"));
CHECK_FALSE(zodiac::has_extension("main.zsx",".zs"));
CHECK_FALSE(zodiac::has_extension("main.z",".zs"));
CHECK_FALSE(zodiac::has_extension("main",".zs"));
CHECK_FALSE(zodiac::has_extension("zs",".zs"));
// A file name that only starts with a dot has no extension.
CHECK_FALSE(zodiac::has_extension(".zs",".zs"));
CHECK_FALSE(zodiac::has_extension("main.ys",".zs"));
}

TEST_CASE("one_line escapes control characters")
{
CHECK(zodiac::one_line("a\nb")=="a\\nb");
CHECK(zodiac::one_line("a\r\nb\tc")=="a\\r\\nb\\tc");
CHECK(zodiac::one_line(std::string_view("a\0b",3))=="a\\x00b");
CHECK(zodiac::one_line("x\x7F")=="x\\x7f");
CHECK(zodiac::one_line("\x1b[0m")=="\\x1b[0m");
}

TEST_CASE("one_line leaves normal text, backslashes and UTF-8 untouched")
{
CHECK(zodiac::one_line("")=="");
CHECK(zodiac::one_line("plain text")=="plain text");
CHECK(zodiac::one_line("a\\b")=="a\\b");
CHECK(zodiac::one_line("Kanka \xC5\x9F\xC3\xB6v")=="Kanka \xC5\x9F\xC3\xB6v");
}

TEST_CASE("glob_match")
{
CHECK(zodiac::glob_match("sounds/*.ogg","sounds/shot.ogg"));
CHECK_FALSE(zodiac::glob_match("sounds/*.ogg","sounds/guns/shot.ogg"));
CHECK(zodiac::glob_match("sounds/**/*.ogg","sounds/guns/rifle/shot.ogg"));
CHECK(zodiac::glob_match("sounds/**/*.ogg","sounds/shot.ogg"));
CHECK(zodiac::glob_match("maps/level?.map","maps/level1.map"));
CHECK_FALSE(zodiac::glob_match("maps/level?.map","maps/level10.map"));
CHECK(zodiac::glob_match("**","a/b/c.txt"));
}

TEST_CASE("glob_match question mark consumes one UTF-8 character")
{
CHECK(zodiac::glob_match("maps/?.map","maps/\xC5\x9F.map"));
CHECK_FALSE(zodiac::glob_match("maps/?.map","maps/\xC5\x9F\xC5\x9F.map"));
CHECK(zodiac::glob_match("maps/??.map","maps/\xC5\x9F\xC5\x9F.map"));
CHECK(zodiac::glob_match("*?.map","\xC5\x9F\xC5\x9F.map"));
CHECK_FALSE(zodiac::glob_match("?","/"));
}

TEST_CASE("glob_match double star edge cases")
{
CHECK(zodiac::glob_match("a/**","a/b/c"));
CHECK(zodiac::glob_match("a/**","a/b"));
CHECK_FALSE(zodiac::glob_match("a/**","b/c"));
CHECK(zodiac::glob_match("**/**/x","a/b/x"));
CHECK(zodiac::glob_match("**/**/x","x"));
CHECK_FALSE(zodiac::glob_match("**/**/x","a/b/y"));
CHECK(zodiac::glob_match("**/x.ogg","x.ogg"));
CHECK(zodiac::glob_match("**/x.ogg","a/b/x.ogg"));
CHECK_FALSE(zodiac::glob_match("**/x.ogg","a/b/x.wav"));
}

TEST_CASE("glob_match empty text and star backtracking")
{
CHECK(zodiac::glob_match("",""));
CHECK_FALSE(zodiac::glob_match("","a"));
CHECK_FALSE(zodiac::glob_match("a",""));
CHECK(zodiac::glob_match("a*b*c","axbxc"));
CHECK_FALSE(zodiac::glob_match("a*b*c","axbx"));
CHECK(zodiac::glob_match("a*b*c","abc"));
CHECK(zodiac::glob_match("*b","abb"));
}
