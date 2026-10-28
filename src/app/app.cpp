#include "app/app.h"
#include "app/demo.h"
#include "model/canvas.h"
#include "platform/popup_menu.h"
#include "storage/json_storage.h"
#include "util/clock.h"
#include "util/path.h"

namespace ic {

namespace {

constexpr uint64_t kStatusTimeoutMs = 4000;

enum MenuId {
    kNew = 1,
    kOpen,
    kSave,
    kSaveAs,
    kRevert,
    kSwitch,
    kClose,
    kFit,
    kQuit,
};

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
    if (!projects_.saveAs(path)) setStatus("Could not save the demo: " + projects_.lastError(), true);
}

Canvas& App::currentCanvas() {
    Canvas* c = projects_.activeCanvas();
    return c ? *c : placeholder_;
}

// The handler holds a reference to one canvas; rebuild it whenever the
// active project changes.
void App::bindHandler() {
    Canvas* c = projects_.activeCanvas();
    if (!c) {
        handler_.reset();
    } else if (!handler_ || &handler_->canvas() != c) {
        handler_ = std::make_unique<InputHandler>(*c, ui_);
        ui_ = UiState{};
    }
    ui_.prune(currentCanvas());
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
        hud.unsaved = p->path.empty();
    } else {
        hud.projectName = "No project - right-click for the menu";
    }
    hud.status = status_;
    hud.statusIsError = statusIsError_;

    renderer_.render(currentCanvas(), ui_, hud);
    output_.present(renderer_.buffer());
    if (handler_) handler_->setLayout(renderer_.screenRects(), renderer_.sidebarRects(), renderer_.canvasArea());
}

int App::run() {
    if (!input_.setup()) return 1;
    renderer_.resize(input_.width(), input_.height());
    bindHandler();
    if (handler_ && currentCanvas().allNodes().size() > 0) {
        // First frame establishes the layout the camera fit needs.
        render();
        handler_->fitAll();
    }
    render();

    running_ = true;
    while (running_) {
        projects_.processAutoSave();
        if (std::string err = projects_.takeAutoSaveError(); !err.empty()) {
            setStatus(err, true);
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
            const int mx = ev->mouseX, my = ev->mouseY;
            const InputResult result = handler_->process(*ev);
            if (result == InputResult::RequestMenu) showProjectMenu(mx, my);
            else handle(result);
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
        case InputResult::RequestQuit:
            if (confirmQuit()) running_ = false;
            break;
        case InputResult::RequestSave:
            if (projects_.saveCurrent()) setStatus("Saved to " + projects_.activeProject()->displayPath());
            else setStatus("Save failed: " + projects_.lastError(), true);
            break;
        case InputResult::RequestNew:
            projects_.createProject("Untitled");
            bindHandler();
            setStatus("New project");
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

void App::showProjectMenu(int x, int y) {
    const ProjectInfo* p = projects_.activeProject();
    const bool open = p != nullptr;
    const bool saved = open && !p->path.empty();

    std::vector<MenuItem> items = {
        MenuItem::item(kNew, "New project", "Ctrl+N"),
        MenuItem::item(kOpen, "Open project..."),
        MenuItem::item(kSwitch, "Switch project...", "", projects_.projects().size() > 1),
        MenuItem::line(),
        MenuItem::item(kSave, "Save", "Ctrl+S", open),
        MenuItem::item(kSaveAs, "Save as...", "", open),
        MenuItem::item(kRevert, "Revert to saved", "", saved),
        MenuItem::item(kClose, "Close project", "", open),
        MenuItem::line(),
        MenuItem::item(kFit, "Fit view", "0", open),
        MenuItem::item(kQuit, "Quit", "Ctrl+Q"),
    };

    PopupMenu menu(input_, output_);
    switch (menu.choose("Project", items, x, y, renderer_.width(), renderer_.height())) {
        case kNew: handle(InputResult::RequestNew); break;
        case kOpen: openProjectDialog(x, y); break;
        case kSwitch: switchProjectDialog(x, y); break;
        case kSave: handle(InputResult::RequestSave); break;
        case kSaveAs: saveAsDialog(); break;
        case kRevert:
            if (projects_.revert()) {
                ui_ = UiState{};
                setStatus("Reverted to the saved version");
            } else {
                setStatus("Revert failed: " + projects_.lastError(), true);
            }
            break;
        case kClose:
            projects_.closeProject(p->uuid);
            bindHandler();
            break;
        case kFit:
            if (handler_) handler_->fitAll();
            break;
        case kQuit: handle(InputResult::RequestQuit); break;
        default: break;
    }
}

void App::openProjectDialog(int x, int y) {
    const auto paths = projects_.listSavedProjects();
    if (paths.empty()) {
        setStatus("No saved projects in " + ProjectManager::defaultProjectsDir(), true);
        return;
    }
    std::vector<MenuItem> items;
    for (size_t i = 0; i < paths.size(); ++i) {
        items.push_back(MenuItem::item(static_cast<int>(i), path::toUtf8(path::fromUtf8(paths[i]).stem())));
    }
    render();  // clear the previous menu
    PopupMenu menu(input_, output_);
    const int choice = menu.choose("Open", items, x, y, renderer_.width(), renderer_.height());
    if (choice < 0) return;

    if (projects_.openProject(paths[static_cast<size_t>(choice)]).empty()) {
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
    Canvas* c = projects_.activeCanvas();
    if (!p || !c) return;

    render();
    PopupMenu menu(input_, output_);
    auto name = menu.prompt("Save as - project name", p->name, renderer_.width(), renderer_.height());
    if (!name || name->empty()) return;

    c->canvasName = *name;
    const std::string path = ProjectManager::suggestPath(*name);
    if (projects_.saveAs(path)) setStatus("Saved to " + p->displayPath());
    else setStatus("Save failed: " + projects_.lastError(), true);
}

void App::switchProjectDialog(int x, int y) {
    std::vector<MenuItem> items;
    const auto& list = projects_.projects();
    for (size_t i = 0; i < list.size(); ++i) {
        items.push_back(MenuItem::item(static_cast<int>(i), list[i].name, list[i].path.empty() ? "unsaved" : ""));
    }
    render();
    PopupMenu menu(input_, output_);
    const int choice = menu.choose("Switch to", items, x, y, renderer_.width(), renderer_.height());
    if (choice < 0) return;
    projects_.switchTo(list[static_cast<size_t>(choice)].uuid);
    bindHandler();
}

// Projects that were never saved would be lost on exit; ask first.
bool App::confirmQuit() {
    const ProjectInfo* p = projects_.activeProject();
    if (!p || !p->path.empty() || currentCanvas().allNodes().empty()) return true;

    std::vector<MenuItem> items = {
        MenuItem::item(1, "Save and quit"),
        MenuItem::item(2, "Quit without saving"),
        MenuItem::item(3, "Cancel", "Esc"),
    };
    PopupMenu menu(input_, output_);
    const int w = renderer_.width(), h = renderer_.height();
    switch (menu.choose("\"" + p->name + "\" is not saved", items, w / 2 - 15, h / 2 - 3, w, h)) {
        case 1:
            if (projects_.saveCurrent()) return true;
            setStatus("Save failed: " + projects_.lastError(), true);
            return false;
        case 2: return true;
        default: return false;
    }
}

}  // namespace ic
