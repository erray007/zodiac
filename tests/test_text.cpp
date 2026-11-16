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
