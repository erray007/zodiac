// bench_types: the speed gate of spec 8.4. Measures Zodiac's script string, array<T> and dictionary against AngelScript's sample
// add-ons (scriptstdstring, scriptarray, scriptdictionary) in one process.
// Contract:
// - Two engines: one with Zodiac's types (registered as ScriptRuntime does: array, string, dictionary), one with the add-ons
//   (RegisterStdString, RegisterScriptArray as the default array, RegisterStdStringUtils, RegisterScriptDictionary), both native.
// - Every operation of spec 8.4 is a pair of AngelScript snippets with the same semantics, the add-on side written the way a
//   script would have to write it with the add-ons (where the add-on has no such operation, e.g. character indexing on Turkish
//   text, it is the script code doing the same work). Each snippet defines int64 run(int reps), which does ITEMS units of work
//   reps times and returns a checksum, and may define void setup(), run once before timing. Both sides must return the same
//   checksum; a difference is a bench bug and fails the run (exit code 3).
// - Per operation: reps is calibrated so that one run of both sides together takes at least twice the target time, then the
//   two sides run interleaved for the given rounds (the side that runs first alternates), each run timed alone on the next
//   of kContexts contexts, with a full garbage collection after it (untimed). It prints one JSON line per operation to
//   stdout (and to --out when given): {"op":name,"new_ns":n,"addon_ns":n,"ratio":r}, new_ns and addon_ns the median
//   nanoseconds per unit of work of each side, ratio the median of the per-round ratios new/addon. tools/check_bench.py
//   gates the lines (ratio at most 1.05).
// - Usage: bench_types [--rounds N] [--target-ms N] [--op NAME]... [--out PATH] [--list]. Defaults: 31 rounds (at least 15
//   are required, spec 8.4), 8 ms. --op (repeatable) measures only the named operations, --list prints the names.
// - Exit codes: 0 measured, 1 bad arguments, 2 a snippet did not build or run, 3 the two sides disagree on a checksum.
// Not a ctest test (spec 8.4): CI runs it on release presets, and Task 7 reuses it to compare compiler flags.
#include <angelscript.h>
#include <core/types/array.hpp>
#include <core/types/dictionary.hpp>
#include <core/types/string_binding.hpp>
#include <scriptarray/scriptarray.h>
#include <scriptdictionary/scriptdictionary.h>
#include <scriptstdstring/scriptstdstring.h>
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace {
// Shared by both sides: the texts every string operation reads and a deterministic pseudo-random sequence.
constexpr std::string_view kCommonPrelude=R"(
string repeat_text(const string &in word,int times)
{
string text;
for(int i=0;i<times;++i)text+=word;
return text;
}
const string ascii_text=repeat_text("The quick brown fox jumps over the lazy dog. ",24);
const string turkish_text=repeat_text("Çağrı ışığı söndü, güzel öğün şimdi yeşil. ",24);
uint seed=12345;
int next_random()
{
seed=seed*1103515245+12345;
return int((seed>>8)&0x7FFFFF);
}
)";
// Zodiac's string counts characters itself.
constexpr std::string_view kZodiacPrelude=R"(
const int ascii_count=ascii_text.length();
const int turkish_count=turkish_text.length();
)";
// What a script needs to handle characters (not bytes) with the add-on string: the texts are valid UTF-8.
constexpr std::string_view kAddonPrelude=R"(
int utf8_size(uint8 lead)
{
if(lead<0x80)return 1;
if(lead<0xE0)return 2;
if(lead<0xF0)return 3;
return 4;
}
int utf8_length(const string &in text)
{
int count=0;
for(uint i=0,n=text.length();i<n;++i)if((text[i]&0xC0)!=0x80)++count;
return count;
}
uint utf8_offset(const string &in text,int index)
{
uint offset=0;
for(int i=0;i<index;++i)offset+=utf8_size(text[offset]);
return offset;
}
int utf8_count(const string &in text,uint bytes)
{
int count=0;
for(uint i=0;i<bytes;++i)if((text[i]&0xC0)!=0x80)++count;
return count;
}
string utf8_char_at(const string &in text,int index)
{
uint offset=utf8_offset(text,index);
return text.substr(offset,utf8_size(text[offset]));
}
int utf8_find(const string &in text,const string &in needle,int start)
{
int at=text.findFirst(needle,utf8_offset(text,start));
return at<0?-1:utf8_count(text,uint(at));
}
string replace_all(const string &in text,const string &in from,const string &in to)
{
string result;
uint start=0;
int at;
while((at=text.findFirst(from,start))>=0){
result+=text.substr(start,uint(at)-start);
result+=to;
start=uint(at)+from.length();
}
result+=text.substr(start);
return result;
}
const int ascii_count=int(ascii_text.length());
const int turkish_count=utf8_length(turkish_text);
)";

