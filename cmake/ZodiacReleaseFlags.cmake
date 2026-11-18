# Release-configuration compile and link flags of every target: Zodiac's own, its dependencies (AngelScript, its sample add-ons, doctest), the kit's
# lib/release archives, the modules, the tests and the tools (zodiac.exe, zodiacw.exe). Include it after project() and before any target or dependency
# is added. Chosen by measurement (Task 7 report, .superpowers/sdd/2026-11-16-zodiac-core-types/task-7-report.md): the owner wants release builds
# fastest first, then smallest, on every platform (game servers will run on Linux).
# - Every release object (C and C++): -O3 -DNDEBUG, -ffunction-sections -fdata-sections and ThinLTO (-flto=thin), so the archives (the kit's
#   lib/release on Windows) hold LLVM bitcode and every link optimizes across modules; zodiac build links games with the kit's own ld.lld.
# - Every release link (executables and shared libraries, the kit's own and, through the captured link line, every game): -flto=thin (the LTO
#   backend runs at the -O3 the driver passes it as -plugin-opt=O3), --gc-sections, -s and --icf=safe. Safe ICF folds identical functions only when
#   their address is never taken in a way that is significant, as the objects' address-significance tables record (clang writes them by default on COFF and ELF),
#   so comparisons of function pointers, such as AngelScript's, keep their meaning; --icf=all saved nothing more on the hello.
# - Both platforms link with lld: llvm-mingw's on Windows, the system's on Linux (cmake/toolchain-clang-linux.cmake, -fuse-ld=lld), with the LLVM
#   archiver and symbol lister, since GNU ar, ranlib and nm cannot index or read bitcode.
#   The kit's executables are linked like games, so bench_types measures exactly what a game gets.
# Debug builds are left alone (CMake's -O0 -g), so they stay fully debuggable.
# Sets ZODIAC_RELEASE_GAME_LINK_FLAGS: the driver flags the release link line is captured with (cmake/CaptureReleaseLink.cmake); they hold the
# optimization level because the driver turns it into the LTO backend's level.
include_guard(GLOBAL)
set(ZODIAC_RELEASE_OPT -O3)
set(CMAKE_C_FLAGS_RELEASE "${ZODIAC_RELEASE_OPT} -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${ZODIAC_RELEASE_OPT} -DNDEBUG")
set(compile -ffunction-sections -fdata-sections -flto=thin)
set(link -flto=thin -Wl,--gc-sections -s -Wl,--icf=safe)
add_compile_options("$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C,CXX>>:${compile}>")
add_link_options("$<$<CONFIG:Release>:${link}>")
set(ZODIAC_RELEASE_GAME_LINK_FLAGS ${ZODIAC_RELEASE_OPT} ${link})
