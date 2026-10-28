#include "test.h"
#include "input/handler.h"
#include "input/history.h"
#include "model/canvas.h"
#include "view/console_renderer.h"

#include <functional>

using namespace ic;

// Undo/redo across every kind of edit, driven through the real input
// handler: each step must undo to exactly the document before it and redo
// to exactly the document after it.

namespace {

const Modifiers kCtrl{true, false, false};
const Modifiers kCtrlShift{true, true, false};

struct Harness {
    Canvas canvas;
    UiState ui;
    ConsoleRenderer renderer;
    InputHandler handler{canvas, ui};
    History probe;  // only for snapshot()

    Harness() { renderer.resize(100, 30); }

    void frame() {
        renderer.render(canvas, ui, HudInfo{});
        handler.setLayout(renderer.screenRects(), renderer.sidebarRects(), renderer.canvasArea(), renderer.editorRect());
    }
    void send(InputEvent e) {
        handler.process(e);
        frame();
    }
    void mouse(InputType t, int x, int y, Modifiers m = {}) {
        InputEvent e;
        e.type = t;
        e.button = MouseButton::Left;
        e.mouseX = x;
        e.mouseY = y;
        e.mods = m;
        send(e);
    }
    void key(int vk, char32_t ch = 0, Modifiers m = {}) {
        InputEvent e;
        e.type = InputType::KeyPress;
        e.key = vk;
        e.ch = ch;
        e.mods = m;
        send(e);
    }
    void type(const std::u32string& s) {
        for (char32_t c : s) key(0, c);
    }
    void undo() { key('Z', 0, kCtrl); }
    void redo() { key('Y', 0, kCtrl); }
    std::string doc() const { return probe.snapshot(canvas); }
    std::pair<int, int> bodyOf(const std::string& uuid) const {
        const CellBox b = cellBox(renderer.screenRects().at(uuid));
        return {b.x + b.w / 2, b.y + b.h / 2};
    }
    void drag(int x0, int y0, int x1, int y1) {
        mouse(InputType::MouseDown, x0, y0);
        mouse(InputType::MouseMove, (x0 + x1) / 2, (y0 + y1) / 2);
        mouse(InputType::MouseMove, x1, y1);
        mouse(InputType::MouseUp, x1, y1);
    }
};

// A, B side by side, C inside A, and a link A -> B.
struct Scene : Harness {
    std::string a, b, c, link;
    Scene() {
        a = canvas.createNode("text of A", {-32, -8, 28, 14}, "A");
        b = canvas.createNode("", {6, -4, 18, 8}, "B");
        c = canvas.createNode("", {3, 4, 10, 4}, "C", a);
        link = canvas.createConnection(a, b, "uses");
        frame();
    }
};

// Runs `action`, then checks that one Ctrl+Z restores the document exactly,
// Ctrl+Y re-applies it exactly, and the status names the step.
void checkUndoable(const char* what, Scene& s, const std::function<void(Scene&)>& action, Msg label) {
    const std::string before = s.doc();
    action(s);
    const std::string after = s.doc();
    if (after == before) {
        ic_test::fail(what, __LINE__, "the action changed nothing");
        return;
    }
    (void)s.handler.takeStatus();
    s.undo();
    if (s.doc() != before) ic_test::fail(what, __LINE__, "undo did not restore the document");
    const std::string status = s.handler.takeStatus();
    if (status != tr(Msg::StatusUndone, {tr(label)})) ic_test::fail(what, __LINE__, "status: " + status);
    s.redo();
    if (s.doc() != after) ic_test::fail(what, __LINE__, "redo did not re-apply the step");
}

}  // namespace