struct Operation {
const char* name;
int items;
const char* zodiac;
const char* addon;
};

// Each snippet pair measures the same work; ITEMS (prepended as a constant) is the number of units new_ns and addon_ns divide by.
const Operation kOperations[]={
// Construction of a short string (inline in Zodiac, within libc++'s small-string buffer) from a literal, then a copy of it.
{"string_construct_short",1000,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){string s="Merhaba";string t=s;n+=t.byte_length();}return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){string s="Merhaba";string t=s;n+=t.length();}return n;})"},
// Construction of a long string (on the heap on both sides) from a literal, then a copy of it.
{"string_construct_long",1000,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){string s="A longer line of dialogue that does not fit inline.";string t=s;n+=t.byte_length();}return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){string s="A longer line of dialogue that does not fit inline.";string t=s;n+=t.length();}return n;})"},
// Concatenation with +: a new string from two others.
{"string_concat_add",1000,
R"(const string first="Merhaba ";const string second="dünya";
int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){string s=first+second;n+=s.byte_length();}return n;})",
R"(const string first="Merhaba ";const string second="dünya";
int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){string s=first+second;n+=s.length();}return n;})"},
// Concatenation with +=: building a 1280-byte string word by word.
{"string_concat_append",256,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){string s;for(int i=0;i<ITEMS;++i)s+="word ";n+=s.byte_length();}return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){string s;for(int i=0;i<ITEMS;++i)s+="word ";n+=s.length();}return n;})"},
// length() of an ASCII text, as a loop condition calls it: characters in Zodiac, bytes in the add-on (the same here).
{"string_length_ascii",1000,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=ascii_text.length();return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=ascii_text.length();return n;})"},
// The character count of a Turkish text: Zodiac's length(), the add-on side counts UTF-8 lead bytes in script.
{"string_length_turkish",100,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=turkish_text.length();return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=utf8_length(turkish_text);return n;})"},
// The character at an index of an ASCII text, as a one-character string: s[i] in Zodiac, substr(i, 1) in the add-on.
{"string_index_ascii",1000,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){string c=ascii_text[(i*7919)%ascii_count];if(c=="o")++n;}return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){string c=ascii_text.substr((i*7919)%ascii_count,1);if(c=="o")++n;}return n;})"},
// The character code at an index of an ASCII text: code_at(i) in Zodiac, the byte s[i] in the add-on.
{"string_code_ascii",1000,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=ascii_text.code_at((i*7919)%ascii_count);return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=ascii_text[(i*7919)%ascii_count];return n;})"},
// The byte at a byte index of an ASCII text: byte_at(i) in Zodiac, s[i] in the add-on.
{"string_byte_ascii",1000,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=ascii_text.byte_at((i*7919)%ascii_count);return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=ascii_text[(i*7919)%ascii_count];return n;})"},
// The character at an index of a Turkish text: s[i] in Zodiac; the add-on side walks the UTF-8 sequences up to the index in script.
{"string_index_turkish",1000,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){string c=turkish_text[(i*7919)%turkish_count];if(c=="ı")++n;}return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){string c=utf8_char_at(turkish_text,(i*7919)%turkish_count);if(c=="ı")++n;}return n;})"},
// find from a start position in an ASCII text: find in Zodiac, findFirst in the add-on (byte and character indices agree).
{"string_find_ascii",1000,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=ascii_text.find("lazy dog",(i*37)%1000);return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=ascii_text.findFirst("lazy dog",(i*37)%1000);return n;})"},
// find in a Turkish text with character indices: find in Zodiac; the add-on side converts the start to bytes and the hit back in script.
{"string_find_turkish",100,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=turkish_text.find("yeşil",(i*37)%(turkish_count-50));return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=utf8_find(turkish_text,"yeşil",(i*37)%(turkish_count-50));return n;})"},
// split of a 1080-byte ASCII text at spaces into array<string> (217 parts, empty ones kept on both sides).
{"string_split",10,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){array<string>@ parts=ascii_text.split(" ");n+=parts.length()+parts[1].byte_length();}return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){array<string>@ parts=ascii_text.split(" ");n+=parts.length()+parts[1].length();}return n;})"},
// Replacing every match in a 1080-byte ASCII text: Zodiac's replace; the add-on has none, so the script loops over findFirst and substr.
{"string_replace",10,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){string s=ascii_text.replace("fox","wolf");n+=s.byte_length()+s.find("wolf");}return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){string s=replace_all(ascii_text,"fox","wolf");n+=s.length()+s.findFirst("wolf");}return n;})"},
// insert_last of 1000 ints into a new array: insert_last in Zodiac, insertLast in the add-on.
{"array_insert_last_int",1000,
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){array<int> a;for(int i=0;i<ITEMS;++i)a.insert_last(i);n+=a.length()+a[ITEMS-1];}return n;})",
R"(int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){array<int> a;for(int i=0;i<ITEMS;++i)a.insertLast(i);n+=a.length()+a[ITEMS-1];}return n;})"},
// insert_last of 1000 strings into a new array.
{"array_insert_last_string",1000,
R"(array<string> words;
void setup(){for(int i=0;i<64;++i)words.insert_last("item"+i);}
int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){array<string> a;for(int i=0;i<ITEMS;++i)a.insert_last(words[i&63]);n+=a.length()+a[ITEMS-1].byte_length();}return n;})",
R"(array<string> words;
void setup(){for(int i=0;i<64;++i)words.insertLast("item"+i);}
int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){array<string> a;for(int i=0;i<ITEMS;++i)a.insertLast(words[i&63]);n+=a.length()+a[ITEMS-1].length();}return n;})"},
// find of an int in a 1000-element array, at spread positions.
{"array_find_int",100,
R"(array<int> numbers;
void setup(){for(int i=0;i<1000;++i)numbers.insert_last(i*3);}
int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=numbers.find(((i*397)%1000)*3);return n;})",
R"(array<int> numbers;
void setup(){for(int i=0;i<1000;++i)numbers.insertLast(i*3);}
int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=numbers.find(((i*397)%1000)*3);return n;})"},
// find of a string in a 1000-element array, at spread positions.
{"array_find_string",100,
R"(array<string> names;array<string> probes;
void setup(){for(int i=0;i<1000;++i)names.insert_last("name_"+i);for(int i=0;i<ITEMS;++i)probes.insert_last("name_"+((i*397)%1000));}
int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=names.find(probes[i]);return n;})",
R"(array<string> names;array<string> probes;
void setup(){for(int i=0;i<1000;++i)names.insertLast("name_"+i);for(int i=0;i<ITEMS;++i)probes.insertLast("name_"+((i*397)%1000));}
int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i)n+=names.find(probes[i]);return n;})"},
// Ascending sort of a copy of 1000 pseudo-random ints: sort() in Zodiac, sortAsc() in the add-on (the copy is the same on both).
{"array_sort_int",1,
R"(array<int> source;
void setup(){for(int i=0;i<1000;++i)source.insert_last(next_random());}
int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){array<int> a=source;a.sort();n+=a[0]+a[500]+a[999];}return n;})",
R"(array<int> source;
void setup(){for(int i=0;i<1000;++i)source.insertLast(next_random());}
int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){array<int> a=source;a.sortAsc();n+=a[0]+a[500]+a[999];}return n;})"},
// Ascending sort of a copy of 1000 pseudo-random strings.
{"array_sort_string",1,
R"(array<string> source;
void setup(){for(int i=0;i<1000;++i)source.insert_last("name_"+next_random());}
int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){array<string> a=source;a.sort();n+=a[0].byte_length()+a[500].byte_length()+(a[0]<a[999]?1:0)+a.find(source[0]);}return n;})",
R"(array<string> source;
void setup(){for(int i=0;i<1000;++i)source.insertLast("name_"+next_random());}
int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){array<string> a=source;a.sortAsc();n+=a[0].length()+a[500].length()+(a[0]<a[999]?1:0)+a.find(source[0]);}return n;})"},
};

