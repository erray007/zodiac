#include "core/host.hpp"
#include <angelscript.h>
#include <scriptarray/scriptarray.h>
#include <bit>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

namespace zodiac {
namespace {
constexpr std::string_view reason_invalid_argument="invalid argument";
constexpr std::string_view reason_declaration="invalid declaration";
constexpr std::string_view reason_not_primitive="only primitive types are allowed on the native fast path";
constexpr std::string_view reason_already_registered="already registered";
bool is_space(char c) noexcept
{
return c==' '||c=='\t'||c=='\n'||c=='\r';
}
bool is_word(char c) noexcept
{
return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';
}
bool is_digit(char c) noexcept
{
return c>='0'&&c<='9';
}
bool is_primitive_name(std::string_view word) noexcept
{
static constexpr std::string_view names[]={"void","bool","int8","int16","int","int32","int64","uint8","uint16","uint","uint32","uint64","float","double"};
for(std::string_view name:names)if(word==name)return true;
return false;
}
// Minimal tokenizer for the native fast path; AngelScript still parses the declaration fully afterwards.
struct Cursor{
std::string_view text;
std::size_t at=0;
void skip() noexcept
{
while(at<text.size()&&is_space(text[at]))++at;
}
char peek() noexcept
{
skip();
return at<text.size()?text[at]:'\0';
}
bool eat(char c) noexcept
{
if(peek()!=c)return false;
++at;
return true;
}
std::string_view word() noexcept
{
skip();
const std::size_t start=at;
while(at<text.size()&&is_word(text[at]))++at;
return text.substr(start,at-start);
}
// A default argument constant: number (with optional exponent sign), true or false.
std::string_view literal() noexcept
{
skip();
const std::size_t start=at;
while(at<text.size()){
const char c=text[at];
const bool exponent_sign=(c=='+'||c=='-')&&at>start&&(text[at-1]=='e'||text[at-1]=='E')&&is_digit(text[start])&&!(at-start>=2&&(text[start+1]=='x'||text[start+1]=='X'));
if(!is_word(c)&&c!='.'&&!exponent_sign)break;
++at;
}
return text.substr(start,at-start);
}
bool done() noexcept
{
skip();
return at==text.size();
}
};
// Returns ZODIAC_OK, ZODIAC_E_NOT_PRIMITIVE or ZODIAC_E_DECLARATION.
int check_type(Cursor& cursor,bool is_return) noexcept
{
std::string_view word=cursor.word();
if(word=="const")word=cursor.word();
if(word.empty())return ZODIAC_E_DECLARATION;
if(!is_primitive_name(word))return ZODIAC_E_NOT_PRIMITIVE;
const char next=cursor.peek();
if(next=='&'||next=='@'||next=='<'||next=='[')return ZODIAC_E_NOT_PRIMITIVE;
if(word=="void"&&!is_return)return ZODIAC_E_DECLARATION;
return ZODIAC_OK;
}
int check_native_declaration(std::string_view declaration) noexcept
{
Cursor cursor{declaration};
if(const int result=check_type(cursor,true);result!=ZODIAC_OK)return result;
const std::string_view name=cursor.word();
if(name.empty()||is_digit(name.front())||!cursor.eat('('))return ZODIAC_E_DECLARATION;
if(!cursor.eat(')')){
const std::size_t parameters=cursor.at;
if(!(cursor.word()=="void"&&cursor.eat(')'))){
cursor.at=parameters;
for(;;){
if(const int result=check_type(cursor,false);result!=ZODIAC_OK)return result;
if(is_word(cursor.peek()))cursor.word();
if(cursor.eat('=')){
if(!cursor.eat('-'))cursor.eat('+');
if(cursor.literal().empty())return ZODIAC_E_DECLARATION;
}
if(cursor.eat(','))continue;
if(cursor.eat(')'))break;
return ZODIAC_E_DECLARATION;
}
}
}
if(!cursor.done()&&(cursor.word()!="property"||!cursor.done()))return ZODIAC_E_DECLARATION;
return ZODIAC_OK;
}
bool is_named(const asITypeInfo* type,const char* name) noexcept
{
const char* name_space=type->GetNamespace();
return std::strcmp(type->GetName(),name)==0&&(!name_space||!*name_space);
}
bool is_std_string(const asITypeInfo* type) noexcept
{
return type&&is_named(type,"string")&&(type->GetFlags()&asOBJ_VALUE)&&type->GetSize()==sizeof(std::string);
}
CScriptArray* to_array(zodiac_array* array) noexcept
{
return reinterpret_cast<CScriptArray*>(array);
}
bool is_string_array(const CScriptArray* array) noexcept
{
return is_std_string(array->GetArrayObjectType()->GetSubType());
}
// Geometric growth so repeated pushes stay amortized O(1); CScriptArray itself grows to the exact size.
void grow_to(CScriptArray* array,asUINT size) noexcept
{
array->Reserve(size<=(asUINT(1)<<31)?std::bit_ceil(size):size);
array->Resize(size);
}
template<class T> T load(const void* address) noexcept
{
T value;
std::memcpy(&value,address,sizeof(T));
return value;
}
template<class T> void store(void* address,T value) noexcept
{
std::memcpy(address,&value,sizeof(T));
}
}

// The C entry points of the ABI table. A zodiac_call* is the asIScriptGeneric* of the current call, and the
// generic function's auxiliary pointer is its Binding, so every accessor is a cast plus a virtual call or two.
struct HostAbi{
using Kind=Host::Kind;
using Value=Host::Value;
using Binding=Host::Binding;
static_assert(std::is_standard_layout_v<Host::Table>);
static_assert(offsetof(Host::Table,abi)==0);
static Host* owner(zodiac_host* host) noexcept
{
return host?reinterpret_cast<Host::Table*>(host)->owner:nullptr;
}
static asIScriptGeneric* generic(zodiac_call* call) noexcept
{
return reinterpret_cast<asIScriptGeneric*>(call);
}
static const Binding& binding(asIScriptGeneric* gen) noexcept
{
return *static_cast<const Binding*>(gen->GetAuxiliary());
}
static void thunk(asIScriptGeneric* gen)
{
const Binding& bound=binding(gen);
if(bound.string_result)::new(gen->GetAddressOfReturnLocation()) std::string();
bound.fn(reinterpret_cast<zodiac_call*>(gen));
}
static void misuse(asIScriptGeneric* gen,const Binding& bound,std::string_view accessor)
{
std::string message(accessor);
message.append(" does not match \"").append(gen->GetFunction()->GetDeclaration(true,true,false)).append("\"");
bound.host->log(ZODIAC_LOG_ERROR,message);
}
template<class T> static T read_number(zodiac_call* call,uint32_t index) noexcept
{
asIScriptGeneric* gen=generic(call);
const Binding& bound=binding(gen);
if(index>=bound.params.size())return T();
const Value& param=bound.params[index];
const void* address=gen->GetAddressOfArg(index);
if(param.by_reference)address=*static_cast<void* const*>(address);
switch(param.kind){
case Kind::boolean:return static_cast<T>(load<bool>(address));
case Kind::int8:return static_cast<T>(load<int8_t>(address));
case Kind::int16:return static_cast<T>(load<int16_t>(address));
case Kind::int32:return static_cast<T>(load<int32_t>(address));
case Kind::int64:return static_cast<T>(load<int64_t>(address));
case Kind::uint8:return static_cast<T>(load<uint8_t>(address));
case Kind::uint16:return static_cast<T>(load<uint16_t>(address));
case Kind::uint32:return static_cast<T>(load<uint32_t>(address));
case Kind::uint64:return static_cast<T>(load<uint64_t>(address));
case Kind::float32:return static_cast<T>(load<float>(address));
case Kind::float64:return static_cast<T>(load<double>(address));
default:return T();
}
}
template<class T> static void write_number(zodiac_call* call,T value,std::string_view accessor)
{
asIScriptGeneric* gen=generic(call);
const Binding& bound=binding(gen);
const Kind kind=bound.result.by_reference?Kind::other:bound.result.kind;
void* address=gen->GetAddressOfReturnLocation();
switch(kind){
case Kind::boolean:store(address,static_cast<bool>(value));return;
case Kind::int8:store(address,static_cast<int8_t>(value));return;
case Kind::int16:store(address,static_cast<int16_t>(value));return;
case Kind::int32:store(address,static_cast<int32_t>(value));return;
case Kind::int64:store(address,static_cast<int64_t>(value));return;
case Kind::uint8:store(address,static_cast<uint8_t>(value));return;
case Kind::uint16:store(address,static_cast<uint16_t>(value));return;
case Kind::uint32:store(address,static_cast<uint32_t>(value));return;
case Kind::uint64:store(address,static_cast<uint64_t>(value));return;
case Kind::float32:store(address,static_cast<float>(value));return;
case Kind::float64:store(address,static_cast<double>(value));return;
default:misuse(gen,bound,accessor);
}
}
static const Value* param(asIScriptGeneric* gen,uint32_t index) noexcept
{
const Binding& bound=binding(gen);
return index<bound.params.size()?&bound.params[index]:nullptr;
}
// Replaces the returned handle; the host owns exactly one reference to whatever is returned.
static void return_handle(asIScriptGeneric* gen,const Value& result,void* object) noexcept
{
void** slot=static_cast<void**>(gen->GetAddressOfReturnLocation());
if(*slot)gen->GetEngine()->ReleaseScriptObject(*slot,result.type);
*slot=object;
}
static int register_function(zodiac_host* host,const char* declaration,zodiac_generic_fn fn,void* user_data)
{
Host* self=owner(host);
return self?self->register_generic(Host::Target::function,nullptr,declaration,fn,user_data):ZODIAC_E_INVALID_ARGUMENT;
}
static int register_native_function(zodiac_host* host,const char* declaration,void (*fn)(void))
{
Host* self=owner(host);
return self?self->register_native(declaration,fn):ZODIAC_E_INVALID_ARGUMENT;
}
static int register_ref_type(zodiac_host* host,const char* name,void (*add_ref)(void*),void (*release)(void*))
{
Host* self=owner(host);
return self?self->register_ref_type(name,add_ref,release):ZODIAC_E_INVALID_ARGUMENT;
}
static int register_method(zodiac_host* host,const char* type_name,const char* declaration,zodiac_generic_fn fn,void* user_data)
{
Host* self=owner(host);
return self?self->register_generic(Host::Target::method,type_name,declaration,fn,user_data):ZODIAC_E_INVALID_ARGUMENT;
}
static int register_factory(zodiac_host* host,const char* type_name,const char* declaration,zodiac_generic_fn fn,void* user_data)
{
Host* self=owner(host);
return self?self->register_generic(Host::Target::factory,type_name,declaration,fn,user_data):ZODIAC_E_INVALID_ARGUMENT;
}
static void* call_user_data(zodiac_call* call) noexcept
{
return binding(generic(call)).user_data;
}
static void* call_object(zodiac_call* call) noexcept
{
return generic(call)->GetObject();
}
static int32_t arg_int32(zodiac_call* call,uint32_t index) noexcept
{
return read_number<int32_t>(call,index);
}
static int64_t arg_int64(zodiac_call* call,uint32_t index) noexcept
{
return read_number<int64_t>(call,index);
}
static float arg_float(zodiac_call* call,uint32_t index) noexcept
{
return read_number<float>(call,index);
}
static double arg_double(zodiac_call* call,uint32_t index) noexcept
{
return read_number<double>(call,index);
}
static int arg_bool(zodiac_call* call,uint32_t index) noexcept
{
return read_number<bool>(call,index)?1:0;
}
static const char* arg_string(zodiac_call* call,uint32_t index,size_t* length) noexcept
{
asIScriptGeneric* gen=generic(call);
const Value* value=param(gen,index);
const std::string* text=value&&value->kind==Kind::string?static_cast<const std::string*>(gen->GetArgObject(index)):nullptr;
if(length)*length=text?text->size():0;
return text?text->data():nullptr;
}
static void* arg_object(zodiac_call* call,uint32_t index) noexcept
{
asIScriptGeneric* gen=generic(call);
const Value* value=param(gen,index);
return value&&(value->kind==Kind::object||value->kind==Kind::array)?gen->GetArgObject(index):nullptr;
}
static zodiac_array* arg_array(zodiac_call* call,uint32_t index) noexcept
{
asIScriptGeneric* gen=generic(call);
const Value* value=param(gen,index);
return value&&value->kind==Kind::array?reinterpret_cast<zodiac_array*>(gen->GetArgObject(index)):nullptr;
}
static void ret_int32(zodiac_call* call,int32_t value)
{
write_number(call,value,"ret_int32");
}
static void ret_int64(zodiac_call* call,int64_t value)
{
write_number(call,value,"ret_int64");
}
static void ret_float(zodiac_call* call,float value)
{
write_number(call,value,"ret_float");
}
static void ret_double(zodiac_call* call,double value)
{
write_number(call,value,"ret_double");
}
static void ret_bool(zodiac_call* call,int value)
{
write_number(call,value!=0,"ret_bool");
}
static void ret_string(zodiac_call* call,const char* data,size_t length)
{
asIScriptGeneric* gen=generic(call);
const Binding& bound=binding(gen);
if(!bound.string_result){
misuse(gen,bound,"ret_string");
return;
}
std::string& result=*static_cast<std::string*>(gen->GetAddressOfReturnLocation());
if(data)result.assign(data,length);
else result.clear();
}
static void ret_object(zodiac_call* call,void* object)
{
asIScriptGeneric* gen=generic(call);
const Binding& bound=binding(gen);
if(!bound.result.handle){
misuse(gen,bound,"ret_object");
if(object&&bound.result.type&&!(bound.result.type->GetFlags()&asOBJ_VALUE))gen->GetEngine()->ReleaseScriptObject(object,bound.result.type);
return;
}
return_handle(gen,bound.result,object);
}
static zodiac_array* ret_array(zodiac_call* call,const char* element_type,uint32_t reserve)
{
asIScriptGeneric* gen=generic(call);
const Binding& bound=binding(gen);
const std::string_view element=element_type?element_type:"";
const Kind kind=element=="string"?Kind::string:element=="int"?Kind::int32:element=="double"?Kind::float64:Kind::other;
if(kind==Kind::other){
std::string message="ret_array: unknown element type \"";
message.append(element).append("\" (expected \"string\", \"int\" or \"double\")");
bound.host->log(ZODIAC_LOG_ERROR,message);
return nullptr;
}
const Value& result=bound.result;
if(result.kind!=Kind::array||!result.handle||result.element!=kind){
misuse(gen,bound,"ret_array");
return nullptr;
}
CScriptArray* array=CScriptArray::Create(result.type);
if(!array)return nullptr;
if(reserve)array->Reserve(reserve<=(uint32_t(1)<<31)?std::bit_ceil(reserve):reserve);
return_handle(gen,result,array);
return reinterpret_cast<zodiac_array*>(array);
}
static uint32_t array_length(zodiac_array* array) noexcept
{
return array?to_array(array)->GetSize():0;
}
static const char* array_get_string(zodiac_array* array,uint32_t index,size_t* length) noexcept
{
CScriptArray* items=to_array(array);
const std::string* text=items&&index<items->GetSize()&&is_string_array(items)?static_cast<const std::string*>(items->At(index)):nullptr;
if(length)*length=text?text->size():0;
return text?text->data():nullptr;
}
static int32_t array_get_int32(zodiac_array* array,uint32_t index) noexcept
{
CScriptArray* items=to_array(array);
return items&&index<items->GetSize()&&items->GetElementTypeId()==asTYPEID_INT32?load<int32_t>(items->At(index)):0;
}
static double array_get_double(zodiac_array* array,uint32_t index) noexcept
{
CScriptArray* items=to_array(array);
return items&&index<items->GetSize()&&items->GetElementTypeId()==asTYPEID_DOUBLE?load<double>(items->At(index)):0.0;
}
static void array_push_string(zodiac_array* array,const char* data,size_t length)
{
CScriptArray* items=to_array(array);
if(!items||!is_string_array(items))return;
const asUINT size=items->GetSize();
grow_to(items,size+1);
if(std::string* text=static_cast<std::string*>(items->At(size));text&&data)text->assign(data,length);
}
static void array_push_int32(zodiac_array* array,int32_t value) noexcept
{
CScriptArray* items=to_array(array);
if(!items||items->GetElementTypeId()!=asTYPEID_INT32)return;
const asUINT size=items->GetSize();
grow_to(items,size+1);
if(void* slot=items->At(size))store(slot,value);
}
static void array_push_double(zodiac_array* array,double value) noexcept
{
CScriptArray* items=to_array(array);
if(!items||items->GetElementTypeId()!=asTYPEID_DOUBLE)return;
const asUINT size=items->GetSize();
grow_to(items,size+1);
if(void* slot=items->At(size))store(slot,value);
}
static void set_exception(zodiac_call*,const char* message) noexcept
{
if(asIScriptContext* context=asGetActiveContext())context->SetException(message?message:"");
}
static void log(zodiac_host* host,zodiac_log_level level,const char* message)
{
if(const Host* self=owner(host))self->log(level,message?message:"");
}
};

Host::Host(asIScriptEngine* engine):table_{},engine_(engine)
{
zodiac_host& abi=table_.abi;
abi.abi_version=ZODIAC_ABI_VERSION;
abi.struct_size=sizeof(zodiac_host);
abi.register_function=&HostAbi::register_function;
abi.register_native_function=&HostAbi::register_native_function;
abi.register_ref_type=&HostAbi::register_ref_type;
abi.register_method=&HostAbi::register_method;
abi.register_factory=&HostAbi::register_factory;
abi.call_user_data=&HostAbi::call_user_data;
abi.call_object=&HostAbi::call_object;
abi.arg_int32=&HostAbi::arg_int32;
abi.arg_int64=&HostAbi::arg_int64;
abi.arg_float=&HostAbi::arg_float;
abi.arg_double=&HostAbi::arg_double;
abi.arg_bool=&HostAbi::arg_bool;
abi.arg_string=&HostAbi::arg_string;
abi.arg_object=&HostAbi::arg_object;
abi.arg_array=&HostAbi::arg_array;
abi.ret_int32=&HostAbi::ret_int32;
abi.ret_int64=&HostAbi::ret_int64;
abi.ret_float=&HostAbi::ret_float;
abi.ret_double=&HostAbi::ret_double;
abi.ret_bool=&HostAbi::ret_bool;
abi.ret_string=&HostAbi::ret_string;
abi.ret_object=&HostAbi::ret_object;
abi.ret_array=&HostAbi::ret_array;
abi.array_length=&HostAbi::array_length;
abi.array_get_string=&HostAbi::array_get_string;
abi.array_get_int32=&HostAbi::array_get_int32;
abi.array_get_double=&HostAbi::array_get_double;
abi.array_push_string=&HostAbi::array_push_string;
abi.array_push_int32=&HostAbi::array_push_int32;
abi.array_push_double=&HostAbi::array_push_double;
abi.set_exception=&HostAbi::set_exception;
abi.log=&HostAbi::log;
table_.owner=this;
}

void Host::log(zodiac_log_level level,std::string_view message) const
{
if(log_handler_)log_handler_(level,message);
}

int Host::fail(int code,std::string_view declaration,std::string_view reason) const
{
if(log_handler_){
std::string message="module ";
message.append(current_module_).append(": cannot register \"").append(declaration).append("\": ").append(reason);
log_handler_(ZODIAC_LOG_ERROR,message);
}
return code;
}

int Host::fail_engine(int engine_code,std::string_view declaration) const
{
switch(engine_code){
case asALREADY_REGISTERED:
case asNAME_TAKEN:
return fail(ZODIAC_E_ALREADY_REGISTERED,declaration,reason_already_registered);
case asINVALID_DECLARATION:
case asINVALID_NAME:
return fail(ZODIAC_E_DECLARATION,declaration,reason_declaration);
case asILLEGAL_BEHAVIOUR_FOR_TYPE:
return fail(ZODIAC_E_DECLARATION,declaration,"declaration not allowed for this type");
case asINVALID_TYPE:
case asINVALID_OBJECT:
return fail(ZODIAC_E_UNKNOWN_TYPE,declaration,"unknown type");
case asNOT_SUPPORTED:
case asWRONG_CALLING_CONV:
return fail(ZODIAC_E_INVALID_ARGUMENT,declaration,"calling convention not supported on this platform");
default:
return fail(ZODIAC_E_INVALID_ARGUMENT,declaration,"angelscript error "+std::to_string(engine_code));
}
}

Host::Value Host::classify(int type_id,bool by_reference) const
{
Value value;
value.by_reference=by_reference;
value.handle=(type_id&asTYPEID_OBJHANDLE)!=0;
if(!(type_id&asTYPEID_MASK_OBJECT)){
switch(type_id){
case asTYPEID_BOOL:value.kind=Kind::boolean;break;
case asTYPEID_INT8:value.kind=Kind::int8;break;
case asTYPEID_INT16:value.kind=Kind::int16;break;
case asTYPEID_INT32:value.kind=Kind::int32;break;
case asTYPEID_INT64:value.kind=Kind::int64;break;
case asTYPEID_UINT8:value.kind=Kind::uint8;break;
case asTYPEID_UINT16:value.kind=Kind::uint16;break;
case asTYPEID_UINT32:value.kind=Kind::uint32;break;
case asTYPEID_UINT64:value.kind=Kind::uint64;break;
case asTYPEID_FLOAT:value.kind=Kind::float32;break;
case asTYPEID_DOUBLE:value.kind=Kind::float64;break;
default:
// Enums are signed integers of their underlying size.
if(type_id>asTYPEID_DOUBLE){
switch(engine_->GetSizeOfPrimitiveType(type_id)){
case 1:value.kind=Kind::int8;break;
case 2:value.kind=Kind::int16;break;
case 4:value.kind=Kind::int32;break;
case 8:value.kind=Kind::int64;break;
default:break;
}
}
}
return value;
}
asITypeInfo* type=engine_->GetTypeInfoById(type_id);
value.type=type;
if(!type||(type->GetFlags()&asOBJ_FUNCDEF))return value;
if(is_std_string(type)){
if(!value.handle)value.kind=Kind::string;
return value;
}
if((type->GetFlags()&asOBJ_TEMPLATE)&&is_named(type,"array")&&type->GetSubTypeCount()==1){
value.kind=Kind::array;
const int element_id=type->GetSubTypeId(0);
if(element_id==asTYPEID_INT32)value.element=Kind::int32;
else if(element_id==asTYPEID_DOUBLE)value.element=Kind::float64;
else if(is_std_string(type->GetSubType(0)))value.element=Kind::string;
return value;
}
value.kind=Kind::object;
return value;
}

void Host::describe(Binding& binding,const asIScriptFunction* function) const
{
asDWORD flags=0;
const int return_id=function->GetReturnTypeId(&flags);
binding.result=classify(return_id,(flags&asTM_INOUTREF)!=0);
binding.string_result=binding.result.kind==Kind::string&&!binding.result.by_reference;
const asUINT count=function->GetParamCount();
binding.params.reserve(count);
for(asUINT index=0;index<count;++index){
int type_id=0;
flags=0;
function->GetParam(index,&type_id,&flags);
binding.params.push_back(classify(type_id,(flags&asTM_INOUTREF)!=0));
}
}

int Host::register_generic(Target target,const char* type_name,const char* declaration,zodiac_generic_fn fn,void* user_data)
{
const std::string_view shown=declaration?declaration:"";
if(!declaration||!fn||(target!=Target::function&&!type_name))return fail(ZODIAC_E_INVALID_ARGUMENT,shown,reason_invalid_argument);
asITypeInfo* type=nullptr;
if(target!=Target::function){
type=engine_->GetTypeInfoByName(type_name);
if(!type)return fail(ZODIAC_E_UNKNOWN_TYPE,shown,"unknown type \""+std::string(type_name)+"\"");
}
// Look duplicates up first: a failed AngelScript registration marks the whole engine configuration as invalid.
const asIScriptFunction* existing=target==Target::function?engine_->GetGlobalFunctionByDecl(declaration):target==Target::method?type->GetMethodByDecl(declaration,false):type->GetFactoryByDecl(declaration);
if(existing)return fail(ZODIAC_E_ALREADY_REGISTERED,shown,reason_already_registered);
Binding& binding=bindings_.emplace_back(Binding{fn,user_data,this,false,{},{}});
int result=0;
if(target==Target::function)result=engine_->RegisterGlobalFunction(declaration,asFUNCTION(HostAbi::thunk),asCALL_GENERIC,&binding);
else if(target==Target::method)result=engine_->RegisterObjectMethod(type_name,declaration,asFUNCTION(HostAbi::thunk),asCALL_GENERIC,&binding);
else result=engine_->RegisterObjectBehaviour(type_name,asBEHAVE_FACTORY,declaration,asFUNCTION(HostAbi::thunk),asCALL_GENERIC,&binding);
const asIScriptFunction* function=result>=0?engine_->GetFunctionById(result):nullptr;
if(!function){
bindings_.pop_back();
return fail_engine(result<0?result:asERROR,shown);
}
describe(binding,function);
return ZODIAC_OK;
}

int Host::register_native(const char* declaration,void (*fn)(void))
{
const std::string_view shown=declaration?declaration:"";
if(!declaration||!fn)return fail(ZODIAC_E_INVALID_ARGUMENT,shown,reason_invalid_argument);
switch(check_native_declaration(shown)){
case ZODIAC_OK:break;
case ZODIAC_E_NOT_PRIMITIVE:return fail(ZODIAC_E_NOT_PRIMITIVE,shown,reason_not_primitive);
default:return fail(ZODIAC_E_DECLARATION,shown,reason_declaration);
}
if(engine_->GetGlobalFunctionByDecl(declaration))return fail(ZODIAC_E_ALREADY_REGISTERED,shown,reason_already_registered);
const int result=engine_->RegisterGlobalFunction(declaration,asFUNCTION(fn),asCALL_CDECL);
return result<0?fail_engine(result,shown):ZODIAC_OK;
}

int Host::register_ref_type(const char* name,void (*add_ref)(void*),void (*release)(void*))
{
const std::string_view shown=name?name:"";
// Either both reference counting callbacks or neither (a type the host never counts).
if(shown.empty()||(add_ref==nullptr)!=(release==nullptr))return fail(ZODIAC_E_INVALID_ARGUMENT,shown,reason_invalid_argument);
if(engine_->GetTypeInfoByName(name))return fail(ZODIAC_E_ALREADY_REGISTERED,shown,reason_already_registered);
int result=engine_->RegisterObjectType(name,0,add_ref?asOBJ_REF:asOBJ_REF|asOBJ_NOCOUNT);
if(result>=0&&add_ref){
result=engine_->RegisterObjectBehaviour(name,asBEHAVE_ADDREF,"void f()",asFUNCTION(add_ref),asCALL_CDECL_OBJLAST);
if(result>=0)result=engine_->RegisterObjectBehaviour(name,asBEHAVE_RELEASE,"void f()",asFUNCTION(release),asCALL_CDECL_OBJLAST);
}
return result<0?fail_engine(result,shown):ZODIAC_OK;
}
}
