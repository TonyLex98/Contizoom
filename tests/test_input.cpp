#include "test.h"
#include "input/handler.h"
#include "model/canvas.h"
#include "view/console_renderer.h"

using namespace ic;

namespace {

// Canvas + renderer + handler wired like the app, minus the console.
struct Harness {
    Canvas canvas;
    UiState ui;
    ConsoleRenderer renderer;
    InputHandler handler{canvas, ui};

    Harness() { renderer.resize(100, 30); }

    void frame() {
        renderer.render(canvas, ui, HudInfo{});
        handler.setLayout(renderer.screenRects(), renderer.sidebarRects(), renderer.canvasArea());
    }

    InputResult send(InputEvent e) {
        const InputResult r = handler.process(e);
        frame();
        return r;
    }
    InputResult mouse(InputType t, int x, int y, Modifiers m = {}) {
        InputEvent e;
        e.type = t;
        e.button = MouseButton::Left;
        e.mouseX = x;
        e.mouseY = y;
        e.mods = m;
        return send(e);
    }
    InputResult key(int vk, char32_t ch = 0, Modifiers m = {}) {
        InputEvent e;
        e.type = InputType::KeyPress;
        e.key = vk;
        e.ch = ch;
        e.mods = m;
        return send(e);
    }
    void type(const std::u32string& s) {
        for (char32_t c : s) key(0, c);
    }

    // Screen cell in the middle of a box's body.
    std::pair<int, int> bodyOf(const std::string& uuid) const {
        const CellBox b = cellBox(renderer.screenRects().at(uuid));
        return {b.x + b.w / 2, b.y + b.h / 2};
    }
};

}  // namespace

TEST(input_click_selects_and_drag_moves) {
    Harness h;
    const std::string id = h.canvas.createNode("", {0, 0, 20, 6}, "box");
    h.frame();
    const auto [x, y] = h.bodyOf(id);

    h.mouse(InputType::MouseDown, x, y);
    CHECK(h.ui.isSelected(id));
    h.mouse(InputType::MouseMove, x + 5, y + 2);
    h.mouse(InputType::MouseUp, x + 5, y + 2);
    // Scale 1: one cell = one unit.
    CHECK_NEAR(h.canvas.getNode(id)->rect.x, 5.0, 1e-9);
    CHECK_NEAR(h.canvas.getNode(id)->rect.y, 2.0, 1e-9);
    CHECK(!h.ui.nodeEditor.active);  // a drag is not a click
}

TEST(input_dragging_parent_and_child_moves_child_once) {
    Harness h;
    const std::string p = h.canvas.createNode("", {0, 0, 40, 16}, "p");
    const std::string c = h.canvas.createNode("", {4, 4, 10, 5}, "c", p);
    h.frame();
    h.ui.selected = {p, c};
    const auto [x, y] = h.bodyOf(p);
    h.mouse(InputType::MouseDown, x + 10, y);  // on the parent, away from the child
    h.mouse(InputType::MouseMove, x + 16, y);
    h.mouse(InputType::MouseUp, x + 16, y);
    CHECK_NEAR(h.canvas.getNode(p)->rect.x, 6.0, 1e-9);
    CHECK_NEAR(h.canvas.getNode(c)->rect.x, 4.0, 1e-9);  // relative position unchanged
}

TEST(input_double_click_header_renames_with_unicode) {
    Harness h;
    const std::string id = h.canvas.createNode("", {0, 0, 24, 8}, "old");
    h.frame();
    const CellBox b = cellBox(h.renderer.screenRects().at(id));
    const int x = b.x + 4, y = b.y;  // header row

    h.mouse(InputType::MouseDown, x, y);
    h.mouse(InputType::MouseUp, x, y);
    h.mouse(InputType::MouseDown, x, y);
    h.mouse(InputType::MouseUp, x, y);
    CHECK(h.ui.nodeEditor.active);
    CHECK(h.ui.nodeEditor.editingName);

    for (int i = 0; i < 3; ++i) h.key(key::Backspace);
    h.type(U"Идея");
    h.key(key::Enter);
    CHECK(!h.ui.nodeEditor.active);
    CHECK_EQ(h.canvas.getNode(id)->name, std::string("Идея"));
}

TEST(input_text_editor_handles_newlines_and_escape) {
    Harness h;
    const std::string id = h.canvas.createNode("ab", {0, 0, 24, 8}, "n");
    h.frame();
    h.ui.selected = {id};
    h.key('E');  // edit text
    CHECK(h.ui.nodeEditor.active && !h.ui.nodeEditor.editingName);
    h.key(key::Left);
    h.key(key::Enter);  // split "a|b"
    h.key(key::Enter, 0, Modifiers{true, false, false});  // Ctrl+Enter commits
    CHECK_EQ(h.canvas.getNode(id)->text, std::string("a\nb"));

    h.key('E');
    h.type(U"zzz");
    h.key(key::Escape);  // cancel discards
    CHECK_EQ(h.canvas.getNode(id)->text, std::string("a\nb"));
}