// Dictionary snippets for one size: insert into a new dictionary, look up every key in shuffled order, iterate over the keys.
struct DictionarySnippets {
std::string insert_name,lookup_name,iterate_name;
std::string zodiac_insert,addon_insert,zodiac_lookup,addon_lookup,zodiac_iterate,addon_iterate;
};

DictionarySnippets dictionary_snippets(int size)
{
const std::string count=std::to_string(size);
DictionarySnippets s;
s.insert_name="dict_insert_"+count;
s.lookup_name="dict_lookup_"+count;
s.iterate_name="dict_iterate_"+count;
// The keys, the table they fill and a shuffled copy of them (Fisher-Yates with next_random), the same on both sides.
const std::string zodiac_setup="array<string> keys;array<string> probes;dictionary table;\n"
"void setup(){for(int i=0;i<ITEMS;++i){keys.insert_last(\"key\"+i);table.set(keys[i],int64(i));}probes=keys;"
"for(int i=ITEMS-1;i>0;--i){int j=next_random()%(i+1);string t=probes[i];probes[i]=probes[j];probes[j]=t;}}\n";
const std::string addon_setup="array<string> keys;array<string> probes;dictionary table;\n"
"void setup(){for(int i=0;i<ITEMS;++i){keys.insertLast(\"key\"+i);table.set(keys[i],int64(i));}probes=keys;"
"for(int i=ITEMS-1;i>0;--i){int j=next_random()%(i+1);string t=probes[i];probes[i]=probes[j];probes[j]=t;}}\n";
// set(key, int64) of ITEMS keys into a new dictionary.
s.zodiac_insert=zodiac_setup+"int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){dictionary d;for(int i=0;i<ITEMS;++i)d.set(keys[i],int64(i));n+=d.length();}return n;}";
s.addon_insert=addon_setup+"int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){dictionary d;for(int i=0;i<ITEMS;++i)d.set(keys[i],int64(i));n+=d.getSize();}return n;}";
// get(key, int64 &out) of every key, in shuffled order.
const std::string lookup="int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r)for(int i=0;i<ITEMS;++i){int64 v;if(table.get(probes[i],v))n+=v;}return n;}";
s.zodiac_lookup=zodiac_setup+lookup;
s.addon_lookup=addon_setup+lookup;
// Every key and its value: keys() in Zodiac, getKeys() in the add-on, then get of each key.
s.zodiac_iterate=zodiac_setup+"int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){array<string>@ k=table.keys();for(int i=0,c=k.length();i<c;++i){int64 v;table.get(k[i],v);n+=v;}}return n;}";
s.addon_iterate=addon_setup+"int64 run(int reps){int64 n=0;for(int r=0;r<reps;++r){array<string>@ k=table.getKeys();for(uint i=0,c=k.length();i<c;++i){int64 v;table.get(k[i],v);n+=v;}}return n;}";
return s;
}

