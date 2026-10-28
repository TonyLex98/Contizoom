#include "platform/popup_menu.h"
#include "platform/console_input.h"
#include "platform/console_output.h"
#include "i18n/i18n.h"
#include "util/utf8.h"
#include "view/cell_buffer.h"

#include <algorithm>

namespace ic {

namespace {

const uint16_t kFrame = color::make(color::BrightCyan, color::Blue);
const uint16_t kItem = color::make(color::White, color::Blue);
const uint16_t kDisabled = color::make(color::DarkGray, color::Blue);
const uint16_t kHighlight = color::make(color::Black, color::BrightCyan);
const uint16_t kHint = color::make(color::Gray, color::Blue);

void drawFrame(CellBuffer& b, const std::string& title) {
    const int w = b.width(), h = b.height();
    b.fill(0, 0, w, h, L' ', kItem);
    for (int x = 1; x < w - 1; ++x) {
        b.put(x, 0, L'─', kFrame);
        b.put(x, h - 1, L'─', kFrame);
    }
    for (int y = 1; y < h - 1; ++y) {
        b.put(0, y, L'│', kFrame);
        b.put(w - 1, y, L'│', kFrame);
    }
    b.put(0, 0, L'┌', kFrame);
    b.put(w - 1, 0, L'┐', kFrame);
    b.put(0, h - 1, L'└', kFrame);
    b.put(w - 1, h - 1, L'┘', kFrame);
    if (!title.empty()) b.text(2, 0, " " + title + " ", kFrame, w - 4);
}

bool selectable(const MenuItem& m) {
    return m.enabled && !m.separator;
}

}  // namespace

int PopupMenu::choose(const std::string& title, const std::vector<MenuItem>& items, int x, int y, int screenW,
                      int screenH) {
    if (items.empty()) return -1;

    int labelW = utf8::width(title) + 4, hintW = 0;
    for (const auto& m : items) {
        labelW = std::max(labelW, utf8::width(m.label));
        hintW = std::max(hintW, utf8::width(m.hint));
    }
    const int w = std::min(screenW, labelW + (hintW ? hintW + 3 : 0) + 4);
    const int h = std::min(screenH, static_cast<int>(items.size()) + 2);
    x = std::clamp(x, 0, std::max(0, screenW - w));
    y = std::clamp(y, 0, std::max(0, screenH - h));

    int cursor = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        if (selectable(items[i])) {
            cursor = static_cast<int>(i);
            break;
        }
    }

    CellBuffer buf;
    buf.resize(w, h);
    auto redraw = [&] {
        drawFrame(buf, title);
        for (size_t i = 0; i < items.size() && static_cast<int>(i) + 1 < h - 1; ++i) {
            const MenuItem& m = items[i];
            const int row = static_cast<int>(i) + 1;
            if (m.separator) {
                for (int cx = 1; cx < w - 1; ++cx) buf.put(cx, row, L'─', kDisabled);
                buf.put(0, row, L'├', kFrame);
                buf.put(w - 1, row, L'┤', kFrame);
                continue;
            }
            const bool hi = static_cast<int>(i) == cursor;
            const uint16_t attr = !m.enabled ? kDisabled : hi ? kHighlight : kItem;
            buf.fill(1, row, w - 2, 1, L' ', attr);
            buf.text(2, row, m.label, attr, w - 4);
            if (!m.hint.empty()) {
                const int hw = utf8::width(m.hint);
                buf.text(w - 2 - hw, row, m.hint, hi ? kHighlight : kHint, hw);
            }
        }
        output_.presentRegion(buf, x, y);
    };

    auto step = [&](int dir) {
        for (int n = 0, i = cursor; n < static_cast<int>(items.size()); ++n) {
            i = (i + dir + static_cast<int>(items.size())) % static_cast<int>(items.size());
            if (selectable(items[i])) {
                cursor = i;
                return;
            }
        }
    };
    auto itemAt = [&](int mx, int my) -> int {
        const int i = my - y - 1;
        if (mx <= x || mx >= x + w - 1 || i < 0 || i >= static_cast<int>(items.size())) return -1;
        return selectable(items[i]) ? i : -1;
    };

