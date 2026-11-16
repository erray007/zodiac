// The main script "zodiac new" writes (its zodiac.toml comes from project_file_text in core/project_file.hpp). Embedded as a raw string literal so the CLI needs no extra build step.
#pragma once
#include <string_view>

namespace zodiac::cli {
inline constexpr std::string_view main_script=R"(// Your game starts here.
void main()
{
}
)";
}