struct Job {
std::string name;
int items;
std::string zodiac;
std::string addon;
};

std::vector<Job> all_jobs()
{
std::vector<Job> jobs;
for(const Operation& operation:kOperations)jobs.push_back({operation.name,operation.items,operation.zodiac,operation.addon});
for(const int size:{10,1000,100000}){
DictionarySnippets s=dictionary_snippets(size);
jobs.push_back({s.insert_name,size,s.zodiac_insert,s.addon_insert});
jobs.push_back({s.lookup_name,size,s.zodiac_lookup,s.addon_lookup});
jobs.push_back({s.iterate_name,size,s.zodiac_iterate,s.addon_iterate});
}
return jobs;
}

[[noreturn]] void fail(int code,const std::string& message)
{
std::fprintf(stderr,"bench_types: %s\n",message.c_str());
std::exit(code);
}

void print_message(const asSMessageInfo* info,void*)
{
const char* kind=info->type==asMSGTYPE_ERROR?"error":info->type==asMSGTYPE_WARNING?"warning":"info";
std::fprintf(stderr,"bench_types: %s (%d, %d): %s: %s\n",info->section,info->row,info->col,kind,info->message);
}

asIScriptEngine* make_engine(bool zodiac_types)
{
asIScriptEngine* engine=asCreateScriptEngine();
if(!engine)fail(2,"cannot create an AngelScript engine");
engine->SetMessageCallback(asFUNCTION(print_message),nullptr,asCALL_CDECL);
if(zodiac_types){
zodiac::register_array(engine);
zodiac::register_string(engine);
zodiac::register_dictionary(engine);
}
else{
RegisterStdString(engine);
RegisterScriptArray(engine,true);
RegisterStdStringUtils(engine);
RegisterScriptDictionary(engine);
}
return engine;
}

