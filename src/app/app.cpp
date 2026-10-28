#include "app/app.h"
#include "app/demo.h"
#include "app/settings.h"
#include "i18n/i18n.h"
#include "model/canvas.h"
#include "platform/popup_menu.h"
#include "storage/exchange.h"
#include "storage/json_storage.h"
#include "util/clock.h"
#include "util/file.h"
#include "util/path.h"
#include "view/labels.h"

#include <algorithm>
#include <cctype>
#include <ctime>

namespace ic {

namespace {

constexpr uint64_t kStatusTimeoutMs = 5000;

enum MenuId {
    // Project
    kNew = 1,
    kOpen,
    kRecent,
    kOpenPath,
    kSwitch,
    kSave,
    kSaveAs,
    kRenameProject,
    kRevert,
    kImport,
    kExport,
    kClose,
    kUndo,
    kRedo,
    kFit,
    kLanguage,
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
    kConnStyle,         // opens the style submenu
    kStyleBase = 100,   // + ConnType
    kRecentBase = 200,  // + index in the recent list
    kFormatBase = 300,  // + exchange::Format, kFormatPicture for the view picture
};
constexpr int kFormatPicture = 99;

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

// Paths typed or dropped into the prompt may come quoted.
std::string cleanPath(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '"' || s.back() == '\'')) s.pop_back();
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '"' || s[start] == '\'')) ++start;
    return s.substr(start);
}

// "My Plan" -> "My_Plan" for a file name.
std::string fileStem(const std::string& name) {
    std::string out;
    for (const unsigned char c : name) {
        if (c >= 0x80 || std::isalnum(c) || c == '-' || c == '_') out += static_cast<char>(c);
        else if (c == ' ' || c == '.') out += '_';
    }
    return out.empty() ? "export" : out;
}

}  // namespace

App::App() : projects_(std::make_unique<JsonStorage>()) {
    ui_.sidebarWidth = Settings::load().sidebarWidth;
}

App::~App() {
    projects_.flushAll();
}

// ====================================================================
// Startup and the recent list
// ====================================================================

void App::openDemo() {
    // A fixed folder name, whatever the language, so there's only ever one.
    const std::string path = path::toUtf8(path::fromUtf8(ProjectManager::defaultProjectsDir()) / "Welcome.canvas");
    if (!projects_.openProject(path).empty()) return;

    projects_.createProject(tr(Msg::DemoName), path);
    buildDemoCanvas(*projects_.activeCanvas());
    if (!projects_.saveCurrent()) setStatus(tr(Msg::StatusDemoFailed, {projects_.lastError()}), true);
}

void App::openStartupProject() {
    for (const auto& r : Settings::load().existingRecent()) {
        if (!projects_.openProject(r.path).empty()) return;
    }
    openDemo();  // first launch (or every recent project is gone)
}

void App::rememberActive() {
    const ProjectInfo* p = projects_.activeProject();
    if (!p || !p->onDisk) return;
    Settings settings = Settings::load();
    settings.touchRecent(p->path, p->name, clock::unixSeconds());
    settings.save();
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
        ui_.resetKeepingLayout();
        return;
    }
    auto& h = handlers_[p->uuid];
    if (!h) h = std::make_unique<InputHandler>(*c, ui_);
    if (boundProject_ != p->uuid) ui_.resetKeepingLayout();
    handler_ = h.get();
    boundProject_ = p->uuid;
    ui_.prune(*c);
}

void App::afterProjectChange(bool fitView) {
    bindHandler();
    render();  // the camera fit needs the layout of a frame
    if (handler_ && fitView) handler_->fitAll();
    rememberActive();
    if (std::string w = projects_.takeWarning(); !w.empty()) setStatus(w, true);
}

void App::setStatus(const std::string& text, bool error) {
    status_ = text;
    statusIsError_ = error;
    statusTime_ = clock::monotonicMillis();
}

void App::render() {
    HudInfo hud;
    if (const ProjectInfo* p = projects_.activeProject()) hud.projectName = p->name;
    else hud.projectName = tr(Msg::HudNoProject);
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
    // A project that was saved before reopens with its own camera, so the
    // user continues where they left off.
    if (const ProjectInfo* p = projects_.activeProject()) afterProjectChange(!p->onDisk);
    else bindHandler();
    render();
    if (!handler_) showStartPage();
    render();

    running_ = true;
    while (running_) {
        projects_.processAutoSave();
        if (std::string err = projects_.takeAutoSaveError(); !err.empty()) {
            setStatus(tr(Msg::StatusAutoSaveFailed, {err}), true);
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
            showStartPage();
        } else if (ev->type == InputType::KeyPress && ev->mods.ctrl) {
            if (ev->key == 'N') handle(InputResult::RequestNew);
            if (ev->key == 'Q') handle(InputResult::RequestQuit);
        }
        if (running_) render();
    }

    projects_.flushAll();
    rememberActive();  // so the next start opens this project
    input_.restore();
    return 0;
}

