#include "input/console_input.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <conio.h>
#include <algorithm>

namespace ic {

ConsoleInput::ConsoleInput()
    : hIn_(GetStdHandle(STD_INPUT_HANDLE)),
      hOut_(GetStdHandle(STD_OUTPUT_HANDLE)) {}

ConsoleInput::~ConsoleInput() {
    restore();
}

bool ConsoleInput::setup() {
    // Visible window size from buffer info (srWindow)
    CONSOLE_SCREEN_BUFFER_INFO sbi;
    GetConsoleScreenBufferInfo(hOut_, &sbi);
    width_  = static_cast<int>(sbi.srWindow.Right - sbi.srWindow.Left + 1);
    height_ = static_cast<int>(sbi.srWindow.Bottom - sbi.srWindow.Top + 1);
    windowLeft_ = static_cast<int>(sbi.srWindow.Left);
    windowTop_ = static_cast<int>(sbi.srWindow.Top);

    DWORD mode;
    GetConsoleMode(hIn_, &mode);
    mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT |
              ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS);
    mode |= ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
    SetConsoleMode(hIn_, mode);

    CONSOLE_CURSOR_INFO ci = { 1, FALSE };
    SetConsoleCursorInfo(hOut_, &ci);

    // Match buffer size to window вЂ” no scroll needed, viewport handles scrolling
    SetConsoleScreenBufferSize(hOut_, { static_cast<SHORT>(width_), static_cast<SHORT>(height_) });
    if (GetConsoleScreenBufferInfo(hOut_, &sbi)) {
        width_  = static_cast<int>(sbi.srWindow.Right - sbi.srWindow.Left + 1);
        height_ = static_cast<int>(sbi.srWindow.Bottom - sbi.srWindow.Top + 1);
        windowLeft_ = static_cast<int>(sbi.srWindow.Left);
        windowTop_ = static_cast<int>(sbi.srWindow.Top);
    }

    return true;
}

void ConsoleInput::updateBufferSize() {
    SetConsoleScreenBufferSize(hOut_, { static_cast<SHORT>(width_), static_cast<SHORT>(height_) });
    CONSOLE_SCREEN_BUFFER_INFO sbi;
    if (GetConsoleScreenBufferInfo(hOut_, &sbi)) {
        width_  = static_cast<int>(sbi.srWindow.Right - sbi.srWindow.Left + 1);
        height_ = static_cast<int>(sbi.srWindow.Bottom - sbi.srWindow.Top + 1);
        windowLeft_ = static_cast<int>(sbi.srWindow.Left);
        windowTop_ = static_cast<int>(sbi.srWindow.Top);
    }
}

void ConsoleInput::restore() {
    DWORD mode;
    if (GetConsoleMode(hIn_, &mode)) {
        mode |= ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT;
        mode &= ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);
        SetConsoleMode(hIn_, mode);
    }

    SetConsoleScreenBufferSize(hOut_, { 120, 9999 });

    CONSOLE_CURSOR_INFO ci = { 1, TRUE };
    SetConsoleCursorInfo(hOut_, &ci);
}

std::optional<InputEvent> ConsoleInput::pollEvent() {
    readPendingInput();
    if (queueCount_ == 0) return std::nullopt;
    RawEvent raw = eventQueue_[queueHead_];
    queueHead_ = (queueHead_ + 1) % QUEUE_SIZE;
    queueCount_--;
    return convert(raw);
}

std::optional<InputEvent> ConsoleInput::waitForEvent() {
    // First drain any already-queued events
    auto ev = pollEvent();
    if (ev) return ev;

    // Block until input is available (100ms timeout to allow resize poll)
    DWORD res = WaitForSingleObject(hIn_, 100);
    if (res != WAIT_OBJECT_0) return std::nullopt;

    // Drain all pending events, consolidate MouseMove
    readPendingInput();
    ev = pollEvent();
    return ev;
}

std::optional<InputEvent> ConsoleInput::pollResize() {
    CONSOLE_SCREEN_BUFFER_INFO sbi;
    if (!GetConsoleScreenBufferInfo(hOut_, &sbi)) return std::nullopt;

    int newW = static_cast<int>(sbi.srWindow.Right - sbi.srWindow.Left + 1);
    int newH = static_cast<int>(sbi.srWindow.Bottom - sbi.srWindow.Top + 1);
    int newLeft = static_cast<int>(sbi.srWindow.Left);
    int newTop = static_cast<int>(sbi.srWindow.Top);

    if (newW != width_ || newH != height_ || newLeft != windowLeft_ || newTop != windowTop_) {
        width_ = newW;
        height_ = newH;
        windowLeft_ = newLeft;
        windowTop_ = newTop;
        RawEvent e{};
        e.type = 5;
        e.newWidth = newW;
        e.newHeight = newH;
        return convert(e);
    }
    return std::nullopt;
}

