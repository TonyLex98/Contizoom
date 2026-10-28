#include "app/demo.h"
#include "input/handler.h"
#include "model/canvas.h"
#include "view/console_renderer.h"

namespace ic {

namespace {

std::string box(Canvas& c, const std::string& parent, const std::string& name, const std::string& text, Rect r) {
    return c.createNode(text, r, name, parent);
}

}  // namespace

void buildDemoCanvas(Canvas& c) {
    c.clear();
    c.canvasName = "Welcome";

    const std::string welcome = box(c, "", "Welcome to Infinite Canvas",
                                    "A zoomable outline: boxes inside boxes, as deep as you like.\n"
                                    "Scroll to zoom into any box; its contents unfold as you get closer.",
                                    {0, 0, 150, 70});

    const std::string basics = box(c, welcome, "Basics",
                                   "N          new box (inside the selected one)\n"
                                   "Dbl-click  edit text; on the header: rename (F2)\n"
                                   "Drag       move   Corner   resize\n"
                                   "X / Del    delete with everything inside",
                                   {4, 6, 64, 18});
    box(c, basics, "Try it", "Select me and press N", {4, 9, 28, 6});

    const std::string structure = box(c, welcome, "Structure",
                                      "Tab        lift a box out of its parent\n"
                                      "Shift+Tab  nest it into the box above\n"
                                      "F          wrap the selection in a frame",
                                      {78, 6, 66, 18});
    const std::string a = box(c, structure, "Idea", "", {3, 9, 12, 5});
    const std::string b = box(c, structure, "Plan", "", {27, 9, 12, 5});
    const std::string d = box(c, structure, "Done", "", {51, 9, 12, 5});
    c.createConnection(a, b, "refine");
    c.createConnection(b, d, "ship");

    const std::string nav = box(c, welcome, "Navigation",
                                "Wheel / + -  zoom      WASD / arrows  pan\n"
                                "0            fit everything on screen\n"
                                "Enter        zoom to the selected box\n"
                                "Sidebar      click a box in the outline to jump there",
                                {4, 30, 64, 18});

    const std::string links = box(c, welcome, "Connections",
                                  "Hover a box, drag from one of its • to another box.\n"
                                  "Click a line: T style, V reverse, F2 name, X delete.",
                                  {78, 30, 66, 18});
    const std::string from = box(c, links, "From", "", {6, 9, 18, 6});
    const std::string to = box(c, links, "To", "", {40, 9, 18, 6});
    if (auto conn = c.getConnection(c.createConnection(from, to, "drag me"))) {
        conn->fromAnchor = {AnchorEdge::Right, 1};
        conn->toAnchor = {AnchorEdge::Left, 1};
    }

    box(c, welcome, "Projects",
        "Every edit is saved automatically; Ctrl+Z / Ctrl+Y undo and redo.\n"
        "Right-click a box, a line or empty space for its menu (open, copy, export...).",
        {4, 52, 140, 10});
    c.createConnection(nav, links, "", ConnType::DashedLine);

    c.autoLayout();
    c.discardPendingChanges();
}

std::string renderSnapshot(Canvas& canvas, int width, int height) {
    ConsoleRenderer renderer;
    renderer.resize(width, height);
    UiState ui;
    InputHandler handler(canvas, ui);
    handler.setLayout({}, {}, renderer.canvasArea());
    handler.fitAll();

    HudInfo hud;
    hud.projectName = canvas.canvasName;
    renderer.render(canvas, ui, hud);
    return renderer.buffer().toText();
}

}  // namespace ic
