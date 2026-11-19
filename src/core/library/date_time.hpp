// zodiac::DateTime: the script's date_time (spec 8.6 of the core-types design), a zone-less civil date and time like
// BGT's dates: milliseconds since 1970-01-01T00:00:00.000 of the proleptic Gregorian calendar, with no time zone and no
// leap seconds (every day has 86 400 000 ms). date_time::now() is the local wall-clock time read that way, utc_now() the
// UTC one (platform/clock.hpp).
// Contract:
// - Every int64 is a valid DateTime (about 292 million years either side of 1970); from_parts accepts exactly the parts
//   of those: month 1-12, day 1 to the month's length (29 February only in leap years), hour 0-23, minute 0-59,
//   second 0-59, millisecond 0-999, any year whose date still fits; anything else is nullopt.
// - The parts are computed on each call from civil_ms with Howard Hinnant's civil_from_days and days_from_civil
//   (http://howardhinnant.github.io/date_algorithms.html), which are exact for the whole range; times before 1970 round
//   down (-1 ms is 1969-12-31 23:59:59.999). weekday() is 0 for Monday to 6 for Sunday, day_of_year() 1 to 366.
// - format(fmt) copies fmt, replacing the tokens {yyyy} (the year, at least 4 digits, with a '-' before negative years),
//   {yy} (the year modulo 100, 2 digits), {MM} {dd} {HH} {mm} {ss} (2 digits), {M} {d} {H} (no padding) and {fff}
//   (3 digits). "{{" is '{' and "}}" is '}'; any other brace, and a brace pair holding anything else ("{x}"), stays as it
//   is. It throws only std::bad_alloc.
// - DateTime is an aggregate of one int64, trivially copyable, so the binding registers it as a POD value type returned in
//   a register.
#pragma once
#include <core/types/string.hpp>
#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>

namespace zodiac {
namespace date_time_detail {
inline constexpr std::int64_t kMsPerDay=86400000;
// Division and remainder rounded towards negative infinity, for positive divisors.
constexpr std::int64_t floor_div(std::int64_t value,std::int64_t divisor) noexcept
{
return value/divisor-(value%divisor<0);
}
constexpr std::int64_t floor_mod(std::int64_t value,std::int64_t divisor) noexcept
{
const std::int64_t remainder=value%divisor;
return remainder<0?remainder+divisor:remainder;
}
constexpr bool is_leap(std::int64_t year) noexcept
{
return year%4==0&&(year%100!=0||year%400==0);
}
constexpr int days_in_month(std::int64_t year,int month) noexcept
{
constexpr int lengths[12]={31,28,31,30,31,30,31,31,30,31,30,31};
return month==2&&is_leap(year)?29:lengths[month-1];
}
// Days since 1970-01-01 of a valid date; exact for every year within +-2^40.
constexpr std::int64_t days_from_civil(std::int64_t year,int month,int day) noexcept
{
year-=month<=2;
const std::int64_t era=(year>=0?year:year-399)/400;
const std::int64_t year_of_era=year-era*400;
const std::int64_t day_of_march_year=(153*(month>2?month-3:month+9)+2)/5+day-1;
const std::int64_t day_of_era=year_of_era*365+year_of_era/4-year_of_era/100+day_of_march_year;
return era*146097+day_of_era-719468;
}
struct CivilDate {
int year;
int month;
int day;
int day_of_year;
};
// The date of days since 1970-01-01, for any days whose milliseconds fit an int64 (|days| < 2^47).
constexpr CivilDate civil_from_days(std::int64_t days) noexcept
{
days+=719468;
const std::int64_t era=(days>=0?days:days-146096)/146097;
const std::int64_t day_of_era=days-era*146097;
const std::int64_t year_of_era=(day_of_era-day_of_era/1460+day_of_era/36524-day_of_era/146096)/365;
const std::int64_t day_of_march_year=day_of_era-(365*year_of_era+year_of_era/4-year_of_era/100);
const std::int64_t march_month=(5*day_of_march_year+2)/153;
const auto day=static_cast<int>(day_of_march_year-(153*march_month+2)/5+1);
std::int64_t year=year_of_era+era*400;
// The March-based year runs from 1 March of year to the end of February of year + 1.
if(march_month<10){
return {static_cast<int>(year),static_cast<int>(march_month+3),day,static_cast<int>(day_of_march_year+60+is_leap(year))};
}
++year;
return {static_cast<int>(year),static_cast<int>(march_month-9),day,static_cast<int>(day_of_march_year-305)};
}
}

struct DateTime {
std::int64_t civil_ms;

static constexpr std::optional<DateTime> from_parts(int year,int month,int day,int hour,int minute,int second,int millisecond) noexcept
{
using namespace date_time_detail;
if(month<1||month>12||day<1||day>days_in_month(year,month))return std::nullopt;
if(hour<0||hour>23||minute<0||minute>59||second<0||second>59||millisecond<0||millisecond>999)return std::nullopt;
const std::int64_t days=days_from_civil(year,month,day);
const std::int64_t time_of_day=((hour*60+minute)*60+second)*std::int64_t{1000}+millisecond;
// days * kMsPerDay + time_of_day, without an intermediate overflow at the negative end: from the next midnight back.
std::int64_t midnight;
if(__builtin_mul_overflow(days+(days<0),kMsPerDay,&midnight))return std::nullopt;
std::int64_t result;
if(__builtin_add_overflow(midnight,days<0?time_of_day-kMsPerDay:time_of_day,&result))return std::nullopt;
return DateTime{result};
}

constexpr std::int64_t days() const noexcept
{
return date_time_detail::floor_div(civil_ms,date_time_detail::kMsPerDay);
}
constexpr int time_of_day_ms() const noexcept
{
return static_cast<int>(date_time_detail::floor_mod(civil_ms,date_time_detail::kMsPerDay));
}
constexpr date_time_detail::CivilDate date() const noexcept
{
return date_time_detail::civil_from_days(days());
}
constexpr int year() const noexcept
{
return date().year;
}
constexpr int month() const noexcept
{
return date().month;
}
constexpr int day() const noexcept
{
return date().day;
}
constexpr int day_of_year() const noexcept
{
return date().day_of_year;
}
constexpr int hour() const noexcept
{
return time_of_day_ms()/3600000;
}
constexpr int minute() const noexcept
{
return time_of_day_ms()/60000%60;
}
constexpr int second() const noexcept
{
return time_of_day_ms()/1000%60;
}
constexpr int millisecond() const noexcept
{
return time_of_day_ms()%1000;
}
// 1970-01-01 was a Thursday (3).
constexpr int weekday() const noexcept
{
return static_cast<int>(date_time_detail::floor_mod(days()+3,7));
}
String format(std::string_view fmt) const;

friend constexpr bool operator==(const DateTime&,const DateTime&)=default;
friend constexpr std::strong_ordering operator<=>(const DateTime&,const DateTime&)=default;
};
}
