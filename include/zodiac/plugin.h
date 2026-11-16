/*
Zodiac plugin ABI v1. This header is the permanent public C contract between the engine and every module.

ABI rules:
- Append-only forever. Existing members never move, change type, or get removed. New members are added at the end of a struct only.
- Both structs that cross the boundary (zodiac_host and zodiac_module_info) carry struct_size, the sizeof the struct had when the other side was built, as their second member. A reader uses an appended member only when struct_size shows it is present.
- Only C types cross the boundary. No AngelScript types, no C++ classes, no std:: types, no exceptions. Errors are reported through return codes or set_exception.
- A module declares in abi_version the minimum host ABI it needs (1 or greater). The host refuses any module whose abi_version is 0 or newer than the host's ZODIAC_ABI_VERSION.
- A module sets zodiac_module_info.struct_size to sizeof(zodiac_module_info). The host refuses a module whose struct_size is smaller than the ABI v1 size, and reads members appended after v1 only when struct_size covers them.
- The ABI v1 size of zodiac_module_info is 64 bytes on 64-bit targets (it ends after shutdown_module). ZODIAC_ABI_VERSION stays 1: appending a member does not change it.
- zodiac_module_info.link_modes (appended after v1): ZODIAC_LINK_STATIC and/or ZODIAC_LINK_DYNAMIC, the ways the module may be linked into a game. 0, or a struct_size too small to contain it (a v1 module), means both are allowed.

Build rules (ZODIAC_DEFINE_MODULE):
- Shared builds (ZODIAC_MODULE_STATIC not defined) export zodiac_module_get_info, which returns &info_variable.
- Static builds define ZODIAC_MODULE_STATIC and ZODIAC_MODULE_ID=<module name>; ZODIAC_MODULE_ID is required (#error otherwise), and zodiac_add_module passes both to the _static target.
- A static build exports nothing. ZODIAC_DEFINE_MODULE defines one record instead: const zodiac_module_info* const zodiac_static_module_<ZODIAC_MODULE_ID>, with C linkage, kept by the compiler (used), pointing at info_variable, in the linker section ZODIAC_STATIC_MODULE_SECTION (".zodmod$m" on Windows, "zodiac_modules" elsewhere).
- The record's name is pasted from ZODIAC_MODULE_ID after macro expansion, so a module named like a macro of a header the source includes first (windows.h defines near, far and interface, and with some SDKs small) would get the wrong name. zodiac_add_module therefore also passes ZODIAC_MODULE_RECORD=zodiac_static_module_<name>, the finished name as one identifier, and ZODIAC_DEFINE_MODULE uses it when it is defined. Plugin authors who build a static module without zodiac_add_module should define it the same way.
- Nothing references the record, so the final link forces it in by name (-u zodiac_static_module_<name>) and the engine finds it by walking that section.
- info_variable must have static storage duration so the record has a constant initializer. Static builds need a GCC-compatible compiler (clang or gcc) for the section attribute.

Lifetime rules:
- register_module is called once, after every dependency has registered.
- shutdown_module is called once at teardown, in reverse registration order, and only if register_module succeeded. By then every script object has been released (the host discards the script modules and runs a full garbage collection first) and no registered function, method, factory or behaviour will be called again. The host is still alive, so host->log may be used inside shutdown_module.

Thread rules:
- Registration happens only on the main thread, and only inside register_module.
- Registered functions may be called from any script thread. They must be thread-safe, or document otherwise.
*/
#ifndef ZODIAC_PLUGIN_H
#define ZODIAC_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZODIAC_ABI_VERSION 1u
#define ZODIAC_OK 0
#define ZODIAC_E_INVALID_ARGUMENT -1
#define ZODIAC_E_DECLARATION -2
#define ZODIAC_E_NOT_PRIMITIVE -3
#define ZODIAC_E_ALREADY_REGISTERED -4
#define ZODIAC_E_UNKNOWN_TYPE -5
#define ZODIAC_LINK_STATIC 1u
#define ZODIAC_LINK_DYNAMIC 2u
typedef struct zodiac_host zodiac_host;
typedef struct zodiac_call zodiac_call;
typedef struct zodiac_array zodiac_array;
typedef void (*zodiac_generic_fn)(zodiac_call* call);
typedef enum zodiac_log_level{ZODIAC_LOG_INFO=0,ZODIAC_LOG_WARNING=1,ZODIAC_LOG_ERROR=2}zodiac_log_level;
typedef struct zodiac_module_info{
uint32_t abi_version;          /* minimum host ABI this module needs; 1 or greater */
uint32_t struct_size;          /* sizeof(zodiac_module_info) of the module */
const char* name;             /* "example"; [a-z0-9_]+ */
const char* version;           /* semver */
const char* license;           /* SPDX id, e.g. "Zlib" */
const char* const* dependencies;
uint32_t dependency_count;
int (*register_module)(zodiac_host* host);   /* ZODIAC_OK or negative code */
void (*shutdown_module)(void);               /* may be NULL; see the lifetime rules above */
/* end of ABI v1 (64 bytes on 64-bit targets); members below are read only when struct_size covers them */
uint32_t link_modes;           /* ZODIAC_LINK_STATIC|ZODIAC_LINK_DYNAMIC; 0 means both */
}zodiac_module_info;
struct zodiac_host{
uint32_t abi_version;
uint32_t struct_size;          /* sizeof(zodiac_host) of the host; check before using appended members */
int (*register_function)(zodiac_host*,const char* declaration,zodiac_generic_fn fn,void* user_data);
int (*register_native_function)(zodiac_host*,const char* declaration,void (*fn)(void)); /* primitive params/return only; C calling convention */
int (*register_ref_type)(zodiac_host*,const char* name,void (*add_ref)(void*),void (*release)(void*));
int (*register_method)(zodiac_host*,const char* type_name,const char* declaration,zodiac_generic_fn fn,void* user_data);
int (*register_factory)(zodiac_host*,const char* type_name,const char* declaration,zodiac_generic_fn fn,void* user_data);
void* (*call_user_data)(zodiac_call*);
void* (*call_object)(zodiac_call*);
int32_t (*arg_int32)(zodiac_call*,uint32_t index);
int64_t (*arg_int64)(zodiac_call*,uint32_t index);
float (*arg_float)(zodiac_call*,uint32_t index);
double (*arg_double)(zodiac_call*,uint32_t index);
int (*arg_bool)(zodiac_call*,uint32_t index);
const char* (*arg_string)(zodiac_call*,uint32_t index,size_t* length); /* UTF-8, valid until the call returns */
void* (*arg_object)(zodiac_call*,uint32_t index);
zodiac_array* (*arg_array)(zodiac_call*,uint32_t index);
void (*ret_int32)(zodiac_call*,int32_t);
void (*ret_int64)(zodiac_call*,int64_t);
void (*ret_float)(zodiac_call*,float);
void (*ret_double)(zodiac_call*,double);
void (*ret_bool)(zodiac_call*,int);
void (*ret_string)(zodiac_call*,const char* data,size_t length);
void (*ret_object)(zodiac_call*,void* object); /* host takes one reference */
zodiac_array* (*ret_array)(zodiac_call*,const char* element_type,uint32_t reserve); /* "string", "int", "double" */
uint32_t (*array_length)(zodiac_array*);
const char* (*array_get_string)(zodiac_array*,uint32_t index,size_t* length);
int32_t (*array_get_int32)(zodiac_array*,uint32_t index);
double (*array_get_double)(zodiac_array*,uint32_t index);
void (*array_push_string)(zodiac_array*,const char* data,size_t length);
void (*array_push_int32)(zodiac_array*,int32_t);
void (*array_push_double)(zodiac_array*,double);
void (*set_exception)(zodiac_call*,const char* message);
void (*log)(zodiac_host*,zodiac_log_level level,const char* message);
};
#define ZODIAC_MODULE_ENTRY_NAME "zodiac_module_get_info"
/* ZODIAC_EXPORT expands to __declspec(dllexport) on Windows, visibility("default") elsewhere */
#if defined(_WIN32)
#define ZODIAC_EXPORT __declspec(dllexport)
#else
#define ZODIAC_EXPORT __attribute__((visibility("default")))
#endif
/* ZODIAC_LINKAGE_C gives the exported entry C linkage when it is defined from C++ */
#ifdef __cplusplus
#define ZODIAC_LINKAGE_C extern "C"
#else
#define ZODIAC_LINKAGE_C
#endif
/* ZODIAC_DEFINE_MODULE(info_variable): the exported entry returning &info_variable (shared builds) or the static registration record (static builds); see the build rules above */
#if defined(ZODIAC_MODULE_STATIC)
#ifndef ZODIAC_MODULE_ID
#error ZODIAC_MODULE_ID (the module name) must be defined when ZODIAC_MODULE_STATIC is defined
#endif
#if !defined(__GNUC__) && !defined(__clang__)
#error static Zodiac module builds need a GCC-compatible compiler (clang or gcc) for the record section attribute
#endif
#if defined(_WIN32)
#define ZODIAC_STATIC_MODULE_SECTION ".zodmod$m"
#else
#define ZODIAC_STATIC_MODULE_SECTION "zodiac_modules"
#endif
#define ZODIAC_STATIC_MODULE_RECORD_JOIN(id) zodiac_static_module_##id
#define ZODIAC_STATIC_MODULE_RECORD(id) ZODIAC_STATIC_MODULE_RECORD_JOIN(id)
/* The record's name: ZODIAC_MODULE_RECORD when defined (one finished identifier, safe from macros), otherwise built from ZODIAC_MODULE_ID */
#if defined(ZODIAC_MODULE_RECORD)
#define ZODIAC_STATIC_MODULE_RECORD_NAME ZODIAC_MODULE_RECORD
#else
#define ZODIAC_STATIC_MODULE_RECORD_NAME ZODIAC_STATIC_MODULE_RECORD(ZODIAC_MODULE_ID)
#endif
#define ZODIAC_DEFINE_MODULE(info_variable) \
ZODIAC_LINKAGE_C __attribute__((used,section(ZODIAC_STATIC_MODULE_SECTION))) const zodiac_module_info* const ZODIAC_STATIC_MODULE_RECORD_NAME=&(info_variable);
#else
#define ZODIAC_DEFINE_MODULE(info_variable) \
ZODIAC_LINKAGE_C ZODIAC_EXPORT const zodiac_module_info* zodiac_module_get_info(void) \
{ \
return &(info_variable); \
}
#endif

#ifdef __cplusplus
}
#endif

#endif
