#include "app/project_manager.h"
#include "i18n/i18n.h"
#include "model/canvas.h"
#include "util/clock.h"
#include "util/file.h"
#include "util/path.h"
#include "util/process.h"
#include "util/uuid.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace ic {
namespace fs = std::filesystem;

namespace {

const char* kLockFile = ".lock";

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

bool samePath(const std::string& a, const std::string& b) {
    std::error_code ec;
    if (fs::equivalent(path::fromUtf8(a), path::fromUtf8(b), ec)) return true;
    return fs::absolute(path::fromUtf8(a), ec).lexically_normal() == fs::absolute(path::fromUtf8(b), ec).lexically_normal();
}

}  // namespace

std::string ProjectInfo::displayPath() const {
    const fs::path p = path::fromUtf8(path);
    return path::toUtf8(p.parent_path().filename() / p.filename());
}

ProjectManager::ProjectManager(std::unique_ptr<IStorage> storage)
    : storage_(std::move(storage)), lockToken_(Uuid::generate()) {}

ProjectManager::~ProjectManager() {
    flushAll();
    for (const auto& p : projects_) releaseLock(p);
}

namespace {

fs::path homeDir() {
    fs::path home;
#ifdef _WIN32
    if (const wchar_t* h = _wgetenv(L"USERPROFILE")) home = h;
#else
    if (const char* h = std::getenv("HOME")) home = h;
#endif
    return home.empty() ? fs::path(".") : home;
}

}  // namespace

std::string ProjectManager::defaultExportDir() {
    std::error_code ec;
    const fs::path docs = homeDir() / "Documents";
    return path::toUtf8(fs::is_directory(docs, ec) ? docs : homeDir());
}

std::string ProjectManager::defaultProjectsDir() {
    const fs::path dir = homeDir() / ".infinite_canvas" / "projects";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return path::toUtf8(dir);
}

std::string ProjectManager::suggestPath(const std::string& name) const {
    const fs::path dir = path::fromUtf8(defaultProjectsDir());
    const std::string base = sanitizeName(name);
    auto taken = [&](const fs::path& p) {
        std::error_code ec;
        if (fs::exists(p, ec)) return true;
        const std::string s = path::toUtf8(p);
        return std::any_of(projects_.begin(), projects_.end(), [&](const ProjectInfo& o) { return samePath(o.path, s); });
    };
    fs::path candidate = dir / path::fromUtf8(base + ".canvas");
    for (int n = 2; taken(candidate); ++n) candidate = dir / path::fromUtf8(base + "_" + std::to_string(n) + ".canvas");
    return path::toUtf8(candidate);
}

ProjectInfo* ProjectManager::find(const std::string& uuid) {
    for (auto& p : projects_) {
        if (p.uuid == uuid) return &p;
    }
    return nullptr;
}

// ====================================================================
// Locks
// ====================================================================

namespace {

// Lock file content: "<pid> <session token>".
bool readLock(const fs::path& lock, unsigned long& pid, std::string& token) {
    std::string text;
    if (!file::read(lock, text)) return false;
    char* end = nullptr;
    pid = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str()) return false;
    token = std::string(end);
    token.erase(0, token.find_first_not_of(" \t"));
    token.erase(token.find_last_not_of(" \t\r\n") + 1);
    return !token.empty();
}

}  // namespace

void ProjectManager::acquireLock(const ProjectInfo& info) {
    const fs::path lock = path::fromUtf8(info.path) / kLockFile;
    unsigned long pid = 0;
    std::string token;
    if (readLock(lock, pid, token) && token != lockToken_ && process::isAlive(pid)) {
        warning_ = tr(Msg::StatusLocked, {info.name, std::to_string(pid)});
    }
    std::error_code ec;
    if (!fs::is_directory(lock.parent_path(), ec)) return;  // not on disk yet
    file::write(lock, std::to_string(process::currentId()) + " " + lockToken_ + "\n");
}

void ProjectManager::releaseLock(const ProjectInfo& info) {
    if (info.path.empty()) return;
    const fs::path lock = path::fromUtf8(info.path) / kLockFile;
    unsigned long pid = 0;
    std::string token;
    std::error_code ec;
    if (readLock(lock, pid, token) && token == lockToken_) fs::remove(lock, ec);
}

// ====================================================================
// Auto-save
// ====================================================================

bool ProjectManager::writeFull(ProjectInfo& info, Canvas& canvas) {
    canvas.discardPendingChanges();
    if (!storage_->saveCanvas(canvas, info.path)) {
        lastError_ = storage_->lastError();
        return false;
    }
    const bool first = !info.onDisk;
    info.onDisk = true;
    info.modifiedAt = clock::unixSeconds();
    if (first) acquireLock(info);
    return true;
}

void ProjectManager::setupAutoSave(const std::string& uuid) {
    auto it = canvases_.find(uuid);
    if (it == canvases_.end() || !find(uuid)) return;

    it->second->onChanged([this, uuid](const Canvas::ChangeSet& changes) {
        ProjectInfo* p = find(uuid);
        auto c = canvases_.find(uuid);
        if (!p || c == canvases_.end()) return;
        Canvas& canvas = *c->second;

        // First write, or wholesale change (undo/redo): everything.
        if (!p->onDisk || changes.full) {
            if (!p->onDisk && canvas.allNodes().empty() && canvas.allConnections().empty()) return;
            if (!writeFull(*p, canvas)) autoSaveError_ = lastError_;
            return;
        }

        bool ok = true;
        const uint64_t now = clock::unixSeconds();
        for (const auto& nodeUuid : changes.nodes) {
            if (auto node = canvas.getNode(nodeUuid)) {
                node->modifiedAt = now;
                ok = storage_->saveNode(*node, p->path) && ok;
            }
        }
        for (const auto& nodeUuid : changes.removed) ok = storage_->removeNode(nodeUuid, p->path) && ok;
        // config.json is small; refreshing it keeps modified_at and the
        // node count current for the Open list.
        canvas.modifiedAt = now;
        ok = storage_->saveConfig(canvas, p->path) && ok;

        if (ok) p->modifiedAt = now;
        else autoSaveError_ = storage_->lastError();
    });
}