void App::handle(InputResult result) {
    switch (result) {
        case InputResult::RequestQuit: running_ = false; break;
        case InputResult::RequestSave:
            if (projects_.saveCurrent()) {
                setStatus(tr(Msg::StatusSaved, {projects_.activeProject()->displayPath()}));
                rememberActive();
            } else {
                setStatus(tr(Msg::StatusSaveFailed, {projects_.lastError()}), true);
            }
            break;
        case InputResult::RequestNew:
            projects_.createProject(tr(Msg::DefaultProject));
            bindHandler();
            setStatus(tr(Msg::StatusNewProject));
            break;
        case InputResult::SidebarNodeClick:
            if (handler_) handler_->zoomToNode(handler_->sidebarClickedUuid());
            break;
        case InputResult::LayoutChanged: {
            Settings settings = Settings::load();
            settings.sidebarWidth = ui_.sidebarWidth;
            settings.save();
            break;
        }
        default: break;
    }
}

// ====================================================================
// Context menus
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
        MenuItem::item(kRename, tr(Msg::MenuRename), "F2", single),
        MenuItem::item(kEditText, tr(Msg::MenuEditText), "E", single),
        MenuItem::item(kNewChild, tr(Msg::MenuNewChildHere), "N"),
        MenuItem::item(kZoomTo, tr(Msg::MenuZoomTo), "Enter"),
        MenuItem::line(),
        MenuItem::item(kFrame, tr(Msg::MenuFrame), "F"),
        MenuItem::item(kPromote, tr(Msg::MenuPromote), "Tab"),
        MenuItem::item(kDemote, tr(Msg::MenuDemote), "Shift+Tab"),
        MenuItem::line(),
        MenuItem::item(kDelete, tr(Msg::MenuDeleteBox), "X"),
        MenuItem::line(),
        MenuItem::item(kProjectMenu, tr(Msg::MenuProjectSub)),
    };
    PopupMenu menu(input_, output_);
    const std::string title = single ? tr(Msg::TitleBox) : tr(Msg::Boxes, {std::to_string(ui_.selected.size())});
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
        MenuItem::item(kConnRename, tr(Msg::MenuRename), "F2"),
        MenuItem::item(kConnStyle, tr(Msg::MenuStyle), "T"),
        MenuItem::item(kConnReverse, tr(Msg::MenuReverse), "V"),
        MenuItem::line(),
        MenuItem::item(kConnDelete, tr(Msg::MenuDelete), "X"),
        MenuItem::line(),
        MenuItem::item(kProjectMenu, tr(Msg::MenuProjectSub)),
    };
    PopupMenu menu(input_, output_);
    switch (menu.choose(tr(Msg::TitleConnection), items, x, y, renderer_.width(), renderer_.height())) {
        case kConnRename: handler_->perform(Action::ConnectionRename); break;
        case kConnReverse: handler_->perform(Action::ConnectionReverse); break;
        case kConnDelete: handler_->perform(Action::ConnectionDelete); break;
        case kConnStyle: {
            auto conn = currentCanvas().getConnection(ui_.selectedConnection);
            if (!conn) break;
            std::vector<MenuItem> styles;
            for (int t = 0; t <= static_cast<int>(ConnType::DashedLine); ++t) {
                const auto type = static_cast<ConnType>(t);
                const std::string label = std::string(styleSample(type)) + "  " + tr(styleLabel(type));
                styles.push_back(MenuItem::item(kStyleBase + t, label, type == conn->type ? tr(Msg::HintCurrent) : ""));
            }
            render();
            const int choice = menu.choose(tr(Msg::TitleStyle), styles, x, y, renderer_.width(), renderer_.height());
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
        items.push_back(MenuItem::item(kNewBoxHere, tr(Msg::MenuNewBoxHere), tr(Msg::HintDblClick)));
        items.push_back(MenuItem::line());
    }
    const std::vector<MenuItem> projectItems = {
        MenuItem::item(kNew, tr(Msg::MenuNewProject), "Ctrl+N"),
        MenuItem::item(kOpen, tr(Msg::MenuOpen)),
        MenuItem::item(kRecent, tr(Msg::MenuRecent)),
        MenuItem::item(kOpenPath, tr(Msg::MenuOpenPath)),
        MenuItem::item(kSwitch, tr(Msg::MenuSwitch), "", projects_.projects().size() > 1),
        MenuItem::line(),
        MenuItem::item(kSave, tr(Msg::MenuSave), "Ctrl+S", open),
        MenuItem::item(kSaveAs, tr(Msg::MenuSaveCopy), "", open),
        MenuItem::item(kRenameProject, tr(Msg::MenuRenameProject), "", open),
        MenuItem::item(kRevert, tr(Msg::MenuRevert), "", open),
        MenuItem::line(),
        MenuItem::item(kImport, tr(Msg::MenuImport)),
        MenuItem::item(kExport, tr(Msg::MenuExportSub), "", open),
        MenuItem::line(),
        MenuItem::item(kClose, tr(Msg::MenuClose), "", open),
        MenuItem::item(kUndo, tr(Msg::MenuUndo), "Ctrl+Z", handler_ && handler_->canUndo()),
        MenuItem::item(kRedo, tr(Msg::MenuRedo), "Ctrl+Y", handler_ && handler_->canRedo()),
        MenuItem::item(kFit, tr(Msg::MenuFit), "0", open),
        MenuItem::item(kLanguage, tr(Msg::MenuLanguage), i18n::nativeName(i18n::language())),
        MenuItem::line(),
        MenuItem::item(kQuit, tr(Msg::MenuQuit), "Ctrl+Q"),
    };
    items.insert(items.end(), projectItems.begin(), projectItems.end());

    PopupMenu menu(input_, output_);
    const std::string title = tr(onCanvas ? Msg::TitleCanvas : Msg::TitleProject);
    switch (menu.choose(title, items, x, y, renderer_.width(), renderer_.height())) {
        case kNewBoxHere: handler_->perform(Action::NewBoxHere); break;
        case kNew: handle(InputResult::RequestNew); break;
        case kOpen: openProjectDialog(x, y); break;
        case kRecent: recentDialog(x, y); break;
        case kOpenPath: openPathDialog(); break;
        case kSwitch: switchProjectDialog(x, y); break;
        case kSave: handle(InputResult::RequestSave); break;
        case kSaveAs: saveAsDialog(); break;
        case kRenameProject: renameDialog(); break;
        case kRevert:
            projects_.flushAll();
            if (projects_.revert()) {
                ui_.resetKeepingLayout();
                if (handler_) handler_->clearHistory();
                setStatus(tr(Msg::StatusReverted));
            } else {
                setStatus(tr(Msg::StatusRevertFailed, {projects_.lastError()}), true);
            }
            break;
        case kImport: importDialog(); break;
        case kExport: exportDialog(x, y); break;
        case kClose:
            rememberActive();
            projects_.closeProject(projects_.activeProject()->uuid);
            bindHandler();
            if (!handler_) {
                render();
                showStartPage();
            }
            break;
        case kUndo: handler_->perform(Action::Undo); break;
        case kRedo: handler_->perform(Action::Redo); break;
        case kFit:
            if (handler_) handler_->fitAll();
            break;
        case kLanguage: languageDialog(x, y); break;
        case kQuit: handle(InputResult::RequestQuit); break;
        default: break;
    }
}

