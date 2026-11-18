/*
   AngelCode Scripting Library
   Copyright (c) 2003-2026 Andreas Jonsson

   This software is provided 'as-is', without any express or implied
   warranty. In no event will the authors be held liable for any
   damages arising from the use of this software.

   Permission is granted to anyone to use this software for any
   purpose, including commercial applications, and to alter it and
   redistribute it freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you
      must not claim that you wrote the original software. If you use
      this software in a product, an acknowledgment in the product
      documentation would be appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and
      must not be misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
      distribution.

   The original version of this library can be located at:
   http://www.angelcode.com/angelscript/

   Andreas Jonsson
   andreas@angelcode.com
*/
// ALTERED SOURCE: the native paths of zodiac::ScriptArray (spec 8.2), replacing the add-on's per-element type switch
// (CScriptArray::Less/Equals) and its insertion sort. For bool, integer, floating-point and enum elements sort, find and
// equality work on the element memory as the element's C++ type (std::sort, std::find, memcmp); for Zodiac's string they
// compare zodiac::String objects directly (find and equality without a memcmp call per element, see same_bytes). Nothing
// here calls into the script engine. Floating point: NaN goes last in both sort orders (so the order is strict weak) and
// equals nothing, -0 equals 0.
#include <core/types/array.hpp>
#include <core/types/string.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string_view>
#include <type_traits>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace zodiac {
namespace {
using Kind=ScriptArray::Kind;

// Calls f with std::type_identity of the C++ type of a primitive kind.
template<class F>
void with_type(Kind kind,F&& f)
{
switch(kind){
case Kind::boolean:f(std::type_identity<bool>{});break;
case Kind::int8:f(std::type_identity<std::int8_t>{});break;
case Kind::int16:f(std::type_identity<std::int16_t>{});break;
case Kind::int32:f(std::type_identity<std::int32_t>{});break;
case Kind::int64:f(std::type_identity<std::int64_t>{});break;
case Kind::uint8:f(std::type_identity<std::uint8_t>{});break;
case Kind::uint16:f(std::type_identity<std::uint16_t>{});break;
case Kind::uint32:f(std::type_identity<std::uint32_t>{});break;
case Kind::uint64:f(std::type_identity<std::uint64_t>{});break;
case Kind::float32:f(std::type_identity<float>{});break;
case Kind::float64:f(std::type_identity<double>{});break;
default:break;
}
}

template<class T>
void sort_values(T* first,T* last,bool descending) noexcept
{
if constexpr(std::is_same_v<T,bool>){
// Two values: count them.
const auto trues=std::count(first,last,true);
T* split=descending?first+trues:last-trues;
std::fill(first,split,descending);
std::fill(split,last,!descending);
}else{
if constexpr(std::is_floating_point_v<T>){
last=std::partition(first,last,[](T value){return !std::isnan(value);});
}
if(descending)std::sort(first,last,std::greater<T>());
else std::sort(first,last);
}
}

template<class T>
int find_value(const std::byte* data,std::uint32_t start,std::uint32_t count,const void* value) noexcept
{
T needle;
std::memcpy(&needle,value,sizeof needle);
const T* first=reinterpret_cast<const T*>(data);
const T* hit=std::find(first+start,first+count,needle);
return hit==first+count?-1:static_cast<int>(hit-first);
}

template<class T>
bool equal_values(const std::byte* a,const std::byte* b,std::uint32_t count) noexcept
{
if constexpr(std::is_floating_point_v<T>){
const T* left=reinterpret_cast<const T*>(a);
return std::equal(left,left+count,reinterpret_cast<const T*>(b));
}else{
// Integers and bools are equal exactly when their bytes are.
return count==0||std::memcmp(a,b,static_cast<std::size_t>(count)*sizeof(T))==0;
}
}

// String elements are pointers to the String objects the array owns.
const String* const* strings(const std::byte* data) noexcept
{
return reinterpret_cast<const String* const*>(data);
}
// String element comparison without a C runtime call per element: the Windows C runtime's memcmp, called once per element
// of equal length, dominated find and equality of the short strings scripts store. Up to 16 bytes the n bytes are compared
// with two overlapping loads of 2, 4 or 8 bytes; longer strings go to an out-of-line loop (16-byte SSE2 blocks, the last
// one overlapping) so the per-element loop keeps few live registers. Every load stays inside both strings.
template<class T>
T load(const char* p) noexcept
{
T value;
std::memcpy(&value,p,sizeof value);
return value;
}
[[gnu::noinline]] bool same_long_bytes(const char* a,const char* b,std::size_t n) noexcept
{
#if defined(__SSE2__)
for(std::size_t k=0;k+16<n;k+=16){
const __m128i x=_mm_loadu_si128(reinterpret_cast<const __m128i*>(a+k));
const __m128i y=_mm_loadu_si128(reinterpret_cast<const __m128i*>(b+k));
if(_mm_movemask_epi8(_mm_cmpeq_epi8(x,y))!=0xFFFF)return false;
}
const __m128i x=_mm_loadu_si128(reinterpret_cast<const __m128i*>(a+n-16));
const __m128i y=_mm_loadu_si128(reinterpret_cast<const __m128i*>(b+n-16));
return _mm_movemask_epi8(_mm_cmpeq_epi8(x,y))==0xFFFF;
#else
return std::memcmp(a,b,n)==0;
#endif
}
// True when the n bytes at a and b are equal.
inline bool same_bytes(const char* a,const char* b,std::size_t n) noexcept
{
if(n>16)return same_long_bytes(a,b,n);
if(n>=8)return ((load<std::uint64_t>(a)^load<std::uint64_t>(b))|(load<std::uint64_t>(a+n-8)^load<std::uint64_t>(b+n-8)))==0;
if(n>=4)return ((load<std::uint32_t>(a)^load<std::uint32_t>(b))|(load<std::uint32_t>(a+n-4)^load<std::uint32_t>(b+n-4)))==0;
if(n>=2)return ((load<std::uint16_t>(a)^load<std::uint16_t>(b))|(load<std::uint16_t>(a+n-2)^load<std::uint16_t>(b+n-2)))==0;
return n==0||*a==*b;
}
// a and b hold the same bytes (the String == of the element type).
inline bool same_string(std::string_view a,std::string_view b) noexcept
{
return a.size()==b.size()&&same_bytes(a.data(),b.data(),a.size());
}
}

