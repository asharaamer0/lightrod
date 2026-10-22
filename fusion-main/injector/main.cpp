#include "loader.h"
#include <commctrl.h>
#include <thread>
#include <memory>
#include <stdexcept>

namespace {
constexpr UINT completed = WM_APP + 1, progress = WM_APP + 2;
constexpr int refreshId = 101, injectId = 102;
HWND processesBox = nullptr, statusLabel = nullptr, injectButton = nullptr, refreshButton = nullptr;
HFONT font = nullptr, heading = nullptr;
HBRUSH background = nullptr;
std::vector<lightrod::loader::Process> processes;
std::thread worker;
bool busy = false;

void refresh() {
    SendMessageW(processesBox, CB_RESETCONTENT, 0, 0);
    try {
        processes = lightrod::loader::findJavaProcesses();
        for (const auto& process : processes) SendMessageW(processesBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(process.label.c_str()));
        if (!processes.empty()) SendMessageW(processesBox, CB_SETCURSEL, 0, 0);
        EnableWindow(injectButton, !processes.empty());
        SetWindowTextW(statusLabel, processes.empty() ? L"No Java game process found. Start Lunar, then click Refresh." :
            L"Select Lunar's Java process, then click Inject. Lightrod.dll must be beside this EXE.");
    } catch (const std::exception& error) { SetWindowTextW(statusLabel, lightrod::loader::errorText(error).c_str()); }
}
HWND control(HWND window, const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id = 0) {
    HWND result = CreateWindowW(type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return result;
}
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE: {
        font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        heading = CreateFontW(-26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        auto title = control(window, L"STATIC", L"Lightrod", 0, 24, 18, 460, 35);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(heading), TRUE);
        control(window, L"STATIC", L"Injector for Lunar Client 1.8.9 / Windows x64", 0, 24, 58, 470, 24);
        processesBox = control(window, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 24, 96, 472, 180);
        refreshButton = control(window, L"BUTTON", L"Refresh", WS_TABSTOP, 24, 140, 110, 34, refreshId);
        injectButton = control(window, L"BUTTON", L"Inject Lightrod", WS_TABSTOP | BS_DEFPUSHBUTTON, 146, 140, 180, 34, injectId);
        statusLabel = control(window, L"STATIC", L"", 0, 24, 194, 472, 66);
        refresh();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == refreshId && !busy) { refresh(); return 0; }
        if (LOWORD(w) == injectId && !busy) {
            const auto selected = SendMessageW(processesBox, CB_GETCURSEL, 0, 0);
            if (selected < 0 || static_cast<size_t>(selected) >= processes.size()) return 0;
            const DWORD pid = processes[static_cast<size_t>(selected)].pid;
            busy = true; EnableWindow(injectButton, FALSE); EnableWindow(refreshButton, FALSE); EnableWindow(processesBox, FALSE);
            SetWindowTextW(statusLabel, L"Loading Lightrod.dll through the Windows DLL loader...");
            worker = std::thread([window, pid] {
                std::wstring result;
                try {
                    std::unique_ptr<void, decltype(&CloseHandle)> target(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), CloseHandle);
                    if (!target) throw std::runtime_error("Cannot inspect this process. Refresh and check its permission level.");
                    lightrod::loader::verifyJavaTarget(target.get());
                    wchar_t executable[32768]{};
                    if (!GetModuleFileNameW(nullptr, executable, 32768)) throw std::runtime_error("Cannot locate the injector folder.");
                    const auto dll = std::filesystem::path(executable).parent_path() / L"Lightrod.dll";
                    lightrod::loader::loadDll(pid, dll, [window](const std::wstring& text) {
                        auto item = std::make_unique<std::wstring>(text);
                        if (PostMessageW(window, progress, 0, reinterpret_cast<LPARAM>(item.get()))) item.release();
                    });
                    result = L"Lightrod.dll loaded. Return to Lunar and press Insert. In-game initialization depends on client compatibility.";
                } catch (const std::exception& error) { result = lightrod::loader::errorText(error); }
                auto item = std::make_unique<std::wstring>(result);
                if (PostMessageW(window, completed, 0, reinterpret_cast<LPARAM>(item.get()))) item.release();
            });
            return 0;
        }
        break;
    case progress:
    case completed: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(l));
        SetWindowTextW(statusLabel, text->c_str());
        if (message == completed) {
            if (worker.joinable()) worker.join();
            busy = false; EnableWindow(refreshButton, TRUE); EnableWindow(processesBox, TRUE); EnableWindow(injectButton, !processes.empty());
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(w), RGB(229, 227, 222));
        SetBkColor(reinterpret_cast<HDC>(w), RGB(29, 29, 31));
        return reinterpret_cast<LRESULT>(background);
    case WM_CLOSE:
        if (busy) { SetWindowTextW(statusLabel, L"A DLL load is still running. Wait for Windows to finish before closing."); return 0; }
        DestroyWindow(window); return 0;
    case WM_DESTROY:
        if (worker.joinable()) worker.join();
        DeleteObject(font); DeleteObject(heading); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int show) {
    const bool smokeTest = arguments && std::wstring(arguments) == L"--smoke-test";
    SetProcessDPIAware();
    background = CreateSolidBrush(RGB(29, 29, 31));
    WNDCLASSW type{}; type.lpfnWndProc = windowProc; type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.hbrBackground = background; type.lpszClassName = L"Lightrod.Injector";
    if (!RegisterClassW(&type)) return 1;
    RECT bounds{0, 0, 520, 280}; AdjustWindowRect(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND window = CreateWindowW(type.lpszClassName, L"Lightrod Injector", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, instance, nullptr);
    if (!window) { DeleteObject(background); return 1; }
    if (smokeTest) {
        if (!processesBox || !statusLabel || !injectButton || !refreshButton) { DestroyWindow(window); DeleteObject(background); return 2; }
        PostMessageW(window, WM_CLOSE, 0, 0);
    } else ShowWindow(window, show);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    DeleteObject(background);
    return 0;
}
