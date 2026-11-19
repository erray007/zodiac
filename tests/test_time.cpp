// Tests the time library (platform/clock.hpp, DateTime and register_time; spec 8.6 of the core-types design): the clocks,
// Hinnant's civil-day algorithms against a day-by-day walk, from_parts' validation and range ends, the format tokens, and
// the script's ticks, ticks_us, timer and date_time with their exceptions.
#include "script_fixture.hpp"
#include <core/library/date_time.hpp>
#include <platform/clock.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

using zodiac::DateTime;
using zodiac_test::ScriptFixture;

namespace {
// Runs "bool main(){<body>}" and returns its result.
bool holds(ScriptFixture& fixture,std::string_view body)
{
const std::string source="bool main(){"+std::string(body)+"}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"bool main()")==asEXECUTION_FINISHED);
return fixture.context->GetReturnByte()!=0;
}
// Runs "string main(){return <expression>;}" and returns the text.
std::string text_of(ScriptFixture& fixture,std::string_view expression)
{
const std::string source="string main(){return "+std::string(expression)+";}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"string main()")==asEXECUTION_FINISHED);
return fixture.returned_string();
}
// Runs "int64 main(){return <expression>;}" and returns the value.
std::int64_t int64_of(ScriptFixture& fixture,std::string_view expression)
{
const std::string source="int64 main(){return "+std::string(expression)+";}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"int64 main()")==asEXECUTION_FINISHED);
return static_cast<std::int64_t>(fixture.context->GetReturnQWord());
}
// Runs "void main(){<statement>;}", which must raise a script exception, and returns its text.
std::string error_of(ScriptFixture& fixture,std::string_view statement)
{
const std::string source="void main(){"+std::string(statement)+";}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"void main()")==asEXECUTION_EXCEPTION);
return fixture.exception_string();
}
DateTime date(int year,int month,int day,int hour=0,int minute=0,int second=0,int millisecond=0)
{
const auto value=DateTime::from_parts(year,month,day,hour,minute,second,millisecond);
REQUIRE(value.has_value());
return *value;
}
}

TEST_CASE("the civil-day algorithms are exact at compile time")
{
using namespace zodiac::date_time_detail;
static_assert(days_from_civil(1970,1,1)==0);
static_assert(days_from_civil(2000,3,1)==11017);
static_assert(days_from_civil(1969,12,31)==-1);
static_assert(civil_from_days(0).year==1970&&civil_from_days(0).month==1&&civil_from_days(0).day==1);
static_assert(civil_from_days(-1).year==1969&&civil_from_days(-1).day_of_year==365);
static_assert(DateTime{-1}.millisecond()==999&&DateTime{-1}.hour()==23&&DateTime{-1}.day()==31);
static_assert(DateTime{0}.weekday()==3);
}

TEST_CASE("civil_from_days and days_from_civil agree with a day-by-day walk over 1200 years")
{
using namespace zodiac::date_time_detail;
// From 1 January 1400 to the end of 2599, against the calendar counted one day at a time.
std::int64_t days=days_from_civil(1400,1,1);
for(int year=1400;year<2600;++year){
int day_of_year=1;
for(int month=1;month<=12;++month){
const int length=days_in_month(year,month);
for(int day=1;day<=length;++day,++days,++day_of_year){
const CivilDate civil=civil_from_days(days);
REQUIRE(civil.year==year);
REQUIRE(civil.month==month);
REQUIRE(civil.day==day);
REQUIRE(civil.day_of_year==day_of_year);
REQUIRE(days_from_civil(year,month,day)==days);
}
}
}
// Proleptic Gregorian before year 1: year 0 is a leap year, as is -4.
CHECK(date(0,2,29).day()==29);
CHECK(date(-4,2,29).year()==-4);
CHECK_FALSE(DateTime::from_parts(-1,2,29,0,0,0,0));
CHECK(date(-1,12,31).civil_ms+86400000==date(0,1,1).civil_ms);
}

