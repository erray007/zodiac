// The operating system's clocks, implemented per platform in src/platform/<os>/clock.cpp. Every function is thread-safe
// and never fails (a conversion the system cannot make returns its input unchanged, see below).
// - monotonic_us: microseconds on a clock that never goes back and is unaffected by changes of the wall clock: the
//   performance counter (QueryPerformanceCounter) on Windows, CLOCK_MONOTONIC elsewhere. Its origin is unspecified (boot);
//   only differences mean something.
// - utc_now_ms: the wall clock as milliseconds since 1970-01-01T00:00:00 UTC (rounded down), leap seconds not counted:
//   GetSystemTimePreciseAsFileTime on Windows, CLOCK_REALTIME elsewhere.
// - utc_to_local_civil_ms: the local wall-clock time at the instant utc_ms (milliseconds since 1970 UTC), as milliseconds
//   since 1970-01-01T00:00 read as if the local time were UTC, by the system's current time zone rules for that instant
//   (daylight saving included): SystemTimeToTzSpecificLocalTimeEx with the dynamic time zone on Windows (the documented
//   successor of SystemTimeToTzSpecificLocalTime, which applies the zone's historical rules), localtime_r's tm_gmtoff
//   elsewhere. An instant the system cannot convert (outside the years 1601 to 30827 on Windows, outside time_t's range)
//   is returned unchanged, as if the zone were UTC.
#pragma once
#include <cstdint>

namespace zodiac::platform {
std::int64_t monotonic_us() noexcept;
std::int64_t utc_now_ms() noexcept;
std::int64_t utc_to_local_civil_ms(std::int64_t utc_ms) noexcept;
}
