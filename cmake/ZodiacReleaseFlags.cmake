# Release-configuration compile and link flags of every target: Zodiac's own, its dependencies (AngelScript, its sample add-ons, doctest), the kit's
# lib/release archives, the modules, the tests and the tools (zodiac.exe, zodiacw.exe). Include it after project() and before any target or dependency
# is added. Chosen by measurement (Task 7 report, .superpowers/sdd/2026-11-16-zodiac-core-types/task-7-report.md): the owner wants release builds
# fastest first, then smallest.
# - Every release object (C and C++): -O3 -DNDEBUG, -ffunction-sections -fdata-sections; on Windows also ThinLTO (-flto=thin), so the kit's
#   lib/release archives hold LLVM bitcode and games get cross-module optimization when zodiac build links them with the kit's ld.lld.
# - Every release link (executables and shared libraries, the kit's own and, through the captured link line, every game): --gc-sections and -s;
#   on Windows also -flto=thin (the LTO backend runs at -O3) and --icf=safe (lld folds only functions whose address is never taken, using the
#   objects' address-significance tables, so AngelScript's function-pointer comparisons stay valid; --icf=all saved nothing more).
#   The kit's executables are linked like games, so bench_types measures exactly what a game gets.
# - Linux (CI only, no kit) keeps the portable subset: its default linker (GNU ld) has no LTO plugin and no ICF.
# Debug builds are left alone (CMake's -O0 -g), so they stay fully debuggable.
# Sets ZODIAC_RELEASE_GAME_LINK_FLAGS: the driver flags the release link line is captured with (cmake/CaptureReleaseLink.cmake); they hold the
# optimization level because the driver turns it into the LTO backend's level (-plugin-opt=O3).
include_guard(GLOBAL)
set(ZODIAC_RELEASE_OPT -O3)
set(CMAKE_C_FLAGS_RELEASE "${ZODIAC_RELEASE_OPT} -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${ZODIAC_RELEASE_OPT} -DNDEBUG")
set(compile -ffunction-sections -fdata-sections)
set(link -Wl,--gc-sections -s)
if(WIN32)
list(APPEND compile -flto=thin)
list(APPEND link -flto=thin -Wl,--icf=safe)
endif()
add_compile_options("$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C,CXX>>:${compile}>")
add_link_options("$<$<CONFIG:Release>:${link}>")
set(ZODIAC_RELEASE_GAME_LINK_FLAGS ${ZODIAC_RELEASE_OPT} ${link})
