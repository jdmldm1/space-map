#include "websocket.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
// ---------------------------------------------------------------------------------------------------------------- Windows
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

struct WebSocketClient::Impl {
    HINTERNET session = nullptr, connection = nullptr, socket = nullptr;
    std::thread reader;
    std::mutex m;
    std::deque<std::string> queue;
    std::atomic<bool> isOpen{ false }, stop{ false };
    std::atomic<unsigned long> exitCode{ 0 };
};

static std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }  // host names and paths are ASCII

WebSocketClient::WebSocketClient() : impl_(new Impl) {}
WebSocketClient::~WebSocketClient() { close(); delete impl_; }
bool WebSocketClient::available() { return true; }
bool WebSocketClient::open() const { return impl_->isOpen; }
unsigned long WebSocketClient::exitCode() const { return impl_->exitCode; }

bool WebSocketClient::connect(const std::string& host, const std::string& path, std::string& error) {
    Impl& I = *impl_;
    I.session = WinHttpOpen(L"SpaceMap/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!I.session) { error = "WinHTTP unavailable"; return false; }
    WinHttpSetTimeouts(I.session, 10000, 10000, 15000, 0);  // the open socket never times out a read: a quiet stream is normal, and WinHTTP ends the socket on a timeout
    I.connection = WinHttpConnect(I.session, widen(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!I.connection) { error = "could not reach " + host; close(); return false; }
    HINTERNET request = WinHttpOpenRequest(I.connection, L"GET", widen(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) { error = "could not open request"; close(); return false; }
    WinHttpSetTimeouts(request, 10000, 10000, 15000, 20000);  // but the handshake itself must not hang
    if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
        !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) {
        error = "connection to " + host + " failed (" + std::to_string((unsigned long)GetLastError()) + ")";
        WinHttpCloseHandle(request);
        close();
        return false;
    }
    I.socket = WinHttpWebSocketCompleteUpgrade(request, 0);
    WinHttpCloseHandle(request);
    if (!I.socket) { error = "WebSocket upgrade refused"; close(); return false; }
    I.stop = false;
    I.isOpen = true;
    I.reader = std::thread([this] {
        Impl& J = *impl_;
        std::string message;
        std::vector<char> buffer(32768);
        while (!J.stop) {
            DWORD got = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            const DWORD rc = WinHttpWebSocketReceive(J.socket, buffer.data(), (DWORD)buffer.size(), &got, &type);
            if (rc == ERROR_WINHTTP_TIMEOUT) continue;
            if (rc != NO_ERROR) { J.exitCode = rc; break; }
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) { J.exitCode = 1000; break; }  // closed by the server
            message.append(buffer.data(), got);
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
                std::lock_guard<std::mutex> lk(J.m);
                if (J.queue.size() < 20000) J.queue.push_back(std::move(message));  // drop rather than grow without bound if the app stalls
                message.clear();
            }
        }
        J.isOpen = false;
    });
    return true;
}

bool WebSocketClient::send(const std::string& text) {
    Impl& I = *impl_;
    if (!I.isOpen || !I.socket) return false;
    return WinHttpWebSocketSend(I.socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)text.data(), (DWORD)text.size()) == NO_ERROR;
}

bool WebSocketClient::poll(std::string& message) {
    Impl& I = *impl_;
    std::lock_guard<std::mutex> lk(I.m);
    if (I.queue.empty()) return false;
    message = std::move(I.queue.front());
    I.queue.pop_front();
    return true;
}

void WebSocketClient::close() {
    Impl& I = *impl_;
    const bool wasOpen = I.isOpen;
    I.stop = true;
    I.isOpen = false;
    if (I.socket && wasOpen) WinHttpWebSocketClose(I.socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);  // say goodbye, so the server frees the connection at once
    if (I.socket) { WinHttpCloseHandle(I.socket); I.socket = nullptr; }  // makes a pending receive fail, which ends the reader
    if (I.reader.joinable()) I.reader.join();
    if (I.connection) { WinHttpCloseHandle(I.connection); I.connection = nullptr; }
    if (I.session) { WinHttpCloseHandle(I.session); I.session = nullptr; }
    std::lock_guard<std::mutex> lk(I.m);
    I.queue.clear();
}

#elif defined(SPACEMAP_HAVE_LIBCURL) && __has_include(<curl/curl.h>)
// ---------------------------------------------------------------------------------------------------------------- libcurl
#include <curl/curl.h>

#if LIBCURL_VERSION_NUM >= 0x075600
struct WebSocketClient::Impl {
    CURL* handle = nullptr;
    std::thread reader;
    std::mutex m, curlMutex;
    std::deque<std::string> queue;
    std::atomic<bool> isOpen{ false }, stop{ false };
};

