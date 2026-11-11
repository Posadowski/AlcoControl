#pragma once
#include <string>

namespace alco::web {

// Static pages: no server-side data is placed into the HTML. The scripts fetch JSON from the
// /api endpoints and use textContent only, so an SSID or a sensor id can never inject markup.
std::string dashboardPage();
std::string wifiPage();
std::string otaPage();

} // namespace alco::web
