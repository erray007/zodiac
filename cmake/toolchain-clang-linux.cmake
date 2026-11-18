# Linux toolchain: clang with libc++, linked by lld, archived by llvm-ar. Each tool can be overridden through the environment (CC, CXX, LD, AR,
# RANLIB, NM, a program name or a path; unset or empty means the default). Defaults: clang and clang++; ld.lld, llvm-ar, llvm-ranlib and llvm-nm
# with the compiler's version suffix first (CXX=clang++-18 finds ld.lld-18, llvm-ar-18, ...), then without one.
# lld and the LLVM binutils are required: release objects are ThinLTO bitcode (cmake/ZodiacReleaseFlags.cmake), which only lld links (GNU ld would
# need the LLVMgold plugin), whose archives need an LLVM symbol index (llvm-ar, llvm-ranlib), and whose symbols only llvm-nm reads
# (tests/check_no_sample_addons.cmake); --icf=safe is an lld option too.
if(DEFINED ENV{CC} AND NOT "$ENV{CC}" STREQUAL "")
set(CMAKE_C_COMPILER "$ENV{CC}")
else()
set(CMAKE_C_COMPILER clang)
endif()
if(DEFINED ENV{CXX} AND NOT "$ENV{CXX}" STREQUAL "")
set(CMAKE_CXX_COMPILER "$ENV{CXX}")
else()
set(CMAKE_CXX_COMPILER clang++)
endif()
cmake_path(GET CMAKE_CXX_COMPILER FILENAME zodiac_compiler_name)
set(zodiac_tool_suffix "")
if(zodiac_compiler_name MATCHES "(-[0-9]+)$")
set(zodiac_tool_suffix "${CMAKE_MATCH_1}")
endif()
# zodiac_linux_tool(<variable> <environment variable> <name>): <variable> becomes the tool the environment names, else <name><suffix> or <name> on PATH.
function(zodiac_linux_tool variable environment name)
if(DEFINED ENV{${environment}} AND NOT "$ENV{${environment}}" STREQUAL "")
set(wanted "$ENV{${environment}}")
else()
set(wanted "${name}${zodiac_tool_suffix}" "${name}")
endif()
find_program(ZODIAC_${environment}_PROGRAM NAMES ${wanted} NO_CACHE)
if(NOT ZODIAC_${environment}_PROGRAM)
list(JOIN wanted " or " shown)
message(FATAL_ERROR "Zodiac's Linux toolchain needs ${shown} (set ${environment} to override): install lld and llvm, for example the lld-18 and llvm-18 packages.")
endif()
set(${variable} "${ZODIAC_${environment}_PROGRAM}" PARENT_SCOPE)
endfunction()
zodiac_linux_tool(zodiac_lld LD ld.lld)
zodiac_linux_tool(CMAKE_AR AR llvm-ar)
zodiac_linux_tool(CMAKE_RANLIB RANLIB llvm-ranlib)
zodiac_linux_tool(CMAKE_NM NM llvm-nm)
set(CMAKE_AR "${CMAKE_AR}" CACHE FILEPATH "Archiver (llvm-ar: release archives hold ThinLTO bitcode)" FORCE)
set(CMAKE_RANLIB "${CMAKE_RANLIB}" CACHE FILEPATH "Archive indexer (llvm-ranlib)" FORCE)
set(CMAKE_NM "${CMAKE_NM}" CACHE FILEPATH "Symbol lister (llvm-nm reads bitcode)" FORCE)
# libc++ on Linux too, the same standard library as llvm-mingw on Windows: libstdc++ hides std::expected from clang 18 (it needs __cpp_concepts >= 202002L, which clang reports only from 19).
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")
# libc++ and libc++abi are linked statically, so binaries need no libc++ package at run time. -stdlib=libc++ stays in the compile flags only (clang ignores it at link time with -nostdlib++ and would warn). -nostdlib++ drops the driver's own -lc++, because the driver never adds libc++abi and
# a static libc++.a does not pull it in; CMAKE_CXX_STANDARD_LIBRARIES is placed after every object and library, so the archives resolve in order (libc++, then the libc++abi it needs).
# -fuse-ld=lld makes the driver treat the linker as lld (it then passes ThinLTO options to it instead of looking for the gold plugin); --ld-path names
# the program found above, since a versioned package installs ld.lld-18 rather than ld.lld.
set(ZODIAC_LINUX_LINK_FLAGS "-fuse-ld=lld --ld-path=${zodiac_lld} -nostdlib++ -static-libgcc")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${ZODIAC_LINUX_LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${ZODIAC_LINUX_LINK_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${ZODIAC_LINUX_LINK_FLAGS}")
set(CMAKE_CXX_STANDARD_LIBRARIES "-Wl,-Bstatic -lc++ -lc++abi -Wl,-Bdynamic -lpthread")
