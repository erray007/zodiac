# zodiac_release_stub: the start-up path of release executables, linked by zodiac build with the payload object, zodiac_core_release, AngelScript and the static modules.
# stub.cpp (release::start) is portable and tested on every platform. win_main.cpp (wWinMain, Windows only) is a separate archive member: the CRT's
# GUI start-up code references wWinMain, which pulls it in together with its reference to zodiac_payload, so only a real release link needs the payload object.
# On other platforms the library holds release::start only; release builds are Windows-only in this plan.
set(ZODIAC_RELEASE_SOURCES stub.cpp stub.hpp)
if(WIN32)
list(APPEND ZODIAC_RELEASE_SOURCES win_main.cpp)
endif()
add_library(zodiac_release_stub STATIC ${ZODIAC_RELEASE_SOURCES})
target_link_libraries(zodiac_release_stub PUBLIC zodiac_core_release)
zodiac_target_warnings(zodiac_release_stub)
