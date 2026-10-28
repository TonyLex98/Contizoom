#ifndef IC_POPUP_MENU_H
#define IC_POPUP_MENU_H

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ic {

class ConsoleInput;
class ConsoleOutput;

struct MenuItem {
    std::string label;
    std::string hint;  // right-aligned, e.g. a shortcut
    int id = -1;
    bool enabled = true;
    bool separator = false;

    static MenuItem item(int id, std::string label, std::string hint = "", bool enabled = true) {
        MenuItem m;
        m.id = id;
        m.label = std::move(label);
        m.hint = std::move(hint);
        m.enabled = enabled;
        return m;
    }
    static MenuItem line() {
        MenuItem m;
        m.separator = true;
        m.enabled = false;
        return m;
    }
};

// Modal popups drawn over the current frame: a menu (mouse or arrow keys
// + Enter, Esc/click outside to cancel) and a one-line text prompt. Each
// runs its own small event loop and returns when the user decides.
class PopupMenu {
public:
    PopupMenu(ConsoleInput& input, ConsoleOutput& output) : input_(input), output_(output) {}

    // Returns the chosen item's id, or -1 if cancelled. The menu opens at
    // (x, y), moved inward as needed to fit the screen.
    int choose(const std::string& title, const std::vector<MenuItem>& items, int x, int y, int screenW, int screenH);

    // Centered one-line text field; nullopt if cancelled.
    std::optional<std::string> prompt(const std::string& title, const std::string& initial, int screenW, int screenH);

private:
    ConsoleInput& input_;
    ConsoleOutput& output_;
};

}  // namespace ic

#endif  // IC_POPUP_MENU_H
