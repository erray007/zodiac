// Smoke test of the bare AngelScript library: an engine without any registered type builds and runs a script. It links
// angelscript only, which holds no sample add-on (those are in angelscript_sample_addons, for tests/bench alone).
#include <doctest/doctest.h>
#include <angelscript.h>

TEST_CASE("angelscript runs a script")
{
asPrepareMultithread();
asIScriptEngine* engine = asCreateScriptEngine();
REQUIRE(engine != nullptr);
asIScriptModule* module = engine->GetModule("smoke", asGM_ALWAYS_CREATE);
REQUIRE(module != nullptr);
REQUIRE(module->AddScriptSection("smoke", "int f(){int total=0; for(int i=1;i<=4;++i)total+=i; return total;}") >= 0);
REQUIRE(module->Build() >= 0);
asIScriptFunction* func = module->GetFunctionByDecl("int f()");
REQUIRE(func != nullptr);
asIScriptContext* ctx = engine->CreateContext();
REQUIRE(ctx != nullptr);
REQUIRE(ctx->Prepare(func) >= 0);
REQUIRE(ctx->Execute() == asEXECUTION_FINISHED);
const int result = static_cast<int>(ctx->GetReturnDWord());
CHECK(result == 10);
ctx->Release();
engine->ShutDownAndRelease();
asUnprepareMultithread();
}
