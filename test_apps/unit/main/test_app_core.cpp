#include <cmath>
#include <string>
#include "unity.h"
#include "api.hpp"
#include "http_util.hpp"
#include "router.hpp"
#include "web_ui.hpp"
#include <set>

using namespace alco::web;

namespace {

// Builds a Request the same way the HTTP adapter does.
Request makeRequest(Method m, const std::string& target, const std::string& body = "") {
    Request r;
    r.method = m;
    splitTarget(target, r.path, r.query);
    r.body = body;
    r.params = parseForm(r.query);
    for (auto& kv : parseForm(r.body)) r.params[kv.first] = kv.second;
    return r;
}

struct FakeHeater : IHeaterControl {
    int starts = 0, stops = 0, setCalls = 0;
    bool isRunning = false;
    bool accept = true;
    float pw = 0.0f, tgt = 80.0f, lastSet = NAN;
    std::string fault = "none";

    void start() override { ++starts; isRunning = true; }
    void stop() override  { ++stops;  isRunning = false; }
    bool running() const override { return isRunning; }
    float power() const override { return pw; }
    float target() const override { return tgt; }
    bool setTarget(float v) override {
        ++setCalls;
        lastSet = v;
        if (!accept) return false;
        tgt = v;
        return true;
    }
    std::string faultText() const override { return fault; }
};

} // namespace

// ======================= util =======================

TEST_CASE("Util: urlDecode handles + and %XX", "[app_core][util]")
{
    TEST_ASSERT_EQUAL_STRING("a b c", urlDecode("a%20b+c").c_str());
    TEST_ASSERT_EQUAL_STRING("AB", urlDecode("%41%42").c_str());
    TEST_ASSERT_EQUAL_STRING("a=b&c", urlDecode("a%3Db%26c").c_str());
    TEST_ASSERT_EQUAL_STRING("\xC5\x82", urlDecode("%C5%82").c_str());     // UTF-8 bytes of a Polish letter
}

TEST_CASE("Util: urlDecode keeps invalid escapes literally", "[app_core][util]")
{
    TEST_ASSERT_EQUAL_STRING("%zz", urlDecode("%zz").c_str());
    TEST_ASSERT_EQUAL_STRING("abc%4", urlDecode("abc%4").c_str());
    TEST_ASSERT_EQUAL_STRING("100%", urlDecode("100%").c_str());
    TEST_ASSERT_EQUAL_STRING("", urlDecode("").c_str());
}

TEST_CASE("Util: parseForm decodes keys and values", "[app_core][util]")
{
    auto m = parseForm("ssid=My%20Net&password=p%26ss%3Dword");
    TEST_ASSERT_EQUAL_INT(2, (int)m.size());
    TEST_ASSERT_EQUAL_STRING("My Net", m["ssid"].c_str());
    TEST_ASSERT_EQUAL_STRING("p&ss=word", m["password"].c_str());
}

TEST_CASE("Util: parseForm edge cases", "[app_core][util]")
{
    TEST_ASSERT_EQUAL_INT(0, (int)parseForm("").size());
    TEST_ASSERT_EQUAL_INT(0, (int)parseForm("&&").size());
    auto dup = parseForm("a=1&a=2");
    TEST_ASSERT_EQUAL_STRING("2", dup["a"].c_str());                       // the last one wins
    auto flag = parseForm("flag&x=");
    TEST_ASSERT_EQUAL_INT(2, (int)flag.size());
    TEST_ASSERT_EQUAL_STRING("", flag["flag"].c_str());
    TEST_ASSERT_EQUAL_INT(0, (int)parseForm("=orphan").size());            // empty key is skipped
    auto eq = parseForm("url=http://h/p?a=b");
    TEST_ASSERT_EQUAL_STRING("http://h/p?a=b", eq["url"].c_str());         // only the first '=' splits
}

