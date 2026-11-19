// Xoshiro256's out-of-line parts and the per-thread generator (contract in random.hpp). The per-thread generator is a
// constant-initialized thread_local with a seeded flag, so reaching it costs a thread-local load and one predictable branch,
// with no initialization guard or wrapper call; the seeding on first use is out of line.
#include <core/library/random.hpp>
#include <platform/entropy.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace zodiac {
namespace {
struct ThreadGenerator {
Xoshiro256 generator;
bool seeded=false;
};
constinit thread_local ThreadGenerator thread_state;
// Makes fallback seeds distinct within the process when the entropy source fails.
constinit std::atomic<std::uint64_t> fallback_count{0};

[[gnu::noinline,gnu::cold]] Xoshiro256& seed_thread_generator() noexcept
{
ThreadGenerator& state=thread_state;
state.generator.seed_from_entropy();
state.seeded=true;
return state.generator;
}
}

std::uint64_t Xoshiro256::rejected(wide product,std::uint64_t span) noexcept
{
// 2^64 mod span: the low words below it are the excess that would bias the result.
const std::uint64_t threshold=(0-span)%span;
while(static_cast<std::uint64_t>(product)<threshold)product=static_cast<wide>(next())*span;
return static_cast<std::uint64_t>(product>>64);
}

void Xoshiro256::seed_from_entropy() noexcept
{
if(platform::fill_entropy(state_,sizeof state_)&&(state_[0]|state_[1]|state_[2]|state_[3])!=0)return;
const auto clock=static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
const auto address=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
const std::uint64_t count=fallback_count.fetch_add(1,std::memory_order_relaxed);
seed(clock^rotl(address,32)^(count*0xD1342543DE82EF95u));
}

Xoshiro256& thread_generator() noexcept
{
ThreadGenerator& state=thread_state;
if(!state.seeded)[[unlikely]]return seed_thread_generator();
return state.generator;
}
}
