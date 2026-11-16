#include <core/kit_layout.hpp>

namespace zodiac {
KitLayout kit_layout(const std::filesystem::path& executable)
{
KitLayout kit;
kit.root=executable.parent_path().parent_path();
kit.bin=kit.root/"bin";
kit.modules=kit.root/"modules";
kit.static_modules=kit.root/"modules"/"static";
kit.release_lib=kit.root/"lib"/"release";
return kit;
}
}