// One side of an operation: its engine, its contexts and the built snippet. Runs take turns over kContexts contexts, each
// with its own script stack at its own address, so no single stack placement (cache sets, 4K aliasing against the data)
// biases a whole measurement; the medians span all of them.
constexpr int kContexts=4;
class Side {
public:
Side(asIScriptEngine* engine,const char* label):engine_(engine),label_(label)
{
for(asIScriptContext*& context:contexts_){
context=engine_->CreateContext();
if(!context)fail(2,"cannot create a script context");
}
}
~Side()
{
for(asIScriptContext* context:contexts_)context->Release();
}
Side(const Side&)=delete;
Side& operator=(const Side&)=delete;
void build(const Job& job,std::string_view prelude,const std::string& snippet)
{
context_=contexts_[0];
const std::string section=job.name+" ("+label_+")";
module_=engine_->GetModule(section.c_str(),asGM_ALWAYS_CREATE);
std::string source(kCommonPrelude);
source.append(prelude).append("const int ITEMS=").append(std::to_string(job.items)).append(";\n").append(snippet);
if(module_->AddScriptSection(section.c_str(),source.data(),source.size())<0||module_->Build()<0)fail(2,section+" does not build");
run_=module_->GetFunctionByDecl("int64 run(int)");
if(!run_)fail(2,section+" has no int64 run(int)");
if(asIScriptFunction* setup=module_->GetFunctionByDecl("void setup()")){
if(context_->Prepare(setup)<0||context_->Execute()!=asEXECUTION_FINISHED)fail(2,section+": setup() failed"+exception());
}
}
// Runs run(reps) once; returns its checksum and stores the time taken in nanoseconds. The garbage collection after it is untimed.
std::int64_t run(int reps,double& ns)
{
context_=contexts_[next_];
next_=(next_+1)%kContexts;
if(context_->Prepare(run_)<0)fail(2,label_+": cannot prepare run()");
context_->SetArgDWord(0,static_cast<asDWORD>(reps));
const auto start=std::chrono::steady_clock::now();
const int result=context_->Execute();
const auto stop=std::chrono::steady_clock::now();
if(result!=asEXECUTION_FINISHED)fail(2,std::string(module_->GetName())+": run() failed"+exception());
ns=std::chrono::duration<double,std::nano>(stop-start).count();
const std::int64_t checksum=static_cast<std::int64_t>(context_->GetReturnQWord());
context_->Unprepare();
engine_->GarbageCollect(asGC_FULL_CYCLE);
return checksum;
}
void discard()
{
if(module_)module_->Discard();
module_=nullptr;
run_=nullptr;
engine_->GarbageCollect(asGC_FULL_CYCLE);
}
private:
std::string exception() const
{
if(context_->GetState()!=asEXECUTION_EXCEPTION)return "";
const char* text=context_->GetExceptionString();
return std::string(": ")+(text?text:"");
}
asIScriptEngine* engine_;
asIScriptContext* contexts_[kContexts];
asIScriptContext* context_=nullptr;
int next_=0;
std::string label_;
asIScriptModule* module_=nullptr;
asIScriptFunction* run_=nullptr;
};

