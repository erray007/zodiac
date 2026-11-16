// Entry point of a release executable on Windows (GUI subsystem, linked with -mwindows -municode).
// zodiac_payload is defined only by the payload object zodiac build writes (core/coff_object.hpp): a u64 little-endian size followed by that many payload bytes.
// This file is its own archive member of zodiac_release_stub, so programs that only call release::start (the tests) never pull in this reference.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <release/stub.hpp>
#include <platform/console.hpp>
#include <platform/process.hpp>
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" const unsigned char zodiac_payload[];

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int)
{
zodiac::platform::disable_error_dialogs();
zodiac::platform::attach_parent_console();
zodiac::platform::setup_utf8_console();
std::uint64_t size=0;
for(std::size_t i=0;i<8;++i)size|=static_cast<std::uint64_t>(zodiac_payload[i])<<(8*i);
const std::string_view payload(reinterpret_cast<const char*>(zodiac_payload+8),static_cast<std::size_t>(size));
return zodiac::release::start(payload,zodiac::platform::executable_path());
}
