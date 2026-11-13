#include <core/text.hpp>
#include <doctest/doctest.h>
#include <string>
#include <string_view>

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
