#include "app/app.h"
#include "app/demo.h"
#include "model/canvas.h"
#include "platform/popup_menu.h"
#include "storage/json_storage.h"
#include "util/clock.h"
#include "util/path.h"

#include <algorithm>
#include <ctime>

namespace ic {

namespace {

constexpr uint64_t kStatusTimeoutMs = 5000;

enum MenuId {
    // Project
    kNew = 1,
    kOpen,
    kSwitch,
    kSave,
    kSaveAs,
    kRevert,
    kExport,
    kClose,
    kUndo,
    kRedo,
    kFit,
    kQuit,
    kProjectMenu,
    kNewBoxHere,
    // Box
    kRename,
    kEditText,
    kNewChild,
    kZoomTo,
    kFrame,
    kPromote,
    kDemote,
    kDelete,
    // Connection
    kConnRename,
    kConnReverse,
    kConnDelete,
    kConnStyle,      // opens the style submenu
    kStyleBase = 100 // + ConnType
};

const char* styleName(ConnType t) {
    switch (t) {
        case ConnType::Arrow: return "Arrow  ──►";
        case ConnType::DashedArrow: return "Dashed arrow  --►";
        case ConnType::DottedArrow: return "Dotted arrow  ··►";
        case ConnType::Line: return "Line  ───";
        case ConnType::DashedLine: return "Dashed line  ---";
        case ConnType::DottedLine: return "Dotted line  ···";
    }
    return "";
}

std::string formatTime(uint64_t unixSeconds) {
    if (unixSeconds == 0) return "";
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

}  // namespace

App::App() : projects_(std::make_unique<JsonStorage>()) {
    placeholder_.canvasName = "No project open";
}

App::~App() {
    projects_.flushAll();
}

void App::openDemo() {
    const std::string path = path::toUtf8(path::fromUtf8(ProjectManager::defaultProjectsDir()) / "Welcome.canvas");
    if (!projects_.openProject(path).empty()) return;

    projects_.createProject("Welcome");
    buildDemoCanvas(*projects_.activeCanvas());
    if (!projects_.saveCurrent()) setStatus("Could not save the demo: " + projects_.lastError(), true);
}

Canvas& App::currentCanvas() {
    Canvas* c = projects_.activeCanvas();
    return c ? *c : placeholder_;
}

// Each project keeps its own handler (and undo history); switching
// projects swaps the active one and resets the transient UI state.
void App::bindHandler() {
    const ProjectInfo* p = projects_.activeProject();
    Canvas* c = projects_.activeCanvas();

    // Drop handlers of projects that were closed.
    for (auto it = handlers_.begin(); it != handlers_.end();) {
        const bool open = std::any_of(projects_.projects().begin(), projects_.projects().end(),
                                      [&](const ProjectInfo& info) { return info.uuid == it->first; });
        it = open ? std::next(it) : handlers_.erase(it);
    }

    if (!p || !c) {
        handler_ = nullptr;
        boundProject_.clear();
        ui_ = UiState{};
        return;
    }
    auto& h = handlers_[p->uuid];
    if (!h) h = std::make_unique<InputHandler>(*c, ui_);
    if (boundProject_ != p->uuid) ui_ = UiState{};
    handler_ = h.get();
    boundProject_ = p->uuid;
    ui_.prune(*c);
}

void App::setStatus(const std::string& text, bool error) {
    status_ = text;
    statusIsError_ = error;
    statusTime_ = clock::monotonicMillis();
}

void App::render() {
    HudInfo hud;
    if (const ProjectInfo* p = projects_.activeProject()) {
        hud.projectName = p->name;
    } else {
        hud.projectName = "No project - right-click for the menu";
    }
    hud.status = status_;
    hud.statusIsError = statusIsError_;

    renderer_.render(currentCanvas(), ui_, hud);
    output_.present(renderer_.buffer());
    if (handler_) {
        handler_->setLayout(renderer_.screenRects(), renderer_.sidebarRects(), renderer_.canvasArea(),
                            renderer_.editorRect());
    }
}

int App::run() {
    if (!input_.setup()) return 1;
    renderer_.resize(input_.width(), input_.height());
    bindHandler();
    if (handler_ && !currentCanvas().allNodes().empty()) {
        // First frame establishes the layout the camera fit needs.
        render();
        handler_->fitAll();
    }
    if (std::string w = projects_.takeWarning(); !w.empty()) setStatus(w, true);
    render();

    running_ = true;
    while (running_) {
        projects_.processAutoSave();
        if (std::string err = projects_.takeAutoSaveError(); !err.empty()) {
            setStatus(err, true);
            render();
        }
        if (std::string w = projects_.takeWarning(); !w.empty()) {
            setStatus(w, true);
            render();
        }
        if (!status_.empty() && clock::monotonicMillis() - statusTime_ > kStatusTimeoutMs) {
            status_.clear();
            render();
        }
        if (auto r = input_.pollResize()) {
            renderer_.resize(r->width, r->height);
            render();
        }

        auto ev = input_.waitForEvent();
        if (!ev) continue;
        if (ev->type == InputType::KeyRelease) continue;
        if (ev->type == InputType::Resize) {
            renderer_.resize(ev->width, ev->height);
            render();
            continue;
        }

        if (handler_) {
            const InputResult result = handler_->process(*ev);
            if (result == InputResult::RequestMenu) {
                render();  // show what the right-click selected before the menu covers it
                showContextMenu();
            } else {
                handle(result);
            }
        } else if (ev->type == InputType::MouseDown) {
            showProjectMenu(ev->mouseX, ev->mouseY);
        } else if (ev->type == InputType::KeyPress && ev->mods.ctrl) {
            if (ev->key == 'N') handle(InputResult::RequestNew);
            if (ev->key == 'Q') handle(InputResult::RequestQuit);
        }
        if (running_) render();
    }

    projects_.flushAll();
    input_.restore();
    return 0;
}

void App::handle(InputResult result) {
    switch (result) {
        case InputResult::RequestQuit: running_ = false; break;
        case InputResult::RequestSave:
            if (projects_.saveCurrent()) setStatus("Saved to " + projects_.activeProject()->displayPath());
            else setStatus("Save failed: " + projects_.lastError(), true);
            break;
        case InputResult::RequestNew:
            projects_.createProject("Untitled");
            bindHandler();
            setStatus("New project - it saves itself as you work");
            break;
        case InputResult::SidebarNodeClick:
            if (handler_) handler_->zoomToNode(handler_->sidebarClickedUuid());
            break;
        default: break;
    }
}

// ====================================================================
// Menus
// ====================================================================

void App::showContextMenu() {
    const ContextTarget& t = handler_->contextTarget();
    switch (t.kind) {
        case ContextTarget::Kind::Node: showNodeMenu(t.x, t.y); break;
        case ContextTarget::Kind::Connection: showConnectionMenu(t.x, t.y); break;
        case ContextTarget::Kind::Canvas: showProjectMenu(t.x, t.y, true); break;
    }
}

void App::showNodeMenu(int x, int y) {
    const bool single = ui_.selected.size() == 1;
    std::vector<MenuItem> items = {
        MenuItem::item(kRename, "Rename", "F2", single),
        MenuItem::item(kEditText, "Edit text", "E", single),
        MenuItem::item(kNewChild, "New box inside, here", "N"),
        MenuItem::item(kZoomTo, "Zoom to", "Enter"),
        MenuItem::line(),
        MenuItem::item(kFrame, "Wrap in frame", "F"),
        MenuItem::item(kPromote, "Lift out of parent", "Tab"),
        MenuItem::item(kDemote, "Nest into box above", "Shift+Tab"),
        MenuItem::line(),
        MenuItem::item(kDelete, "Delete with contents", "X"),
        MenuItem::line(),
        MenuItem::item(kProjectMenu, "Project..."),
    };
    PopupMenu menu(input_, output_);
    const std::string title = single ? "Box" : std::to_string(ui_.selected.size()) + " boxes";
    switch (menu.choose(title, items, x, y, renderer_.width(), renderer_.height())) {
        case kRename: handler_->perform(Action::Rename); break;
        case kEditText: handler_->perform(Action::EditText); break;
        case kNewChild: handler_->perform(Action::NewBoxHere); break;
        case kZoomTo: handler_->perform(Action::ZoomTo); break;
        case kFrame: handler_->perform(Action::Frame); break;
        case kPromote: handler_->perform(Action::Promote); break;
        case kDemote: handler_->perform(Action::Demote); break;
        case kDelete: handler_->perform(Action::Delete); break;
        case kProjectMenu:
            render();
            showProjectMenu(x, y);
            break;
        default: break;
    }
}

void App::showConnectionMenu(int x, int y) {
    std::vector<MenuItem> items = {
        MenuItem::item(kConnRename, "Rename", "F2"),
        MenuItem::item(kConnStyle, "Style...", "T"),
        MenuItem::item(kConnReverse, "Reverse direction", "V"),
        MenuItem::line(),
        MenuItem::item(kConnDelete, "Delete", "X"),
        MenuItem::line(),
        MenuItem::item(kProjectMenu, "Project..."),
    };
    PopupMenu menu(input_, output_);
    switch (menu.choose("Connection", items, x, y, renderer_.width(), renderer_.height())) {
        case kConnRename: handler_->perform(Action::ConnectionRename); break;
        case kConnReverse: handler_->perform(Action::ConnectionReverse); break;
        case kConnDelete: handler_->perform(Action::ConnectionDelete); break;
        case kConnStyle: {
            auto conn = currentCanvas().getConnection(ui_.selectedConnection);
            if (!conn) break;
            std::vector<MenuItem> styles;
            for (int t = 0; t <= static_cast<int>(ConnType::DashedLine); ++t) {
                const auto type = static_cast<ConnType>(t);
                styles.push_back(MenuItem::item(kStyleBase + t, styleName(type), type == conn->type ? "current" : ""));
            }
            render();
            const int choice = menu.choose("Style", styles, x, y, renderer_.width(), renderer_.height());
            if (choice >= kStyleBase) handler_->setConnectionStyle(static_cast<ConnType>(choice - kStyleBase));
            break;
        }
        case kProjectMenu:
            render();
            showProjectMenu(x, y);
            break;
        default: break;
    }
}

void App::showProjectMenu(int x, int y, bool onCanvas) {
    const bool open = projects_.activeProject() != nullptr;
    std::vector<MenuItem> items;
    if (onCanvas && handler_) {
        items.push_back(MenuItem::item(kNewBoxHere, "New box here", "Dbl-click"));
        items.push_back(MenuItem::line());
    }
    const std::vector<MenuItem> projectItems = {
        MenuItem::item(kNew, "New project", "Ctrl+N"),
        MenuItem::item(kOpen, "Open project..."),
        MenuItem::item(kSwitch, "Switch project...", "", projects_.projects().size() > 1),
        MenuItem::line(),
        MenuItem::item(kSave, "Save now", "Ctrl+S", open),
        MenuItem::item(kSaveAs, "Save a copy as...", "", open),
        MenuItem::item(kRevert, "Revert to last save", "", open),
        MenuItem::item(kExport, "Export as Markdown", "", open),
        MenuItem::item(kClose, "Close project", "", open),
        MenuItem::line(),
        MenuItem::item(kUndo, "Undo", "Ctrl+Z", handler_ && handler_->canUndo()),
        MenuItem::item(kRedo, "Redo", "Ctrl+Y", handler_ && handler_->canRedo()),
        MenuItem::item(kFit, "Fit view", "0", open),
        MenuItem::line(),
        MenuItem::item(kQuit, "Quit", "Ctrl+Q"),
    };
    items.insert(items.end(), projectItems.begin(), projectItems.end());

    PopupMenu menu(input_, output_);
    switch (menu.choose(onCanvas ? "Canvas" : "Project", items, x, y, renderer_.width(), renderer_.height())) {
        case kNewBoxHere: handler_->perform(Action::NewBoxHere); break;
        case kNew: handle(InputResult::RequestNew); break;
        case kOpen: openProjectDialog(x, y); break;
        case kSwitch: switchProjectDialog(x, y); break;
        case kSave: handle(InputResult::RequestSave); break;
        case kSaveAs: saveAsDialog(); break;
        case kRevert:
            projects_.flushAll();
            if (projects_.revert()) {
                ui_ = UiState{};
                if (handler_) handler_->clearHistory();
                setStatus("Reverted to the last saved state");
            } else {
                setStatus("Revert failed: " + projects_.lastError(), true);
            }
            break;
        case kExport: {
            std::string out;
            if (projects_.exportMarkdown(out)) setStatus("Exported to " + out);
            else setStatus("Export failed: " + projects_.lastError(), true);
            break;
        }
        case kClose:
            projects_.closeProject(projects_.activeProject()->uuid);
            bindHandler();
            break;
        case kUndo: handler_->perform(Action::Undo); break;
        case kRedo: handler_->perform(Action::Redo); break;
        case kFit:
            if (handler_) handler_->fitAll();
            break;
        case kQuit: handle(InputResult::RequestQuit); break;
        default: break;
    }
}

void App::openProjectDialog(int x, int y) {
    const auto list = projects_.listSavedProjects();
    if (list.empty()) {
        setStatus("No saved projects in " + ProjectManager::defaultProjectsDir(), true);
        return;
    }
    std::vector<MenuItem> items;
    for (size_t i = 0; i < list.size(); ++i) {
        std::string hint = formatTime(list[i].modifiedAt);
        if (list[i].nodeCount >= 0) hint += "  " + std::to_string(list[i].nodeCount) + " boxes";
        items.push_back(MenuItem::item(static_cast<int>(i), list[i].name, hint));
    }
    render();  // clear the previous menu
    PopupMenu menu(input_, output_);
    const int choice = menu.choose("Open (recent first)", items, x, y, renderer_.width(), renderer_.height());
    if (choice < 0) return;

    if (projects_.openProject(list[static_cast<size_t>(choice)].path).empty()) {
        setStatus("Open failed: " + projects_.lastError(), true);
        return;
    }
    bindHandler();
    render();
    if (handler_) handler_->fitAll();
    setStatus("Opened " + projects_.activeProject()->name);
}

void App::saveAsDialog() {
    ProjectInfo* p = projects_.activeProject();
    if (!p) return;

    render();
    PopupMenu menu(input_, output_);
    auto name = menu.prompt("Save a copy as - name", p->name + " copy", renderer_.width(), renderer_.height());
    if (!name || name->empty()) return;

    if (projects_.saveAs(*name)) setStatus("Now working in " + projects_.activeProject()->displayPath());
    else setStatus("Save failed: " + projects_.lastError(), true);
}

void App::switchProjectDialog(int x, int y) {
    std::vector<MenuItem> items;
    const auto& list = projects_.projects();
    for (size_t i = 0; i < list.size(); ++i) {
        items.push_back(MenuItem::item(static_cast<int>(i), list[i].name, list[i].displayPath()));
    }
    render();
    PopupMenu menu(input_, output_);
    const int choice = menu.choose("Switch to", items, x, y, renderer_.width(), renderer_.height());
    if (choice < 0) return;
    projects_.switchTo(list[static_cast<size_t>(choice)].uuid);
    bindHandler();
}

}  // namespace ic
