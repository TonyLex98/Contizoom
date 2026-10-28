#ifndef IC_CONSOLE_INPUT_H
#define IC_CONSOLE_INPUT_H

#include "input/input_event.h"

#include <deque>
#include <optional>

namespace ic {

// Raw keyboard/mouse capture on the Windows console. Translates console
// input records into InputEvents in screen cells; it doesn't interpret
// them. Exactly one instance should own the console session: setup()
// switches the console into raw mode and the destructor restores exactly
// the state that was there before.
class ConsoleInput {
public:
    ConsoleInput() = default;
    ~ConsoleInput();
    ConsoleInput(const ConsoleInput&) = delete;
    ConsoleInput& operator=(const ConsoleInput&) = delete;

    bool setup();
    void restore();

    // Waits up to `timeoutMs` for the next event.
    std::optional<InputEvent> waitForEvent(int timeoutMs = 100);

    // Reports a change of the visible window size, if any, since the last
    // call (the console doesn't always send resize records).
    std::optional<InputEvent> pollResize();

    int width() const { return width_; }
    int height() const { return height_; }

private:
    void* hIn_ = nullptr;
    void* hOut_ = nullptr;
    bool active_ = false;
    unsigned long savedInputMode_ = 0;
    unsigned long savedOutputMode_ = 0;
    bool savedCursorVisible_ = true;
    short savedBufferW_ = 0, savedBufferH_ = 0;

    int width_ = 120;
    int height_ = 40;
    int windowLeft_ = 0;
    int windowTop_ = 0;
    bool leftHeld_ = false;
    bool rightHeld_ = false;
    wchar_t pendingHighSurrogate_ = 0;

    std::deque<InputEvent> queue_;

    void readPending();
    void fitBufferToWindow();
};

}  // namespace ic

#endif  // IC_CONSOLE_INPUT_H
