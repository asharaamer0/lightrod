#include "lightrod.h"
#include "pressGate.h"
#include "../../../menu/menu.h"
#include "../../../sdk/sdk.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <ShlObj.h>

namespace {
using namespace lightrod;
std::mutex settingsMutex, saveMutex;
Settings settings;
std::filesystem::path configPath;
std::string notice;
std::atomic<bool> menuOpen{false}, permitted{false}, running{false}, initialized{false};
std::atomic<bool> restorePermitted{false};
std::atomic<unsigned> revision{1};
std::atomic<int> capture{-1};
UINT inputMessage = 0;
unsigned appliedRevision = 0;
bool waitForRelease = true;
PressGate togglePress;
jclass mouseClass = nullptr;
jmethodID isGrabbed = nullptr, setGrabbed = nullptr;
bool mouseReleased = false, previousGrab = false;
bool previousGameplay = false;
auto savedAt = std::chrono::steady_clock::time_point{};

void report(const std::string& text) {
    { std::lock_guard<std::mutex> lock(settingsMutex); notice = text; }
    OutputDebugStringA(("[Lightrod] " + text + "\n").c_str());
    try {
        if (!configPath.empty()) {
            std::filesystem::create_directories(configPath.parent_path());
            auto logPath = configPath.parent_path() / L"lightrod.log";
            if (std::filesystem::exists(logPath) && std::filesystem::file_size(logPath) > 256 * 1024)
                std::filesystem::remove(logPath);
            std::ofstream out(logPath, std::ios::app);
            out << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << " " << text << '\n';
        }
    } catch (...) {}
}
bool down(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }

class WindowInput final : public Input {
    std::set<int> keys;
    std::set<Click> buttons;
    HWND owner = nullptr;
    bool send(int code, bool mouse, bool pressed, bool restoring = false) {
        if (pressed) {
            if (!(restoring ? restorePermitted.load() : permitted.load()) || menuOpen || !Menu::InputHookReady || GetForegroundWindow() != Menu::HandleWindow) return false;
            owner = Menu::HandleWindow;
        }
        if (!owner || !IsWindow(owner)) return !pressed;
        if (!pressed && !Menu::InputHookReady) return true;
        if (!pressed && owner != Menu::HandleWindow) return true; // Old WndProc released these before replacement.
        // Keep releases addressed to the original game window even after focus
        // changes. Never send global OS input into a different application.
        const WPARAM packed = static_cast<WPARAM>(code | (mouse ? 0x100 : 0) | (pressed ? 0x200 : 0) | (restoring ? 0x400 : 0));
        DWORD_PTR result = 0;
        return SendMessageTimeoutA(owner, inputMessage, packed, static_cast<LPARAM>(appliedRevision),
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 25, &result) && result == 1;
    }
public:
    std::int64_t afterDispatch(std::int64_t) override {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    bool key(int code, bool pressed) override {
        if (pressed) keys.insert(code); // Track before dispatch, including timeout.
        const bool ok = send(code, false, pressed);
        if (!pressed && ok) keys.erase(code);
        return ok;
    }
    bool mouse(Click button, bool pressed) override {
        if (pressed) buttons.insert(button);
        const bool ok = send(button == Click::Left ? VK_LBUTTON : VK_RBUTTON, true, pressed);
        if (!pressed && ok) buttons.erase(button);
        return ok;
    }
    bool restoreSword(int code, bool pressed) override {
        if (pressed) keys.insert(code);
        const bool ok = send(code, false, pressed, true);
        if (!pressed && ok) keys.erase(code);
        return ok;
    }
    void releaseAll() override {
        for (auto it = buttons.begin(); it != buttons.end();) {
            if (send(*it == Click::Left ? VK_LBUTTON : VK_RBUTTON, true, false)) it = buttons.erase(it);
            else ++it;
        }
        for (auto it = keys.begin(); it != keys.end();) {
            if (send(*it, false, false)) it = keys.erase(it);
            else ++it;
        }
    }
    bool clean() const override { return keys.empty() && buttons.empty(); }
} input;
Engine engine(input);
}

void Lightrod::Init() {
    if (initialized.exchange(true)) return;
    inputMessage = RegisterWindowMessageA("Lightrod.Input.v1");
    PWSTR appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        configPath = std::filesystem::path(appData) / L"Lightrod" / L"settings.cfg";
        CoTaskMemFree(appData);
    } else {
        report("Could not locate AppData; settings will not be saved.");
    }
    try {
        std::lock_guard<std::mutex> lock(settingsMutex);
        settings = configPath.empty() ? Settings{} : loadConfig(configPath);
    } catch (const std::exception& ex) { report(std::string("Load failed: ") + ex.what()); }
    if (!inputMessage) report("Could not register the game input message.");
    mouseClass = Java::findClass(Java::Env, Java::tiEnv, "org/lwjgl/input/Mouse");
    if (mouseClass) {
        isGrabbed = Java::Env->GetStaticMethodID(mouseClass, "isGrabbed", "()Z");
        setGrabbed = Java::Env->GetStaticMethodID(mouseClass, "setGrabbed", "(Z)V");
    }
    if (Java::Env->ExceptionCheck() || !mouseClass || !isGrabbed || !setGrabbed) {
        Java::Env->ExceptionClear();
        report("LWJGL 2 cursor API unavailable. This client build is unsupported.");
    }
}
Settings Lightrod::GetSettings() { std::lock_guard<std::mutex> lock(settingsMutex); return settings; }
void Lightrod::Apply(Settings value) {
    { std::lock_guard<std::mutex> lock(settingsMutex); settings = validate(value); }
    BlockInput();
}
void Lightrod::Toggle() { auto s = GetSettings(); s.enabled = !s.enabled; Apply(s); Save(); }
void Lightrod::BlockInput() { permitted = false; restorePermitted = false; running = false; ++revision; }
void Lightrod::SetMenuOpen(bool open) {
    menuOpen = open;
    CancelCapture();
    BlockInput();
}
bool Lightrod::Save() {
    std::lock_guard<std::mutex> saveLock(saveMutex);
    std::string error;
    if (configPath.empty()) { report("Settings folder is unavailable."); return false; }
    if (!saveConfig(configPath, GetSettings(), error)) { report("Save failed: " + error); return false; }
    { std::lock_guard<std::mutex> lock(settingsMutex); notice = "Saved"; savedAt = std::chrono::steady_clock::now(); }
    return true;
}
std::string Lightrod::Notice() {
    std::lock_guard<std::mutex> lock(settingsMutex);
    if (notice == "Saved" && std::chrono::steady_clock::now() - savedAt > std::chrono::seconds(2)) return {};
    return notice;
}
const char* Lightrod::Status() { return !GetSettings().enabled ? "OFF" : running ? "RUNNING" : "READY"; }
void Lightrod::BeginCapture(int field) { BlockInput(); capture = field; }
void Lightrod::CancelCapture() { capture = -1; }
int Lightrod::Capturing() { return capture; }
bool Lightrod::Capture(int key) {
    const int field = capture;
    if (field < 0) return false;
    if (key == VK_ESCAPE) { CancelCapture(); return true; }
    if (!validBinding(key, field == 0 || field == 3)) {
        std::lock_guard<std::mutex> lock(settingsMutex);
        notice = "Choose a valid key. Insert is reserved; Escape cancels.";
        return true;
    }
    auto s = GetSettings();
    if (field == 0) s.trigger = key;
    if (field == 1) s.sword = key;
    if (field == 2) s.rod = key;
    if (field == 3) s.toggle = key;
    const auto warning = bindingWarning(s);
    if (!warning.empty()) { std::lock_guard<std::mutex> lock(settingsMutex); notice = warning; return true; }
    Apply(s); CancelCapture(); Save();
    return true;
}
std::string Lightrod::KeyName(int key) {
    switch (key) {
    case VK_LBUTTON: return "Mouse 1"; case VK_RBUTTON: return "Mouse 2";
    case VK_MBUTTON: return "Mouse 3"; case VK_XBUTTON1: return "Mouse 4"; case VK_XBUTTON2: return "Mouse 5";
    default: break;
    }
    UINT scan = MapVirtualKeyA(key, MAPVK_VK_TO_VSC);
    if (key == VK_RCONTROL || key == VK_RMENU || (key >= VK_PRIOR && key <= VK_DOWN) || key == VK_DIVIDE || key == VK_NUMLOCK) scan |= 0x100;
    char name[64]{};
    if (GetKeyNameTextA(static_cast<LONG>(scan << 16), name, 64)) return name;
    return "Key " + std::to_string(key);
}
bool Lightrod::SyntheticMessage(UINT message) { return inputMessage && message == inputMessage; }
bool Lightrod::TranslateInput(WPARAM w, LPARAM l, UINT& msg, WPARAM& key, LPARAM& param) {
    const bool pressed = (w & 0x200) != 0;
    const bool mouse = (w & 0x100) != 0;
    const bool restoring = (w & 0x400) != 0;
    const int code = static_cast<int>(w & 0xff);
    if (pressed && (static_cast<unsigned>(l) != revision || !(restoring ? restorePermitted.load() : permitted.load()) || menuOpen ||
        GetForegroundWindow() != Menu::HandleWindow)) return false;
    if (pressed) {
        const auto current = GetSettings();
        if (!current.enabled) return false;
        if (restoring) {
            if (!current.returnToSword || mouse || code != current.sword) return false;
        } else if (!down(current.trigger)) return false;
    }
    if (mouse) {
        msg = code == VK_LBUTTON ? (pressed ? WM_LBUTTONDOWN : WM_LBUTTONUP) : (pressed ? WM_RBUTTONDOWN : WM_RBUTTONUP);
        key = pressed ? (code == VK_LBUTTON ? MK_LBUTTON : MK_RBUTTON) : 0;
        POINT point{}; GetCursorPos(&point); ScreenToClient(Menu::HandleWindow, &point);
        param = MAKELPARAM(point.x, point.y);
    } else {
        msg = pressed ? WM_KEYDOWN : WM_KEYUP;
        key = code;
        UINT scan = MapVirtualKeyA(code, MAPVK_VK_TO_VSC_EX);
        param = 1 | ((scan & 0xff) << 16) | ((scan & 0xff00) ? (1 << 24) : 0);
        if (!pressed) param |= static_cast<LPARAM>(0xc0000000u);
    }
    return true;
}
void Lightrod::Update() {
    if (!initialized) return;
    const unsigned rev = revision;
    if (rev != appliedRevision) {
        engine.cancel(); waitForRelease = true; appliedRevision = rev;
        togglePress.suppress(down(GetSettings().toggle));
    }
    auto s = GetSettings();
    const bool focused = Menu::HandleWindow && GetForegroundWindow() == Menu::HandleWindow && !IsIconic(Menu::HandleWindow);
    bool gameplay = false;
    // Only this Fusion worker uses its JNIEnv. Local refs never accumulate.
    if (focused && Java::Env && SDK::Minecraft) {
        if (Java::Env->PushLocalFrame(16) == JNI_OK) {
            gameplay = SDK::Minecraft->thePlayer->GetInstance() && SDK::Minecraft->theWorld->GetInstance() && !SDK::Minecraft->IsInGuiState();
            if (Java::Env->ExceptionCheck()) { Java::Env->ExceptionClear(); gameplay = false; }
            Java::Env->PopLocalFrame(nullptr);
        } else Java::Env->ExceptionClear();
    }
    if (previousGameplay && !gameplay && menuOpen) Menu::SetOpen(false);
    previousGameplay = gameplay;
    if (mouseClass && isGrabbed && setGrabbed) {
        if (menuOpen && focused && !mouseReleased) {
            previousGrab = Java::Env->CallStaticBooleanMethod(mouseClass, isGrabbed) == JNI_TRUE;
            Java::Env->CallStaticVoidMethod(mouseClass, setGrabbed, JNI_FALSE);
            mouseReleased = true;
        } else if (!menuOpen && mouseReleased && focused) {
            // Respect the game's own cursor state if a different screen opened.
            if (previousGrab && gameplay) Java::Env->CallStaticVoidMethod(mouseClass, setGrabbed, JNI_TRUE);
            mouseReleased = false;
        }
        if (Java::Env->ExceptionCheck()) {
            Java::Env->ExceptionClear();
            isGrabbed = setGrabbed = nullptr;
            report("Could not change cursor capture; macro suspended.");
        }
    }
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (togglePress.update(down(s.toggle), now, focused && (gameplay || menuOpen) && capture < 0)) {
        Toggle(); s = GetSettings();
    }
    const bool held = down(s.trigger);
    if (!held) waitForRelease = false;
    if (!gameplay || menuOpen || !s.enabled) waitForRelease = true;
    const bool safe = gameplay && !menuOpen && capture < 0 && s.enabled && bindingWarning(s).empty() &&
        inputMessage && setGrabbed && Menu::InputHookReady && Menu::Initialized;
    const bool allowed = safe && !waitForRelease && held;
    permitted = allowed && rev == revision;
    restorePermitted = safe && s.returnToSword && rev == revision;
    // Failed releases are retried before any new cycle may start.
    if (!engine.running() && !input.clean()) {
        permitted = false; restorePermitted = false; running = false; input.releaseAll(); return;
    }
    const bool wasFailed = engine.failed();
    engine.update(now, allowed && permitted, s, restorePermitted);
    if (engine.failed() && !wasFailed) report("Input delivery failed; released owned input. Release the trigger to retry.");
    running = engine.running() && (permitted || restorePermitted) && appliedRevision == revision;
}
void Lightrod::Shutdown() {
    if (!initialized.exchange(false)) return;
    BlockInput(); CancelCapture(); engine.cancel(); input.releaseAll(); Save();
    // Keep the module loaded if a hung game window has not acknowledged a
    // release yet. Unloading its WndProc first would strand the pending state.
    while (!input.clean()) { input.releaseAll(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (mouseReleased && previousGrab && setGrabbed && GetForegroundWindow() == Menu::HandleWindow && !SDK::Minecraft->IsInGuiState())
        Java::Env->CallStaticVoidMethod(mouseClass, setGrabbed, JNI_TRUE);
    if (Java::Env->ExceptionCheck()) Java::Env->ExceptionClear();
    if (mouseClass) { Java::Env->DeleteLocalRef(mouseClass); mouseClass = nullptr; }
}
