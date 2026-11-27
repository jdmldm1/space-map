#pragma once
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>

inline double nowUnix() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Calendar fields for a UTC time before 1970 (the C library's gmtime rejects negative times on Windows).
inline std::tm calendarTimeBefore1970(double unixSeconds) {
    const long long total = static_cast<long long>(std::floor(unixSeconds));
    long long days = total / 86400, rem = total % 86400;
    if (rem < 0) { rem += 86400; --days; }
    const long long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
    std::tm result{};
    result.tm_year = static_cast<int>(y - 1900); result.tm_mon = static_cast<int>(m) - 1; result.tm_mday = static_cast<int>(d);
    result.tm_hour = static_cast<int>(rem / 3600); result.tm_min = static_cast<int>(rem % 3600 / 60); result.tm_sec = static_cast<int>(rem % 60);
    const int cum[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    result.tm_yday = cum[result.tm_mon] + static_cast<int>(d) - 1 + (leap && result.tm_mon > 1 ? 1 : 0);
    result.tm_wday = static_cast<int>(((days % 7) + 11) % 7);
    return result;
}

inline std::tm calendarTime(double unixSeconds, bool local) {
    if (unixSeconds < 0 && !local) return calendarTimeBefore1970(unixSeconds);
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
