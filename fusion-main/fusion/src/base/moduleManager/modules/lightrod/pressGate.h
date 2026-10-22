#pragma once
#include <cstdint>

namespace lightrod {
// One shortcut action per physical press, with a short bounce guard. Separate
// instances keep Insert and the enable binding independent.
class PressGate {
public:
    explicit PressGate(std::int64_t debounceNs = 150000000) : debounce_(debounceNs) {}
    bool update(bool down, std::int64_t now, bool eligible = true) {
        const bool edge = down && !previous_;
        previous_ = down;
        if (!edge || !eligible || now < next_) return false;
        next_ = now + debounce_;
        return true;
    }
    void suppress(bool down) { previous_ = down; }
private:
    bool previous_ = false;
    std::int64_t debounce_, next_ = 0;
};
}