TEST(undo_covers_every_kind_of_edit) {
    struct Case {
        const char* name;
        Msg label;
        std::function<void(Scene&)> action;
    };
    const std::vector<Case> cases = {
        {"move", Msg::StepMove,
         [](Scene& s) {
             const auto [x, y] = s.bodyOf(s.b);
             s.drag(x, y, x + 4, y + 3);
         }},
        {"resize", Msg::StepResize,
         [](Scene& s) {
             s.ui.selected = {s.b};
             s.frame();
             const CellBox box = cellBox(s.renderer.screenRects().at(s.b));
             s.drag(box.right(), box.bottom(), box.right() + 5, box.bottom() + 2);
         }},
        {"new box", Msg::StepNewBox,
         [](Scene& s) {
             s.ui.clearSelection();
             s.key('N');
             s.type(U"Fresh");
             s.key(key::Enter);
         }},
        {"delete", Msg::StepDelete,
         [](Scene& s) {
             s.ui.selected = {s.a};
             s.key('X');
         }},
        {"frame", Msg::StepFrame,
         [](Scene& s) {
             s.ui.selected = {s.a, s.b};
             s.key('F');
         }},
        {"promote", Msg::StepPromote,
         [](Scene& s) {
             s.ui.selected = {s.c};
             s.key(key::Tab);
         }},
        {"demote", Msg::StepDemote,
         [](Scene& s) {
             s.ui.selected = {s.b};
             s.key(key::Tab, 0, Modifiers{false, true, false});
         }},
        {"rename", Msg::StepRename,
         [](Scene& s) {
             s.ui.selected = {s.b};
             s.key(key::F2);
             s.type(U"!");
             s.key(key::Enter);
         }},
        {"text", Msg::StepText,
         [](Scene& s) {
             s.ui.selected = {s.a};
             s.key('E');
             s.type(U" more");
             s.key(key::Enter, 0, kCtrl);
         }},
        {"connect", Msg::StepConnect,
         [](Scene& s) {
             const auto [x0, y0] = s.bodyOf(s.c);
             const auto [x1, y1] = s.bodyOf(s.b);
             s.mouse(InputType::MouseDown, x0, y0, kCtrl);  // Ctrl+drag connects
             s.mouse(InputType::MouseMove, x1, y1, kCtrl);
             s.mouse(InputType::MouseUp, x1, y1, kCtrl);
         }},
        {"connection label", Msg::StepConnRename,
         [](Scene& s) {
             s.ui.selectedConnection = s.link;
             s.key(key::F2);
             s.type(U" a lot");
             s.key(key::Enter);
         }},
        {"connection style", Msg::StepConnStyle,
         [](Scene& s) {
             s.ui.selectedConnection = s.link;
             s.key('T');
         }},
        {"connection reverse", Msg::StepConnReverse,
         [](Scene& s) {
             s.ui.selectedConnection = s.link;
             s.key('V');
         }},
        {"connection delete", Msg::StepConnDelete,
         [](Scene& s) {
             s.ui.selectedConnection = s.link;
             s.key('X');
         }},
    };
    for (const auto& c : cases) {
        Scene s;
        checkUndoable(c.name, s, c.action, c.label);
    }
}

TEST(undo_new_box_and_its_name_are_one_step) {
    Harness h;
    h.frame();
    h.key('N');
    h.type(U"Groceries");
    h.key(key::Enter);
    CHECK_EQ(h.canvas.allNodes().begin()->second->name, std::string("Groceries"));
    h.undo();
    CHECK(h.canvas.allNodes().empty());  // not back to "New box"
    h.redo();
    CHECK_EQ(h.canvas.allNodes().size(), size_t(1));
    CHECK_EQ(h.canvas.allNodes().begin()->second->name, std::string("Groceries"));
}

TEST(undo_renaming_later_is_its_own_step) {
    Harness h;
    h.frame();
    h.key('N');
    h.key(key::Enter);  // keep "New box"
    const std::string id = h.canvas.allNodes().begin()->first;
    h.key(key::F2);
    h.type(U"!");
    h.key(key::Enter);
    h.undo();
    CHECK_EQ(h.canvas.getNode(id)->name, std::string(tr(Msg::DefaultBox)));
    h.undo();
    CHECK(h.canvas.allNodes().empty());
}

TEST(undo_ctrl_z_while_typing_undoes_words_then_the_box) {
    Harness h;
    h.frame();
    h.key('N');
    h.type(U"big plan");  // replaces the "New box" placeholder
    CHECK_EQ(h.ui.nodeEditor.text, std::string("big plan"));
    h.undo();
    CHECK_EQ(h.ui.nodeEditor.text, std::string("big"));  // " plan" was one group
    h.key('Z', 0, kCtrlShift);
    CHECK_EQ(h.ui.nodeEditor.text, std::string("big plan"));  // redo inside the editor
    h.undo();
    h.undo();
    CHECK_EQ(h.ui.nodeEditor.text, std::string(""));  // the typed word
    h.undo();
    CHECK_EQ(h.ui.nodeEditor.text, std::string(tr(Msg::DefaultBox)));  // the deletions
    CHECK(h.ui.nodeEditor.active);
    h.undo();  // nothing typed left: closes the editor and removes the new box
    CHECK(!h.ui.nodeEditor.active);
    CHECK(h.canvas.allNodes().empty());
}