// The screen shown when no project is open: recent projects first, then
// the ways to start something.
void App::showStartPage() {
    const auto recent = Settings::load().existingRecent();
    std::vector<MenuItem> items;
    for (size_t i = 0; i < recent.size() && i < 9; ++i) {
        items.push_back(MenuItem::item(kRecentBase + static_cast<int>(i), recent[i].name, formatTime(recent[i].openedAt)));
    }
    if (recent.empty()) items.push_back(MenuItem::item(0, tr(Msg::RecentNone), "", false));
    items.push_back(MenuItem::line());
    items.push_back(MenuItem::item(kNew, tr(Msg::MenuNewProject), "Ctrl+N"));
    items.push_back(MenuItem::item(kOpen, tr(Msg::MenuOpen)));
    items.push_back(MenuItem::item(kOpenPath, tr(Msg::MenuOpenPath)));
    items.push_back(MenuItem::item(kImport, tr(Msg::MenuImport)));
    items.push_back(MenuItem::item(kLanguage, tr(Msg::MenuLanguage), i18n::nativeName(i18n::language())));
    items.push_back(MenuItem::line());
    items.push_back(MenuItem::item(kQuit, tr(Msg::MenuQuit), "Ctrl+Q"));

    PopupMenu menu(input_, output_);
    const int w = renderer_.width(), h = renderer_.height();
    const int choice = menu.choose(tr(Msg::TitleStart), items, w / 2 - 22, h / 2 - 8, w, h);
    if (choice >= kRecentBase && choice < kRecentBase + static_cast<int>(recent.size())) {
        openPath(recent[static_cast<size_t>(choice - kRecentBase)].path);
        return;
    }
    switch (choice) {
        case kNew: handle(InputResult::RequestNew); break;
        case kOpen: openProjectDialog(w / 2 - 22, h / 2 - 8); break;
        case kOpenPath: openPathDialog(); break;
        case kImport: importDialog(); break;
        case kLanguage: languageDialog(w / 2 - 22, h / 2 - 8); break;
        case kQuit: handle(InputResult::RequestQuit); break;
        default: break;
    }
}

