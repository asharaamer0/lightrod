#include "config.h"
#include <Windows.h>
#include <charconv>
#include <fstream>
#include <map>

namespace lightrod {
bool isMouse(int k) { return k == 1 || k == 2 || k == 4 || k == 5 || k == 6; }
bool validBinding(int k, bool mouseAllowed) {
    return k > 0 && k < 255 && k != VK_ESCAPE && k != VK_INSERT && k != 3 && k != 7 &&
        k != VK_PACKET && k != VK_PROCESSKEY && (mouseAllowed || !isMouse(k)) &&
        (isMouse(k) || MapVirtualKeyA(k, MAPVK_VK_TO_VSC) != 0);
}
Settings validate(Settings s) {
    const Settings d;
    if (!validBinding(s.trigger, true)) s.trigger = d.trigger;
    if (!validBinding(s.toggle, true)) s.toggle = d.toggle;
    if (!validBinding(s.sword, false)) s.sword = d.sword;
    if (!validBinding(s.rod, false)) s.rod = d.rod;
    if (s.swordClick < Click::None || s.swordClick > Click::Right) s.swordClick = d.swordClick;
    if (s.rodClick < Click::None || s.rodClick > Click::Right) s.rodClick = d.rodClick;
    s.hold = std::clamp(s.hold, 1, 2000);
    s.sync = std::clamp(s.sync, 0, 2000);
    s.delay1 = std::clamp(s.delay1, 10, 2000);
    s.delay2 = std::clamp(s.delay2, 10, 2000);
    return s;
}
std::string bindingWarning(const Settings& s) {
    if (s.trigger == s.sword || s.trigger == s.rod) return "Trigger overlaps a slot key. Choose a different trigger.";
    if (s.toggle == s.trigger || s.toggle == s.sword || s.toggle == s.rod) return "Toggle overlaps another binding. Choose a different toggle.";
    if (s.sword == s.rod) return "Sword and rod use the same key.";
    return {};
}
Settings loadConfig(const std::filesystem::path& path) {
    Settings s;
    std::ifstream in(path);
    std::string line;
    std::map<std::string, int> entries;
    while (std::getline(in, line)) {
        if (line.size() > 256) continue;
        const auto sep = line.find('=');
        if (sep == std::string::npos) continue;
        auto value = line.substr(sep + 1);
        if (!value.empty() && value.back() == '\r') value.pop_back();
        int n = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), n);
        if (result.ec == std::errc() && result.ptr == value.data() + value.size()) entries[line.substr(0, sep)] = n;
    }
    const auto read = [&](const char* key, int& out) { if (entries.count(key)) out = entries[key]; };
    int enabled = s.enabled, restore = s.returnToSword, a = static_cast<int>(s.swordClick), b = static_cast<int>(s.rodClick);
    read("enabled", enabled);
    if (enabled == 0 || enabled == 1) s.enabled = enabled != 0;
    read("returnToSword", restore);
    if (restore == 0 || restore == 1) s.returnToSword = restore != 0;
    read("trigger", s.trigger); read("sword", s.sword); read("rod", s.rod); read("toggle", s.toggle);
    read("swordClick", a); read("rodClick", b);
    if (a >= 0 && a <= 2) s.swordClick = static_cast<Click>(a);
    if (b >= 0 && b <= 2) s.rodClick = static_cast<Click>(b);
    read("hold", s.hold); read("sync", s.sync); read("delay1", s.delay1); read("delay2", s.delay2);
    return validate(s);
}
bool saveConfig(const std::filesystem::path& path, const Settings& settings, std::string& error) {
    const auto s = validate(settings);
    try {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        auto temp = path; temp += L".tmp";
        {
            std::ofstream out(temp, std::ios::trunc);
            out.exceptions(std::ios::failbit | std::ios::badbit);
            out << "enabled=" << s.enabled << "\nreturnToSword=" << s.returnToSword << "\ntrigger=" << s.trigger << "\nsword=" << s.sword
                << "\nrod=" << s.rod << "\ntoggle=" << s.toggle << "\nswordClick=" << static_cast<int>(s.swordClick)
                << "\nrodClick=" << static_cast<int>(s.rodClick) << "\nhold=" << s.hold << "\nsync=" << s.sync
                << "\ndelay1=" << s.delay1 << "\ndelay2=" << s.delay2 << '\n';
            out.flush();
        }
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not replace settings file (Windows error " + std::to_string(GetLastError()) + ")");
        error.clear();
        return true;
    } catch (const std::exception& ex) { error = ex.what(); return false; }
}
}
