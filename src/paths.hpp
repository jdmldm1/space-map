#pragma once
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "raylib.h"

namespace paths {

inline std::string executableDirectory() { return GetApplicationDirectory(); }

inline bool isWritableDirectory(const std::string& directory) {
    const std::string probe = directory + ".spacemap_write_test";
    std::ofstream file(probe);
    const bool writable = file.good();
    file.close();
    std::error_code ignored;
    std::filesystem::remove(probe, ignored);
    return writable;
}

inline std::string assetPath(const std::string& relative) {
    const std::string exeDir = executableDirectory();
    const std::string searchRoots[] = { exeDir, exeDir + "../share/spacemap/", "" };
    for (const std::string& root : searchRoots) {
        std::error_code ignored;
        if (std::filesystem::exists(root + relative, ignored)) return root + relative;
    }
    return relative;
}

inline const std::string& dataDirectory() {
    static const std::string directory = [] {
        const std::string exeDir = executableDirectory();
        if (isWritableDirectory(exeDir)) return exeDir;
#ifdef _WIN32
        const char* base = std::getenv("LOCALAPPDATA");
        std::string perUser = (base ? std::string(base) : exeDir) + "/SpaceMap/";
#else
        const char* xdg = std::getenv("XDG_DATA_HOME");
        const char* home = std::getenv("HOME");
        std::string perUser = xdg ? std::string(xdg) + "/spacemap/" : std::string(home ? home : ".") + "/.local/share/spacemap/";
#endif
        std::error_code ignored;
        std::filesystem::create_directories(perUser, ignored);
        return perUser;
    }();
    return directory;
}

inline std::string dataPath(const std::string& relative) { return dataDirectory() + relative; }

}