TEST_CASE("from_parts refuses invalid parts and dates outside the int64 range")
{
CHECK_FALSE(DateTime::from_parts(2023,2,29,0,0,0,0));
CHECK_FALSE(DateTime::from_parts(1900,2,29,0,0,0,0));
CHECK(DateTime::from_parts(2000,2,29,0,0,0,0));
CHECK_FALSE(DateTime::from_parts(2026,0,1,0,0,0,0));
CHECK_FALSE(DateTime::from_parts(2026,13,1,0,0,0,0));
CHECK_FALSE(DateTime::from_parts(2026,4,31,0,0,0,0));
CHECK_FALSE(DateTime::from_parts(2026,1,0,0,0,0,0));
CHECK_FALSE(DateTime::from_parts(2026,1,1,24,0,0,0));
CHECK_FALSE(DateTime::from_parts(2026,1,1,0,60,0,0));
CHECK_FALSE(DateTime::from_parts(2026,1,1,0,0,60,0));
CHECK_FALSE(DateTime::from_parts(2026,1,1,0,0,0,1000));
CHECK_FALSE(DateTime::from_parts(2026,1,1,-1,0,0,0));
CHECK_FALSE(DateTime::from_parts(std::numeric_limits<int>::max(),1,1,0,0,0,0));
CHECK_FALSE(DateTime::from_parts(std::numeric_limits<int>::min(),1,1,0,0,0,0));
// The ends of the range: every int64 is a date, and from_parts gives back exactly the extreme ones.
for(const std::int64_t end:{std::numeric_limits<std::int64_t>::min(),std::numeric_limits<std::int64_t>::max()}){
const DateTime extreme{end};
INFO(end);
const auto again=DateTime::from_parts(extreme.year(),extreme.month(),extreme.day(),extreme.hour(),extreme.minute(),extreme.second(),extreme.millisecond());
REQUIRE(again);
CHECK(again->civil_ms==end);
}
const DateTime last{std::numeric_limits<std::int64_t>::max()};
CHECK(last.format("{yyyy}-{MM}-{dd} {HH}:{mm}:{ss}.{fff}").bytes()=="292278994-08-17 07:12:55.807");
const DateTime first{std::numeric_limits<std::int64_t>::min()};
CHECK(first.format("{yyyy}-{MM}-{dd} {HH}:{mm}:{ss}.{fff}").bytes()=="-292275055-05-16 16:47:04.192");
CHECK_FALSE(DateTime::from_parts(last.year(),last.month(),last.day(),last.hour(),last.minute(),last.second()+1,0));
CHECK_FALSE(DateTime::from_parts(first.year(),first.month(),first.day(),first.hour(),first.minute(),first.second()-1,0));
}

TEST_CASE("weekday, day_of_year and the time parts")
{
CHECK(date(2026,11,19).weekday()==3);
CHECK(date(2024,1,1).weekday()==0);
CHECK(date(2023,12,31).weekday()==6);
CHECK(date(1969,12,29).weekday()==0);
CHECK(date(2024,12,31).day_of_year()==366);
CHECK(date(2025,12,31).day_of_year()==365);
CHECK(date(2024,3,1).day_of_year()==61);
const DateTime moment=date(1955,11,5,6,7,8,9);
CHECK(moment.year()==1955);
CHECK(moment.month()==11);
CHECK(moment.day()==5);
CHECK(moment.hour()==6);
CHECK(moment.minute()==7);
CHECK(moment.second()==8);
CHECK(moment.millisecond()==9);
}

