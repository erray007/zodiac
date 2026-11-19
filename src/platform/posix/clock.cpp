// The clocks on POSIX systems (see platform/clock.hpp): clock_gettime (served from the vDSO on Linux, no system call) on
// CLOCK_BOOTTIME where the system has it, which unlike CLOCK_MONOTONIC keeps counting while the system is suspended, as the
// Windows performance counter does (probed once; CLOCK_MONOTONIC otherwise), and on CLOCK_REALTIME; and for the local civil
// time LocalOffsetCache (platform/local_offset_cache.hpp) over localtime_r's tm_gmtoff (the zone's offset from UTC at that
// instant, daylight saving included), with tzset() as the zone refresh, so no calendar arithmetic is redone here and a zone
// change is seen within about a second (localtime_r alone never re-reads the zone).
#include <platform/clock.hpp>
#include <platform/local_offset_cache.hpp>
#include <ctime>
#include <limits>
#include <time.h>

namespace zodiac::platform {
namespace {
clockid_t monotonic_clock() noexcept
{
#ifdef CLOCK_BOOTTIME
timespec probe;
if(clock_gettime(CLOCK_BOOTTIME,&probe)==0)return CLOCK_BOOTTIME;
#endif
return CLOCK_MONOTONIC;
}

struct PosixZone {
void refresh() noexcept
{
tzset();
}
std::optional<std::int64_t> offset_seconds(std::int64_t utc_seconds) const noexcept
{
if constexpr(sizeof(time_t)<sizeof(std::int64_t)){
if(utc_seconds<std::numeric_limits<time_t>::min()||utc_seconds>std::numeric_limits<time_t>::max())return std::nullopt;
}
const auto instant=static_cast<time_t>(utc_seconds);
tm local;
if(!localtime_r(&instant,&local))return std::nullopt;
return static_cast<std::int64_t>(local.tm_gmtoff);
}
};

constinit detail::LocalOffsetCache<PosixZone> local_offsets;
}

std::int64_t monotonic_us() noexcept
{
static const clockid_t clock=monotonic_clock();
timespec now;
clock_gettime(clock,&now);
return static_cast<std::int64_t>(now.tv_sec)*1000000+now.tv_nsec/1000;
}

std::int64_t utc_now_ms() noexcept
{
timespec now;
clock_gettime(CLOCK_REALTIME,&now);
return static_cast<std::int64_t>(now.tv_sec)*1000+now.tv_nsec/1000000;
}

std::int64_t utc_to_local_civil_ms(std::int64_t utc_ms) noexcept
{
return local_offsets.to_local(utc_ms);
}
}
