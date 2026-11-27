#pragma once
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>

inline double nowUnix() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::tm calendarTime(double unixSeconds, bool local) {
    const std::time_t seconds = static_cast<std::time_t>(unixSeconds);
    std::tm result{};
#ifdef _WIN32
    if (local) localtime_s(&result, &seconds); else gmtime_s(&result, &seconds);
#else
    if (local) localtime_r(&seconds, &result); else gmtime_r(&seconds, &result);
#endif
    return result;
}

inline std::string formatTime(double unixSeconds, const char* format, bool local = false) {
    const std::tm calendar = calendarTime(unixSeconds, local);
    char buffer[64];
    std::strftime(buffer, sizeof buffer, format, &calendar);
    return buffer;
}

inline double utcToUnix(int year, int month, int day, int hour, int minute, int second) {
    const int shiftedYear = year - (month <= 2);
    const int era = (shiftedYear >= 0 ? shiftedYear : shiftedYear - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(shiftedYear - era * 400);
    const unsigned dayOfYear = static_cast<unsigned>((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    const double daysSinceEpoch = static_cast<double>(era) * 146097.0 + static_cast<double>(dayOfEra) - 719468.0;
    return daysSinceEpoch * 86400.0 + hour * 3600.0 + minute * 60.0 + second;
}
