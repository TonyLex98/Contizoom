#include "app/demo.h"
#include "i18n/i18n.h"
#include "input/handler.h"
#include "model/canvas.h"
#include "view/console_renderer.h"

namespace ic {

namespace {

std::string box(Canvas& c, const std::string& parent, Msg name, Msg text, Rect r) {
    return c.createNode(text == Msg::Count ? "" : tr(text), r, tr(name), parent);
}

}  // namespace

// The welcome project, in the current UI language.
void buildDemoCanvas(Canvas& c) {
    c.clear();
    c.canvasName = tr(Msg::DemoName);
    const Msg none = Msg::Count;

    const std::string welcome = box(c, "", Msg::DemoTitle, Msg::DemoIntro, {0, 0, 150, 70});

    const std::string basics = box(c, welcome, Msg::DemoBasics, Msg::DemoBasicsText, {4, 6, 64, 18});
    box(c, basics, Msg::DemoTryIt, Msg::DemoTryItText, {4, 9, 28, 6});

    const std::string structure = box(c, welcome, Msg::DemoStructure, Msg::DemoStructureText, {78, 6, 66, 18});
    const std::string a = box(c, structure, Msg::DemoIdea, none, {3, 9, 12, 5});
    const std::string b = box(c, structure, Msg::DemoPlan, none, {27, 9, 12, 5});
    const std::string d = box(c, structure, Msg::DemoDone, none, {51, 9, 12, 5});
    c.createConnection(a, b, tr(Msg::DemoRefine));
    c.createConnection(b, d, tr(Msg::DemoShip));

    const std::string nav = box(c, welcome, Msg::DemoNavigation, Msg::DemoNavigationText, {4, 30, 64, 18});

    const std::string links = box(c, welcome, Msg::Connections, Msg::DemoConnectionsText, {78, 30, 66, 18});
    const std::string from = box(c, links, Msg::DemoFrom, none, {6, 9, 18, 6});
    const std::string to = box(c, links, Msg::DemoTo, none, {40, 9, 18, 6});
    if (auto conn = c.getConnection(c.createConnection(from, to, tr(Msg::DemoDragMe)))) {
        conn->fromAnchor = {AnchorEdge::Right, 1};
        conn->toAnchor = {AnchorEdge::Left, 1};
    }

    box(c, welcome, Msg::DemoProjects, Msg::DemoProjectsText, {4, 52, 140, 10});
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