TEST_CASE("Util: splitTarget separates path and query", "[app_core][util]")
{
    std::string p, q;
    splitTarget("/api/x?a=1&b=2", p, q);
    TEST_ASSERT_EQUAL_STRING("/api/x", p.c_str());
    TEST_ASSERT_EQUAL_STRING("a=1&b=2", q.c_str());
    splitTarget("/x", p, q);
    TEST_ASSERT_EQUAL_STRING("/x", p.c_str());
    TEST_ASSERT_EQUAL_STRING("", q.c_str());
    splitTarget("/x?", p, q);
    TEST_ASSERT_EQUAL_STRING("/x", p.c_str());
    TEST_ASSERT_EQUAL_STRING("", q.c_str());
    splitTarget("/", p, q);
    TEST_ASSERT_EQUAL_STRING("/", p.c_str());
}

TEST_CASE("Util: htmlEscape neutralises markup", "[app_core][util]")
{
    TEST_ASSERT_EQUAL_STRING("&lt;script&gt;alert(&quot;x&quot;)&lt;/script&gt;&amp;&#39;",
                             htmlEscape("<script>alert(\"x\")</script>&'").c_str());
    TEST_ASSERT_EQUAL_STRING("plain", htmlEscape("plain").c_str());
}

TEST_CASE("Util: jsonEscape produces a valid JSON string body", "[app_core][util]")
{
    TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c", jsonEscape("a\"b\\c").c_str());
    TEST_ASSERT_EQUAL_STRING("l1\\nl2\\tx", jsonEscape("l1\nl2\tx").c_str());
    TEST_ASSERT_EQUAL_STRING("\\u0001", jsonEscape(std::string(1, '\x01')).c_str());
    TEST_ASSERT_EQUAL_STRING("plain \xC5\x82", jsonEscape("plain \xC5\x82").c_str());   // UTF-8 passes through
}

