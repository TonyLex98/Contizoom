#ifndef IC_INPUT_EVENT_H
#define IC_INPUT_EVENT_H

namespace ic {

enum class InputType { KeyPress, KeyRelease, MouseMove, MouseDown, MouseUp, MouseScroll, Resize, Quit };

enum class MouseButton { None, Left, Right, Middle };

struct Modifiers {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

// Virtual key codes (the Windows VK_* values, which also match ASCII for
// letters and digits). Letter shortcuts use these so they work with any
// keyboard layout; typed text uses InputEvent::ch instead.
namespace key {
constexpr int Backspace = 0x08, Tab = 0x09, Enter = 0x0D, Escape = 0x1B, Space = 0x20;
constexpr int End = 0x23, Home = 0x24, Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28, Delete = 0x2E;
constexpr int Add = 0x6B, Subtract = 0x6D, OemPlus = 0xBB, OemMinus = 0xBD;
}  // namespace key

// A raw input event in screen coordinates (console cells).
struct InputEvent {
    InputType type = InputType::KeyPress;
    Modifiers mods;
    int key = 0;        // virtual key code
    char32_t ch = 0;    // typed character, 0 if none
    int mouseX = 0;
    int mouseY = 0;
    MouseButton button = MouseButton::None;
    int scroll = 0;     // +1 = wheel up (zoom in), -1 = wheel down
    int width = 0;      // Resize
    int height = 0;
};

}  // namespace ic

#endif  // IC_INPUT_EVENT_H
