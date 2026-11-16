/*
Header-only C++ convenience layer over the Zodiac plugin ABI (plugin.h). Everything here compiles down to the C table,
so plugins written with it keep the stable ABI.

zodiac::plugin::function<&fn>(host, "declaration") registers a plain C++ function in one line. The script declaration
must match fn's signature. Supported types:
- parameters: int32_t, int64_t, float, double, bool, std::string_view (valid for the duration of the call)
- return: void, int32_t, int64_t, float, double, bool, std::string, std::vector<std::string> (script array<string>@)
Any C++ exception escaping fn becomes a catchable script exception carrying e.what().

The wrapper reaches the host table through a per-function pointer set at registration. Registration runs on the main
thread before scripts execute, so calls only read it. Registering the same function with several hosts makes it use the
last one, which is safe because every Zodiac host in a process shares one implementation.
*/
#ifndef ZODIAC_PLUGIN_HPP
#define ZODIAC_PLUGIN_HPP

#include <zodiac/plugin.h>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace zodiac::plugin {
namespace detail {
template<class T> inline constexpr bool unsupported=false;
template<class T> struct argument{
static_assert(unsupported<T>,"zodiac::plugin::function: unsupported parameter type (use int32_t, int64_t, float, double, bool or std::string_view)");
};
template<> struct argument<int32_t>{
static int32_t read(const zodiac_host* host,zodiac_call* call,uint32_t index) noexcept{return host->arg_int32(call,index);}
};
template<> struct argument<int64_t>{
static int64_t read(const zodiac_host* host,zodiac_call* call,uint32_t index) noexcept{return host->arg_int64(call,index);}
};
template<> struct argument<float>{
static float read(const zodiac_host* host,zodiac_call* call,uint32_t index) noexcept{return host->arg_float(call,index);}
};
template<> struct argument<double>{
static double read(const zodiac_host* host,zodiac_call* call,uint32_t index) noexcept{return host->arg_double(call,index);}
};
template<> struct argument<bool>{
static bool read(const zodiac_host* host,zodiac_call* call,uint32_t index) noexcept{return host->arg_bool(call,index)!=0;}
};
template<> struct argument<std::string_view>{
static std::string_view read(const zodiac_host* host,zodiac_call* call,uint32_t index) noexcept
{
size_t length=0;
const char* data=host->arg_string(call,index,&length);
return data?std::string_view(data,length):std::string_view();
}
};
template<class T> struct result{
static_assert(unsupported<T>,"zodiac::plugin::function: unsupported return type (use void, int32_t, int64_t, float, double, bool, std::string or std::vector<std::string>)");
};
template<> struct result<int32_t>{
static void write(const zodiac_host* host,zodiac_call* call,int32_t value){host->ret_int32(call,value);}
};
template<> struct result<int64_t>{
static void write(const zodiac_host* host,zodiac_call* call,int64_t value){host->ret_int64(call,value);}
};
template<> struct result<float>{
static void write(const zodiac_host* host,zodiac_call* call,float value){host->ret_float(call,value);}
};
template<> struct result<double>{
static void write(const zodiac_host* host,zodiac_call* call,double value){host->ret_double(call,value);}
};
template<> struct result<bool>{
static void write(const zodiac_host* host,zodiac_call* call,bool value){host->ret_bool(call,value?1:0);}
};
template<> struct result<std::string>{
static void write(const zodiac_host* host,zodiac_call* call,const std::string& value){host->ret_string(call,value.data(),value.size());}
};
template<> struct result<std::vector<std::string>>{
static void write(const zodiac_host* host,zodiac_call* call,const std::vector<std::string>& values)
{
zodiac_array* array=host->ret_array(call,"string",static_cast<uint32_t>(values.size()));
if(!array)return;
for(const std::string& value:values)host->array_push_string(array,value.data(),value.size());
}
};
template<class F> struct signature;
template<class R,class... A> struct signature<R(*)(A...)>{
using return_type=R;
using arguments=void(std::remove_cvref_t<A>...);
};
template<class R,class... A> struct signature<R(*)(A...) noexcept>:signature<R(*)(A...)>{};
template<auto Fn> inline const zodiac_host* bound_host=nullptr;
template<auto Fn,class R,class... A,size_t... I> inline void invoke(const zodiac_host* host,zodiac_call* call,void(*)(A...),std::index_sequence<I...>)
{
if constexpr(std::is_void_v<R>)Fn(argument<A>::read(host,call,static_cast<uint32_t>(I))...);
else result<std::remove_cvref_t<R>>::write(host,call,Fn(argument<A>::read(host,call,static_cast<uint32_t>(I))...));
}
template<class F> struct arity;
template<class... A> struct arity<void(A...)>:std::integral_constant<size_t,sizeof...(A)>{};
template<auto Fn> void wrapper(zodiac_call* call) noexcept
{
using traits=signature<decltype(Fn)>;
const zodiac_host* host=bound_host<Fn>;
try{
invoke<Fn,typename traits::return_type>(host,call,static_cast<typename traits::arguments*>(nullptr),std::make_index_sequence<arity<typename traits::arguments>::value>());
}
catch(const std::exception& error){
host->set_exception(call,error.what());
}
catch(...){
host->set_exception(call,"unknown C++ exception");
}
}
}
// Registers Fn under the script declaration. Returns ZODIAC_OK or a negative ZODIAC_E_* code.
template<auto Fn> int function(zodiac_host* host,const char* declaration)
{
static_assert(std::is_pointer_v<decltype(Fn)>&&std::is_function_v<std::remove_pointer_t<decltype(Fn)>>,"zodiac::plugin::function expects a pointer to a free function");
if(!host)return ZODIAC_E_INVALID_ARGUMENT;
detail::bound_host<Fn> = host;
return host->register_function(host,declaration,&detail::wrapper<Fn>,nullptr);
}
}

#endif
