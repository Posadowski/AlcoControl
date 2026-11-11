#include "http_util.hpp"
#include "http_types.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace alco::web {

namespace {
int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace

std::string urlDecode(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < in.size() && hexVal(in[i + 1]) >= 0 && hexVal(in[i + 2]) >= 0) {
            out += static_cast<char>((hexVal(in[i + 1]) << 4) | hexVal(in[i + 2]));
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

std::map<std::string, std::string> parseForm(std::string_view in) {
    std::map<std::string, std::string> out;
    size_t pos = 0;
    while (pos <= in.size()) {
        size_t amp = in.find('&', pos);
        if (amp == std::string_view::npos) amp = in.size();
        const std::string_view pair = in.substr(pos, amp - pos);
        if (!pair.empty()) {
            const size_t eq = pair.find('=');
            const std::string key = urlDecode(pair.substr(0, eq));
            const std::string val = (eq == std::string_view::npos) ? std::string() : urlDecode(pair.substr(eq + 1));
            if (!key.empty()) out[key] = val;
        }
        pos = amp + 1;
    }
    return out;
}

void splitTarget(std::string_view target, std::string& path, std::string& query) {
    const size_t q = target.find('?');
    if (q == std::string_view::npos) {
        path.assign(target);
        query.clear();
    } else {
        path.assign(target.substr(0, q));
        query.assign(target.substr(q + 1));
    }
}

std::string htmlEscape(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;
        }
    }
    return out;
}

std::string jsonEscape(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string jsonNumber(float v, int decimals) {
    if (!std::isfinite(v)) return "null";
    float scale = 1.0f;
    for (int i = 0; i < decimals; ++i) scale *= 10.0f;
    if (std::fabs(v) < 0.5f / scale) v = 0.0f;               // avoid "-0.00"
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(v));
    return buf;
}

bool parseFloat(std::string_view s, float& out) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    if (s.empty() || s.size() > 32) return false;
    const std::string tmp(s);
    char* end = nullptr;
    const float v = std::strtof(tmp.c_str(), &end);
    if (end != tmp.c_str() + tmp.size() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool isValidHttpUrl(std::string_view url) {
    if (url.size() > 200) return false;
    size_t prefix = 0;
    if (url.substr(0, 7) == "http://") prefix = 7;
    else if (url.substr(0, 8) == "https://") prefix = 8;
    else return false;
    if (url.size() <= prefix) return false;                  // nothing after the scheme
    for (char c : url) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u == 0x7f || c == '"' || c == '<' || c == '>' || c == '\\' || c == '\'') return false;
    }
    return true;
}

const char* statusLine(int code) {
    switch (code) {
        case 200: return "200 OK";
        case 202: return "202 Accepted";
        case 400: return "400 Bad Request";
        case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed";
        case 408: return "408 Request Timeout";
        case 409: return "409 Conflict";
        case 413: return "413 Payload Too Large";
        case 500: return "500 Internal Server Error";
        case 503: return "503 Service Unavailable";
        default:  return "500 Internal Server Error";
    }
}

// ---- Response helpers ----
Response Response::html(std::string body, int status) {
    Response r;
    r.status = status;
    r.body = std::move(body);
    return r;
}

Response Response::json(std::string body, int status) {
    Response r;
    r.status = status;
    r.contentType = "application/json";
    r.body = std::move(body);
    return r;
}

Response Response::error(int status, const std::string& message) {
    return json("{\"error\":\"" + jsonEscape(message) + "\"}", status);
}

} // namespace alco::web
