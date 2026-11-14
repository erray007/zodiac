#include <core/text.hpp>
#include <algorithm>
#include <cstddef>

namespace zodiac {
namespace {
bool needs_escape(char c) noexcept
{
const unsigned char byte=static_cast<unsigned char>(c);
return byte<0x20||byte==0x7F;
}
}
std::string error_text(const std::error_code& code)
{
std::string text=code.message();
const std::size_t end=text.find_last_not_of(" \t\r\n.");
text.resize(end==std::string::npos?0:end+1);
if(text.empty())text="error "+std::to_string(code.value());
return text;
}
bool is_one_line(std::string_view text) noexcept
{
return std::ranges::none_of(text,needs_escape);
}
std::string one_line(std::string_view text)
{
const std::size_t escapes=static_cast<std::size_t>(std::ranges::count_if(text,needs_escape));
std::string out;
out.reserve(text.size()+escapes*3);
constexpr char digits[]="0123456789abcdef";
for(const char c:text)
{
if(!needs_escape(c))
{
out.push_back(c);
continue;
}
switch(c)
{
case '\n':out.append("\\n");break;
case '\r':out.append("\\r");break;
case '\t':out.append("\\t");break;
default:
{
const unsigned char byte=static_cast<unsigned char>(c);
out.append("\\x");
out.push_back(digits[byte>>4]);
out.push_back(digits[byte&0xF]);
}
}
}
return out;
}
}