// ====================================================================
// Dialogs
// ====================================================================

bool App::openPath(const std::string& path) {
    if (projects_.openProject(path).empty()) {
        setStatus(tr(Msg::StatusOpenFailed, {projects_.lastError()}), true);
        return false;
    }
    afterProjectChange();
    setStatus(tr(Msg::StatusOpened, {projects_.activeProject()->name}));
    return true;
}

void App::openProjectDialog(int x, int y) {
    const auto list = projects_.listSavedProjects();
    if (list.empty()) {
        setStatus(tr(Msg::StatusNoSaved, {ProjectManager::defaultProjectsDir()}), true);
        return;
    }
    std::vector<MenuItem> items;
    for (size_t i = 0; i < list.size(); ++i) {
        std::string hint = formatTime(list[i].modifiedAt);
        if (list[i].nodeCount >= 0) hint += "  " + tr(Msg::Boxes, {std::to_string(list[i].nodeCount)});
        items.push_back(MenuItem::item(static_cast<int>(i), list[i].name, hint));
    }
    render();  // clear the previous menu
    PopupMenu menu(input_, output_);
    const int choice = menu.choose(tr(Msg::TitleOpen), items, x, y, renderer_.width(), renderer_.height());
    if (choice >= 0) openPath(list[static_cast<size_t>(choice)].path);
}

void App::recentDialog(int x, int y) {
    const auto recent = Settings::load().existingRecent();
    std::vector<MenuItem> items;
    for (size_t i = 0; i < recent.size(); ++i) {
        items.push_back(MenuItem::item(static_cast<int>(i), recent[i].name, formatTime(recent[i].openedAt)));
    }
    if (items.empty()) items.push_back(MenuItem::item(-1, tr(Msg::RecentNone), "", false));
    render();
    PopupMenu menu(input_, output_);
    const int choice = menu.choose(tr(Msg::TitleRecent), items, x, y, renderer_.width(), renderer_.height());
    if (choice >= 0) openPath(recent[static_cast<size_t>(choice)].path);
}

void App::openPathDialog() {
    render();
    PopupMenu menu(input_, output_);
    auto path = menu.prompt(tr(Msg::TitleOpenPath), ProjectManager::defaultProjectsDir() + "\\",
                            renderer_.width(), renderer_.height());
    if (path && !cleanPath(*path).empty()) openPath(cleanPath(*path));
}

void App::importDialog() {
    render();
    PopupMenu menu(input_, output_);
    auto path = menu.prompt(tr(Msg::TitleImport), ProjectManager::defaultExportDir() + "\\", renderer_.width(),
                            renderer_.height());
    if (!path || cleanPath(*path).empty()) return;

    if (projects_.importProject(cleanPath(*path)).empty()) {
        setStatus(tr(Msg::StatusImportFailed, {projects_.lastError()}), true);
        return;
    }
    afterProjectChange();
    setStatus(tr(Msg::StatusImported, {projects_.activeProject()->name,
                                       tr(Msg::Boxes, {std::to_string(currentCanvas().allNodes().size())})}));
}

