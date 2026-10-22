#include "loader.h"
#include <TlHelp32.h>
#include <algorithm>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <cwctype>

namespace lightrod::loader {
namespace {
struct CloseHandleDeleter { void operator()(void* value) const { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
using Handle = std::unique_ptr<void, CloseHandleDeleter>;
[[noreturn]] void fail(const char* operation) {
    const DWORD error = GetLastError();
    throw std::runtime_error(std::string(operation) + " failed (Windows error " + std::to_string(error) + ")." +
        (error == ERROR_ACCESS_DENIED ? " Use the same permission level as Lunar; no security bypass is attempted." : ""));
}
std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return value;
}
std::vector<MODULEENTRY32W> modules(DWORD pid) {
    HANDLE raw = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 4; ++attempt) {
        raw = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (raw != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
    }
    if (raw == INVALID_HANDLE_VALUE) fail("Inspect process modules");
    Handle snapshot(raw);
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot.get(), &entry)) fail("Read process modules");
    std::vector<MODULEENTRY32W> result;
    do { result.push_back(entry); } while (Module32NextW(snapshot.get(), &entry));
    return result;
}
void verify64Bit(HANDLE process) {
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    const auto query = reinterpret_cast<IsWow64Process2Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
    if (query) {
        USHORT processMachine = 0, nativeMachine = 0;
        if (!query(process, &processMachine, &nativeMachine)) fail("Check target architecture");
        if (processMachine != IMAGE_FILE_MACHINE_UNKNOWN || nativeMachine != IMAGE_FILE_MACHINE_AMD64)
            throw std::runtime_error("The selected process must be native Windows x64, matching Lightrod.dll.");
    } else {
        BOOL wow = FALSE;
        if (!IsWow64Process(process, &wow)) fail("Check target architecture");
        if (wow) throw std::runtime_error("A 32-bit Java process cannot load the x64 Lightrod DLL.");
    }
}
void verifyDll(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("DLL missing or unreadable. Keep Lightrod.dll beside the injector EXE.");
    const auto length = file.tellg();
    IMAGE_DOS_HEADER dos{};
    file.seekg(0); file.read(reinterpret_cast<char*>(&dos), sizeof(dos));
    if (!file || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        static_cast<std::streamoff>(dos.e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > length)
        throw std::runtime_error("The DLL has an invalid PE header.");
    IMAGE_NT_HEADERS64 nt{};
    file.seekg(dos.e_lfanew); file.read(reinterpret_cast<char*>(&nt), sizeof(nt));
    if (!file || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || !(nt.FileHeader.Characteristics & IMAGE_FILE_DLL))
        throw std::runtime_error("The companion file must be a Windows x64 DLL.");
}
std::uintptr_t remoteLoader(const std::vector<MODULEENTRY32W>& loaded) {
    const auto local = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!local) fail("Resolve Windows DLL loader");
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(local), &owner)) fail("Resolve loader module");
    wchar_t ownerPath[32768]{};
    if (!GetModuleFileNameW(owner, ownerPath, 32768)) fail("Read loader module path");
    const auto name = lower(std::filesystem::path(ownerPath).filename().wstring());
    const auto offset = reinterpret_cast<std::uintptr_t>(local) - reinterpret_cast<std::uintptr_t>(owner);
    for (const auto& entry : loaded) if (lower(entry.szModule) == name && offset < entry.modBaseSize)
        return reinterpret_cast<std::uintptr_t>(entry.modBaseAddr) + offset;
    throw std::runtime_error("The target Windows loader module could not be resolved.");
}
struct RemotePath {
    HANDLE process;
    void* address;
    ~RemotePath() { if (address) VirtualFreeEx(process, address, 0, MEM_RELEASE); }
};
}

