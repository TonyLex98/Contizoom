#include "app/project_menu.h"
#include "input/console_input.h"
#include <algorithm>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>

namespace ic {

ProjectMenu::ProjectMenu() {}

ProjectAction ProjectMenu::open(ProjectManager& pm, int mouseX, int mouseY) {
    pm_ = &pm;
    mouseX_ = mouseX;
    mouseY_ = mouseY;
    cursorPos_ = 0;
    chosenName_.clear();

    auto items = buildItems();
    cursorPos_ = 0;
    for (size_t i = 0; i < items.size(); i++) {
        if (!items[i].isSeparator_ && !items[i].isDisabled_) {
            break;
        }
        cursorPos_++;
    }

    ConsoleInput input;

    while (true) {
        drawMenu();

        auto evOpt = input.waitForEvent();
        if (!evOpt) continue;

        if (evOpt->type == InputType::KeyPress) {
            if (evOpt->mods.ctrl) {
                char k = static_cast<char>(std::tolower(static_cast<unsigned char>(evOpt->key)));
                if (k == 'q') return ProjectAction::Quit;
                if (k == 's') return ProjectAction::SaveCurrent;
                if (k == 'n') return ProjectAction::CreateNew;
            } else {
                ProjectAction action = handleKey(evOpt->key, false);
                if (action != ProjectAction::None) return action;
            }
        } else if (evOpt->type == InputType::MouseDown ||
                   evOpt->type == InputType::KeyRelease) {
            return ProjectAction::None;
        }
    }
}

void ProjectMenu::drawMenu() {
    ConsoleInput input;
    int vw = input.getWidth();
    int vh = input.getHeight();

    auto items = buildItems();
    int menuH = static_cast<int>(items.size()) + 3;
    int menuW = 20;

    int mx = mouseX_;
    int my = mouseY_;

    if (mx + menuW > vw) mx = vw - menuW - 1;
    if (my + menuH > vh) my = vh - menuH - 1;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx + menuW > vw) mx = vw - menuW;
    if (my + menuH > vh) my = vh - menuH;

#ifdef _WIN32
    void* hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    std::vector<CHAR_INFO> buf(menuW * menuH);
    for (auto& c : buf) { c.Char.UnicodeChar = L' '; c.Attributes = 0x09; }

    auto put = [&](int x, int y, wchar_t ch, uint16_t attr) {
        if (x >= 0 && x < menuW && y >= 0 && y < menuH) {
            buf[y * menuW + x].Char.UnicodeChar = ch;
            buf[y * menuW + x].Attributes = attr;
        }
    };

    auto putStr = [&](int x, int y, const std::string& s, uint16_t attr = 0x09) {
        for (size_t i = 0; i < s.size() && x + static_cast<int>(i) < menuW; i++) {
            put(x + static_cast<int>(i), y, static_cast<wchar_t>(s[i]), attr);
        }
    };

    // Border - top
    put(0, 0, L'\u250C', 0x0F);
    for (int x = 1; x < menuW - 1; x++) put(x, 0, L'\u2500', 0x0F);
    put(menuW - 1, 0, L'\u2510', 0x0F);

    // Border - bottom
    put(0, menuH - 1, L'\u2514', 0x0F);
    for (int x = 1; x < menuW - 1; x++) put(x, menuH - 1, L'\u2500', 0x0F);
    put(menuW - 1, menuH - 1, L'\u2518', 0x0F);

    // Border - sides
    for (int y = 1; y < menuH - 1; y++) {
        put(0, y, L'\u2502', 0x0F);
        put(menuW - 1, y, L'\u2502', 0x0F);
    }