void ScriptArray::native_sort(bool descending) noexcept
{
if(count_<2)return;
if(kind_==Kind::string){
auto** first=reinterpret_cast<const String**>(data_);
if(descending)std::sort(first,first+count_,[](const String* a,const String* b){return b->bytes()<a->bytes();});
else std::sort(first,first+count_,[](const String* a,const String* b){return a->bytes()<b->bytes();});
return;
}
with_type(kind_,[this,descending]<class T>(std::type_identity<T>){
T* first=reinterpret_cast<T*>(data_);
sort_values(first,first+count_,descending);
});
}

int ScriptArray::native_find(const void* value,std::uint32_t start) const noexcept
{
if(kind_==Kind::string){
const std::string_view needle=static_cast<const String*>(value)->bytes();
const String* const* elements=strings(data_);
for(std::uint32_t index=start;index<count_;++index){
if(same_string(elements[index]->bytes(),needle))return static_cast<int>(index);
}
return -1;
}
int found=-1;
with_type(kind_,[&]<class T>(std::type_identity<T>){found=find_value<T>(data_,start,count_,value);});
return found;
}

bool ScriptArray::native_equals(const ScriptArray& other) const noexcept
{
if(kind_==Kind::string){
const String* const* left=strings(data_);
const String* const* right=strings(other.data_);
for(std::uint32_t index=0;index<count_;++index){
if(!same_string(left[index]->bytes(),right[index]->bytes()))return false;
}
return true;
}
bool equal=false;
with_type(kind_,[&]<class T>(std::type_identity<T>){equal=equal_values<T>(data_,other.data_,count_);});
return equal;
}
}
