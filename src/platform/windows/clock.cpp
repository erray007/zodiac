// The clocks on Windows (see platform/clock.hpp).
// - monotonic_us reads the performance counter. Its frequency is fixed at boot, so it is read once (a function-local
//   static, so a static initializer of another file may call monotonic_us first); with the 10 MHz frequency of every
//   current Windows version the conversion is one division by a constant, otherwise whole seconds and the remainder are
//   scaled apart so no product can overflow.
// - utc_now_ms reads GetSystemTimePreciseAsFileTime (100 ns ticks since 1601).
// - utc_to_local_civil_ms converts through SYSTEMTIME with SystemTimeToTzSpecificLocalTimeEx and the dynamic time zone read
//   on each call, so a time zone change made while the game runs is seen.
// Windows 10 is Zodiac's minimum (foundation spec); the toolchain's headers default to Windows 7 and hide
// SystemTimeToTzSpecificLocalTimeEx behind Windows 8.
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <platform/clock.hpp>
#include <windows.h>

namespace zodiac::platform {
namespace {
// FILETIME ticks (100 ns) per millisecond, and the ticks from 1601-01-01 to 1970-01-01.
constexpr std::int64_t kTicksPerMs=10000;
constexpr std::int64_t kEpochTicks=116444736000000000;
// The milliseconds since 1970 that FileTimeToSystemTime accepts: FILETIME values from 0 to 2^63 - 1.
constexpr std::int64_t kFirstMs=-kEpochTicks/kTicksPerMs;
constexpr std::int64_t kLastMs=(INT64_MAX-kEpochTicks)/kTicksPerMs;

std::int64_t counter_frequency() noexcept
{
LARGE_INTEGER frequency;
QueryPerformanceFrequency(&frequency);
return frequency.QuadPart;
}
std::int64_t floor_div(std::int64_t value,std::int64_t divisor) noexcept
{
const std::int64_t quotient=value/divisor;
return quotient-(value%divisor<0);
}
}

std::int64_t monotonic_us() noexcept
{
static const std::int64_t frequency=counter_frequency();
LARGE_INTEGER counter;
QueryPerformanceCounter(&counter);
const std::int64_t ticks=counter.QuadPart;
if(frequency==10000000)[[likely]]return ticks/10;
return ticks/frequency*1000000+ticks%frequency*1000000/frequency;
}

std::int64_t utc_now_ms() noexcept
{
FILETIME now;
GetSystemTimePreciseAsFileTime(&now);
const std::int64_t ticks=static_cast<std::int64_t>((static_cast<std::uint64_t>(now.dwHighDateTime)<<32)|now.dwLowDateTime);
return floor_div(ticks-kEpochTicks,kTicksPerMs);
}

std::int64_t utc_to_local_civil_ms(std::int64_t utc_ms) noexcept
{
if(utc_ms<kFirstMs||utc_ms>kLastMs)return utc_ms;
const auto utc_ticks=static_cast<std::uint64_t>(utc_ms*kTicksPerMs+kEpochTicks);
FILETIME utc_file{static_cast<DWORD>(utc_ticks),static_cast<DWORD>(utc_ticks>>32)};
SYSTEMTIME utc_parts;
SYSTEMTIME local_parts;
FILETIME local_file;
DYNAMIC_TIME_ZONE_INFORMATION zone;
if(GetDynamicTimeZoneInformation(&zone)==TIME_ZONE_ID_INVALID)return utc_ms;
if(!FileTimeToSystemTime(&utc_file,&utc_parts)||!SystemTimeToTzSpecificLocalTimeEx(&zone,&utc_parts,&local_parts)||!SystemTimeToFileTime(&local_parts,&local_file))return utc_ms;
const auto local_ticks=static_cast<std::int64_t>((static_cast<std::uint64_t>(local_file.dwHighDateTime)<<32)|local_file.dwLowDateTime);
// SYSTEMTIME keeps whole milliseconds, and utc_ms had no finer part, so the division is exact.
return (local_ticks-kEpochTicks)/kTicksPerMs;
}
}