// ====================================================================
// Project lifecycle
// ====================================================================

std::string ProjectManager::createProject(const std::string& name, const std::string& path) {
    auto canvas = std::make_unique<Canvas>();
    canvas->canvasName = name;
    canvas->createdAt = canvas->modifiedAt = clock::unixSeconds();

    ProjectInfo info;
    info.uuid = Uuid::generate();
    info.name = name;
    info.path = path.empty() ? suggestPath(name) : path;
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
        if (samePath(p.path, path)) {
            switchTo(p.uuid);
            return p.uuid;
        }
    }
    if (!storage_->isValid(path)) {
        lastError_ = tr(Msg::ErrNotAProject, {path});
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
    info.onDisk = true;
    info.createdAt = canvas->createdAt;
    info.modifiedAt = canvas->modifiedAt;

    canvases_[info.uuid] = std::move(canvas);
    projects_.push_back(info);
    acquireLock(info);
    setupAutoSave(info.uuid);
    switchTo(info.uuid);
    return info.uuid;
}

bool ProjectManager::closeProject(const std::string& uuid) {
    auto it = canvases_.find(uuid);
    if (it == canvases_.end()) return false;
    it->second->flushChanges();
    if (ProjectInfo* p = find(uuid)) releaseLock(*p);

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
    Canvas* canvas = activeCanvas();
    if (!info || !canvas) return false;
    return writeFull(*info, *canvas);
}

bool ProjectManager::saveAs(const std::string& name) {
    ProjectInfo* info = activeProject();
    Canvas* canvas = activeCanvas();
    if (!info || !canvas || name.empty()) return false;

    // Bring the old location up to date first, then continue in the copy.
    canvas->flushChanges();
    releaseLock(*info);

    ProjectInfo copy = *info;
    copy.name = name;
    copy.path = suggestPath(name);
    copy.onDisk = false;
    const std::string oldName = canvas->canvasName;
    canvas->canvasName = name;
    if (!writeFull(copy, *canvas)) {
        canvas->canvasName = oldName;
        acquireLock(*info);
        return false;
    }
    *info = copy;
    return true;
}

bool ProjectManager::revert() {
    ProjectInfo* info = activeProject();
    Canvas* canvas = activeCanvas();
    if (!info || !canvas) return false;
    if (!info->onDisk) {
        lastError_ = tr(Msg::ErrNothingSaved);
        return false;
    }
    // Discard pending edits instead of saving them first: that's the point.
    canvas->discardPendingChanges();
    const bool ok = storage_->loadCanvas(*canvas, info->path);
    if (!ok) lastError_ = storage_->lastError();
    return ok;
}

bool ProjectManager::renameActive(const std::string& name) {
    ProjectInfo* info = activeProject();
    Canvas* canvas = activeCanvas();
    if (!info || !canvas || name.empty()) return false;
    info->name = name;
    canvas->canvasName = name;
    canvas->markConfigChanged();
    return true;
}

bool ProjectManager::exportActive(exchange::Format format, const std::string& outPath) {
    Canvas* canvas = activeCanvas();
    if (!canvas) return false;
    const fs::path target = path::fromUtf8(outPath);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    if (!file::write(target, exchange::exportAs(*canvas, format))) {
        lastError_ = tr(Msg::ErrCannotWrite, {outPath});
        return false;
    }
    return true;
}

std::string ProjectManager::importProject(const std::string& filePath) {
    std::string content;
    if (!file::read(path::fromUtf8(filePath), content)) {
        lastError_ = tr(Msg::ErrCannotRead, {filePath});
        return "";
    }
    auto canvas = std::make_unique<Canvas>();
    std::string error;
    if (!exchange::importText(filePath, content, *canvas, error)) {
        lastError_ = filePath + ": " + error;
        return "";
    }
    if (canvas->canvasName.empty()) canvas->canvasName = path::toUtf8(path::fromUtf8(filePath).stem());
    const uint64_t now = clock::unixSeconds();
    if (canvas->createdAt == 0) canvas->createdAt = now;
    canvas->modifiedAt = now;

    ProjectInfo info;
    info.uuid = Uuid::generate();
    info.name = canvas->canvasName;
    info.path = suggestPath(info.name);
    info.createdAt = canvas->createdAt;
    info.modifiedAt = now;

    canvases_[info.uuid] = std::move(canvas);
    projects_.push_back(info);
    setupAutoSave(info.uuid);
    // Saved right away: an import is a new project on disk.
    if (!writeFull(projects_.back(), *canvases_[info.uuid])) {
        const std::string err = lastError_;
        closeProject(info.uuid);
        lastError_ = err;
        return "";
    }
    switchTo(info.uuid);
    return info.uuid;
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

std::string ProjectManager::takeWarning() {
    std::string w;
    w.swap(warning_);
    return w;
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

std::vector<ProjectSummary> ProjectManager::listSavedProjects() const {
    std::vector<ProjectSummary> result;
    for (const auto& p : storage_->listCanvases(defaultProjectsDir())) {
        ProjectSummary s;
        if (storage_->readSummary(p, s)) result.push_back(s);
    }
    std::sort(result.begin(), result.end(),
              [](const ProjectSummary& a, const ProjectSummary& b) { return a.modifiedAt > b.modifiedAt; });
    return result;
}

}  // namespace ic
