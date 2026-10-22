#pragma once
#include <Windows.h>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <exception>

namespace lightrod::loader {
struct Process { DWORD pid; std::wstring label; };
std::vector<Process> findJavaProcesses();
bool hasJvm(DWORD pid);
void verifyJavaTarget(HANDLE process);
void loadDll(DWORD pid, const std::filesystem::path& dll,
    const std::function<void(const std::wstring&)>& progress = {});
std::wstring errorText(const std::exception& error);
}