void ConsoleInput::readPendingInput() {
    DWORD count;
    if (!GetNumberOfConsoleInputEvents(hIn_, &count)) return;
    if (count == 0) return;

    // Read ALL pending events, not just one
    // For MouseMove, consolidate into a single event (last position wins)
    RawEvent lastMouseMove{};
    lastMouseMove.type = -1; // Marker: no move yet
    bool hasMouseMove = false;

    for (DWORD i = 0; i < count; i++) {
        INPUT_RECORD rec;
        DWORD read;
        if (!ReadConsoleInputW(hIn_, &rec, 1, &read) || read == 0) break;

        if (rec.EventType == KEY_EVENT) {
            DWORD ck = rec.Event.KeyEvent.dwControlKeyState;
            RawEvent e{};
            e.type = rec.Event.KeyEvent.bKeyDown ? 0 : 6;
            e.key = static_cast<int>(rec.Event.KeyEvent.wVirtualKeyCode);
            e.ch = rec.Event.KeyEvent.uChar.UnicodeChar;
            e.ctrl = (ck & 0x04) != 0;
            e.shift = (ck & 0x01) != 0;
            e.alt = ((ck & 0x02) | (ck & 0x08)) != 0;
            pushEvent(e);
        }
        else if (rec.EventType == MOUSE_EVENT) {
            DWORD ck = rec.Event.MouseEvent.dwControlKeyState;
            RawEvent e{};
            e.mouseX = static_cast<int>(rec.Event.MouseEvent.dwMousePosition.X) - windowLeft_;
            e.mouseY = static_cast<int>(rec.Event.MouseEvent.dwMousePosition.Y) - windowTop_;
            e.ctrl = (ck & 0x04) != 0;
            e.shift = (ck & 0x01) != 0;
            e.alt = ((ck & 0x02) | (ck & 0x08)) != 0;

            DWORD flags = rec.Event.MouseEvent.dwEventFlags;
            DWORD btns = rec.Event.MouseEvent.dwButtonState;
            bool left = (btns & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
            bool right = (btns & RIGHTMOST_BUTTON_PRESSED) != 0;

            if (flags == MOUSE_WHEELED) {
                e.type = 4;
                SHORT delta = GET_WHEEL_DELTA_WPARAM(btns);
                e.scrollDelta = static_cast<int>(delta);
                pushEvent(e);
            }
            else if (flags == MOUSE_MOVED) {
                // Consolidate: only keep the last MouseMove
                lastMouseMove = e;
                lastMouseMove.type = 1;
                hasMouseMove = true;
            }
            else if (left && !leftHeld_) {
                e.type = 2; e.button = 0;
                leftHeld_ = true;
                pushEvent(e);
            }
            else if (right && !rightHeld_) {
                e.type = 2; e.button = 1;
                rightHeld_ = true;
                pushEvent(e);
            }
            else if (!left && leftHeld_) {
                e.type = 3; e.button = 0;
                leftHeld_ = false;
                pushEvent(e);
            }
            else if (!right && rightHeld_) {
                e.type = 3; e.button = 1;
                rightHeld_ = false;
                pushEvent(e);
            }
            else {
                e.type = 3; e.button = 0;
                leftHeld_ = false; rightHeld_ = false;
                pushEvent(e);
            }
        }
        else if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            CONSOLE_SCREEN_BUFFER_INFO sbi;
            if (GetConsoleScreenBufferInfo(hOut_, &sbi)) {
                windowLeft_ = static_cast<int>(sbi.srWindow.Left);
                windowTop_ = static_cast<int>(sbi.srWindow.Top);
            }
            RawEvent e{};
            e.type = 5;
            e.newWidth = static_cast<int>(rec.Event.WindowBufferSizeEvent.dwSize.X);
            e.newHeight = static_cast<int>(rec.Event.WindowBufferSizeEvent.dwSize.Y);
            width_ = e.newWidth;
            height_ = e.newHeight;
            pushEvent(e);
        }
    }

    // Push consolidated MouseMove at the end (latest position)
    if (hasMouseMove) {
        pushEvent(lastMouseMove);
    }
}

void ConsoleInput::pushEvent(const RawEvent& e) {
    if (queueCount_ >= QUEUE_SIZE) return;
    eventQueue_[queueTail_] = e;
    queueTail_ = (queueTail_ + 1) % QUEUE_SIZE;
    queueCount_++;
}

InputEvent ConsoleInput::convert(const RawEvent& raw) {
    InputEvent ev{};
    ev.mods.ctrl = raw.ctrl;
    ev.mods.shift = raw.shift;
    ev.mods.alt = raw.alt;

    switch (raw.type) {
        case 0:
            ev.type = InputType::KeyPress;
            ev.key = static_cast<char>(raw.key);
            ev.ch = raw.ch;
            break;
        case 6:
            ev.type = InputType::KeyRelease;
            ev.key = static_cast<char>(raw.key);
            break;
        case 1:
            ev.type = InputType::MouseMove;
            ev.mouseX = raw.mouseX; ev.mouseY = raw.mouseY;
            break;
        case 2:
            ev.type = InputType::MouseDown;
            ev.mouseX = raw.mouseX; ev.mouseY = raw.mouseY;
            ev.button = raw.button;
            break;
        case 3:
            ev.type = InputType::MouseUp;
            ev.mouseX = raw.mouseX; ev.mouseY = raw.mouseY;
            ev.button = raw.button;
            break;
        case 4:
            ev.type = InputType::MouseScroll;
            ev.mouseX = raw.mouseX; ev.mouseY = raw.mouseY;
            ev.scrollY = (raw.scrollDelta > 0) ? 1.0 : -1.0;
            break;
        case 5:
            ev.type = InputType::Resize;
            ev.width = raw.newWidth; ev.height = raw.newHeight;
            break;
    }
    return ev;
}

} // namespace ic
