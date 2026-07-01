#include "app/app.h"
#include "model/canvas.h"
#include "view/console_renderer.h"
#include "storage/json_storage.h"
#include "input/handler.h"
#include "input/console_input.h"
#include "app/project_manager.h"
#include "app/project_menu.h"
#include <iostream>

namespace ic {

App::App() {
    projectManager_ = std::make_unique<ProjectManager>();
    projectManager_->setStorage(std::make_unique<JsonStorage>());
}

App::~App() {
    // ProjectManager will save dirty projects on destruction
}

bool App::initialize(int viewWidth, int viewHeight) {
    if (!renderer_)
        renderer_ = std::make_unique<ConsoleRenderer>();
    static_cast<ConsoleRenderer*>(renderer_.get())->initialize(viewWidth, viewHeight);

    if (!projectMenu_)
        projectMenu_ = std::make_unique<ProjectMenu>();

    return true;
}

void App::setRenderer(std::unique_ptr<IRenderer> r) { renderer_ = std::move(r); }
void App::setProjectManager(std::unique_ptr<ProjectManager> pm) { projectManager_ = std::move(pm); }

std::string App::createProject(const std::string& name) {
    std::string uuid = projectManager_->createProject(name);
    rebuildInputHandler();
    return uuid;
}

std::string App::openProject(const std::string& path) {
    std::string uuid = projectManager_->openProject(path);
    if (!uuid.empty()) rebuildInputHandler();
    return uuid;
}

bool App::closeProject(const std::string& uuid) {
    bool ok = projectManager_->closeProject(uuid);
    if (ok) rebuildInputHandler();
    return ok;
}

bool App::saveCurrent() {
    return projectManager_->saveCurrent();
}

bool App::saveAs(const std::string& newPath) {
    return projectManager_->saveAs(newPath);
}

bool App::revert() {
    return projectManager_->revert();
}

void App::newCanvas(const std::string& name) {
    createProject(name);
}

Canvas* App::canvas() {
    return projectManager_->activeCanvas();
}

const Canvas* App::canvas() const {
    return projectManager_->activeCanvas();
}

void App::rebuildInputHandler() {
    Canvas* c = canvas();
    if (c) {
        if (!inputHandler_ || &inputHandler_->canvas() != c) {
            inputHandler_ = std::make_unique<InputHandler>(*c);
        }
    } else {
        inputHandler_.reset();
    }
}

void App::showProjectMenu(int mx, int my) {
    if (!projectMenu_) return;

    ProjectAction action = projectMenu_->open(*projectManager_, mx, my);

    switch (action) {
        case ProjectAction::CreateNew: {
            createProject("Untitled");
            break;
        }
        case ProjectAction::OpenExisting: {
            break;
        }
        case ProjectAction::SaveCurrent:
            saveCurrent();
            break;
        case ProjectAction::SaveAs:
            break;
        case ProjectAction::CloseCurrent: {
            auto info = projectManager_->activeProject();
            if (info) closeProject(info->uuid);
            break;
        }
        case ProjectAction::CloseAll:
            projectManager_->closeAll();
            rebuildInputHandler();
            break;
        case ProjectAction::RevertChanges:
            revert();
            break;
        case ProjectAction::Quit:
            quit();
            break;
        case ProjectAction::None:
            break;
    }
}

void App::run() {
    running_ = true;

    ConsoleInput input;
    input.setup();

    int w = input.getWidth();
    int h = input.getHeight();
    static_cast<ConsoleRenderer*>(renderer_.get())->initialize(w, h);
    static_cast<ConsoleRenderer*>(renderer_.get())->clear();
    renderOnce();

    while (running_) {
        auto resizeOpt = input.pollResize();
        if (resizeOpt) {
            w = resizeOpt->width;
            h = resizeOpt->height;
            input.updateBufferSize();
            static_cast<ConsoleRenderer*>(renderer_.get())->initialize(w, h);
            if (inputHandler_) inputHandler_->setViewSize(w, h);
            renderOnce();
        }

        auto evOpt = input.waitForEvent();
        if (!evOpt) continue;

        // Right-click: show context menu
        if (evOpt->type == InputType::MouseDown && evOpt->button == 1) {
            showProjectMenu(evOpt->mouseX, evOpt->mouseY);
            renderOnce();
            continue;
        }

        InputResult res = inputHandler_ ? inputHandler_->process(*evOpt) : InputResult::NotHandled;
        switch (res) {
            case InputResult::RequestQuit:   running_ = false; break;
            case InputResult::RequestSave:   saveCurrent(); break;
            case InputResult::RequestNew:    createProject("Untitled"); break;
            case InputResult::SidebarNodeClick:
                if (inputHandler_) zoomToNode(inputHandler_->sidebarClickedUuid());
                break;
            default: break;
        }

        if (evOpt->type == InputType::Resize) {
            w = evOpt->width;
            h = evOpt->height;
            input.updateBufferSize();
            static_cast<ConsoleRenderer*>(renderer_.get())->initialize(w, h);
            if (inputHandler_) inputHandler_->setViewSize(w, h);
        }

        bool needRender = (evOpt->type != InputType::KeyRelease);
        if (evOpt->type == InputType::MouseMove && inputHandler_ && !inputHandler_->isDragging())
            needRender = false;

        if (needRender) renderOnce();
    }

    input.restore();
    std::cout << "\nExiting...\n";
    if (onQuit_) onQuit_(*this);
}

bool App::processFrame() {
    renderOnce();
    if (onFrame_) onFrame_(*this);
    return running_;
}

void App::renderOnce() {
    Canvas* c = canvas();
    if (c && renderer_) {
        renderer_->render(*c);
        if (auto cr = dynamic_cast<ConsoleRenderer*>(renderer_.get())) {
            if (inputHandler_) {
                inputHandler_->setScreenRects(cr->screenRects());
                inputHandler_->setSidebarNodeRects(cr->sidebarNodeRects());
                inputHandler_->setCanvasArea(cr->canvasArea());
            }
        }
    } else if (renderer_) {
        static_cast<ConsoleRenderer*>(renderer_.get())->clear();
    }
}

void App::zoomToNode(const std::string& uuid) {
    Canvas* c = canvas();
    if (!c) return;
    auto node = c->getNode(uuid);
    if (!node) return;
    c->viewport_.centerX = node->rect.x + node->rect.width / 2.0;
    c->viewport_.centerY = node->rect.y + node->rect.height / 2.0;
    c->viewport_.zoomLevel = node->zoomLevel;
    renderOnce();
}

void App::quit() { running_ = false; }

} // namespace ic
