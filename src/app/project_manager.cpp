#include "app/project_manager.h"
#include "model/canvas.h"
#include "storage/storage.h"
#include "util/clock.h"
#include "util/path.h"
#include "util/uuid.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace ic {
namespace fs = std::filesystem;

namespace {

// Keeps letters (including non-ASCII), digits, '-' and '_'; spaces and dots
// become '_'; characters that are illegal in file names are dropped.
std::string sanitizeName(const std::string& name) {
    std::string out;
    for (const unsigned char ch : name) {
        if (ch >= 0x80 || std::isalnum(ch) || ch == '-' || ch == '_') out += static_cast<char>(ch);
        else if (ch == ' ' || ch == '.') out += '_';
    }
    return out.empty() ? "Untitled" : out;
}

}  // namespace

std::string ProjectInfo::displayPath() const {
    if (path.empty()) return "[not saved]";
    const fs::path p = path::fromUtf8(path);
    return path::toUtf8(p.parent_path().filename() / p.filename());
}

ProjectManager::ProjectManager(std::unique_ptr<IStorage> storage) : storage_(std::move(storage)) {}

ProjectManager::~ProjectManager() {
    flushAll();
}

std::string ProjectManager::defaultProjectsDir() {
    fs::path home;
#ifdef _WIN32
    if (const wchar_t* h = _wgetenv(L"USERPROFILE")) home = h;
#else
    if (const char* h = std::getenv("HOME")) home = h;
#endif
    if (home.empty()) home = ".";
    const fs::path dir = home / ".infinite_canvas" / "projects";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return path::toUtf8(dir);
}

std::string ProjectManager::suggestPath(const std::string& name) {
    const fs::path dir = path::fromUtf8(defaultProjectsDir());
    const std::string base = sanitizeName(name);
    fs::path candidate = dir / path::fromUtf8(base + ".canvas");
    for (int n = 2; fs::exists(candidate); ++n) candidate = dir / path::fromUtf8(base + "_" + std::to_string(n) + ".canvas");
    return path::toUtf8(candidate);
}

ProjectInfo* ProjectManager::find(const std::string& uuid) {
    for (auto& p : projects_) {
        if (p.uuid == uuid) return &p;
    }
    return nullptr;
}

void ProjectManager::setupAutoSave(const std::string& uuid) {
    auto it = canvases_.find(uuid);
    ProjectInfo* info = find(uuid);
    if (it == canvases_.end() || !info) return;

    it->second->onChanged([this, uuid](const std::set<std::string>& changed, bool canvasChanged) {
        ProjectInfo* p = find(uuid);
        auto c = canvases_.find(uuid);
        if (!p || p->path.empty() || c == canvases_.end()) return;  // not saved yet: nothing to auto-save

        bool ok = true;
        if (canvasChanged) {
            // Structural change: rewrite everything so deleted nodes'
            // files are removed too.
            ok = storage_->saveCanvas(*c->second, p->path);
        } else {
            const uint64_t now = clock::unixSeconds();
            for (const auto& nodeUuid : changed) {
                if (auto node = c->second->getNode(nodeUuid)) {
                    node->modifiedAt = now;
                    ok = storage_->saveNode(*node, p->path) && ok;
                }
            }
        }
        if (ok) p->modifiedAt = clock::unixSeconds();
        else autoSaveError_ = "Auto-save failed: " + storage_->lastError();
    });
}

std::string ProjectManager::createProject(const std::string& name) {
    auto canvas = std::make_unique<Canvas>();
    canvas->canvasName = name;
    canvas->createdAt = canvas->modifiedAt = clock::unixSeconds();

    ProjectInfo info;
    info.uuid = Uuid::generate();
    info.name = name;
    info.createdAt = info.modifiedAt = canvas->createdAt;

    canvases_[info.uuid] = std::move(canvas);
    projects_.push_back(info);
    setupAutoSave(info.uuid);
    switchTo(info.uuid);
    return info.uuid;
}

