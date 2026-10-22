#include "../fusion/src/base/moduleManager/modules/lightrod/engine.h"
#include "../fusion/src/base/moduleManager/modules/lightrod/config.h"
#include "../fusion/src/base/moduleManager/modules/lightrod/pressGate.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>
#include <Windows.h>

using namespace lightrod;
using Event = std::tuple<int, char, int, bool>;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct FakeInput : Input {
    int now = 0;
    bool failNext = false;
    int failDownKey = 0;
    bool failCleanup = false;
    std::vector<Event> events;
    std::set<int> keys, buttons;
    bool key(int k, bool pressed) override { return emit('K', k, pressed, keys); }
    bool mouse(Click k, bool pressed) override { return emit('M', static_cast<int>(k), pressed, buttons); }
    bool emit(char type, int k, bool pressed, std::set<int>& held) {
        check(!pressed || !held.count(k), "Overlapping synthetic presses");
        if (pressed) held.insert(k); else held.erase(k);
        events.emplace_back(now, type, k, pressed);
        if (failNext || (pressed && type == 'K' && k == failDownKey)) {
            failNext = false; failDownKey = 0; return false;
        }
        return true;
    }
    void releaseAll() override {
        if (failCleanup) return;
        for (int k : buttons) events.emplace_back(now, 'M', k, false);
        for (int k : keys) events.emplace_back(now, 'K', k, false);
        buttons.clear(); keys.clear();
    }
    bool clean() const override { return keys.empty() && buttons.empty(); }
};
struct Run {
    FakeInput input;
    Engine engine{input};
    Settings settings;
    void at(int ms, bool allowed = true, bool safeToRestore = false) {
        input.now = ms;
        engine.update(static_cast<std::int64_t>(ms) * 1000000, allowed, settings, safeToRestore);
    }
    void through(int last) { for (int t = 0; t <= last; ++t) at(t); }
};
int main() {
    try {
        int passed = 0;
        const auto test = [&](const char* name, auto body) { body(); ++passed; std::cout << "PASS " << name << '\n'; };
        test("exact default sequence and absolute cycle anchors", [] {
            Run r; r.through(276);
            std::vector<Event> expected{
                {0,'K','1',true}, {20,'K','1',false}, {69,'K','2',true},
                {74,'M',2,true}, {94,'M',2,false}, {94,'K','2',false},
                {94,'K','1',true}, {114,'K','1',false},
                {138,'K','1',true}, {158,'K','1',false}, {207,'K','2',true},
                {212,'M',2,true}, {232,'M',2,false}, {232,'K','2',false},
                {232,'K','1',true}, {252,'K','1',false}, {276,'K','1',true}};
            check(r.input.events == expected, "Default sequence changed");
        });
        test("cancel at every millisecond releases all owned input", [] {
            for (int end = 0; end <= 300; ++end) {
                Run r; r.settings.swordClick = Click::Left; r.through(end); r.at(end, false);
                check(!r.engine.running() && r.input.clean(), "Cancellation left an input held");
                const auto count = r.input.events.size(); r.at(end + 1000, false);
                check(count == r.input.events.size(), "Action occurred after cancellation");
            }
        });
        test("disabled settings cancel an active click", [] {
            Run r; r.through(80); r.settings.enabled = false; r.at(81);
            check(!r.engine.running() && r.input.clean(), "Disable did not cancel");
        });
        test("every independent click mode including no extra sword click", [] {
            for (auto a : {Click::None, Click::Left, Click::Right}) for (auto b : {Click::None, Click::Left, Click::Right}) {
                Run r; r.settings.swordClick = a; r.settings.rodClick = b; r.through(137);
                int presses = 0;
                for (const auto& e : r.input.events) if (std::get<1>(e) == 'M' && std::get<3>(e)) ++presses;
                check(presses == (a != Click::None) + (b != Click::None), "Unexpected mouse action");
                check(r.input.clean(), "Cycle did not release input");
            }
        });
        test("configured slot bindings are emitted verbatim", [] {
            Run r; r.settings.sword = '7'; r.settings.rod = '9'; r.through(70);
            check(r.input.events.front() == Event{0,'K','7',true}, "Sword key ignored");
            check(r.input.events[2] == Event{69,'K','9',true}, "Rod key ignored");
        });
        test("lower delays repeat faster", [] {
            Run r; r.settings.returnToSword = false; r.settings.delay1 = 10; r.settings.delay2 = 10; r.through(40);
            int presses = 0;
            for (auto e : r.input.events) if (std::get<1>(e) == 'K' && std::get<2>(e) == '1' && std::get<3>(e)) ++presses;
            check(presses == 3, "Delay changes not used");
            check(r.settings.effectiveHold() == 4, "Hold clamp differs from source");
        });
        test("zero synchronization and minimum hold work", [] {
            Run r; r.settings.sync = 0; r.settings.hold = 1; r.settings.swordClick = Click::Left; r.through(2);
            check(r.input.events[1] == Event{0,'M',1,true}, "Zero sync delayed the click");
            check(r.input.clean(), "Minimum hold not released");
        });
        test("late worker wake preserves real hold and skips missed periods", [] {
            Run r; r.settings.returnToSword = false; r.at(0); r.at(1000); r.at(1005); r.at(1025); r.at(1026);
            check(r.input.events[3] == Event{1005,'M',2,true}, "Late sync not measured from press");
            check(r.input.events[4] == Event{1025,'M',2,false}, "Late hold shortened");
            int swordPresses = 0;
            for (auto e : r.input.events) if (std::get<1>(e) == 'K' && std::get<2>(e) == '1' && std::get<3>(e)) ++swordPresses;
            check(swordPresses == 2, "Stall caused catch-up burst");
        });
        test("input failure cancels and waits for trigger release", [] {
            Run r; r.input.failNext = true; r.at(0);
            check(r.engine.failed() && r.input.clean(), "Failure did not clean up");
            const auto count = r.input.events.size(); r.at(100);
            check(count == r.input.events.size(), "Failed engine retried while held");
            r.at(101, false); r.at(138); check(r.engine.running(), "Release did not allow recovery after the cycle guard");
        });
        test("resuming starts at sword with a fresh anchor", [] {
            Run r; r.through(80); r.at(81, false); r.at(500);
            check(r.input.events.back() == Event{500,'K','1',true}, "Resume did not begin at sword");
        });
        test("a failed mouse press or release cannot strand the slot key", [] {
            for (int failTime : {74, 94}) {
                Run r; r.through(failTime - 1); r.input.failNext = true; r.at(failTime);
                check(r.engine.failed() && r.input.clean(), "Mouse delivery failure stranded input");
            }
        });
        test("source hold formula survives impossible sync values", [] {
            Settings s; s.sync = 2000; s.hold = 2000; s.delay1 = 10; s.delay2 = 10;
            check(s.effectiveHold() == 1, "Hold underflow");
            Run r; r.settings = s; r.through(100); r.at(101, false);
            check(r.input.clean(), "Cancellation during long sync failed");
        });
        const auto path = std::filesystem::temp_directory_path() / ("lightrod-test-" + std::to_string(GetCurrentProcessId()) + ".cfg");
        test("settings survive saving and restart including enabled state", [&] {
            Settings s; s.enabled = false; s.returnToSword = false; s.trigger = 5; s.sword = '7'; s.toggle = 6;
            s.swordClick = Click::Left; s.hold = 100; s.sync = 17; s.delay1 = 141; s.delay2 = 199;
            std::string error; check(saveConfig(path, s, error), "Could not save test settings");
            const auto loaded = loadConfig(path);
            check(!loaded.enabled && !loaded.returnToSword && loaded.trigger == 5 && loaded.sword == '7' && loaded.toggle == 6 &&
                loaded.hold == 100 && loaded.sync == 17 && loaded.delay1 == 141 && loaded.delay2 == 199 &&
                loaded.swordClick == Click::Left && loaded.rodClick == Click::Right, "Round-trip lost settings");
        });
        test("malformed optional fields retain valid configuration", [&] {
            { std::ofstream out(path); out << "trigger=88\ndelay1=123\ndelay2=bad\nhold=99999999999999\nsync=-5\nrodClick=77\nsword=27\nenabled=0\n"; }
            const auto s = loadConfig(path);
            check(s.trigger == 'X' && s.delay1 == 123 && s.delay2 == 69 && s.hold == 20 && s.sync == 0 &&
                s.rodClick == Click::Right && s.sword == '1' && !s.enabled, "Invalid field damaged valid settings");
        });
        test("binding validation distinguishes mouse from slot keys and reserves controls", [] {
            check(validBinding(5, true) && !validBinding(5, false) && !validBinding(27, true) && !validBinding(45, true), "Invalid binding accepted");
            Settings s; s.trigger = s.sword; check(!bindingWarning(s).empty(), "Trigger conflict not reported");
            s = {}; s.toggle = s.trigger; check(!bindingWarning(s).empty(), "Toggle conflict not reported");
        });
        test("failed save preserves the last valid file", [&] {
            Settings s; s.delay1 = 333; std::string error;
            check(saveConfig(path, s, error), "Initial save failed");
            auto temp = path; temp += L".tmp"; std::filesystem::create_directory(temp);
            s.delay1 = 444;
            check(!saveConfig(path, s, error) && !error.empty(), "Save failure was hidden");
            check(loadConfig(path).delay1 == 333, "Failed save destroyed valid settings");
            std::filesystem::remove(temp);
        });
        test("return option off preserves the original rod-to-next-cycle sequence", [] {
            Run r; r.settings.returnToSword = false; r.through(137);
            check(r.input.events.size() == 6 && r.input.events.back() == Event{94,'K','2',false}, "Option off still restored early");
            r.at(138); check(r.input.events.back() == Event{138,'K','1',true}, "Original next-cycle return lost");
        });
        test("trigger release restores sword during every part of the rod step", [] {
            for (int end = 69; end < 114; ++end) {
                Run r; r.through(end); r.at(end + 1, false, true);
                for (int t = end + 2; t <= end + 22; ++t) r.at(t, false, true);
                check(r.input.clean() && !r.engine.running(), "Release cleanup did not finish");
                int restores = 0;
                for (auto e : r.input.events) if (std::get<1>(e) == 'K' && std::get<2>(e) == '1' && std::get<3>(e) && std::get<0>(e) >= 69) ++restores;
                check(restores == 1, "Release cleanup repeated or missed the sword selection");
            }
        });
        test("release before rod never schedules an unnecessary sword press", [] {
            Run r; r.through(30); const auto count = r.input.events.size(); r.at(31, false, true); r.at(300, false, true);
            check(r.input.events.size() == count && r.input.clean(), "Release before rod added a selection");
        });
        test("return selection never executes the configured sword click", [] {
            Run r; r.settings.swordClick = Click::Left; r.through(137);
            int left = 0, right = 0;
            for (auto e : r.input.events) if (std::get<1>(e) == 'M' && std::get<3>(e)) {
                if (std::get<2>(e) == 1) ++left; else ++right;
            }
            check(left == 1 && right == 1 && r.input.clean(), "Return added a mouse click");
        });
        test("rapid taps cannot bypass the period or queue overlapping cycles", [] {
            for (bool restore : {false, true}) {
                Run r; r.settings.returnToSword = restore;
                for (int t = 0; t < 2000; ++t) r.at(t, t % 2 == 0, true);
                r.at(2000, false); check(r.input.clean(), "Spam left input held");
                int previous = -1000;
                for (auto e : r.input.events) if (std::get<1>(e) == 'K' && std::get<2>(e) == '1' && std::get<3>(e)) {
                    check(std::get<0>(e) - previous >= 138, "Spam bypassed the configured cycle interval");
                    previous = std::get<0>(e);
                }
                const auto count = r.input.events.size(); r.at(3000, false, true);
                check(r.input.events.size() == count, "Spam queued future actions");
            }
        });
        test("re-pressing during return finishes one restore before the next cycle", [] {
            Run r; r.through(80); r.at(81, false, true);
            for (int t = 82; t <= 137; ++t) r.at(t, t % 2 == 0, true);
            int rods = 0, swords = 0;
            for (auto e : r.input.events) if (std::get<1>(e) == 'K' && std::get<3>(e)) {
                if (std::get<2>(e) == '2') ++rods; else ++swords;
            }
            check(rods == 1 && swords == 2 && r.input.clean(), "Rapid re-press duplicated return/cycle");
            r.at(138, true, true); check(r.input.events.back() == Event{138,'K','1',true}, "Cycle did not resume after guard");
        });
        test("focus/menu/disable hard cancellation never starts a return", [] {
            for (int end : {69, 75, 80, 95, 105}) {
                Run r; r.through(end); const auto count = r.input.events.size(); r.at(end + 1, false, false);
                for (size_t i = count; i < r.input.events.size(); ++i) check(!std::get<3>(r.input.events[i]), "Unsafe cancellation pressed a key");
                check(r.input.clean(), "Hard cancellation stranded input");
            }
            Run r; r.through(80); r.settings.enabled = false; const auto count = r.input.events.size(); r.at(81, false, true);
            for (size_t i = count; i < r.input.events.size(); ++i) check(!std::get<3>(r.input.events[i]), "Disable returned sword");
        });
        test("return delivery failure cleans up without retrying while held", [] {
            Run r; r.through(93); r.input.failDownKey = '1'; r.at(94);
            check(r.engine.failed() && r.input.clean(), "Return press failure stranded input");
            Run normal; normal.through(93); normal.at(94); normal.input.failNext = true; normal.at(114);
            check(normal.engine.failed() && normal.input.clean(), "Return release failure stranded the sword");
            const auto count = normal.input.events.size(); normal.at(115);
            check(normal.input.events.size() == count, "Failed return retried while held");
        });
        test("unacknowledged rod cleanup cannot start a sword press", [] {
            Run r; r.through(80); r.input.failCleanup = true;
            const auto count = r.input.events.size(); r.at(81, false, true);
            check(r.engine.failed() && r.input.events.size() == count, "Restore started before rod cleanup succeeded");
            r.input.failCleanup = false; r.engine.cancel(); check(r.input.clean(), "Failed cleanup could not recover");
        });
        test("older config enables return and malformed return falls back safely", [&] {
            { std::ofstream out(path); out << "delay1=123\nreturnToSword=bad\n"; }
            const auto s = loadConfig(path); check(s.returnToSword && s.delay1 == 123, "New option damaged old configuration");
        });
        test("shortcut auto-repeat and rapid press bounce produce one action", [] {
            PressGate gate;
            check(gate.update(true, 0), "First shortcut press was missed");
            for (int ms = 1; ms < 150; ++ms) check(!gate.update(ms % 2 == 0, static_cast<std::int64_t>(ms) * 1000000), "Shortcut bounce produced an action");
            gate.update(false, 150000000);
            check(gate.update(true, 151000000), "Shortcut did not rearm after release/debounce");
            for (int ms = 152; ms < 1000; ++ms) check(!gate.update(true, static_cast<std::int64_t>(ms) * 1000000), "Held shortcut auto-repeated");
            gate.suppress(true); check(!gate.update(true, 2000000000), "Already held shortcut activated after a screen change");
            gate.update(false, 2001000000); check(gate.update(true, 2002000000), "Shortcut release failed to rearm");
        });
        std::filesystem::remove(path);
        std::cout << passed << " test groups passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
