#include "test.h"
#include "app/demo.h"
#include "model/canvas.h"
#include "view/console_renderer.h"

using namespace ic;

namespace {

std::string renderAt(Canvas& c, double scale, ConsoleRenderer& r, const UiState& ui = UiState{}) {
    r.resize(100, 30);
    c.viewport.scale = scale;
    r.render(c, ui, HudInfo{});
    return r.buffer().toText();
}

// Occurrences of `glyph` inside the canvas area (not the help row).
size_t countInCanvas(const ConsoleRenderer& r, wchar_t glyph) {
    size_t n = 0;
    const CanvasArea& a = r.canvasArea();
    for (int y = a.top; y <= a.bottom; ++y)
        for (int x = a.left; x <= a.right; ++x) n += r.buffer().charAt(x, y) == glyph;
    return n;
}

}  // namespace

TEST(renderer_demo_snapshot_shows_content_and_chrome) {
    Canvas c;
    buildDemoCanvas(c);
    const std::string text = renderSnapshot(c, 120, 40);
    CHECK(text.find("Welcome to Infinite Canvas") != std::string::npos);
    CHECK(text.find("OUTLINE") != std::string::npos);
    CHECK(text.find("Basics") != std::string::npos);
    CHECK(text.find("N new") != std::string::npos);  // help row
}

TEST(renderer_draws_box_with_unicode_name) {
    Canvas c;
    c.createNode("тело", {0, 0, 30, 8}, "Заголовок");
    c.viewport.centerX = 15;
    c.viewport.centerY = 4;
    ConsoleRenderer r;
    const std::string text = renderAt(c, 1.0, r);
    CHECK(text.find("Заголовок") != std::string::npos);
    CHECK(text.find("тело") != std::string::npos);
    CHECK(text.find("┌") != std::string::npos);
    CHECK_EQ(r.screenRects().size(), static_cast<size_t>(1));
}

TEST(renderer_collapses_subtree_when_zoomed_out) {
    Canvas c;
    const std::string p = c.createNode("", {0, 0, 40, 20}, "Parent");
    c.createNode("", {5, 5, 10, 5}, "ChildName", p);
    c.viewport.centerX = 20;
    c.viewport.centerY = 10;

    ConsoleRenderer r;
    CHECK(renderAt(c, 1.0, r).find("ChildName") != std::string::npos);
    CHECK_EQ(r.screenRects().size(), static_cast<size_t>(2));

    // Far out the parent is a single marker and stands in for its child.
    renderAt(c, 0.02, r);
    CHECK_EQ(r.screenRects().size(), static_cast<size_t>(1));
}

TEST(renderer_keeps_children_inside_parent_border) {
    Canvas c;
    const std::string p = c.createNode("", {0, 0, 40, 20}, "P");
    c.createNode("", {35, 15, 30, 30}, "spills", p);  // larger than the room left
    c.viewport.centerX = 20;
    c.viewport.centerY = 10;
    ConsoleRenderer r;
    renderAt(c, 1.0, r);
    const ScreenRect parent = r.screenRects().at(p);
    for (const auto& [id, sr] : r.screenRects()) {
        if (id == p) continue;
        CHECK(sr.x >= parent.x + 1 - 1e-9);
        CHECK(sr.x + sr.w <= parent.x + parent.w - 1 + 1e-9);
    }
}

TEST(renderer_shows_anchors_only_where_useful) {
    Canvas c;
    const std::string a = c.createNode("", {0, 0, 20, 8}, "A");
    c.createNode("", {40, 0, 20, 8}, "B");
    c.viewport.centerX = 30;
    c.viewport.centerY = 4;
    ConsoleRenderer r;
    renderAt(c, 1.0, r);
    CHECK_EQ(countInCanvas(r, L'•'), static_cast<size_t>(0));

    UiState hover;
    hover.hovered = a;
    renderAt(c, 1.0, r, hover);
    CHECK_EQ(countInCanvas(r, L'•'), static_cast<size_t>(9));  // the hovered box only

    UiState connecting;
    connecting.connectionSource = a;
    renderAt(c, 1.0, r, connecting);
    CHECK_EQ(countInCanvas(r, L'•'), static_cast<size_t>(18));  // every box is a drop target
}

TEST(renderer_text_editor_does_not_hide_children) {
    Canvas c;
    const std::string p = c.createNode("short", {0, 0, 80, 20}, "Parent");
    c.createNode("", {40, 10, 20, 6}, "VisibleChild", p);
    c.viewport.centerX = 40;
    c.viewport.centerY = 10;
    UiState ui;
    ui.nodeEditor.active = true;
    ui.nodeEditor.nodeUuid = p;
    ui.nodeEditor.text = "short";
    ConsoleRenderer r;
    const std::string text = renderAt(c, 1.0, r, ui);
    CHECK(text.find("VisibleChild") != std::string::npos);
    CHECK(r.editorRect().valid());
    CHECK(r.editorRect().w < 80);
}

TEST(renderer_links_hidden_boxes_through_visible_ancestors) {
    Canvas c;
    const std::string p1 = c.createNode("", {0, 0, 40, 20}, "Left");
    const std::string p2 = c.createNode("", {200, 0, 40, 20}, "Right");
    const std::string a = c.createNode("", {5, 5, 4, 3}, "a", p1);
    const std::string b = c.createNode("", {5, 5, 4, 3}, "b", p2);
    c.createConnection(a, b);
    c.viewport.centerX = 120;
    c.viewport.centerY = 10;
    ConsoleRenderer r;
    // Zoomed out so the children collapse into their parents' boxes; the
    // link still shows, now between Left and Right.
    renderAt(c, 0.1, r);
    CHECK(r.screenRects().count(a) == 0);
    CHECK(r.screenRects().count(p1) == 1);
    CHECK(countInCanvas(r, L'►') == 1);
}

TEST(renderer_draws_connection_between_boxes) {
    Canvas c;
    const std::string a = c.createNode("", {0, 0, 20, 8}, "A");
    const std::string b = c.createNode("", {40, 0, 20, 8}, "B");
    c.createConnection(a, b, "next");
    c.viewport.centerX = 30;
    c.viewport.centerY = 4;
    ConsoleRenderer r;
    const std::string text = renderAt(c, 1.0, r);
    CHECK(text.find("►") != std::string::npos);
    CHECK(text.find("next") != std::string::npos);
}