WebSocketClient::WebSocketClient() : impl_(new Impl) {}
WebSocketClient::~WebSocketClient() { close(); delete impl_; }
bool WebSocketClient::available() { return true; }
bool WebSocketClient::open() const { return impl_->isOpen; }
unsigned long WebSocketClient::exitCode() const { return 0; }

bool WebSocketClient::connect(const std::string& host, const std::string& path, std::string& error) {
    Impl& I = *impl_;
    I.handle = curl_easy_init();
    if (!I.handle) { error = "libcurl could not start"; return false; }
    curl_easy_setopt(I.handle, CURLOPT_URL, ("wss://" + host + path).c_str());
    curl_easy_setopt(I.handle, CURLOPT_CONNECT_ONLY, 2L);  // WebSocket mode: connect and upgrade, then send/recv by hand
    curl_easy_setopt(I.handle, CURLOPT_USERAGENT, "SpaceMap/1.0");
    curl_easy_setopt(I.handle, CURLOPT_CONNECTTIMEOUT, 10L);
    const CURLcode rc = curl_easy_perform(I.handle);
    if (rc != CURLE_OK) { error = std::string("connection failed: ") + curl_easy_strerror(rc); close(); return false; }
    I.stop = false;
    I.isOpen = true;
    I.reader = std::thread([this] {
        Impl& J = *impl_;
        std::string message;
        std::vector<char> buffer(32768);
        while (!J.stop) {
            size_t got = 0;
            const struct curl_ws_frame* meta = nullptr;
            CURLcode r;
            { std::lock_guard<std::mutex> lk(J.curlMutex); r = curl_ws_recv(J.handle, buffer.data(), buffer.size(), &got, &meta); }
            if (r == CURLE_AGAIN) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }
            if (r != CURLE_OK || !meta || (meta->flags & CURLWS_CLOSE)) break;
            if (!(meta->flags & (CURLWS_TEXT | CURLWS_BINARY))) continue;  // pings are answered by libcurl itself
            message.append(buffer.data(), got);
            if (meta->bytesleft == 0 && !(meta->flags & CURLWS_CONT)) {
                std::lock_guard<std::mutex> lk(J.m);
                if (J.queue.size() < 20000) J.queue.push_back(std::move(message));
                message.clear();
            }
        }
        J.isOpen = false;
    });
    return true;
}

bool WebSocketClient::send(const std::string& text) {
    Impl& I = *impl_;
    if (!I.isOpen || !I.handle) return false;
    size_t offset = 0;
    for (int tries = 0; offset < text.size() && tries < 500; ++tries) {
        size_t sent = 0;
        CURLcode r;
        { std::lock_guard<std::mutex> lk(I.curlMutex); r = curl_ws_send(I.handle, text.data() + offset, text.size() - offset, &sent, 0, CURLWS_TEXT); }
        if (r == CURLE_AGAIN) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
        if (r != CURLE_OK) return false;
        offset += sent;
    }
    return offset >= text.size();
}

bool WebSocketClient::poll(std::string& message) {
    Impl& I = *impl_;
    std::lock_guard<std::mutex> lk(I.m);
    if (I.queue.empty()) return false;
    message = std::move(I.queue.front());
    I.queue.pop_front();
    return true;
}

void WebSocketClient::close() {
    Impl& I = *impl_;
    I.stop = true;
    I.isOpen = false;
    if (I.reader.joinable()) I.reader.join();
    if (I.handle) { curl_easy_cleanup(I.handle); I.handle = nullptr; }
    std::lock_guard<std::mutex> lk(I.m);
    I.queue.clear();
}
#else
#define SPACEMAP_NO_WEBSOCKET 1
#endif  // libcurl version
#else
#define SPACEMAP_NO_WEBSOCKET 1
#endif

#ifdef SPACEMAP_NO_WEBSOCKET
// ---------------------------------------------------------------------------------------------------------------- not available
struct WebSocketClient::Impl {};
WebSocketClient::WebSocketClient() : impl_(new Impl) {}
WebSocketClient::~WebSocketClient() { delete impl_; }
bool WebSocketClient::available() { return false; }
bool WebSocketClient::connect(const std::string&, const std::string&, std::string& error) { error = "this build has no WebSocket support (needs libcurl 7.86 or newer)"; return false; }
bool WebSocketClient::send(const std::string&) { return false; }
bool WebSocketClient::poll(std::string&) { return false; }
bool WebSocketClient::open() const { return false; }
unsigned long WebSocketClient::exitCode() const { return 0; }
void WebSocketClient::close() {}
#endif
