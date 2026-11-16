// Fixture module with a reference type, for the teardown order test: a script can keep a ref_thing alive in a global handle.
// It logs "ref_object release" when the last ref_thing is destroyed and "ref_object shutdown" from shutdown_module.
// A ref_thing released after shutdown_module additionally logs "ref_object released after shutdown".
#include <zodiac/plugin.h>
#include <atomic>
#include <cstdint>

namespace {
zodiac_host* g_host=nullptr;
bool g_shut_down=false;
struct Thing{
std::atomic<uint32_t> references{1};
};
void thing_add_ref(void* object)
{
static_cast<Thing*>(object)->references.fetch_add(1,std::memory_order_relaxed);
}
void thing_release(void* object)
{
Thing* thing=static_cast<Thing*>(object);
if(thing->references.fetch_sub(1,std::memory_order_acq_rel)!=1)return;
delete thing;
if(!g_host)return;
g_host->log(g_host,ZODIAC_LOG_INFO,"ref_object release");
if(g_shut_down)g_host->log(g_host,ZODIAC_LOG_ERROR,"ref_object released after shutdown");
}
void thing_factory(zodiac_call* call)
{
g_host->ret_object(call,new Thing());
}
int register_module(zodiac_host* host)
{
g_host=host;
g_shut_down=false;
int code=host->register_ref_type(host,"ref_thing",&thing_add_ref,&thing_release);
if(code!=ZODIAC_OK)return code;
return host->register_factory(host,"ref_thing","ref_thing@ f()",&thing_factory,nullptr);
}
void shutdown_module(void)
{
g_shut_down=true;
if(g_host)g_host->log(g_host,ZODIAC_LOG_INFO,"ref_object shutdown");
}
const zodiac_module_info g_info={ZODIAC_ABI_VERSION,sizeof(zodiac_module_info),"ref_object","0.0.1","Zlib",nullptr,0,&register_module,&shutdown_module,0};
}

ZODIAC_DEFINE_MODULE(g_info)
