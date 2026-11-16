// Files written by "zodiac new". Embedded as raw string literals so the CLI needs no extra build step.
#pragma once
#include <string_view>

namespace zodiac::cli {
// The project file starts with the [game] name line; the generator inserts the escaped name between the two parts.
inline constexpr std::string_view toml_before_name=R"(# Zodiac project file.
[game]
name = ")";
inline constexpr std::string_view toml_after_name=R"("
main = "main.zs"

[modules]
# Modules this game uses, for example "example". Both lists are searched as shared libraries.
static = []
dynamic = []
)";
inline constexpr std::string_view main_script=R"(// Your game starts here.
void main()
{
}
)";
}