    // Items
    int row = 1;
    int selectableCount = 0;
    std::vector<int> selectableRows;
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].isSeparator_) {
            putStr(1, row, std::string(menuW - 2, '\u2500'), 0x08);
            row++;
            continue;
        }
        selectableRows.push_back(static_cast<int>(selectableCount));
        selectableCount++;

        std::string label = " " + items[i].label;
        if (static_cast<int>(label.size()) > menuW - 2)
            label = label.substr(0, menuW - 4) + "..";

        bool isSelected = (static_cast<int>(selectableRows.size()) - 1 == cursorPos_);
        uint16_t attr;
        if (items[i].isDisabled_) {
            attr = 0x08;
        } else if (isSelected) {
            attr = 0x0E;
        } else {
            attr = 0x0F;
        }

        if (isSelected && !items[i].isDisabled_) {
            label = "\u2192 " + items[i].label;
        } else {
            label = "\u00B7 " + items[i].label;
        }
        if (items[i].isDisabled_) {
            label = "  " + items[i].label;
        }

        if (static_cast<int>(label.size()) > menuW - 2)
            label = label.substr(0, menuW - 4) + "..";

        putStr(1, row, label, attr);
        row++;
    }

    // Footer
    std::string footer = "  Esc:Cancel";
    putStr(1, menuH - 1, footer, 0x08);

    // Write to console
    COORD size = { static_cast<SHORT>(menuW), static_cast<SHORT>(menuH) };
    COORD origin = { static_cast<SHORT>(mx), static_cast<SHORT>(my) };
    SMALL_RECT region = {
        static_cast<SHORT>(mx), static_cast<SHORT>(my),
        static_cast<SHORT>(mx + menuW - 1), static_cast<SHORT>(my + menuH - 1)
    };
    WriteConsoleOutputW(hOut, buf.data(), size, origin, &region);
#endif
}

std::vector<ProjectMenu::MenuItem> ProjectMenu::buildItems() {
    std::vector<MenuItem> items;
    if (!pm_) return items;

    auto active = pm_->activeProject();
    bool hasPath = active && !active->path.empty();
    bool hasActive = !!active;

    { MenuItem m; m.label = "New Project"; m.action_ = ProjectAction::CreateNew; items.push_back(m); }
    { MenuItem m; m.label = "Open Project..."; m.action_ = ProjectAction::OpenExisting; items.push_back(m); }
    { MenuItem m; m.label = ""; m.isSeparator_ = true; items.push_back(m); }
    { MenuItem m; m.label = "Save"; m.isDisabled_ = !hasPath; m.action_ = ProjectAction::SaveCurrent; items.push_back(m); }
    { MenuItem m; m.label = "Save As..."; m.action_ = ProjectAction::SaveAs; items.push_back(m); }
    { MenuItem m; m.label = "Revert"; m.isDisabled_ = !hasPath; m.action_ = ProjectAction::RevertChanges; items.push_back(m); }
    { MenuItem m; m.label = ""; m.isSeparator_ = true; items.push_back(m); }
    { MenuItem m; m.label = "Close Project"; m.isDisabled_ = !hasActive; m.action_ = ProjectAction::CloseCurrent; items.push_back(m); }
    { MenuItem m; m.label = "Close All"; m.action_ = ProjectAction::CloseAll; items.push_back(m); }
    { MenuItem m; m.label = ""; m.isSeparator_ = true; items.push_back(m); }
    { MenuItem m; m.label = "Quit"; m.action_ = ProjectAction::Quit; items.push_back(m); }

    return items;
}

ProjectAction ProjectMenu::handleKey(char key, bool) {
    if (key == 27 || key == '\x1b') return ProjectAction::None;

    auto items = buildItems();

    std::vector<int> selectableRows;
    for (size_t i = 0; i < items.size(); i++) {
        if (!items[i].isSeparator_) {
            selectableRows.push_back(static_cast<int>(i));
        }
    }

    if (selectableRows.empty()) return ProjectAction::None;

    if (key == 'k' || key == 'H' || key == 38) {
        if (cursorPos_ > 0) cursorPos_--;
        return ProjectAction::None;
    }
    if (key == 'j' || key == 'P' || key == 40) {
        if (cursorPos_ < static_cast<int>(selectableRows.size()) - 1) cursorPos_++;
        return ProjectAction::None;
    }
    if (key == '\r') {
        if (cursorPos_ >= 0 && cursorPos_ < static_cast<int>(selectableRows.size())) {
            int idx = selectableRows[cursorPos_];
            if (idx >= 0 && idx < static_cast<int>(items.size())) {
                return items[idx].action_;
            }
        }
        return ProjectAction::None;
    }

    return ProjectAction::None;
}

} // namespace ic
