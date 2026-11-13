/*
Zodiac plugin ABI v1. This header is the permanent public C contract between the engine and every module.

ABI rules:
- Append-only forever. Existing members never move, change type, or get removed. New members are added at the end of a struct only, and every struct that can grow carries struct_size so the other side can tell what is present.
- Only C types cross the boundary. No AngelScript types, no C++ classes, no std:: types, no exceptions. Errors are reported through return codes or set_exception.
- A module declares in abi_version the minimum host ABI it needs. The host refuses any module whose abi_version is newer than the host's ZODIAC_ABI_VERSION.

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
typedef struct zodiac_host zodiac_host;
typedef struct zodiac_call zodiac_call;
typedef struct zodiac_array zodiac_array;
typedef void (*zodiac_generic_fn)(zodiac_call* call);
typedef enum zodiac_log_level{ZODIAC_LOG_INFO=0,ZODIAC_LOG_WARNING=1,ZODIAC_LOG_ERROR=2}zodiac_log_level;
typedef struct zodiac_module_info{
uint32_t abi_version;          /* minimum host ABI this module needs */
const char* name;              /* "example"; [a-z0-9_]+ */
const char* version;           /* semver */
const char* license;           /* SPDX id, e.g. "Zlib" */
const char* const* dependencies;
uint32_t dependency_count;
int (*register_module)(zodiac_host* host);   /* ZODIAC_OK or negative code */
void (*shutdown_module)(void);               /* may be NULL */
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
/* ZODIAC_DEFINE_MODULE(info_variable) defines the exported entry returning &info_variable (shared builds) */
#if defined(ZODIAC_MODULE_STATIC)
/* Static registration is not implemented yet (a later plan adds it), so this expands to nothing for now. */
#define ZODIAC_DEFINE_MODULE(info_variable)
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
