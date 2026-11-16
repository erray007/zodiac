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
# libc++ on Linux too, the same standard library as llvm-mingw on Windows: libstdc++ hides std::expected from clang 18 (it needs __cpp_concepts >= 202002L, which clang reports only from 19).
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")
# libc++ and libc++abi are linked statically, so binaries need no libc++ package at run time. -stdlib=libc++ stays in the compile flags only (clang ignores it at link time with -nostdlib++ and would warn). -nostdlib++ drops the driver's own -lc++, because the driver never adds libc++abi and
# a static libc++.a does not pull it in; CMAKE_CXX_STANDARD_LIBRARIES is placed after every object and library, so the archives resolve in order (libc++, then the libc++abi it needs).
set(ZODIAC_LINUX_LINK_FLAGS "-nostdlib++ -static-libgcc")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${ZODIAC_LINUX_LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${ZODIAC_LINUX_LINK_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${ZODIAC_LINUX_LINK_FLAGS}")
set(CMAKE_CXX_STANDARD_LIBRARIES "-Wl,-Bstatic -lc++ -lc++abi -Wl,-Bdynamic -lpthread")