std::vector<Process> findJavaProcesses() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE) fail("List Java processes");
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) fail("Read process list");
    std::vector<Process> result;
    do {
        const auto name = lower(entry.szExeFile);
        if (name != L"javaw.exe" && name != L"java.exe") continue;
        struct Title { DWORD pid; std::wstring value; } title{entry.th32ProcessID, {}};
        EnumWindows([](HWND window, LPARAM context) -> BOOL {
            auto& item = *reinterpret_cast<Title*>(context);
            DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
            if (pid != item.pid || !IsWindowVisible(window)) return TRUE;
            wchar_t text[512]{}; GetWindowTextW(window, text, 512);
            if (*text) { item.value = text; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&title));
        result.push_back({entry.th32ProcessID, std::wstring(entry.szExeFile) + L"  |  PID " + std::to_wstring(entry.th32ProcessID) +
            (title.value.empty() ? L"" : L"  |  " + title.value)});
    } while (Process32NextW(snapshot.get(), &entry));
    std::stable_sort(result.begin(), result.end(), [](const Process& a, const Process& b) {
        return (lower(a.label).find(L"lunar") != std::wstring::npos) > (lower(b.label).find(L"lunar") != std::wstring::npos);
    });
    return result;
}
bool hasJvm(DWORD pid) {
    for (const auto& entry : modules(pid)) if (lower(entry.szModule) == L"jvm.dll") return true;
    return false;
}
void verifyJavaTarget(HANDLE process) {
    wchar_t path[32768]{}; DWORD count = 32768;
    if (!QueryFullProcessImageNameW(process, 0, path, &count)) fail("Read selected process");
    const auto name = lower(std::filesystem::path(path).filename().wstring());
    if (name != L"javaw.exe" && name != L"java.exe") throw std::runtime_error("Select the Lunar Java process.");
    verify64Bit(process);
    if (!hasJvm(GetProcessId(process))) throw std::runtime_error("This process has no JVM loaded. Wait for Lunar to reach its game window.");
}
void loadDll(DWORD pid, const std::filesystem::path& dll, const std::function<void(const std::wstring&)>& progress) {
    verifyDll(dll);
    const auto fullPath = std::filesystem::canonical(dll);
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid));
    if (!process) fail("Open target process");
    verify64Bit(process.get());
    const auto loaded = modules(pid);
    const auto filename = lower(fullPath.filename().wstring());
    for (const auto& entry : loaded) if (lower(entry.szModule) == filename)
        throw std::runtime_error("This DLL is already loaded in the selected process. Use Insert in the game.");
    const auto loader = remoteLoader(loaded);
    const auto bytes = (fullPath.native().size() + 1) * sizeof(wchar_t);
    RemotePath remote{process.get(), VirtualAllocEx(process.get(), nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)};
    if (!remote.address) fail("Allocate DLL path");
    SIZE_T written = 0;
    if (!WriteProcessMemory(process.get(), remote.address, fullPath.c_str(), bytes, &written) || written != bytes) fail("Write DLL path");
    Handle thread(CreateRemoteThread(process.get(), nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(loader), remote.address, 0, nullptr));
    if (!thread) fail("Start Windows DLL loader");
    const HANDLE waiting[]{thread.get(), process.get()};
    DWORD wait = WaitForMultipleObjects(2, waiting, FALSE, 15000);
    if (wait == WAIT_TIMEOUT) {
        if (progress) progress(L"Windows is still loading the DLL. Keep this window open while it finishes.");
        wait = WaitForMultipleObjects(2, waiting, FALSE, INFINITE);
    }
    if (wait == WAIT_OBJECT_0 + 1) throw std::runtime_error("The selected process exited during loading.");
    if (wait != WAIT_OBJECT_0) {
        // The target thread may still be reading the path. Retain the tiny
        // allocation until target exit instead of freeing it underneath Windows.
        remote.address = nullptr;
        fail("Wait for Windows DLL loader");
    }
    // Thread exit codes truncate HMODULE on x64. Verify the full module table.
    for (const auto& entry : modules(pid)) if (lower(entry.szModule) == filename) return;
    throw std::runtime_error("The DLL was not retained by the target. Check client compatibility and Lightrod's diagnostics.");
}
std::wstring errorText(const std::exception& error) {
    const char* value = error.what();
    const int length = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    if (length <= 0) return L"The DLL could not be loaded.";
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), length);
    result.resize(length - 1);
    return result;
}
}
