#include "platform/console_input.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>

namespace ic {

namespace {

Modifiers modifiersFrom(DWORD state) {
    Modifiers m;
    m.ctrl = (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    m.shift = (state & SHIFT_PRESSED) != 0;
    m.alt = (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
    return m;
}

}  // namespace

ConsoleInput::~ConsoleInput() {
    restore();
}

bool ConsoleInput::setup() {
    if (active_) return true;
    hIn_ = GetStdHandle(STD_INPUT_HANDLE);
    hOut_ = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD inMode = 0, outMode = 0;
    if (!GetConsoleMode(hIn_, &inMode) || !GetConsoleMode(hOut_, &outMode)) return false;
    savedInputMode_ = inMode;
    savedOutputMode_ = outMode;

    CONSOLE_CURSOR_INFO ci{};
    GetConsoleCursorInfo(hOut_, &ci);
    savedCursorVisible_ = ci.bVisible != FALSE;

    CONSOLE_SCREEN_BUFFER_INFO sbi{};
    GetConsoleScreenBufferInfo(hOut_, &sbi);
    savedBufferW_ = sbi.dwSize.X;
    savedBufferH_ = sbi.dwSize.Y;

    // Raw input: no echo, no line buffering, no Ctrl+C handling, no
    // QuickEdit (it would swallow mouse clicks); mouse and resize events on.
    DWORD mode = inMode;
    mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
    mode |= ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
    SetConsoleMode(hIn_, mode);

    // Frames are written as cell attributes, not VT sequences.
    SetConsoleMode(hOut_, outMode & ~ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    ci.bVisible = FALSE;
    SetConsoleCursorInfo(hOut_, &ci);

    active_ = true;
    fitBufferToWindow();
    return true;
}

void ConsoleInput::restore() {
    if (!active_) return;
    active_ = false;
    SetConsoleMode(hIn_, savedInputMode_);
    SetConsoleMode(hOut_, savedOutputMode_);
    if (savedBufferW_ > 0 && savedBufferH_ > 0) SetConsoleScreenBufferSize(hOut_, {savedBufferW_, savedBufferH_});
    CONSOLE_CURSOR_INFO ci{};
    GetConsoleCursorInfo(hOut_, &ci);
    ci.bVisible = savedCursorVisible_ ? TRUE : FALSE;
    SetConsoleCursorInfo(hOut_, &ci);
}

// The buffer is shrunk to the visible window so there's no scrollbar and
// cell (0,0) is the window's top-left corner.
void ConsoleInput::fitBufferToWindow() {
    CONSOLE_SCREEN_BUFFER_INFO sbi{};
    if (!GetConsoleScreenBufferInfo(hOut_, &sbi)) return;
    width_ = sbi.srWindow.Right - sbi.srWindow.Left + 1;
    height_ = sbi.srWindow.Bottom - sbi.srWindow.Top + 1;
    SetConsoleScreenBufferSize(hOut_, {static_cast<SHORT>(width_), static_cast<SHORT>(height_)});
    if (GetConsoleScreenBufferInfo(hOut_, &sbi)) {
        width_ = sbi.srWindow.Right - sbi.srWindow.Left + 1;
        height_ = sbi.srWindow.Bottom - sbi.srWindow.Top + 1;
        windowLeft_ = sbi.srWindow.Left;
        windowTop_ = sbi.srWindow.Top;
    }
}

std::optional<InputEvent> ConsoleInput::pollResize() {
    CONSOLE_SCREEN_BUFFER_INFO sbi{};
    if (!GetConsoleScreenBufferInfo(hOut_, &sbi)) return std::nullopt;
    const int w = sbi.srWindow.Right - sbi.srWindow.Left + 1;
    const int h = sbi.srWindow.Bottom - sbi.srWindow.Top + 1;
    if (w == width_ && h == height_ && sbi.srWindow.Left == windowLeft_ && sbi.srWindow.Top == windowTop_)
        return std::nullopt;

    fitBufferToWindow();
    InputEvent e;
    e.type = InputType::Resize;
    e.width = width_;
    e.height = height_;
    return e;
}

std::optional<InputEvent> ConsoleInput::waitForEvent(int timeoutMs) {
    if (queue_.empty()) {
        if (WaitForSingleObject(hIn_, static_cast<DWORD>(timeoutMs)) != WAIT_OBJECT_0) return std::nullopt;
        readPending();
    }
    if (queue_.empty()) return std::nullopt;
    InputEvent e = queue_.front();
    queue_.pop_front();
    return e;
}

void ConsoleInput::readPending() {
    DWORD count = 0;
    if (!GetNumberOfConsoleInputEvents(hIn_, &count) || count == 0) return;

    for (DWORD i = 0; i < count; ++i) {
        INPUT_RECORD rec;
        DWORD read = 0;
        if (!ReadConsoleInputW(hIn_, &rec, 1, &read) || read == 0) break;

        if (rec.EventType == KEY_EVENT) {
            const KEY_EVENT_RECORD& k = rec.Event.KeyEvent;
            InputEvent e;
            e.type = k.bKeyDown ? InputType::KeyPress : InputType::KeyRelease;
            e.key = k.wVirtualKeyCode;
            e.mods = modifiersFrom(k.dwControlKeyState);

            // Characters outside the BMP arrive as two UTF-16 halves.
            const wchar_t wc = k.uChar.UnicodeChar;
            if (wc >= 0xD800 && wc <= 0xDBFF) {
                pendingHighSurrogate_ = wc;
                continue;
            }
            if (wc >= 0xDC00 && wc <= 0xDFFF && pendingHighSurrogate_) {
                e.ch = 0x10000 + ((static_cast<char32_t>(pendingHighSurrogate_) - 0xD800) << 10) + (wc - 0xDC00);
                pendingHighSurrogate_ = 0;
            } else {
                e.ch = static_cast<char32_t>(wc);
            }
            const int repeat = k.bKeyDown ? std::max<int>(1, k.wRepeatCount) : 1;
            for (int r = 0; r < repeat; ++r) queue_.push_back(e);
        } else if (rec.EventType == MOUSE_EVENT) {
            const MOUSE_EVENT_RECORD& m = rec.Event.MouseEvent;
            InputEvent e;
            e.mouseX = m.dwMousePosition.X - windowLeft_;
            e.mouseY = m.dwMousePosition.Y - windowTop_;
            e.mods = modifiersFrom(m.dwControlKeyState);
            const bool left = (m.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
            const bool right = (m.dwButtonState & RIGHTMOST_BUTTON_PRESSED) != 0;

            if (m.dwEventFlags & MOUSE_WHEELED) {
                e.type = InputType::MouseScroll;
                e.scroll = static_cast<SHORT>(HIWORD(m.dwButtonState)) > 0 ? 1 : -1;
                queue_.push_back(e);
                continue;
            }
            if (m.dwEventFlags & MOUSE_HWHEELED) continue;

            // Button transitions first (a record can carry both a move and a
            // press/release), each as its own event, in arrival order.
            auto pushButton = [&](bool now, bool& held, MouseButton b) {
                if (now == held) return;
                held = now;
                InputEvent be = e;
                be.type = now ? InputType::MouseDown : InputType::MouseUp;
                be.button = b;
                queue_.push_back(be);
            };
            const size_t before = queue_.size();
            pushButton(left, leftHeld_, MouseButton::Left);
            pushButton(right, rightHeld_, MouseButton::Right);

            if ((m.dwEventFlags & MOUSE_MOVED) && queue_.size() == before) {
                e.type = InputType::MouseMove;
                // Coalesce runs of moves (last position wins) but never move a
                // move past a click: drag end points must stay exact.
                if (!queue_.empty() && queue_.back().type == InputType::MouseMove) queue_.back() = e;
                else queue_.push_back(e);
            }
        } else if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            if (auto r = pollResize()) queue_.push_back(*r);
        }
    }
}

}  // namespace ic