TEST_CASE("format tokens, escapes and literal braces")
{
const DateTime moment=date(2026,1,2,3,4,5,6);
CHECK(moment.format("{yyyy}/{M}/{d} {H}:{mm}:{ss}.{fff} {x} {{").bytes()=="2026/1/2 3:04:05.006 {x} {");
CHECK(moment.format("{yy}{MM}{dd}{HH}").bytes()=="26010203");
CHECK(date(2026,11,19,23,0,0,0).format("{M}/{d} {H}").bytes()=="11/19 23");
CHECK(moment.format("}} } { {} {yyyy {mmm} {fff").bytes()=="} } { {} {yyyy {mmm} {fff");
CHECK(moment.format("{{yyyy}}").bytes()=="{yyyy}");
CHECK(moment.format("").bytes().empty());
CHECK(date(7,3,4).format("{yyyy} {yy}").bytes()=="0007 07");
CHECK(date(-44,3,15).format("{yyyy} {yy}").bytes()=="-0044 56");
// A format longer than the stack buffer, every token expanding.
std::string long_format;
std::string expected;
for(int i=0;i<100;++i){
long_format+="{yyyy}{fff}";
expected+="2026006";
}
CHECK(moment.format(long_format).bytes()==expected);
}

TEST_CASE("the clocks never go back and agree with each other")
{
std::int64_t previous=zodiac::platform::monotonic_us();
for(int i=0;i<100000;++i){
const std::int64_t now=zodiac::platform::monotonic_us();
REQUIRE(now>=previous);
previous=now;
}
// A plausible wall clock: after 2020 and before 2200.
const std::int64_t utc=zodiac::platform::utc_now_ms();
CHECK(utc>1577836800000);
CHECK(utc<7258118400000);
// Local offsets lie within a day; instants the system cannot convert come back unchanged.
const std::int64_t offset=zodiac::platform::utc_to_local_civil_ms(utc)-utc;
CHECK(offset>-86400000);
CHECK(offset<86400000);
CHECK(zodiac::platform::utc_to_local_civil_ms(std::numeric_limits<std::int64_t>::max())==std::numeric_limits<std::int64_t>::max());
CHECK(zodiac::platform::utc_to_local_civil_ms(std::numeric_limits<std::int64_t>::min())==std::numeric_limits<std::int64_t>::min());
}

TEST_CASE("script date_time arithmetic, parts and validation")
{
ScriptFixture fixture;
CHECK(holds(fixture,"date_time d=date_time(2024,2,29)+int64(86400000)*365; return d.year()==2025&&d.month()==2&&d.day()==28;"));
CHECK(holds(fixture,"return date_time(2024,12,31).day_of_year()==366;"));
CHECK(holds(fixture,"return date_time(2026,11,19).weekday()==3;"));
CHECK(error_of(fixture,"date_time(2023,2,29)")=="invalid date");
CHECK(error_of(fixture,"date_time d(2026,1,1,24)")=="invalid date");
CHECK(holds(fixture,"date_time d(2026,1,2,3,4,5,6); return d.year()==2026&&d.month()==1&&d.day()==2&&d.hour()==3&&d.minute()==4&&d.second()==5&&d.millisecond()==6;"));
CHECK(int64_of(fixture,"date_time(2026,1,2)-date_time(2026,1,1)")==86400000);
CHECK(holds(fixture,"date_time d; return d==date_time(1970,1,1)&&d.weekday()==3;"));
CHECK(holds(fixture,"date_time d(2026,1,31); d+=86400000; d-=1; return d==date_time(2026,1,31,23,59,59,999);"));
CHECK(holds(fixture,"date_time d(2026,3,1); return d-1==date_time(2026,2,28,23,59,59,999)&&(d-int64(86400000))+86400000==d;"));
CHECK(holds(fixture,"date_time a(2026,1,1); date_time b(2026,1,1,0,0,0,1); return a<b&&b>a&&a<=a&&a!=b&&!(a==b)&&a>=a;"));
CHECK(holds(fixture,"date_time a(2026,1,1); date_time b=a; b+=1; return a!=b&&b-a==1;"));
CHECK(holds(fixture,"array<date_time> list={date_time(2026,5,1),date_time(1999,1,1)}; list.sort(); return list[0].year()==1999;"));
}

