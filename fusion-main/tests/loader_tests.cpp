#include "../injector/loader.h"
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;
void check(bool value, const char* error) { if (!value) throw std::runtime_error(error); }
struct Host {
    Handle ready{nullptr, CloseHandle}, stop{nullptr, CloseHandle}, process{nullptr, CloseHandle}, thread{nullptr, CloseHandle};
    DWORD pid = 0;
    ~Host() { if (stop) SetEvent(stop.get()); if (process) WaitForSingleObject(process.get(), 5000); }
};
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--host") {
        Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[2]), CloseHandle);
        Handle stop(OpenEventW(SYNCHRONIZE, FALSE, argv[3]), CloseHandle);
        if (!ready || !stop) return 2;
        SetEvent(ready.get());
        return WaitForSingleObject(stop.get(), 30000) == WAIT_OBJECT_0 ? 0 : 3;
    }
    try {
        check(argc == 2, "Pass the test probe DLL path");
        wchar_t executable[32768]{}; check(GetModuleFileNameW(nullptr, executable, 32768) != 0, "Cannot locate test host");
        const auto token = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        const auto readyName = L"Local\\Lightrod.Loader.Ready." + token;
        const auto stopName = L"Local\\Lightrod.Loader.Stop." + token;
        Host host;
        host.ready.reset(CreateEventW(nullptr, TRUE, FALSE, readyName.c_str()));
        host.stop.reset(CreateEventW(nullptr, TRUE, FALSE, stopName.c_str()));
        check(host.ready && host.stop, "Cannot create test events");
        std::wstring command = L"\"" + std::wstring(executable) + L"\" --host " + readyName + L" " + stopName;
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION info{};
        check(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info), "Cannot create disposable host");
        host.process.reset(info.hProcess); host.thread.reset(info.hThread); host.pid = info.dwProcessId;
        check(WaitForSingleObject(host.ready.get(), 5000) == WAIT_OBJECT_0, "Test host was not ready");
        lightrod::loader::loadDll(host.pid, argv[1]);
        std::cout << "PASS normal Windows DLL load into disposable x64 test host\n";
        bool rejected = false;
        try { lightrod::loader::loadDll(host.pid, argv[1]); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Duplicate DLL load was accepted");
        std::cout << "PASS duplicate load rejected\n";
        rejected = false;
        try { lightrod::loader::loadDll(host.pid, executable); } catch (const std::exception&) { rejected = true; }
        check(rejected, "EXE file accepted as a DLL");
        std::cout << "PASS non-DLL PE rejected\n";
        rejected = false;
        try { lightrod::loader::verifyJavaTarget(host.process.get()); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Non-Java process accepted by GUI target validation");
        std::cout << "PASS non-Java target rejected\n";
        rejected = false;
        try { lightrod::loader::loadDll(0, argv[1]); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Invalid process accepted");
        std::cout << "PASS invalid target rejected\n";
        rejected = false;
        try { lightrod::loader::loadDll(host.pid, std::filesystem::path(argv[1]).parent_path() / L"missing-lightrod-test.dll"); }
        catch (const std::exception&) { rejected = true; }
        check(rejected, "Missing DLL accepted");
        std::cout << "PASS missing DLL rejected\n";
        lightrod::loader::findJavaProcesses();
        SetEvent(host.stop.get());
        check(WaitForSingleObject(host.process.get(), 5000) == WAIT_OBJECT_0, "Disposable host did not stop");
        DWORD exit = 1; GetExitCodeProcess(host.process.get(), &exit); check(exit == 0, "Disposable host failed");
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