TEST_CASE("Util: jsonNumber never emits invalid JSON", "[app_core][util]")
{
    TEST_ASSERT_EQUAL_STRING("80.00", jsonNumber(80.0f).c_str());
    TEST_ASSERT_EQUAL_STRING("79.94", jsonNumber(79.9375f).c_str());
    TEST_ASSERT_EQUAL_STRING("-5.50", jsonNumber(-5.5f).c_str());
    TEST_ASSERT_EQUAL_STRING("12", jsonNumber(12.34f, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("null", jsonNumber(NAN).c_str());
    TEST_ASSERT_EQUAL_STRING("null", jsonNumber(INFINITY).c_str());
    TEST_ASSERT_EQUAL_STRING("null", jsonNumber(-INFINITY).c_str());
    TEST_ASSERT_EQUAL_STRING("0.00", jsonNumber(-0.001f).c_str());                      // not "-0.00"
}

TEST_CASE("Util: parseFloat is strict", "[app_core][util]")
{
    float v = -1.0f;
    TEST_ASSERT_TRUE(parseFloat("80", v));
    TEST_ASSERT_EQUAL_FLOAT(80.0f, v);
    TEST_ASSERT_TRUE(parseFloat("  72.5 ", v));
    TEST_ASSERT_EQUAL_FLOAT(72.5f, v);
    TEST_ASSERT_TRUE(parseFloat("-3", v));
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, v);

    v = 123.0f;
    TEST_ASSERT_FALSE(parseFloat("", v));
    TEST_ASSERT_FALSE(parseFloat("abc", v));
    TEST_ASSERT_FALSE(parseFloat("80x", v));
    TEST_ASSERT_FALSE(parseFloat("8 0", v));
    TEST_ASSERT_FALSE(parseFloat("nan", v));
    TEST_ASSERT_FALSE(parseFloat("inf", v));
    TEST_ASSERT_FALSE(parseFloat("1e999", v));
    TEST_ASSERT_EQUAL_FLOAT(123.0f, v);                                                 // untouched on failure
}

TEST_CASE("Util: isValidHttpUrl", "[app_core][util]")
{
    TEST_ASSERT_TRUE(isValidHttpUrl("http://192.168.1.10/fw"));
    TEST_ASSERT_TRUE(isValidHttpUrl("https://example.com:8443/path?x=1"));
    TEST_ASSERT_FALSE(isValidHttpUrl(""));
    TEST_ASSERT_FALSE(isValidHttpUrl("http://"));
    TEST_ASSERT_FALSE(isValidHttpUrl("https://"));
    TEST_ASSERT_FALSE(isValidHttpUrl("ftp://example.com/fw"));
    TEST_ASSERT_FALSE(isValidHttpUrl("javascript:alert(1)"));
    TEST_ASSERT_FALSE(isValidHttpUrl("http://exa mple.com"));
    TEST_ASSERT_FALSE(isValidHttpUrl("http://a.com/\"onload=x"));
    TEST_ASSERT_FALSE(isValidHttpUrl("http://a.com/<b>"));
    TEST_ASSERT_FALSE(isValidHttpUrl(std::string("http://") + std::string(200, 'a')));  // 207 chars
}

TEST_CASE("Util: statusLine is a stable static string", "[app_core][util]")
{
    TEST_ASSERT_EQUAL_STRING("200 OK", statusLine(200));
    TEST_ASSERT_EQUAL_STRING("404 Not Found", statusLine(404));
    TEST_ASSERT_EQUAL_STRING("405 Method Not Allowed", statusLine(405));
    TEST_ASSERT_EQUAL_STRING("413 Payload Too Large", statusLine(413));
    TEST_ASSERT_EQUAL_STRING("500 Internal Server Error", statusLine(599));            // unknown -> 500
    TEST_ASSERT_TRUE(statusLine(404) == statusLine(404));                               // same pointer
}

// ======================= router =======================

TEST_CASE("Router: exact match by method and path", "[app_core][router]")
{
    Router r;
    r.add(Method::Get, "/a", [](const Request&) { return Response::html("A"); });
    r.add(Method::Post, "/a", [](const Request&) { return Response::html("A-post"); });
    r.add(Method::Get, "/b", [](const Request&) { return Response::html("B"); });

    TEST_ASSERT_EQUAL_STRING("A", r.dispatch(makeRequest(Method::Get, "/a")).body.c_str());
    TEST_ASSERT_EQUAL_STRING("A-post", r.dispatch(makeRequest(Method::Post, "/a")).body.c_str());
    TEST_ASSERT_EQUAL_STRING("B", r.dispatch(makeRequest(Method::Get, "/b")).body.c_str());
    TEST_ASSERT_EQUAL_INT(3, (int)r.size());
}

TEST_CASE("Router: 404 for unknown path, 405 for a known path with another method", "[app_core][router]")
{
    Router r;
    r.add(Method::Post, "/api/x", [](const Request&) { return Response::html("ok"); });

    Response nf = r.dispatch(makeRequest(Method::Get, "/nope"));
    TEST_ASSERT_EQUAL_INT(404, nf.status);
    TEST_ASSERT_EQUAL_STRING("application/json", nf.contentType.c_str());

    Response na = r.dispatch(makeRequest(Method::Get, "/api/x"));
    TEST_ASSERT_EQUAL_INT(405, na.status);
    TEST_ASSERT_EQUAL_INT(200, r.dispatch(makeRequest(Method::Post, "/api/x")).status);
}

TEST_CASE("Router: the query string does not break matching and reaches the handler",
          "[app_core][router]")
{
    Router r;
    std::string seen;
    r.add(Method::Get, "/api/x", [&seen](const Request& req) {
        const std::string* a = req.param("a");
        seen = a ? *a : "(none)";
        return Response::html("ok");
    });
    TEST_ASSERT_EQUAL_INT(200, r.dispatch(makeRequest(Method::Get, "/api/x?a=hello%20world")).status);
    TEST_ASSERT_EQUAL_STRING("hello world", seen.c_str());
}

TEST_CASE("Router: a trailing slash is ignored, the root stays the root", "[app_core][router]")
{
    Router r;
    r.add(Method::Get, "/ota", [](const Request&) { return Response::html("ota"); });
    r.add(Method::Get, "/", [](const Request&) { return Response::html("root"); });
    TEST_ASSERT_EQUAL_STRING("ota", r.dispatch(makeRequest(Method::Get, "/ota/")).body.c_str());
    TEST_ASSERT_EQUAL_STRING("root", r.dispatch(makeRequest(Method::Get, "/")).body.c_str());
    TEST_ASSERT_EQUAL_INT(404, r.dispatch(makeRequest(Method::Get, "/ota/extra")).status);
}

TEST_CASE("Router: adding the same route again replaces the handler", "[app_core][router]")
{
    Router r;
    r.add(Method::Get, "/x", [](const Request&) { return Response::html("old"); });
    r.add(Method::Get, "/x", [](const Request&) { return Response::html("new"); });
    TEST_ASSERT_EQUAL_INT(1, (int)r.size());
    TEST_ASSERT_EQUAL_STRING("new", r.dispatch(makeRequest(Method::Get, "/x")).body.c_str());
}

TEST_CASE("Router: more routes than the old 8-handler httpd limit", "[app_core][router]")
{
    Router r;
    for (int i = 0; i < 40; ++i) {
        const std::string path = "/r" + std::to_string(i);
        r.add(Method::Get, path, [i](const Request&) { return Response::html(std::to_string(i)); });
    }
    TEST_ASSERT_EQUAL_INT(40, (int)r.size());
    TEST_ASSERT_EQUAL_STRING("39", r.dispatch(makeRequest(Method::Get, "/r39")).body.c_str());
}

TEST_CASE("Router: Response helpers set type and escape messages", "[app_core][router]")
{
    Response j = Response::json("{}");
    TEST_ASSERT_EQUAL_STRING("application/json", j.contentType.c_str());
    Response h = Response::html("x");
    TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8", h.contentType.c_str());
    Response e = Response::error(400, "bad \"input\"");
    TEST_ASSERT_EQUAL_INT(400, e.status);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"bad \\\"input\\\"\"}", e.body.c_str());
}

