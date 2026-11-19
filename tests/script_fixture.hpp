// Shared helpers for tests that run scripts against a fresh engine and Host.
#pragma once
#include <doctest/doctest.h>
#include <angelscript.h>
#include <core/host.hpp>
#include <core/library/library.hpp>
#include <core/types/array.hpp>
#include <core/types/dictionary.hpp>
#include <core/types/string.hpp>
#include <core/types/string_binding.hpp>
#include <string>
#include <utility>
#include <vector>

namespace zodiac_test {
struct LogEntry{
zodiac_log_level level;
std::string message;
};
inline void collect_engine_message(const asSMessageInfo* info,void* param)
{
std::string& messages=*static_cast<std::string*>(param);
messages.append(info->section).append(" (").append(std::to_string(info->row)).append("): ").append(info->message).push_back('\n');
}
inline asIScriptEngine* create_engine(std::string& messages)
{
asIScriptEngine* engine=asCreateScriptEngine();
REQUIRE(engine!=nullptr);
engine->SetMessageCallback(asFUNCTION(collect_engine_message),&messages,asCALL_CDECL);
// The same registrations as ScriptRuntime: the array first, since split and join use array<string>; the dictionary after
// both, since keys() returns array<string>; the random numbers right after the array (random_choice is a method of
// array<T>, added before the first instance exists); then the rest of the library.
zodiac::register_array(engine);
zodiac::register_random(engine);
zodiac::register_string(engine);
zodiac::register_dictionary(engine);
zodiac::register_math(engine);
zodiac::register_exception_helpers(engine);
return engine;
}
// The engine is shut down before the Host is destroyed, because registered functions point into the Host.
struct ScriptFixture{
std::string messages;
asIScriptEngine* engine;
zodiac::Host host;
std::vector<LogEntry> logs;
asIScriptContext* context=nullptr;
ScriptFixture():engine(create_engine(messages)),host(engine)
{
host.set_current_module("test");
host.set_log_handler([this](zodiac_log_level level,std::string_view message){logs.push_back({level,std::string(message)});});
}
~ScriptFixture()
{
if(context)context->Release();
engine->ShutDownAndRelease();
}
ScriptFixture(const ScriptFixture&)=delete;
ScriptFixture& operator=(const ScriptFixture&)=delete;
zodiac_host* abi(){return host.abi();}
// Builds source into a fresh module and prepares a context on entry. Returns the execution result.
int run(const char* source,const char* entry)
{
asIScriptModule* module=engine->GetModule("test",asGM_ALWAYS_CREATE);
REQUIRE(module!=nullptr);
REQUIRE(module->AddScriptSection("test",source)>=0);
INFO(messages);
REQUIRE(module->Build()>=0);
asIScriptFunction* function=module->GetFunctionByDecl(entry);
REQUIRE(function!=nullptr);
if(!context)context=engine->CreateContext();
REQUIRE(context!=nullptr);
REQUIRE(context->Prepare(function)>=0);
return context->Execute();
}
std::string returned_string()
{
return std::string(static_cast<const zodiac::String*>(context->GetAddressOfReturnValue())->bytes());
}
std::string exception_string()
{
const char* text=context->GetExceptionString();
return text?text:"";
}
void release_context()
{
if(context)context->Release();
context=nullptr;
}
};
}
