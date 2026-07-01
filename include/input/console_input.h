#ifndef IC_CONSOLE_INPUT_H
#define IC_CONSOLE_INPUT_H

#include "input/handler.h"
#include <optional>

namespace ic {

// Pure input capture for Windows console.
// Responsibilities:
//   1. Capture raw keyboard input (no echo, no line buffering)
//   2. Capture raw mouse events (move, click, scroll)
//   3. Detect console resize (buffer or window change) — called every frame
//   4. Return raw screen coordinates (cells, not pixels)
//
// Does NOT:
//   - Handle zoom logic
//   - Handle pan logic
//   - Interpret events
//   - Modify any state

class ConsoleInput {
public:
    ConsoleInput();
    ~ConsoleInput();

    // Initialize console for raw event capture.
    bool setup();

    // Restore console to normal mode
    void restore();

    // Non-blocking read: returns the next pending event or nullopt.
    std::optional<InputEvent> pollEvent();

    // Blocking: waits for input OR resize event. Returns first event available.
    std::optional<InputEvent> waitForEvent();

    // Poll current console dimensions.
    std::optional<InputEvent> pollResize();

    // Update buffer size to match current window
    void updateBufferSize();

    int getWidth()   const { return width_; }
    int getHeight()  const { return height_; }

private:
    int width_ = 120;
    int height_ = 40;
    bool leftHeld_ = false;
    bool rightHeld_ = false;

    void* hIn_ = nullptr;
    void* hOut_ = nullptr;

    struct RawEvent {
        int type;       // 0=key, 1=mouse_move, 2=mouse_down, 3=mouse_up, 4=scroll, 5=resize, 6=key_up
        int key;        // Virtual key code
        wchar_t ch;     // Unicode character (0 if not printable)
        int ext;
        int mouseX;
        int mouseY;
        int button;
        int scrollDelta;
        bool ctrl;
        bool shift;
        bool alt;
        int newWidth;
        int newHeight;
    };

    static constexpr int QUEUE_SIZE = 64;
    RawEvent eventQueue_[QUEUE_SIZE];
    int queueHead_ = 0;
    int queueTail_ = 0;
    int queueCount_ = 0;

    void pushEvent(const RawEvent& e);
    std::optional<RawEvent> popRaw();
    InputEvent convert(const RawEvent& raw);
    void readPendingInput();
};

} // namespace ic

#endif // IC_CONSOLE_INPUT_H
