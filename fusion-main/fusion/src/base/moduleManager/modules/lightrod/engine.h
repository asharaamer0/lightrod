#pragma once
#include <algorithm>
#include <cstdint>

namespace lightrod {
enum class Click { None, Left, Right };
// Windows virtual-key codes. Mouse buttons are legal only for trigger/toggle.
struct Settings {
    bool enabled = true;
    bool returnToSword = true;
    int trigger = 'Z', sword = '1', rod = '2', toggle = 0x77;
    Click swordClick = Click::None, rodClick = Click::Right;
    int hold = 20, sync = 5, delay1 = 69, delay2 = 69;
    int effectiveHold() const {
        return (std::max)(1, (std::min)(hold, (std::min)(delay1, delay2) - sync - 1));
    }
};
struct Input {
    virtual ~Input() = default;
    virtual bool key(int code, bool down) = 0;
    virtual bool mouse(Click button, bool down) = 0;
    virtual bool restoreSword(int code, bool down) { return key(code, down); }
    virtual void releaseAll() = 0;
    virtual bool clean() const = 0;
    virtual std::int64_t afterDispatch(std::int64_t now) { return now; }
};

// Called only by Fusion's worker. Time is steady-clock nanoseconds, injected in
// tests. No sleeps, JNI calls, threads, or inventory/packet mutations live here.
class Engine {
public:
    explicit Engine(Input& input) : input_(input) {}
    bool running() const { return state_ != State::Idle; }
    bool failed() const { return failed_; }
    void cancel() {
        state_ = State::Idle;
        rodSelected_ = false;
        input_.releaseAll();
    }
    void update(std::int64_t now, bool allowed, const Settings& settings, bool safeToRestore = false) {
        if (!allowed || !settings.enabled) {
            // A trigger release may finish selection cleanup in valid gameplay.
            // Menus, focus loss, disable, and errors always use hard cancellation.
            if (safeToRestore && settings.enabled && settings_.returnToSword && !failed_) {
                if (state_ == State::Return) {
                    if (now >= due_) finishReturn(false);
                    return;
                }
                if (rodSelected_) {
                    input_.releaseAll();
                    if (!input_.clean()) { fail(); return; }
                    beginReturn(now);
                    return;
                }
            }
            if (running()) cancel();
            failed_ = false;
            return;
        }
        if (failed_) return; // Retry only after the activation is released.
        if (!running()) {
            // Repeated taps cannot restart a cancelled cycle ahead of its period.
            // Nothing is queued while waiting: the trigger must still be held.
            if (now < nextStart_) return;
            settings_ = settings;
            start_ = now;
            nextStart_ = start_ + ms(settings_.delay1 + settings_.delay2);
            if (!beginPhase(false, now)) return;
        }
        // Transitions with no waiting may share an update. Actual holds always
        // start at the actual press time, even when the worker wakes late.
        for (int transitions = 0; transitions < 8 && now >= due_; ++transitions) {
            if (state_ == State::Sync) {
                if (!input_.mouse(click_, true)) { fail(); return; }
                state_ = State::Hold;
                due_ = input_.afterDispatch(now) + ms(settings_.effectiveHold());
            } else if (state_ == State::Hold) {
                if (click_ != Click::None && !input_.mouse(click_, false)) { fail(); return; }
                if (!input_.key(key_, false)) { fail(); return; }
                if (rodPhase_ && settings_.returnToSword) {
                    if (!beginReturn(now)) return;
                    continue;
                }
                state_ = rodPhase_ ? State::Cycle : State::Rod;
                due_ = start_ + ms(rodPhase_ ? settings_.delay1 + settings_.delay2 : settings_.delay1);
            } else if (state_ == State::Return) {
                if (!finishReturn(true)) return;
            } else if (state_ == State::Rod) {
                if (!beginPhase(true, now)) return;
            } else if (state_ == State::Cycle) {
                const auto period = ms(settings_.delay1 + settings_.delay2);
                start_ += period;
                // Skip missed periods after a stall instead of bursting inputs.
                if (now - start_ >= period) start_ += ((now - start_) / period) * period;
                settings_ = settings;
                nextStart_ = start_ + ms(settings_.delay1 + settings_.delay2);
                if (!beginPhase(false, now)) return;
            } else break;
        }
    }
private:
    enum class State { Idle, Sync, Hold, Rod, Cycle, Return };
    static std::int64_t ms(int value) { return static_cast<std::int64_t>(value) * 1000000; }
    bool beginPhase(bool rod, std::int64_t now) {
        rodPhase_ = rod;
        key_ = rod ? settings_.rod : settings_.sword;
        click_ = rod ? settings_.rodClick : settings_.swordClick;
        if (!input_.key(key_, true)) { fail(); return false; }
        rodSelected_ = rod;
        state_ = click_ == Click::None ? State::Hold : State::Sync;
        due_ = input_.afterDispatch(now) + ms(click_ == Click::None ? settings_.effectiveHold() : settings_.sync);
        return true;
    }
    bool beginReturn(std::int64_t now) {
        if (!input_.restoreSword(settings_.sword, true)) { fail(); return false; }
        rodSelected_ = false;
        state_ = State::Return;
        due_ = input_.afterDispatch(now) + ms(settings_.effectiveHold());
        return true;
    }
    bool finishReturn(bool repeat) {
        if (!input_.restoreSword(settings_.sword, false)) { fail(); return false; }
        state_ = repeat ? State::Cycle : State::Idle;
        due_ = start_ + ms(settings_.delay1 + settings_.delay2);
        return true;
    }
    void fail() { cancel(); failed_ = true; }
    Input& input_;
    Settings settings_;
    State state_ = State::Idle;
    bool rodPhase_ = false, rodSelected_ = false, failed_ = false;
    int key_ = 0;
    Click click_ = Click::None;
    std::int64_t start_ = 0, due_ = 0, nextStart_ = 0;
};
}
