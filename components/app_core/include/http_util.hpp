#pragma once
#include <map>
#include <string>
#include <string_view>

namespace alco::web {

// "a%20b+c" -> "a b c". Invalid escapes (e.g. "%zz", a trailing "%4") are kept literally.
std::string urlDecode(std::string_view in);

// application/x-www-form-urlencoded (also a URL query). Later duplicates win; empty keys are skipped.
std::map<std::string, std::string> parseForm(std::string_view in);

// "/a/b?x=1" -> path "/a/b", query "x=1".
void splitTarget(std::string_view target, std::string& path, std::string& query);

std::string htmlEscape(std::string_view in);
std::string jsonEscape(std::string_view in);          // content of a JSON string, without the quotes

// Finite numbers as fixed-point text, anything else as "null" (valid JSON, unlike "nan"/"inf").
std::string jsonNumber(float v, int decimals = 2);

// Strict: the whole text must be one finite number (surrounding spaces are allowed).
bool parseFloat(std::string_view s, float& out);

// http:// or https:// followed by a host, no spaces/control characters/quotes, max 200 chars.
bool isValidHttpUrl(std::string_view url);

// Static string, safe to pass to httpd_resp_set_status(). Unknown codes map to 500.
const char* statusLine(int code);

} // namespace alco::web
