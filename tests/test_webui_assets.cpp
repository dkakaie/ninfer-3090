// The embedded Web UI assets: make_webui_assets.cmake folds every file under src/serve/webui into
// the binary as hex constants, decoded on first access. These checks prove the round trip -- every
// asset is present with its real content signature (a dropped or truncated file would fail), the
// lookup answers exactly the paths the /webui route asks for, and the MIME table covers every kind.
// Header-only: no socket, no model, no GPU.

#include "webui_assets.h" // generated into the build tree by make_webui_assets.cmake

#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace ninfer::serve::webui;

int failures = 0;

void check(bool condition, std::string_view message) {
    if (condition) { return; }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

// Fetches an asset the way the /webui route does (leading slash, exact name) and checks its body
// carries a signature only the real file has.
void check_asset(const char* name, const char* signature, long long minimum_bytes) {
    std::string data;
    if (!find_asset(name, &data)) {
        check(false, "asset missing from the binary: " + std::string(name));
        return;
    }
    if (std::string(data).find(signature) == std::string::npos) {
        check(false, ("asset lost its content: " + std::string(name)).c_str());
        return;
    }
    if (static_cast<long long>(data.size()) < minimum_bytes) {
        check(false, ("asset is truncated: " + std::string(name)).c_str());
    }
}

} // namespace

int main() {
    // index.html: the document shell the route serves for /webui/.
    check_asset("/index.html", "<!doctype html>", 4000);
    check_asset("/index.html", "keyBanner", 4000);
    // app.js: the client the page loads; it must still be the embedded Studio client.
    check_asset("/app.js", "EventSource", 20000);
    check_asset("/app.js", "ninfer.key", 20000);
    // Styles and icons.
    check_asset("/app.css", "#keyBanner", 2000);
    check_asset("/tokens.css", "--ui-", 1000);
    check_asset("/icons.svg", "<svg", 500);
    // Fonts: binary payloads -- the hex round trip is exactly what this protects.
    check_asset("/fonts/OFL.txt", "License", 1000);
    check_asset("/fonts/outfit-latin-wght.woff2", "wOF2", 10000);
    check_asset("/fonts/outfit-latin-ext-wght.woff2", "wOF2", 10000);

    // The lookup is an exact match over the embedded table: unknown names and traversal-shaped
    // names 404 instead of resolving.
    std::string data;
    check(!find_asset("/nope.txt", &data), "an unknown asset resolved");
    check(!find_asset("/", &data), "the bare root path resolved to an asset");
    check(!find_asset("/..%2Findex.html", &data), "an encoded traversal resolved");
    check(!find_asset("/webui/index.html", &data), "a prefixed path resolved");

    // The MIME table answers the content type for every asset kind the route serves.
    check(std::string(mime_for("/index.html")) == "text/html; charset=utf-8", "html MIME wrong");
    check(std::string(mime_for("/app.js")) == "text/javascript; charset=utf-8", "js MIME wrong");
    check(std::string(mime_for("/app.css")) == "text/css; charset=utf-8", "css MIME wrong");
    check(std::string(mime_for("/icons.svg")) == "image/svg+xml", "svg MIME wrong");
    check(std::string(mime_for("/fonts/outfit-latin-wght.woff2")) == "font/woff2", "woff2 MIME wrong");
    check(std::string(mime_for("/fonts/OFL.txt")) == "application/octet-stream",
          "unlisted extensions do not fall back to octet-stream");

    if (failures != 0) {
        std::cerr << failures << " webui asset check(s) failed\n";
        return 1;
    }
    std::cout << "webui asset checks passed\n";
    return 0;
}
