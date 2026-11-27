#pragma once
// A small outgoing WebSocket client over TLS (wss://), used for live AIS from aisstream.io.
//   * Windows: the system WinHTTP WebSocket API, so no extra libraries.
//   * Linux:   libcurl's WebSocket API (libcurl 7.86 or newer, built with WebSocket support).
// A reader thread collects complete text messages; poll() hands them out without blocking. The implementation is in
// websocket.cpp so that <windows.h> never meets raylib's headers.
#include <string>

class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();
    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    static bool available();  // false when this build has no WebSocket support
    bool connect(const std::string& host, const std::string& path, std::string& error);
    bool send(const std::string& text);
    bool poll(std::string& message);  // one received message, or false when none is waiting
    bool open() const;                // false after the server closes the connection or on a network error
    unsigned long exitCode() const;   // why the reader stopped: 0 while open, 1000 for a normal close by the server, otherwise the system error code
    void close();

private:
    struct Impl;
    Impl* impl_;
};