void App::exportDialog(int x, int y) {
    const ProjectInfo* p = projects_.activeProject();
    if (!p) return;
    const std::vector<MenuItem> items = {
        MenuItem::item(kFormatBase + static_cast<int>(exchange::Format::Markdown), tr(Msg::FormatMarkdown)),
        MenuItem::item(kFormatBase + static_cast<int>(exchange::Format::Opml), tr(Msg::FormatOpml)),
        MenuItem::item(kFormatBase + static_cast<int>(exchange::Format::Bundle), tr(Msg::FormatBundle)),
        MenuItem::item(kFormatBase + static_cast<int>(exchange::Format::PlainText), tr(Msg::FormatPlainText)),
        MenuItem::item(kFormatBase + kFormatPicture, tr(Msg::FormatPicture)),
    };
    render();
    PopupMenu menu(input_, output_);
    const int choice = menu.choose(tr(Msg::TitleExport), items, x, y, renderer_.width(), renderer_.height());
    if (choice < kFormatBase) return;
    const int formatId = choice - kFormatBase;

    const std::string stem = fileStem(p->name);
    const std::string suggested =
        path::toUtf8(path::fromUtf8(ProjectManager::defaultExportDir()) /
                     path::fromUtf8(formatId == kFormatPicture
                                        ? stem + "-view.txt"
                                        : stem + exchange::extension(static_cast<exchange::Format>(formatId))));
    render();
    auto target = menu.prompt(tr(Msg::TitleExportPath), suggested, renderer_.width(), renderer_.height());
    if (!target || cleanPath(*target).empty()) return;
    const std::string out = cleanPath(*target);

    bool ok = false;
    if (formatId == kFormatPicture) {
        render();  // the picture is the canvas as it is on screen, without the menus
        ok = file::write(path::fromUtf8(out), renderer_.buffer().toText());
        if (!ok) setStatus(tr(Msg::StatusExportFailed, {tr(Msg::ErrCannotWrite, {out})}), true);
    } else {
        ok = projects_.exportActive(static_cast<exchange::Format>(formatId), out);
        if (!ok) setStatus(tr(Msg::StatusExportFailed, {projects_.lastError()}), true);
    }
    if (ok) setStatus(tr(Msg::StatusExported, {out}));
}

void App::saveAsDialog() {
    ProjectInfo* p = projects_.activeProject();
    if (!p) return;

    render();
    PopupMenu menu(input_, output_);
    auto name = menu.prompt(tr(Msg::TitleSaveCopy), tr(Msg::CopyName, {p->name}), renderer_.width(),
                            renderer_.height());
    if (!name || name->empty()) return;

    if (projects_.saveAs(*name)) {
        setStatus(tr(Msg::StatusNowIn, {projects_.activeProject()->displayPath()}));
        rememberActive();
    } else {
        setStatus(tr(Msg::StatusSaveFailed, {projects_.lastError()}), true);
    }
}

void App::renameDialog() {
    ProjectInfo* p = projects_.activeProject();
    if (!p) return;
    render();
    PopupMenu menu(input_, output_);
    auto name = menu.prompt(tr(Msg::TitleRenameProject), p->name, renderer_.width(), renderer_.height());
    if (!name || name->empty() || *name == p->name) return;
    projects_.renameActive(*name);
    rememberActive();
    setStatus(tr(Msg::StatusRenamed, {*name}));
}

void App::switchProjectDialog(int x, int y) {
    std::vector<MenuItem> items;
    const auto& list = projects_.projects();
    for (size_t i = 0; i < list.size(); ++i) {
        items.push_back(MenuItem::item(static_cast<int>(i), list[i].name, list[i].displayPath()));
    }
    render();
    PopupMenu menu(input_, output_);
    const int choice = menu.choose(tr(Msg::TitleSwitch), items, x, y, renderer_.width(), renderer_.height());
    if (choice < 0) return;
    projects_.switchTo(list[static_cast<size_t>(choice)].uuid);
    bindHandler();
    rememberActive();
}

void App::languageDialog(int x, int y) {
    std::vector<MenuItem> items;
    for (int l = 0; l < static_cast<int>(i18n::Language::Count); ++l) {
        const auto lang = static_cast<i18n::Language>(l);
        // Each language in its own name, so it can be found in any UI language.
        items.push_back(MenuItem::item(l, i18n::nativeName(lang), lang == i18n::language() ? "●" : ""));
    }
    render();
    PopupMenu menu(input_, output_);
    const int choice = menu.choose(tr(Msg::TitleLanguage), items, x, y, renderer_.width(), renderer_.height());
    if (choice < 0) return;

    const auto lang = static_cast<i18n::Language>(choice);
    i18n::setLanguage(lang);
    Settings settings = Settings::load();
    settings.language = i18n::code(lang);
    settings.save();
    setStatus(tr(Msg::StatusLanguage, {i18n::nativeName(lang)}));
}

}  // namespace ic
