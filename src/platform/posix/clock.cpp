// The clocks on POSIX systems (see platform/clock.hpp): clock_gettime on CLOCK_MONOTONIC and CLOCK_REALTIME (served from
// the vDSO on Linux, no system call), and localtime_r's tm_gmtoff (the zone's offset from UTC at that instant, daylight
// saving included) for the local civil time, so no calendar arithmetic is redone here.
#include <platform/clock.hpp>
#include <ctime>
#include <limits>
#include <time.h>

namespace zodiac::platform {
std::int64_t monotonic_us() noexcept
{
timespec now;
clock_gettime(CLOCK_MONOTONIC,&now);
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
// The second that contains utc_ms (rounded down, so instants before 1970 fall in the right second).
const std::int64_t seconds=utc_ms/1000-(utc_ms%1000<0);
if constexpr(sizeof(time_t)<sizeof(std::int64_t)){
if(seconds<std::numeric_limits<time_t>::min()||seconds>std::numeric_limits<time_t>::max())return utc_ms;
}
const auto instant=static_cast<time_t>(seconds);
tm local;
if(!localtime_r(&instant,&local))return utc_ms;
const std::int64_t offset_ms=static_cast<std::int64_t>(local.tm_gmtoff)*1000;
// Offsets are at most a day, so only the outermost instants could overflow.
std::int64_t civil_ms;
if(__builtin_add_overflow(utc_ms,offset_ms,&civil_ms))return utc_ms;
return civil_ms;
}
}
