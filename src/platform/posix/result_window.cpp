// The build result window on hosts without one yet (see platform/result_window.hpp): no desktop, so zodiac build reports on the console.
#include <platform/result_window.hpp>

namespace zodiac::platform {
bool has_interactive_desktop() noexcept
{
return false;
}

void show_result_window(std::string_view,std::string_view,bool,const std::filesystem::path*)
{
}
}
