// Random numbers (spec 8.5 of the core-types design): the xoshiro256** generator of Blackman and Vigna behind the script's
// random functions and random_generator, and the per-thread generator they use.
// Contract:
// - Xoshiro256 is 32 bytes of state and trivially copyable; a copy continues the same sequence. A default-constructed one
//   is all zero, which xoshiro256** never leaves (it only returns 0): seed it, or call seed_from_entropy, before use.
// - seed(n) expands n by SplitMix64: state word i is the i-th SplitMix64 output starting from n (the reference seeding, so
//   seed(42) gives the published sequence; SplitMix64 is a bijection, so the state is never all zero).
// - seed_from_entropy() takes the 32 bytes from platform::fill_entropy; when the source fails, from SplitMix64 over the
//   clock, the object's address and a process-wide counter (distinct per call). Never leaves an all-zero state.
// - next() is the next 64-bit output. range(min, max) is uniform over the inclusive range, min and max swapped when
//   min > max, by Lemire's multiply-shift with rejection (unbiased; the full int64 range is next() itself). range32 is the
//   same for int bounds, the same values range gives for them. below(count) is uniform in [0, count), count > 0.
//   unit() is (next() >> 11) * 2^-53, in [0, 1). coin() is the top bit of next().
// - thread_generator() is the calling thread's generator (thread_local), seeded from entropy on its first use in the thread.
#pragma once
#include <cstdint>
#include <type_traits>

namespace zodiac {
class Xoshiro256 {
public:
constexpr Xoshiro256() noexcept=default;
void seed(std::uint64_t value) noexcept
{
for(std::uint64_t& word:state_){
value+=0x9E3779B97F4A7C15u;
std::uint64_t z=value;
z=(z^(z>>30))*0xBF58476D1CE4E5B9u;
z=(z^(z>>27))*0x94D049BB133111EBu;
word=z^(z>>31);
}
}
void seed_from_entropy() noexcept;
std::uint64_t next() noexcept
{
const std::uint64_t result=rotl(state_[1]*5,7)*9;
const std::uint64_t t=state_[1]<<17;
state_[2]^=state_[0];
state_[3]^=state_[1];
state_[1]^=state_[2];
state_[0]^=state_[3];
state_[2]^=t;
state_[3]=rotl(state_[3],45);
return result;
}
std::int64_t range(std::int64_t min,std::int64_t max) noexcept
{
const std::int64_t low=max<min?max:min;
const std::int64_t high=max<min?min:max;
const std::uint64_t span=static_cast<std::uint64_t>(high)-static_cast<std::uint64_t>(low)+1;
if(span==0)[[unlikely]]return static_cast<std::int64_t>(next());
return static_cast<std::int64_t>(static_cast<std::uint64_t>(low)+scaled(span));
}
std::int32_t range32(std::int32_t min,std::int32_t max) noexcept
{
const std::int32_t low=max<min?max:min;
const std::int32_t high=max<min?min:max;
// 1 to 2^32, never 0.
const std::uint64_t span=static_cast<std::uint64_t>(static_cast<std::int64_t>(high)-low)+1;
return static_cast<std::int32_t>(low+static_cast<std::int64_t>(scaled(span)));
}
std::uint32_t below(std::uint32_t count) noexcept
{
return static_cast<std::uint32_t>(scaled(count));
}
double unit() noexcept
{
return static_cast<double>(next()>>11)*0x1p-53;
}
bool coin() noexcept
{
return (next()>>63)!=0;
}
private:
__extension__ typedef unsigned __int128 wide;
static constexpr std::uint64_t rotl(std::uint64_t x,int k) noexcept
{
return (x<<k)|(x>>(64-k));
}
// Lemire: the high word of next() * span, uniform in [0, span) once the low words below 2^64 mod span are rejected.
// Only a low word below span can be one, so the division is reached with probability span / 2^64.
std::uint64_t scaled(std::uint64_t span) noexcept
{
const wide product=static_cast<wide>(next())*span;
if(static_cast<std::uint64_t>(product)<span)[[unlikely]]return rejected(product,span);
return static_cast<std::uint64_t>(product>>64);
}
[[gnu::noinline,gnu::cold]] std::uint64_t rejected(wide product,std::uint64_t span) noexcept;

std::uint64_t state_[4]={};
};
static_assert(sizeof(Xoshiro256)==32);
static_assert(std::is_trivially_copyable_v<Xoshiro256>);

Xoshiro256& thread_generator() noexcept;
}
