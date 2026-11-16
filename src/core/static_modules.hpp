// zodiac::linked_static_modules returns the module infos linked statically into this executable.
//
// Every static module library (zodiac_mod_<name>_static, compiled with ZODIAC_MODULE_STATIC) carries one record, the
// const zodiac_module_info* named zodiac_static_module_<name>, in a dedicated linker section (see ZODIAC_DEFINE_MODULE in
// zodiac/plugin.h). Nothing references a record, so the link must force it in: zodiac build passes -u zodiac_static_module_<name>.
// On Windows (COFF) the records live in .zodmod$m; static_modules.cpp defines a begin marker in .zodmod$a and an end marker in
// .zodmod$z, and the linker sorts grouped sections alphabetically, so every record lands between them. The linker may pad between
// contributions with zero bytes, so null entries are skipped.
// On other platforms (ELF) the records live in the section zodiac_modules; walking it is not implemented yet and the result is empty.
// static_modules.cpp is built only into the release core (it defines the markers); this header is declared everywhere.
// Thread-safe; the span stays valid for the life of the process.
#pragma once
#include <zodiac/plugin.h>
#include <span>

namespace zodiac {
std::span<const zodiac_module_info* const> linked_static_modules() noexcept;
}