// ======================= telemetry =======================

TEST_CASE("Telemetry: the first sensor to report is the main one", "[app_core][telemetry]")
{
    TelemetryStore t;
    TEST_ASSERT_TRUE(t.update("A", 20.0f, 100));
    TEST_ASSERT_FALSE(t.update("B", 30.0f, 110));
    TEST_ASSERT_TRUE(t.update("A", 21.0f, 200));

    SensorReading m{};
    TEST_ASSERT_TRUE(t.mainReading(m));
    TEST_ASSERT_EQUAL_STRING("A", m.id.c_str());
    TEST_ASSERT_EQUAL_FLOAT(21.0f, m.value);
    TEST_ASSERT_TRUE(m.tsMs == 200);
}

TEST_CASE("Telemetry: updates in place, keeps insertion order, snapshot is a copy", "[app_core][telemetry]")
{
    TelemetryStore t;
    t.update("A", 1.0f, 1);
    t.update("B", 2.0f, 2);
    t.update("A", 3.0f, 3);

    auto snap = t.snapshot();
    TEST_ASSERT_EQUAL_INT(2, (int)snap.size());
    TEST_ASSERT_EQUAL_STRING("A", snap[0].id.c_str());
    TEST_ASSERT_EQUAL_FLOAT(3.0f, snap[0].value);
    TEST_ASSERT_EQUAL_STRING("B", snap[1].id.c_str());

    t.update("B", 9.0f, 9);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, snap[1].value);                  // the copy did not change
}

TEST_CASE("Telemetry: no readings means no main reading", "[app_core][telemetry]")
{
    TelemetryStore t;
    SensorReading m{};
    TEST_ASSERT_FALSE(t.mainReading(m));
    TEST_ASSERT_EQUAL_INT(0, (int)t.snapshot().size());
}

// ======================= JSON builders and heater routes =======================

TEST_CASE("Api: sensorsJson", "[app_core][api]")
{
    TEST_ASSERT_EQUAL_STRING("{\"sensors\":[]}", sensorsJson({}).c_str());
    std::vector<SensorReading> v = {{"28-AB", 20.5f, 1000}, {"we\"ird", NAN, 2000}};
    TEST_ASSERT_EQUAL_STRING(
        "{\"sensors\":[{\"id\":\"28-AB\",\"value\":20.50,\"ts\":1000},"
        "{\"id\":\"we\\\"ird\",\"value\":null,\"ts\":2000}]}",
        sensorsJson(v).c_str());
}

TEST_CASE("Api: heater status JSON", "[app_core][api]")
{
    FakeHeater h;
    h.isRunning = true;
    h.pw = 30.0f;
    h.tgt = 80.0f;
    TelemetryStore t;
    t.update("A", 79.9375f, 5);
    TEST_ASSERT_EQUAL_STRING(
        "{\"running\":true,\"temperature\":79.94,\"target\":80.00,\"power\":30.00,\"fault\":\"none\"}",
        heaterStatusJson(h, t).c_str());
}