double median(std::vector<double> values)
{
const std::size_t middle=values.size()/2;
std::nth_element(values.begin(),values.begin()+static_cast<std::ptrdiff_t>(middle),values.end());
const double upper=values[middle];
if(values.size()%2)return upper;
return (upper+*std::max_element(values.begin(),values.begin()+static_cast<std::ptrdiff_t>(middle)))/2;
}

void append_number(std::string& out,double value)
{
char digits[64];
const auto end=std::to_chars(digits,digits+sizeof digits,value,std::chars_format::fixed,value<10?4:2).ptr;
out.append(digits,end);
}

std::string measure(const Job& job,Side& zodiac,Side& addon,int rounds,double target_ns)
{
zodiac.build(job,kZodiacPrelude,job.zodiac);
addon.build(job,kAddonPrelude,job.addon);
auto check=[&job](std::int64_t zodiac_sum,std::int64_t addon_sum,int reps){
if(zodiac_sum!=addon_sum)fail(3,job.name+": Zodiac's checksum "+std::to_string(zodiac_sum)+" differs from the add-on's "+std::to_string(addon_sum)+" at reps "+std::to_string(reps));
};
// Calibration: grow reps until one run of both sides takes at least twice the target (each run doubles as a warm-up).
int reps=1;
for(;;){
double zodiac_ns=0,addon_ns=0;
const std::int64_t zodiac_sum=zodiac.run(reps,zodiac_ns);
const std::int64_t addon_sum=addon.run(reps,addon_ns);
check(zodiac_sum,addon_sum,reps);
const double total=zodiac_ns+addon_ns;
if(total>=2*target_ns||reps>=(1<<24))break;
const double scale=total>0?2*target_ns/total*1.1:16;
reps=static_cast<int>(std::min<double>(1<<24,std::max<double>(reps*2.0,reps*scale)));
}
std::vector<double> zodiac_times,addon_times,ratios;
zodiac_times.reserve(static_cast<std::size_t>(rounds));
addon_times.reserve(static_cast<std::size_t>(rounds));
ratios.reserve(static_cast<std::size_t>(rounds));
for(int round=0;round<rounds;++round){
double zodiac_ns=0,addon_ns=0;
std::int64_t zodiac_sum,addon_sum;
if(round%2==0){
zodiac_sum=zodiac.run(reps,zodiac_ns);
addon_sum=addon.run(reps,addon_ns);
}
else{
addon_sum=addon.run(reps,addon_ns);
zodiac_sum=zodiac.run(reps,zodiac_ns);
}
check(zodiac_sum,addon_sum,reps);
zodiac_times.push_back(zodiac_ns);
addon_times.push_back(addon_ns);
ratios.push_back(zodiac_ns/addon_ns);
}
zodiac.discard();
addon.discard();
const double units=static_cast<double>(reps)*job.items;
std::string line="{\"op\":\"";
line.append(job.name).append("\",\"new_ns\":");
append_number(line,median(zodiac_times)/units);
line.append(",\"addon_ns\":");
append_number(line,median(addon_times)/units);
line.append(",\"ratio\":");
char digits[32];
line.append(digits,std::to_chars(digits,digits+sizeof digits,median(ratios),std::chars_format::fixed,4).ptr);
line.append("}\n");
return line;
}

