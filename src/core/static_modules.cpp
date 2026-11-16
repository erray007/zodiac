#include <core/static_modules.hpp>
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <vector>

#if defined(_WIN32)
// The markers bracketing the records. The linker merges .zodmod$a, .zodmod$m (every ZODIAC_DEFINE_MODULE record) and .zodmod$z into one
// .zodmod section ordered by the text after '$', so the records sit between the two markers. used keeps both emitted; the unbraced
// extern "C" gives these const variables external linkage (a braced block would leave them internal), so the optimizer cannot reason about what surrounds them.
extern "C" __attribute__((used,section(".zodmod$a"))) const zodiac_module_info* const zodiac_static_modules_begin=nullptr;
extern "C" __attribute__((used,section(".zodmod$z"))) const zodiac_module_info* const zodiac_static_modules_end=nullptr;

namespace {
using Record=const zodiac_module_info*;
// Hides where an address came from: the records are objects the compiler never sees, so the walk from one marker to the other must not be folded away.
const Record* opaque(const Record* address) noexcept
{
__asm__("" : "+r"(address));
return address;
}
std::span<const Record> collect()
{
const Record* const first=opaque(&zodiac_static_modules_begin+1);
const Record* const last=opaque(&zodiac_static_modules_end);
// Usually there is no padding, so the section itself is the result and nothing is copied.
if(std::find(first,last,nullptr)==last)return {first,last};
static const std::vector<Record> compacted=[first,last]{
std::vector<Record> records;
records.reserve(static_cast<std::size_t>(std::count_if(first,last,[](Record record){return record!=nullptr;})));
std::copy_if(first,last,std::back_inserter(records),[](Record record){return record!=nullptr;});
return records;
}();
return compacted;
}
}
#endif

namespace zodiac {
std::span<const zodiac_module_info* const> linked_static_modules() noexcept
{
#if defined(_WIN32)
// Collected once (thread-safe static initialization); an allocation failure while compacting padded records terminates.
static const std::span<const zodiac_module_info* const> records=collect();
return records;
#else
// ELF: the records are in the section zodiac_modules, bounded by the linker-provided __start_zodiac_modules and __stop_zodiac_modules. Not walked yet.
return {};
#endif
}
}