TEST_CASE("Api: heater status without a reading reports null and escapes the fault text", "[app_core][api]")
{
    FakeHeater h;
    h.fault = "bad \"sensor\"";
    TelemetryStore t;
    TEST_ASSERT_EQUAL_STRING(
        "{\"running\":false,\"temperature\":null,\"target\":80.00,\"power\":0.00,"
        "\"fault\":\"bad \\\"sensor\\\"\"}",
        heaterStatusJson(h, t).c_str());
}

TEST_CASE("Api: routes serve status and sensors", "[app_core][api]")
{
    FakeHeater h;
    TelemetryStore t;
    t.update("A", 25.0f, 1);
    Router r;
    registerHeaterRoutes(r, h, t);

    Response s = r.dispatch(makeRequest(Method::Get, "/api/heater/status"));
    TEST_ASSERT_EQUAL_INT(200, s.status);
    TEST_ASSERT_EQUAL_STRING("application/json", s.contentType.c_str());
    Response q = r.dispatch(makeRequest(Method::Get, "/api/sensors"));
    TEST_ASSERT_EQUAL_STRING("{\"sensors\":[{\"id\":\"A\",\"value\":25.00,\"ts\":1}]}", q.body.c_str());
}

TEST_CASE("Api: start and stop need POST", "[app_core][api]")
{
    FakeHeater h;
    TelemetryStore t;
    Router r;
    registerHeaterRoutes(r, h, t);

    TEST_ASSERT_EQUAL_INT(405, r.dispatch(makeRequest(Method::Get, "/api/heater/start")).status);
    TEST_ASSERT_EQUAL_INT(405, r.dispatch(makeRequest(Method::Get, "/api/heater/stop")).status);
    TEST_ASSERT_EQUAL_INT(0, h.starts);                         // a plain GET (link, prefetch) must not start it
    TEST_ASSERT_EQUAL_INT(0, h.stops);

    Response a = r.dispatch(makeRequest(Method::Post, "/api/heater/start"));
    TEST_ASSERT_EQUAL_INT(200, a.status);
    TEST_ASSERT_EQUAL_STRING("{\"running\":true}", a.body.c_str());
    TEST_ASSERT_EQUAL_INT(1, h.starts);

    Response b = r.dispatch(makeRequest(Method::Post, "/api/heater/stop"));
    TEST_ASSERT_EQUAL_STRING("{\"running\":false}", b.body.c_str());
    TEST_ASSERT_EQUAL_INT(1, h.stops);
}

TEST_CASE("Api: setting the target", "[app_core][api]")
{
    FakeHeater h;
    TelemetryStore t;
    Router r;
    registerHeaterRoutes(r, h, t);

    Response ok = r.dispatch(makeRequest(Method::Post, "/api/heater/target", "value=72.5"));
    TEST_ASSERT_EQUAL_INT(200, ok.status);
    TEST_ASSERT_EQUAL_STRING("{\"target\":72.50}", ok.body.c_str());
    TEST_ASSERT_EQUAL_FLOAT(72.5f, h.lastSet);

    // the value may also come in the query string
    TEST_ASSERT_EQUAL_INT(200, r.dispatch(makeRequest(Method::Post, "/api/heater/target?value=65")).status);
    TEST_ASSERT_EQUAL_FLOAT(65.0f, h.lastSet);
}

TEST_CASE("Api: invalid targets are rejected before reaching the heater", "[app_core][api][error]")
{
    FakeHeater h;
    TelemetryStore t;
    Router r;
    registerHeaterRoutes(r, h, t);

    TEST_ASSERT_EQUAL_INT(400, r.dispatch(makeRequest(Method::Post, "/api/heater/target", "")).status);
    TEST_ASSERT_EQUAL_INT(400, r.dispatch(makeRequest(Method::Post, "/api/heater/target", "value=abc")).status);
    TEST_ASSERT_EQUAL_INT(400, r.dispatch(makeRequest(Method::Post, "/api/heater/target", "value=nan")).status);
    TEST_ASSERT_EQUAL_INT(400, r.dispatch(makeRequest(Method::Post, "/api/heater/target", "value=80;rm")).status);
    TEST_ASSERT_EQUAL_INT(0, h.setCalls);

    h.accept = false;                                          // heater says: out of range
    Response rej = r.dispatch(makeRequest(Method::Post, "/api/heater/target", "value=500"));
    TEST_ASSERT_EQUAL_INT(400, rej.status);
    TEST_ASSERT_EQUAL_INT(1, h.setCalls);
    TEST_ASSERT_EQUAL_FLOAT(80.0f, h.tgt);                     // unchanged
}

