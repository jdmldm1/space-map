#include "process.hpp"

#include <cstdlib>
#include <vector>

#ifdef _WIN32
#include <windows.h>

int runCommandHidden(const std::string& commandLine) {
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');
    if (!CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return -1;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return static_cast<int>(exitCode);
}

#else
#include <sys/wait.h>

int runCommandHidden(const std::string& commandLine) {
    const int status = std::system(commandLine.c_str());
    return status == -1 ? -1 : WEXITSTATUS(status);
}

#endif
