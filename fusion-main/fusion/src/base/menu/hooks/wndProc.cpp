#include "../menu.h"
#include "../../moduleManager/modules/lightrod/lightrod.h"
#include "../../../../ext/imgui/imgui_impl_win32.h"
#include <array>
#include "../../base.h"
#include "../../moduleManager/modules/lightrod/pressGate.h"
#include <chrono>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
using WindowProc = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);
static WindowProc original = nullptr;
static HWND hookedWindow = nullptr;
static std::array<bool, 256> deliveredKeys{}, deliveredMouse{};
static lightrod::PressGate insertPress;

LRESULT CALLBACK hook_WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    Menu::CallbackScope callback;
    if (Lightrod::SyntheticMessage(msg)) {
        UINT translated; WPARAM key; LPARAM param;
        if (!Lightrod::TranslateInput(w, l, translated, key, param)) return 0;
        ((w & 0x100) ? deliveredMouse : deliveredKeys)[w & 0xff] = (w & 0x200) != 0;
        CallWindowProcA(original, hwnd, translated, key, param);
        return 1;
    }
    if (msg == WM_KILLFOCUS || (msg == WM_ACTIVATEAPP && !w)) {
        insertPress.suppress((GetAsyncKeyState(VK_INSERT) & 0x8000) != 0);
        Lightrod::BlockInput();
        Menu::SetOpen(false);
    }
    const bool keyDown = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    const bool keyUp = msg == WM_KEYUP || msg == WM_SYSKEYUP;
    const bool repeat = (l & (1LL << 30)) != 0;
    if ((keyDown || keyUp) && w == VK_INSERT) {
        const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (insertPress.update(keyDown, now, !repeat || keyUp)) Menu::SetOpen(!Menu::Open);
        return 0;
    }
    if (Menu::Open && Lightrod::Capturing() >= 0) {
        int code = keyDown && !repeat ? static_cast<int>(w) : 0;
        if (msg == WM_LBUTTONDOWN) code = VK_LBUTTON;
        if (msg == WM_RBUTTONDOWN) code = VK_RBUTTON;
        if (msg == WM_MBUTTONDOWN) code = VK_MBUTTON;
        if (msg == WM_XBUTTONDOWN) code = HIWORD(w) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
        if (code) { Lightrod::Capture(code); return 0; }
    } else if (keyDown && w == VK_ESCAPE && Menu::Open) {
        Menu::SetOpen(false);
        return 0;
    }
    if (Menu::Open && Menu::Initialized) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, w, l);
        // Releases still reach LWJGL to clear physical controls held before opening.
        if (keyUp || msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_MBUTTONUP || msg == WM_XBUTTONUP)
            return CallWindowProcA(original, hwnd, msg, w, l);
        if (keyDown || msg == WM_CHAR || msg == WM_SYSCHAR || msg == WM_MOUSEMOVE ||
            (msg >= WM_LBUTTONDOWN && msg <= WM_MOUSEHWHEEL) || msg == WM_INPUT)
            return 0;
    }
    return CallWindowProcA(original, hwnd, msg, w, l);
}
void Menu::Hook_wndProc() {
    if (hookedWindow == Menu::HandleWindow) return;
    Unhook_wndProc();
    hookedWindow = Menu::HandleWindow;
    original = reinterpret_cast<WindowProc>(SetWindowLongPtrA(hookedWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hook_WndProc)));
    Menu::InputHookReady = original != nullptr;
    if (!Menu::InputHookReady) {
        OutputDebugStringA("[Lightrod] Could not attach the game window procedure.\n");
        hookedWindow = nullptr;
        Base::Running = false;
    }
}
void Menu::Unhook_wndProc() {
    // Fullscreen may replace the HWND before the worker gets another update.
    // Clear synthetic state in LWJGL on its owning window thread first.
    if (hookedWindow && original && GetCurrentThreadId() == GetWindowThreadProcessId(hookedWindow, nullptr)) {
        for (size_t code = 0; code < deliveredKeys.size(); ++code) {
            for (bool mouse : {false, true}) {
                bool& held = (mouse ? deliveredMouse : deliveredKeys)[code];
                if (!held) continue;
                UINT msg; WPARAM key; LPARAM param;
                Lightrod::TranslateInput(code | (mouse ? 0x100 : 0), 0, msg, key, param);
                CallWindowProcA(original, hookedWindow, msg, key, param);
                held = false;
            }
        }
    }
    if (hookedWindow && original && IsWindow(hookedWindow))
        SetWindowLongPtrA(hookedWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
    hookedWindow = nullptr;
    Menu::InputHookReady = false;
}