// ======================= static pages =======================

namespace {

// every "/api/..." path that appears in a page
std::set<std::string> apiPathsIn(const std::string& html) {
    std::set<std::string> out;
    size_t pos = 0;
    while ((pos = html.find("/api/", pos)) != std::string::npos) {
        size_t end = pos + 5;
        while (end < html.size() && ((html[end] >= 'a' && html[end] <= 'z') || html[end] == '/' || html[end] == '_')) ++end;
        out.insert(html.substr(pos, end - pos));
        pos = end;
    }
    return out;
}

size_t countOf(const std::string& hay, const std::string& needle) {
    size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) { ++n; pos += needle.size(); }
    return n;
}
} // namespace

TEST_CASE("Ui: every page is a complete document with the navigation", "[app_core][ui]")
{
    const std::string pages[] = {dashboardPage(), wifiPage(), otaPage()};
    for (const auto& p : pages) {
        TEST_ASSERT_TRUE(p.rfind("<!doctype html>", 0) == 0);
        TEST_ASSERT_TRUE(p.find("</html>") != std::string::npos);
        TEST_ASSERT_TRUE(p.find("href=\"/\"") != std::string::npos);
        TEST_ASSERT_TRUE(p.find("href=\"/wifi\"") != std::string::npos);
        TEST_ASSERT_TRUE(p.find("href=\"/ota\"") != std::string::npos);
        TEST_ASSERT_TRUE(p.find("charset=\"utf-8\"") != std::string::npos);
        TEST_ASSERT_EQUAL_INT((int)countOf(p, "<script>"), (int)countOf(p, "</script>"));
    }
}

TEST_CASE("Ui: untrusted text is never put into markup", "[app_core][ui][security]")
{
    // SSIDs, sensor ids and update URLs come from outside; the pages must use textContent only.
    const std::string pages[] = {dashboardPage(), wifiPage(), otaPage()};
    for (const auto& p : pages) {
        TEST_ASSERT_TRUE(p.find("innerHTML") == std::string::npos);
        TEST_ASSERT_TRUE(p.find("outerHTML") == std::string::npos);
        TEST_ASSERT_TRUE(p.find("insertAdjacentHTML") == std::string::npos);
        TEST_ASSERT_TRUE(p.find("document.write") == std::string::npos);
        TEST_ASSERT_TRUE(p.find("eval(") == std::string::npos);
    }
}

TEST_CASE("Ui: the pages only call endpoints that exist", "[app_core][ui][contract]")
{
    FakeHeater h;
    TelemetryStore t;
    Router r;
    registerHeaterRoutes(r, h, t);

    // routes registered in main.cpp (registerNetworkRoutes); keep this list in sync with it
    const std::set<std::string> network = {"/api/wifi/scan", "/api/wifi/connect", "/api/ota/server",
                                           "/api/ota/check", "/api/ota/update", "/api/ota/progress"};

    const std::string pages[] = {dashboardPage(), wifiPage(), otaPage()};
    int checked = 0;
    for (const auto& p : pages) {
        for (const auto& path : apiPathsIn(p)) {
            bool known = network.count(path) > 0;
            if (!known) {
                const int g = r.dispatch(makeRequest(Method::Get, path)).status;
                const int po = r.dispatch(makeRequest(Method::Post, path)).status;
                known = (g != 404 || po != 404);
            }
            if (!known) { printf("  unknown endpoint in a page: %s\n", path.c_str()); }
            TEST_ASSERT_TRUE(known);
            ++checked;
        }
    }
    TEST_ASSERT_TRUE(checked >= 10);
}
