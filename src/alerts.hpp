#pragma once
// Pass alerts: desktop notifications and calendar (.ics) export for satellite passes.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "process.hpp"
#include "time_utils.hpp"

inline const char* compassPoint(double azRad) {
    static const char* names[] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
    double deg = std::fmod(azRad * 180.0 / 3.14159265358979323846, 360.0);
    if (deg < 0) deg += 360.0;
    return names[(int)std::floor(deg / 22.5 + 0.5) % 16];
}

// Show a desktop notification using whatever the platform provides. Runs on a detached thread because the
// Windows balloon has to stay alive for a few seconds. Text is sanitised so it cannot break out of the command line.
inline void osNotify(std::string title, std::string text) {
    std::thread([title = std::move(title), text = std::move(text)]() {
        auto clean = [](std::string s) {
            for (char& c : s) if (c == '"' || c == '\'' || c == '`' || c == '$' || c == '\\' || c == '%' || c == '&' || c == ';' || c == '|' || c == '<' || c == '>' || c == '\n' || c == '\r') c = ' ';
            return s;
        };
#if defined(_WIN32)
        runCommandHidden("powershell -NoProfile -WindowStyle Hidden -Command \"Add-Type -AssemblyName System.Windows.Forms; Add-Type -AssemblyName System.Drawing; "
                         "$n = New-Object System.Windows.Forms.NotifyIcon; $n.Icon = [System.Drawing.SystemIcons]::Information; $n.Visible = $true; "
                         "$n.ShowBalloonTip(15000, '" + clean(title) + "', '" + clean(text) + "', 'Info'); Start-Sleep 16; $n.Dispose()\"");
#elif defined(__APPLE__)
        runCommandHidden("osascript -e 'display notification \"" + clean(text) + "\" with title \"" + clean(title) + "\"'");
#else
        runCommandHidden("notify-send \"" + clean(title) + "\" \"" + clean(text) + "\"");
#endif
    }).detach();
}

struct IcsEvent {
    std::string uid, summary, description, location;
    double start = 0, end = 0;
    int alarmMinutes = 10;
};

inline std::string icsTime(double when) { return formatTime(when, "%Y%m%dT%H%M%SZ"); }

inline std::string icsEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == ',' || c == ';' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c != '\r') out += c;
    }
    return out;
}

inline bool writeIcs(const std::string& path, const std::vector<IcsEvent>& events, double now) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const char* crlf = "\r\n";
    f << "BEGIN:VCALENDAR" << crlf << "VERSION:2.0" << crlf << "PRODID:-//Space Map//Satellite passes//EN" << crlf << "CALSCALE:GREGORIAN" << crlf << "X-WR-CALNAME:Space Map satellite passes" << crlf;
    for (const IcsEvent& e : events) {
        f << "BEGIN:VEVENT" << crlf << "UID:" << e.uid << crlf << "DTSTAMP:" << icsTime(now) << crlf << "DTSTART:" << icsTime(e.start) << crlf << "DTEND:" << icsTime(e.end) << crlf
          << "SUMMARY:" << icsEscape(e.summary) << crlf << "DESCRIPTION:" << icsEscape(e.description) << crlf;
        if (!e.location.empty()) f << "LOCATION:" << icsEscape(e.location) << crlf;
        f << "BEGIN:VALARM" << crlf << "TRIGGER:-PT" << e.alarmMinutes << "M" << crlf << "ACTION:DISPLAY" << crlf << "DESCRIPTION:" << icsEscape(e.summary) << crlf << "END:VALARM" << crlf;
        f << "END:VEVENT" << crlf;
    }
    f << "END:VCALENDAR" << crlf;
    return f.good();
}
