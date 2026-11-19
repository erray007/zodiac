// The clocks on Windows (see platform/clock.hpp).
// - monotonic_us reads the performance counter, which keeps counting while the system sleeps or hibernates. Its frequency
//   is fixed at boot, so it is read once (a function-local static, so a static initializer of another file may call
//   monotonic_us first); with the 10 MHz frequency of every current Windows version the conversion is one division by a
//   constant, otherwise whole seconds and the remainder are scaled apart so no product can overflow.
// - utc_now_ms reads GetSystemTimePreciseAsFileTime (100 ns ticks since 1601).
// - utc_to_local_civil_ms goes through LocalOffsetCache (platform/local_offset_cache.hpp); its zone is the dynamic time zone
//   (GetDynamicTimeZoneInformation, re-read at most once a second), and an offset is the difference SYSTEMTIME conversion
//   with SystemTimeToTzSpecificLocalTimeEx makes, which applies the zone's rules of that year.
// Windows 10 is Zodiac's minimum (foundation spec); the toolchain's headers default to Windows 7 and hide
// SystemTimeToTzSpecificLocalTimeEx behind Windows 8.
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <platform/clock.hpp>
#include <platform/local_offset_cache.hpp>
#include <windows.h>

namespace zodiac::platform {
namespace {
// FILETIME ticks (100 ns) per second and per millisecond, and the ticks from 1601-01-01 to 1970-01-01.
constexpr std::int64_t kTicksPerSecond=10000000;
constexpr std::int64_t kTicksPerMs=10000;
constexpr std::int64_t kEpochTicks=116444736000000000;
// The seconds since 1970 that FileTimeToSystemTime accepts: FILETIME values from 0 to 2^63 - 1.
constexpr std::int64_t kFirstSecond=-kEpochTicks/kTicksPerSecond;
constexpr std::int64_t kLastSecond=(INT64_MAX-kEpochTicks)/kTicksPerSecond;

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
std::int64_t ticks_of(const FILETIME& file) noexcept
{
return static_cast<std::int64_t>((static_cast<std::uint64_t>(file.dwHighDateTime)<<32)|file.dwLowDateTime);
}

struct WindowsZone {
DYNAMIC_TIME_ZONE_INFORMATION info{};
bool valid=false;

void refresh() noexcept
{
valid=GetDynamicTimeZoneInformation(&info)!=TIME_ZONE_ID_INVALID;
}
std::optional<std::int64_t> offset_seconds(std::int64_t utc_seconds) const noexcept
{
if(!valid||utc_seconds<kFirstSecond||utc_seconds>kLastSecond)return std::nullopt;
const auto utc_ticks=static_cast<std::uint64_t>(utc_seconds*kTicksPerSecond+kEpochTicks);
const FILETIME utc_file{static_cast<DWORD>(utc_ticks),static_cast<DWORD>(utc_ticks>>32)};
SYSTEMTIME utc_parts;
SYSTEMTIME local_parts;
FILETIME local_file;
if(!FileTimeToSystemTime(&utc_file,&utc_parts)||!SystemTimeToTzSpecificLocalTimeEx(&info,&utc_parts,&local_parts)||!SystemTimeToFileTime(&local_parts,&local_file))return std::nullopt;
// Both sides are whole seconds, so the division is exact.
return (ticks_of(local_file)-static_cast<std::int64_t>(utc_ticks))/kTicksPerSecond;
}
};

constinit detail::LocalOffsetCache<WindowsZone> local_offsets;
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
return floor_div(ticks_of(now)-kEpochTicks,kTicksPerMs);
}

std::int64_t utc_to_local_civil_ms(std::int64_t utc_ms) noexcept
{
return local_offsets.to_local(utc_ms);
}
}