std::string ProjectManager::openProject(const std::string& path) {
    // Already open? Just switch to it.
    for (const auto& p : projects_) {
        std::error_code ec;
        if (!p.path.empty() && fs::equivalent(path::fromUtf8(p.path), path::fromUtf8(path), ec)) {
            switchTo(p.uuid);
            return p.uuid;
        }
    }
    if (!storage_->isValid(path)) {
        lastError_ = "Not a project: " + path;
        return "";
    }
    auto canvas = std::make_unique<Canvas>();
    if (!storage_->loadCanvas(*canvas, path)) {
        lastError_ = storage_->lastError();
        return "";
    }

    ProjectInfo info;
    info.uuid = Uuid::generate();
    info.name = canvas->canvasName.empty() ? path::toUtf8(path::fromUtf8(path).stem()) : canvas->canvasName;
    info.path = path;
    info.createdAt = canvas->createdAt;
    info.modifiedAt = canvas->modifiedAt;

    canvases_[info.uuid] = std::move(canvas);
    projects_.push_back(info);
    setupAutoSave(info.uuid);
    switchTo(info.uuid);
    return info.uuid;
}

bool ProjectManager::closeProject(const std::string& uuid) {
    auto it = canvases_.find(uuid);
    if (it == canvases_.end()) return false;
    it->second->flushChanges();

    canvases_.erase(it);
    projects_.erase(std::remove_if(projects_.begin(), projects_.end(), [&](const ProjectInfo& p) { return p.uuid == uuid; }),
                    projects_.end());
    if (activeUuid_ == uuid) activeUuid_ = projects_.empty() ? "" : projects_.back().uuid;
    return true;
}

void ProjectManager::closeAll() {
    // Collect first: closeProject() mutates projects_.
    std::vector<std::string> ids;
    for (const auto& p : projects_) ids.push_back(p.uuid);
    for (const auto& id : ids) closeProject(id);
}

void ProjectManager::switchTo(const std::string& uuid) {
    if (canvases_.count(uuid)) activeUuid_ = uuid;
}

bool ProjectManager::saveCurrent() {
    ProjectInfo* info = activeProject();
    if (!info) return false;
    if (info->path.empty()) return saveAs(suggestPath(info->name));

    Canvas* canvas = activeCanvas();
    canvas->flushChanges();
    if (!storage_->saveCanvas(*canvas, info->path)) {
        lastError_ = storage_->lastError();
        return false;
    }
    info->modifiedAt = clock::unixSeconds();
    return true;
}

bool ProjectManager::saveAs(const std::string& path) {
    ProjectInfo* info = activeProject();
    Canvas* canvas = activeCanvas();
    if (!info || !canvas) return false;

    if (!storage_->saveCanvas(*canvas, path)) {
        lastError_ = storage_->lastError();
        return false;
    }
    info->path = path;
    info->name = canvas->canvasName.empty() ? info->name : canvas->canvasName;
    info->modifiedAt = clock::unixSeconds();
    // Anything pending is already on disk now.
    canvas->discardPendingChanges();
    setupAutoSave(info->uuid);
    return true;
}

bool ProjectManager::revert() {
    ProjectInfo* info = activeProject();
    Canvas* canvas = activeCanvas();
    if (!info || !canvas || info->path.empty()) return false;

    // Discard pending edits instead of saving them first: that's the point.
    canvas->discardPendingChanges();
    const bool ok = storage_->loadCanvas(*canvas, info->path);
    if (!ok) lastError_ = storage_->lastError();
    return ok;
}

void ProjectManager::processAutoSave() {
    for (auto& [_, c] : canvases_) c->processPendingChanges();
}

void ProjectManager::flushAll() {
    for (auto& [_, c] : canvases_) c->flushChanges();
}

std::string ProjectManager::takeAutoSaveError() {
    std::string e;
    e.swap(autoSaveError_);
    return e;
}

Canvas* ProjectManager::activeCanvas() {
    auto it = canvases_.find(activeUuid_);
    return it != canvases_.end() ? it->second.get() : nullptr;
}

const Canvas* ProjectManager::activeCanvas() const {
    auto it = canvases_.find(activeUuid_);
    return it != canvases_.end() ? it->second.get() : nullptr;
}

ProjectInfo* ProjectManager::activeProject() {
    return find(activeUuid_);
}

const ProjectInfo* ProjectManager::activeProject() const {
    for (const auto& p : projects_) {
        if (p.uuid == activeUuid_) return &p;
    }
    return nullptr;
}

std::vector<std::string> ProjectManager::listSavedProjects() const {
    return storage_->listCanvases(defaultProjectsDir());
}

}  // namespace ic
