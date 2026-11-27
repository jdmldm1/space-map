#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "process.hpp"

inline double steadySeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

inline double unixSeconds() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

inline std::string curlCommand(const std::string& url, const std::string& outputPath, int timeoutSeconds, bool compressed, bool failOnHttpError) {
    return std::string("curl -s ") + (failOnHttpError ? "-f -L " : "") + "-m " + std::to_string(timeoutSeconds) + (compressed ? " --compressed" : "") +
           " -A \"spacemap/1.0\" -o \"" + outputPath + "\" \"" + url + "\"";
}

inline bool curlGet(const std::string& url, const std::string& outputPath, int timeoutSeconds = 15, bool compressed = false) {
    namespace fs = std::filesystem;
    std::error_code ignored;
    fs::create_directories(fs::path(outputPath).parent_path(), ignored);
    const std::string partialPath = outputPath + ".part";
    const int exitCode = runCommandHidden(curlCommand(url, partialPath, timeoutSeconds, compressed, true));
    if (exitCode != 0 || !fs::exists(partialPath, ignored) || fs::file_size(partialPath, ignored) == 0) {
        fs::remove(partialPath, ignored);
        return false;
    }
    fs::remove(outputPath, ignored);
    fs::rename(partialPath, outputPath, ignored);
    return !ignored;
}

inline bool curlCapture(const std::string& url, const std::string& outputPath, int seconds) {
    namespace fs = std::filesystem;
    std::error_code ignored;
    fs::create_directories(fs::path(outputPath).parent_path(), ignored);
    fs::remove(outputPath, ignored);
    runCommandHidden(curlCommand(url, outputPath, seconds, false, false));
    return fs::exists(outputPath, ignored) && fs::file_size(outputPath, ignored) > 0;
}

inline std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}