TEST(input_new_box_goes_inside_selection) {
    Harness h;
    const std::string p = h.canvas.createNode("", {0, 0, 40, 16}, "p");
    h.frame();
    h.ui.selected = {p};
    h.key('N');
    CHECK_EQ(h.canvas.getNode(p)->childrenUuids.size(), static_cast<size_t>(1));
    CHECK(h.ui.nodeEditor.active && h.ui.nodeEditor.editingName);  // ready to type a name
}

TEST(input_delete_removes_selected_subtree) {
    Harness h;
    const std::string p = h.canvas.createNode("", {0, 0, 40, 16}, "p");
    h.canvas.createNode("", {4, 4, 10, 5}, "c", p);
    h.frame();
    h.ui.selected = {p};
    h.key('X');
    CHECK(h.canvas.allNodes().empty());
    CHECK(h.ui.selected.empty());
}

TEST(input_keyboard_pan_directions_are_consistent) {
    Harness h;
    h.frame();
    h.key('W');
    CHECK(h.canvas.viewport.centerY < 0);  // camera moves up
    h.key('D');
    CHECK(h.canvas.viewport.centerX > 0);  // camera moves right
}

TEST(input_plus_and_minus_zoom) {
    Harness h;
    h.frame();
    h.key(key::OemPlus, U'+');
    CHECK(h.canvas.viewport.scale > 1.0);
    h.key(key::OemMinus, U'-');
    h.key(key::OemMinus, U'-');
    CHECK(h.canvas.viewport.scale < 1.0);
}

TEST(input_wheel_zoom_keeps_point_under_cursor) {
    Harness h;
    h.frame();
    const CanvasArea a = h.renderer.canvasArea();
    const int mx = a.left + 10, my = a.top + 5;
    const double wx = h.canvas.viewport.centerX + (mx - a.centerX()) / h.canvas.viewport.scale;

    InputEvent e;
    e.type = InputType::MouseScroll;
    e.scroll = 1;
    e.mouseX = mx;
    e.mouseY = my;
    h.send(e);

    const double wxAfter = h.canvas.viewport.centerX + (mx - a.centerX()) / h.canvas.viewport.scale;
    CHECK(h.canvas.viewport.scale > 1.0);
    CHECK_NEAR(wxAfter, wx, 1e-9);
}

TEST(input_drag_from_anchor_creates_connection) {
    Harness h;
    const std::string a = h.canvas.createNode("", {0, 0, 20, 8}, "A");
    const std::string b = h.canvas.createNode("", {40, 0, 20, 8}, "B");
    h.frame();
    const CellBox ba = cellBox(h.renderer.screenRects().at(a));
    int ax = 0, ay = 0;
    anchorCell(ba, Anchor{AnchorEdge::Right, 1}, ax, ay);
    const auto [bx, by] = h.bodyOf(b);

    h.mouse(InputType::MouseDown, ax, ay);
    CHECK_EQ(h.ui.connectionSource, a);
    h.mouse(InputType::MouseMove, bx, by);
    h.mouse(InputType::MouseUp, bx, by);
    CHECK_EQ(h.canvas.allConnections().size(), static_cast<size_t>(1));
    const auto& conn = h.canvas.allConnections().begin()->second;
    CHECK_EQ(conn->fromUuid, a);
    CHECK_EQ(conn->toUuid, b);
    CHECK(conn->fromAnchor == (Anchor{AnchorEdge::Right, 1}));
}

TEST(input_shift_drag_selects_area) {
    Harness h;
    const std::string a = h.canvas.createNode("", {0, 0, 10, 5}, "a");
    const std::string b = h.canvas.createNode("", {30, 0, 10, 5}, "b");
    h.frame();
    const CellBox ba = cellBox(h.renderer.screenRects().at(a));
    const Modifiers shift{false, true, false};
    h.mouse(InputType::MouseDown, ba.x - 2, ba.y - 2 < 1 ? ba.bottom() + 2 : ba.y - 2, shift);
    h.mouse(InputType::MouseMove, ba.right() + 2, ba.y + 1, shift);
    h.mouse(InputType::MouseUp, ba.right() + 2, ba.y + 1, shift);
    CHECK(h.ui.isSelected(a));
    CHECK(!h.ui.isSelected(b));
}

TEST(input_right_click_requests_menu) {
    Harness h;
    h.frame();
    InputEvent e;
    e.type = InputType::MouseDown;
    e.button = MouseButton::Right;
    e.mouseX = 50;
    e.mouseY = 10;
    CHECK(h.handler.process(e) == InputResult::RequestMenu);
}
