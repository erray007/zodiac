// Shared helpers for tests that run scripts against a fresh engine and Host.
#pragma once
#include <doctest/doctest.h>
#include <angelscript.h>
#include <scriptarray/scriptarray.h>
#include <scriptdictionary/scriptdictionary.h>
#include <scripthelper/scripthelper.h>
#include <scriptstdstring/scriptstdstring.h>
#include <core/host.hpp>
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
RegisterStdString(engine);
RegisterScriptArray(engine,true);
RegisterStdStringUtils(engine);
RegisterScriptDictionary(engine);
RegisterExceptionRoutines(engine);
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
return *static_cast<std::string*>(context->GetAddressOfReturnValue());
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