    redraw();
    while (true) {
        auto ev = input_.waitForEvent();
        if (!ev) continue;
        switch (ev->type) {
            case InputType::KeyPress:
                if (ev->key == key::Escape) return -1;
                if (ev->key == key::Up) step(-1);
                else if (ev->key == key::Down || ev->key == key::Tab) step(1);
                else if (ev->key == key::Enter && cursor >= 0) return items[cursor].id;
                break;
            case InputType::MouseMove:
                if (int i = itemAt(ev->mouseX, ev->mouseY); i >= 0) cursor = i;
                break;
            case InputType::MouseDown: {
                const int i = itemAt(ev->mouseX, ev->mouseY);
                if (i >= 0) return items[i].id;
                const bool inside = ev->mouseX >= x && ev->mouseX < x + w && ev->mouseY >= y && ev->mouseY < y + h;
                if (!inside) return -1;  // click outside cancels
                break;
            }
            case InputType::MouseScroll: step(ev->scroll > 0 ? -1 : 1); break;
            case InputType::Resize: return -1;
            default: break;  // key releases and button releases are ignored
        }
        redraw();
    }
}

std::optional<std::string> PopupMenu::prompt(const std::string& title, const std::string& initial, int screenW,
                                             int screenH) {
    const int w = std::min(screenW, std::max(40, utf8::width(title) + 8));
    const int h = 5;
    const int x = std::max(0, (screenW - w) / 2), y = std::max(0, (screenH - h) / 2);

    std::string text = initial;
    int cursor = utf8::length(text);  // in code points
    CellBuffer buf;
    buf.resize(w, h);

    auto redraw = [&] {
        drawFrame(buf, title);
        const int fieldW = w - 4;
        const uint16_t field = color::make(color::White, color::Black);
        buf.fill(2, 1, fieldW, 1, L' ', field);
        // Scroll so the cursor stays visible in long names (widths in cells).
        const int cursorCells = utf8::widthOfPrefix(text, cursor);
        int first = 0;
        while (first < cursor && cursorCells - utf8::widthOfPrefix(text, first) >= fieldW) ++first;
        buf.text(2, 1, text.substr(utf8::byteOffset(text, first)), field, fieldW);
        buf.setAttr(2 + cursorCells - utf8::widthOfPrefix(text, first), 1, color::Inverse);
        buf.text(2, 3, tr(Msg::PromptHelp), kHint, fieldW);
        output_.presentRegion(buf, x, y);
    };

    redraw();
    while (true) {
        auto ev = input_.waitForEvent();
        if (!ev) continue;
        if (ev->type == InputType::Resize) return std::nullopt;
        if (ev->type == InputType::MouseDown) {
            const bool inside = ev->mouseX >= x && ev->mouseX < x + w && ev->mouseY >= y && ev->mouseY < y + h;
            if (!inside) return std::nullopt;
            continue;
        }
        if (ev->type != InputType::KeyPress) continue;

        const int len = utf8::length(text);
        switch (ev->key) {
            case key::Escape: return std::nullopt;
            case key::Enter: return text;
            case key::Backspace:
                if (cursor > 0) utf8::eraseAt(text, --cursor);
                break;
            case key::Delete:
                if (cursor < len) utf8::eraseAt(text, cursor);
                break;
            case key::Left: cursor = std::max(0, cursor - 1); break;
            case key::Right: cursor = std::min(len, cursor + 1); break;
            case key::Home: cursor = 0; break;
            case key::End: cursor = len; break;
            default:
                if (ev->ch >= 0x20 && !ev->mods.ctrl) utf8::insertAt(text, cursor++, ev->ch);
                break;
        }
        redraw();
    }
}

}  // namespace ic
