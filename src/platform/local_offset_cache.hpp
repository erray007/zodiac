// The cache behind utc_to_local_civil_ms on every platform (platform/clock.hpp), so a date_time::now() per frame costs no
// time zone lookup. Internal to src/platform/<os>/clock.cpp.
// - Zone is the platform's zone reader, used only under the cache's mutex: refresh() re-reads the system's time zone (the
//   registry on Windows, tzset() elsewhere), and offset_seconds(utc_seconds) is the local offset from UTC (seconds, daylight
//   saving included) in the second that starts at utc_seconds, or nullopt when the system cannot convert it.
// - The zone is re-read at most once per second of monotonic time: the first call after the deadline takes the mutex,
//   refreshes, empties the cache and moves the deadline one second on; so a zone change is seen within about a second.
// - The cache is one atomic word, the UTC minute (44 bits) and its offset (20 bits, seconds), filled only when the offset is
//   the same at the minute's first and last second (time zone transitions fall on minute boundaries; this check keeps
//   historical transitions at odd seconds exact anyway). A call for an instant in that minute before the deadline is two
//   atomic loads and a monotonic clock read, no lock. Everything else (another minute, the deadline passed, a minute or
//   offset outside the packed range) goes through the mutex and the zone.
#pragma once
#include <platform/clock.hpp>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>

namespace zodiac::platform::detail {
template<class Zone>
class LocalOffsetCache {
public:
constexpr LocalOffsetCache() noexcept=default;
LocalOffsetCache(const LocalOffsetCache&)=delete;
LocalOffsetCache& operator=(const LocalOffsetCache&)=delete;

std::int64_t to_local(std::int64_t utc_ms) noexcept
{
const std::int64_t utc_seconds=floor_div(utc_ms,1000);
const std::int64_t minute=floor_div(utc_seconds,60);
const std::int64_t now_us=monotonic_us();
if(now_us<refresh_at_us_.load(std::memory_order_acquire)){
const std::int64_t packed=entry_.load(std::memory_order_acquire);
if((packed>>kOffsetBits)==minute&&packed!=kEmpty)[[likely]]return utc_ms+offset_of(packed)*1000;
}
return refill(utc_ms,utc_seconds,minute,now_us);
}
// The zone reader, for tests (the cache's own calls go through the mutex).
const Zone& zone() const noexcept
{
return zone_;
}

private:
static constexpr int kOffsetBits=20;
static constexpr std::int64_t kOffsetLimit=std::int64_t{1}<<(kOffsetBits-1);
static constexpr std::int64_t kMinuteLimit=std::int64_t{1}<<(63-kOffsetBits);
static constexpr std::int64_t kEmpty=INT64_MIN;
static constexpr std::int64_t kRefreshUs=1000000;

static constexpr std::int64_t floor_div(std::int64_t value,std::int64_t divisor) noexcept
{
return value/divisor-(value%divisor<0);
}
static constexpr std::int64_t offset_of(std::int64_t packed) noexcept
{
return static_cast<std::int64_t>(static_cast<std::uint64_t>(packed)<<(64-kOffsetBits))>>(64-kOffsetBits);
}
static constexpr std::int64_t pack(std::int64_t minute,std::int64_t offset) noexcept
{
return static_cast<std::int64_t>(static_cast<std::uint64_t>(minute)<<kOffsetBits|(static_cast<std::uint64_t>(offset)&((std::uint64_t{1}<<kOffsetBits)-1)));
}

[[gnu::noinline]] std::int64_t refill(std::int64_t utc_ms,std::int64_t utc_seconds,std::int64_t minute,std::int64_t now_us) noexcept
{
const std::lock_guard<std::mutex> guard(mutex_);
if(now_us>=refresh_at_us_.load(std::memory_order_relaxed)){
zone_.refresh();
entry_.store(kEmpty,std::memory_order_relaxed);
refresh_at_us_.store(now_us+kRefreshUs,std::memory_order_release);
}
const std::int64_t packed=entry_.load(std::memory_order_relaxed);
if((packed>>kOffsetBits)==minute&&packed!=kEmpty)return utc_ms+offset_of(packed)*1000;
const std::optional<std::int64_t> offset=zone_.offset_seconds(utc_seconds);
if(!offset)return utc_ms;
if(minute>-kMinuteLimit&&minute<kMinuteLimit&&*offset>-kOffsetLimit&&*offset<kOffsetLimit){
if(zone_.offset_seconds(minute*60)==offset&&zone_.offset_seconds(minute*60+59)==offset){
entry_.store(pack(minute,*offset),std::memory_order_release);
}
}
std::int64_t local_ms;
if(__builtin_add_overflow(utc_ms,*offset*1000,&local_ms))return utc_ms;
return local_ms;
}

std::atomic<std::int64_t> entry_{kEmpty};
std::atomic<std::int64_t> refresh_at_us_{INT64_MIN};
std::mutex mutex_;
Zone zone_{};
};
}
