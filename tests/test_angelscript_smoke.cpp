#include <doctest/doctest.h>
#include <angelscript.h>
#include <scriptstdstring/scriptstdstring.h>

TEST_CASE("angelscript runs a script")
{
asPrepareMultithread();
asIScriptEngine* engine = asCreateScriptEngine();
REQUIRE(engine != nullptr);
RegisterStdString(engine);
asIScriptModule* module = engine->GetModule("smoke", asGM_ALWAYS_CREATE);
REQUIRE(module != nullptr);
REQUIRE(module->AddScriptSection("smoke", "int f(){string s=\"\xC5\x9F\"; return int(s.length());}") >= 0);
REQUIRE(module->Build() >= 0);
asIScriptFunction* func = module->GetFunctionByDecl("int f()");
REQUIRE(func != nullptr);
asIScriptContext* ctx = engine->CreateContext();
REQUIRE(ctx != nullptr);
REQUIRE(ctx->Prepare(func) >= 0);
REQUIRE(ctx->Execute() == asEXECUTION_FINISHED);
const int result = static_cast<int>(ctx->GetReturnDWord());
CHECK(result == 2);
ctx->Release();
engine->ShutDownAndRelease();
asUnprepareMultithread();
}