TEST(undo_connection_label_typing) {
    Scene s;
    s.ui.selectedConnection = s.link;
    s.key(key::F2);
    s.type(U" x");
    s.undo();
    CHECK(s.ui.connEditor.active);
    CHECK_EQ(s.ui.connEditor.text, std::string("uses"));
    s.key(key::Enter);
    CHECK_EQ(s.canvas.getConnection(s.link)->name, std::string("uses"));
}

TEST(undo_drag_whose_mouse_up_was_lost_is_still_recorded) {
    Scene s;
    const double x0 = s.canvas.getNode(s.b)->rect.x;
    const auto [x, y] = s.bodyOf(s.b);
    s.mouse(InputType::MouseDown, x, y);
    s.mouse(InputType::MouseMove, x + 6, y);
    // Released outside the window: no MouseUp. The next click ends the drag.
    s.mouse(InputType::MouseDown, 0, 0);
    s.mouse(InputType::MouseUp, 0, 0);
    CHECK_NEAR(s.canvas.getNode(s.b)->rect.x, x0 + 6, 1e-9);
    s.undo();
    CHECK_NEAR(s.canvas.getNode(s.b)->rect.x, x0, 1e-9);
}

TEST(undo_ctrl_z_during_a_drag_ends_it_first) {
    Scene s;
    const double x0 = s.canvas.getNode(s.b)->rect.x;
    const auto [x, y] = s.bodyOf(s.b);
    s.mouse(InputType::MouseDown, x, y);
    s.mouse(InputType::MouseMove, x + 6, y);
    s.undo();  // the drag is finished, recorded, and undone
    CHECK_NEAR(s.canvas.getNode(s.b)->rect.x, x0, 1e-9);
    s.mouse(InputType::MouseMove, x + 9, y);  // no longer dragging
    s.mouse(InputType::MouseUp, x + 9, y);
    CHECK_NEAR(s.canvas.getNode(s.b)->rect.x, x0, 1e-9);
    s.redo();
    CHECK_NEAR(s.canvas.getNode(s.b)->rect.x, x0 + 6, 1e-9);
}

TEST(undo_escape_cancels_a_drag_without_a_step) {
    Scene s;
    const std::string before = s.doc();
    const auto [x, y] = s.bodyOf(s.b);
    s.mouse(InputType::MouseDown, x, y);
    s.mouse(InputType::MouseMove, x + 6, y + 2);
    s.key(key::Escape);
    s.mouse(InputType::MouseUp, x + 6, y + 2);
    CHECK_EQ(s.doc(), before);
    CHECK(!s.handler.canUndo());
}

TEST(undo_selects_what_changed_and_reports_empty_history) {
    Scene s;
    s.undo();
    CHECK_EQ(s.handler.takeStatus(), std::string(tr(Msg::StatusNothingToUndo)));
    s.redo();
    CHECK_EQ(s.handler.takeStatus(), std::string(tr(Msg::StatusNothingToRedo)));

    s.ui.selected = {s.a};
    s.key('X');  // deletes A and C (and the link)
    s.undo();
    CHECK(s.ui.isSelected(s.a));
    CHECK(s.ui.isSelected(s.c));
    CHECK(s.ui.isSelected(s.b));  // the restored link ends at B
}

TEST(undo_does_not_bring_back_an_old_project_name) {
    Canvas c;
    History h;
    c.canvasName = "Old";
    const std::string before = h.snapshot(c);
    c.createNode("", {0, 0, 10, 4}, "x");
    CHECK(h.record(before, c));
    c.canvasName = "New";  // renamed (or saved as) after the edit
    CHECK(h.undo(c));
    CHECK_EQ(c.canvasName, std::string("New"));
    CHECK(c.allNodes().empty());
}

TEST(undo_history_respects_its_memory_budget) {
    Canvas c;
    History h(1000, 4096);
    for (int i = 0; i < 100; ++i) {
        const std::string before = h.snapshot(c);
        c.createNode(std::string(50, 'x'), {double(i) * 30, 0, 20, 4}, "box " + std::to_string(i));
        CHECK(h.record(before, c));
    }
    CHECK(h.bytes() <= 4096 + h.snapshot(c).size());  // the newest step is always kept
    CHECK(h.undoDepth() < 100);
    CHECK(h.undoDepth() >= 1);
    while (h.canUndo()) h.undo(c);
    CHECK(!c.allNodes().empty());  // only the oldest steps were dropped
}