TEST_CASE("script date_time overflow raises integer overflow")
{
ScriptFixture fixture;
CHECK(error_of(fixture,"date_time d; d+=9223372036854775807; d+=1")=="integer overflow");
CHECK(error_of(fixture,"date_time d=date_time()-9223372036854775807; d=d-2")=="integer overflow");
CHECK(error_of(fixture,"date_time d=date_time()-9223372036854775807; d-=2")=="integer overflow");
CHECK(error_of(fixture,"date_time d=date_time()+9223372036854775807; d=d+1")=="integer overflow");
CHECK(error_of(fixture,"int64 x=(date_time()+9223372036854775807)-(date_time()-9223372036854775807)")=="integer overflow");
}

TEST_CASE("script date_time to_string")
{
ScriptFixture fixture;
CHECK(text_of(fixture,"date_time(2026,1,2,3,4,5,6).to_string(\"{yyyy}/{M}/{d} {H}:{mm}:{ss}.{fff} {x} {{\")")=="2026/1/2 3:04:05.006 {x} {");
CHECK(text_of(fixture,"date_time(2026,11,19,8,9,10).to_string()")=="2026-11-19 08:09:10");
}

TEST_CASE("script date_time::now and utc_now differ by the local offset")
{
ScriptFixture fixture;
const std::int64_t difference=int64_of(fixture,"date_time::now()-date_time::utc_now()");
const std::int64_t t=zodiac::platform::utc_now_ms();
const std::int64_t offset=zodiac::platform::utc_to_local_civil_ms(t)-t;
CHECK((difference-offset<1000&&offset-difference<1000));
CHECK(holds(fixture,"date_time u=date_time::utc_now(); return u.year()>=2020&&u.year()<2200;"));
}

TEST_CASE("script ticks never decrease and ticks_us agrees")
{
ScriptFixture fixture;
CHECK(holds(fixture,"int64 last=ticks(); for(int i=0;i<10000;++i){int64 now=ticks(); if(now<last)return false; last=now;} return last>=0;"));
CHECK(holds(fixture,"int64 last=ticks_us(); for(int i=0;i<10000;++i){int64 now=ticks_us(); if(now<last)return false; last=now;} return true;"));
CHECK(holds(fixture,"for(int i=0;i<1000;++i){int64 t=ticks(); int64 us=ticks_us(); int64 t2=ticks(); if(us/1000<t-2||us/1000>t2+2)return false;} return true;"));
}

TEST_CASE("script timer")
{
ScriptFixture fixture;
CHECK(holds(fixture,"timer t; return t.running&&t.elapsed>=0&&t.elapsed<1000;"));
CHECK(holds(fixture,"timer t; t.pause(); t.pause(); int64 e=t.elapsed; int64 s=ticks(); while(ticks()-s<30){} return !t.running&&t.elapsed==e;"));
CHECK(holds(fixture,"timer t; t.pause(); int64 s=ticks(); while(ticks()-s<30){} t.resume(); t.resume(); s=ticks(); while(ticks()-s<30){} return t.running&&t.elapsed>=30&&t.elapsed<60+30;"));
CHECK(holds(fixture,"timer t; t.set(5000); return t.running&&t.elapsed>=5000&&t.elapsed<5000+1000;"));
CHECK(holds(fixture,"timer t; t.pause(); t.set(5000); return !t.running&&t.elapsed==5000&&t.elapsed_seconds==5.0;"));
CHECK(holds(fixture,"timer t; t.set(5000); t.restart(); return t.running&&t.elapsed<20;"));
CHECK(holds(fixture,"timer t; t.pause(); t.set(5000); t.restart(); return t.running&&t.elapsed<20;"));
CHECK(holds(fixture,"timer t; t.pause(); t.set(9223372036854775807); return t.elapsed==4611686018427387;"));
CHECK(holds(fixture,"timer t; t.set(-9223372036854775807-1); return t.elapsed<-4611686018427000;"));
CHECK(holds(fixture,"timer a; a.pause(); a.set(1234); timer b=a; return b.elapsed==1234&&!b.running;"));
}
