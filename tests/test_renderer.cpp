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
