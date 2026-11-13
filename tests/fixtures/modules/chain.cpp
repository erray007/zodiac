// Configurable fixture module for the module loader tests. CMake sets the ZODIAC_FIXTURE_* macros per target:
// NAME (required, string), REPORTED (name in the info, defaults to NAME), DEP (one dependency, string), ABI, RESULT (return code of register_module), LOG_ERROR (string logged as an error while registering).
// Registering and shutting down log "<NAME> register" and "<NAME> shutdown" so the tests can observe the order.
#include <zodiac/plugin.h>
#include <stdint.h>

#ifndef ZODIAC_FIXTURE_REPORTED
#define ZODIAC_FIXTURE_REPORTED ZODIAC_FIXTURE_NAME
#endif
#ifndef ZODIAC_FIXTURE_ABI
#define ZODIAC_FIXTURE_ABI ZODIAC_ABI_VERSION
#endif
#ifndef ZODIAC_FIXTURE_RESULT
#define ZODIAC_FIXTURE_RESULT ZODIAC_OK
#endif

namespace {
zodiac_host* g_host=nullptr;
#ifdef ZODIAC_FIXTURE_DEP
const char* const g_dependencies[]={ZODIAC_FIXTURE_DEP};
const char* const* const g_dependency_list=g_dependencies;
constexpr uint32_t g_dependency_count=1;
#else
const char* const* const g_dependency_list=nullptr;
constexpr uint32_t g_dependency_count=0;
#endif
int register_module(zodiac_host* host)
{
g_host=host;
host->log(host,ZODIAC_LOG_INFO,ZODIAC_FIXTURE_NAME " register");
#ifdef ZODIAC_FIXTURE_LOG_ERROR
host->log(host,ZODIAC_LOG_ERROR,ZODIAC_FIXTURE_LOG_ERROR);
#endif
return ZODIAC_FIXTURE_RESULT;
}
void shutdown_module(void)
{
if(g_host)g_host->log(g_host,ZODIAC_LOG_INFO,ZODIAC_FIXTURE_NAME " shutdown");
}
const zodiac_module_info g_info={ZODIAC_FIXTURE_ABI,ZODIAC_FIXTURE_REPORTED,"0.0.1","Zlib",g_dependency_list,g_dependency_count,&register_module,&shutdown_module};
}

ZODIAC_DEFINE_MODULE(g_info)
