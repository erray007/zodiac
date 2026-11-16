// Example Zodiac module (license Zlib): a template for module authors and the fixture for the module loader tests.
#include <zodiac/plugin.h>
#include <zodiac/plugin.hpp>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace {
std::string echo(std::string_view text)
{
return std::string(text);
}
void print(std::string_view text)
{
std::fwrite(text.data(),1,text.size(),stdout);
std::fputc('\n',stdout);
}
}

// Native fast path: primitive parameters only, C calling convention, no per-call marshalling.
extern "C" int32_t zodiac_example_add(int32_t a,int32_t b)
{
return static_cast<int32_t>(static_cast<uint32_t>(a)+static_cast<uint32_t>(b));
}

namespace {
int register_module(zodiac_host* host)
{
int code=zodiac::plugin::function<&echo>(host,"string example_echo(const string &in)");
if(code!=ZODIAC_OK)return code;
code=host->register_native_function(host,"int example_add(int, int)",reinterpret_cast<void(*)(void)>(&zodiac_example_add));
if(code!=ZODIAC_OK)return code;
return zodiac::plugin::function<&print>(host,"void example_print(const string &in)");
}
}

// External linkage so a static build can reference it; the shared build exports only zodiac_module_get_info.
extern "C" const zodiac_module_info zodiac_module_info_example={ZODIAC_ABI_VERSION,sizeof(zodiac_module_info),"example","0.1.0","Zlib",nullptr,0,&register_module,nullptr,ZODIAC_LINK_STATIC|ZODIAC_LINK_DYNAMIC};
ZODIAC_DEFINE_MODULE(zodiac_module_info_example)