bool parse_int(std::string_view text,int& value)
{
const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
return error==std::errc()&&end==text.data()+text.size();
}

// Measurement hygiene, the same for both sides: on Windows a high priority class, so other programs interrupt the
// interleaved runs less (Linux needs privileges for that), then a short spin so the CPU leaves its idle clock before the
// first operation. Pinning to one CPU was tried and made runs noisier on a loaded machine, so the scheduler keeps its choice.
void quiet_process()
{
#if defined(_WIN32)
SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
#endif
const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(300);
volatile std::uint64_t spin=0;
while(std::chrono::steady_clock::now()<until)spin=spin+1;
}
}

int main(int argc,char** argv)
{
int rounds=31;
int target_ms=8;
std::vector<std::string> only;
const char* out_path=nullptr;
bool list=false;
const std::vector<Job> jobs=all_jobs();
for(int i=1;i<argc;++i){
const std::string_view arg=argv[i];
const bool has_value=i+1<argc;
if(arg=="--rounds"&&has_value){
if(!parse_int(argv[++i],rounds)||rounds<15)fail(1,"--rounds needs a whole number of at least 15 (spec 8.4)");
}
else if(arg=="--target-ms"&&has_value){
if(!parse_int(argv[++i],target_ms)||target_ms<1)fail(1,"--target-ms needs a whole number of at least 1");
}
else if(arg=="--op"&&has_value){
const std::string name=argv[++i];
if(std::none_of(jobs.begin(),jobs.end(),[&name](const Job& job){return job.name==name;}))fail(1,"unknown operation "+name+" (--list prints them)");
only.push_back(name);
}
else if(arg=="--out"&&has_value)out_path=argv[++i];
else if(arg=="--list")list=true;
else fail(1,"usage: bench_types [--rounds N] [--target-ms N] [--op NAME]... [--out PATH] [--list]");
}
if(list){
for(const Job& job:jobs)std::printf("%s\n",job.name.c_str());
return 0;
}
std::FILE* out=nullptr;
if(out_path){
out=std::fopen(out_path,"wb");
if(!out)fail(1,std::string("cannot write ")+out_path+": "+std::strerror(errno));
}
quiet_process();
asPrepareMultithread();
asIScriptEngine* zodiac_engine=make_engine(true);
asIScriptEngine* addon_engine=make_engine(false);
const double target_ns=target_ms*1e6;
{
// The sides release their contexts before the engines shut down.
Side zodiac(zodiac_engine,"zodiac");
Side addon(addon_engine,"add-on");
for(const Job& job:jobs){
if(!only.empty()&&std::find(only.begin(),only.end(),job.name)==only.end())continue;
const std::string line=measure(job,zodiac,addon,rounds,target_ns);
std::fwrite(line.data(),1,line.size(),stdout);
std::fflush(stdout);
if(out&&(std::fwrite(line.data(),1,line.size(),out)!=line.size()||std::fflush(out)!=0))fail(1,std::string("cannot write ")+out_path);
}
}
if(out&&std::fclose(out)!=0)fail(1,std::string("cannot write ")+out_path);
zodiac_engine->ShutDownAndRelease();
addon_engine->ShutDownAndRelease();
asUnprepareMultithread();
return 0;
}
