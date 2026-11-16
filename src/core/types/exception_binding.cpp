// The script exception helpers of spec 8.8 (contract in string_binding.hpp): throw(), exception_info() and its alias
// getExceptionInfo(), which existing scripts written for the sample scripthelper add-on keep using.
#include <core/types/script_guard.hpp>
#include <core/types/string.hpp>
#include <core/types/string_binding.hpp>
#include <angelscript.h>
#include <stdexcept>
#include <string>

namespace zodiac {
namespace {
void throw_exception(const String& message) noexcept
{
raise_script_exception(message.c_str());
}
String exception_info()
{
asIScriptContext* context=asGetActiveContext();
const char* text=context?context->GetExceptionString():nullptr;
return text?String(text):String();
}
void check(int result,const char* declaration)
{
if(result<0)throw std::runtime_error(std::string("cannot register \"")+declaration+"\" (angelscript error "+std::to_string(result)+")");
}
}

void register_exception_helpers(asIScriptEngine* engine)
{
check(engine->RegisterGlobalFunction("void throw(const string &in)",asFUNCTION(throw_exception),asCALL_CDECL),"void throw(const string &in)");
check(engine->RegisterGlobalFunction("string exception_info()",asFUNCTION(guarded<&exception_info>::call),asCALL_CDECL),"string exception_info()");
check(engine->RegisterGlobalFunction("string getExceptionInfo()",asFUNCTION(guarded<&exception_info>::call),asCALL_CDECL),"string getExceptionInfo()");
}
}
