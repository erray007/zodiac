// Entry point of a release executable on Windows (GUI subsystem, linked with -mwindows -municode).
// zodiac_payload and zodiac_payload_end are defined only by the payload object zodiac build writes (core/coff_object.hpp): they bound a u64
// little-endian size followed by the payload, and start_linked checks the size against them before reading anything.
// This file is its own archive member of zodiac_release_stub, so programs that only call release::start (the tests) never pull in these references.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <release/stub.hpp>
#include <platform/console.hpp>
#include <platform/process.hpp>
#include <windows.h>

extern "C" const unsigned char zodiac_payload[];
extern "C" const unsigned char zodiac_payload_end[];

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int)
{
zodiac::platform::disable_error_dialogs();
zodiac::platform::attach_parent_console();
zodiac::platform::setup_utf8_console();
try{
return zodiac::release::start_linked(zodiac_payload,zodiac_payload_end,zodiac::platform::executable_path());
}
catch(...){
// Only executable_path can throw here (out of memory); nothing may reach std::terminate.
return 1;
}
}
