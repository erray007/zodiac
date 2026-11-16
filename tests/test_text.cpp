#include <core/text.hpp>
#include <doctest/doctest.h>
#include <string>
#include <string_view>

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
