#include <core/text.hpp>
#include <algorithm>
#include <cstddef>
#include <type_traits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <memory>
#endif

namespace zodiac {
namespace {
#ifdef _WIN32
struct LocalFreeDeleter {
void operator()(wchar_t* buffer) const noexcept{LocalFree(buffer);}
};
// The system's text for a Win32 error code in the user's language, as UTF-8. std::system_category goes through the ANSI
// code page here, which is not UTF-8 on most systems (Turkish Windows uses 1254). Empty when the system has no text.
std::string windows_error_text(DWORD code)
{
wchar_t* raw=nullptr;
const DWORD length=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,reinterpret_cast<LPWSTR>(&raw),0,nullptr);
const std::unique_ptr<wchar_t,LocalFreeDeleter> buffer(raw);
if(length==0||raw==nullptr)return {};
const int size=WideCharToMultiByte(CP_UTF8,0,raw,static_cast<int>(length),nullptr,0,nullptr,nullptr);
if(size<=0)return {};
std::string text(static_cast<std::size_t>(size),'\0');
if(WideCharToMultiByte(CP_UTF8,0,raw,static_cast<int>(length),text.data(),size,nullptr,nullptr)!=size)return {};
return text;
}
#endif
bool needs_escape(char c) noexcept
{
const unsigned char byte=static_cast<unsigned char>(c);
return byte<0x20||byte==0x7F;
}
// Length in bytes of the UTF-8 character starting at text[index]: the lead byte plus its continuation bytes (10xxxxxx).
std::size_t character_length(std::string_view text,std::size_t index) noexcept
{
std::size_t end=index+1;
while(end<text.size()&&(static_cast<unsigned char>(text[end])&0xC0)==0x80)++end;
return end-index;
}
// Matches one path segment (no '/') against one pattern segment with '*' and '?'.
bool segment_match(std::string_view pattern,std::string_view text) noexcept
{
std::size_t p=0;
std::size_t t=0;
std::size_t star=std::string_view::npos;
std::size_t mark=0;
while(t<text.size())
{
if(p<pattern.size()&&pattern[p]=='*')
{
star=p++;
mark=t;
}
else if(p<pattern.size()&&pattern[p]=='?')
{
++p;
t+=character_length(text,t);
}
else if(p<pattern.size()&&pattern[p]==text[t])
{
++p;
++t;
}
else if(star!=std::string_view::npos)
{
p=star+1;
mark+=character_length(text,mark);
t=mark;
}
else return false;
}
while(p<pattern.size()&&pattern[p]=='*')++p;
return p==pattern.size();
}
}
bool glob_match(std::string_view pattern,std::string_view path) noexcept
{
for(;;)
{
const std::size_t pattern_cut=pattern.find('/');
const std::string_view segment=pattern.substr(0,pattern_cut);
const bool more_pattern=pattern_cut!=std::string_view::npos;
const std::string_view pattern_rest=more_pattern?pattern.substr(pattern_cut+1):std::string_view();
if(segment=="**")
{
if(!more_pattern)return true;
for(;;)
{
if(glob_match(pattern_rest,path))return true;
const std::size_t cut=path.find('/');
if(cut==std::string_view::npos)return false;
path.remove_prefix(cut+1);
}
}
const std::size_t path_cut=path.find('/');
const bool more_path=path_cut!=std::string_view::npos;
if(more_pattern!=more_path||!segment_match(segment,path.substr(0,path_cut)))return false;
if(!more_pattern)return true;
pattern=pattern_rest;
path.remove_prefix(path_cut+1);
}
}
bool has_extension(const std::filesystem::path& path,std::string_view extension)
{
const std::filesystem::path actual=path.extension();
const auto& text=actual.native();
if(text.size()!=extension.size())return false;
// Code units are compared as unsigned numbers (wchar_t on Windows, char elsewhere); only A-Z fold.
const auto lower=[](unsigned long c){return c>=0x41&&c<=0x5A?c+0x20:c;};
for(std::size_t i=0;i<text.size();++i){
if(lower(static_cast<std::make_unsigned_t<std::filesystem::path::value_type>>(text[i]))!=lower(static_cast<unsigned char>(extension[i])))return false;
}
return true;
}
bool same_path_text(const std::filesystem::path& a,const std::filesystem::path& b) noexcept
{
#ifdef _WIN32
const std::wstring& left=a.native();
const std::wstring& right=b.native();
return CompareStringOrdinal(left.c_str(),static_cast<int>(left.size()),right.c_str(),static_cast<int>(right.size()),TRUE)==CSTR_EQUAL;
#else
return a.native()==b.native();
#endif
}
std::string error_text(const std::error_code& code)
{
#ifdef _WIN32
std::string text=code.category()==std::system_category()?windows_error_text(static_cast<DWORD>(code.value())):code.message();
#else
std::string text=code.message();
#endif
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
