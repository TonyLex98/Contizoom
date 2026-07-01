#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "app/project_manager.h"
#include "model/canvas.h"
#include "storage/json_storage.h"
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <cctype>

namespace ic {
namespace fs = std::filesystem;

static std::string sanitizeProjectName(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    for (unsigned char ch : name) {
        if (std::isalnum(ch) || ch == '-' || ch == '_') {
            result += static_cast<char>(ch);
        } else if (ch == ' ' || ch == '.') {
            result += '_';
        }
    }
    return result.empty() ? "Untitled" : result;
}

static std::string defaultProjectPath(const ProjectInfo& info) {
    fs::path dir = ProjectManager::defaultProjectsDir();
    std::string base = sanitizeProjectName(info.name);
    fs::path candidate = dir / (base + ".canvas");
    int suffix = 2;
    while (fs::exists(candidate)) {
        candidate = dir / (base + "_" + std::to_string(suffix++) + ".canvas");
    }
    return candidate.string();
}

ProjectManager::ProjectManager() {
}

ProjectManager::~ProjectManager() {
    // Flush pending auto-save on all canvases
    for (auto& [uuid, canvas] : canvases_) {
        canvas->flushChanges();
    }
}

void ProjectManager::setStorage(std::unique_ptr<IStorage> storage) {
    storage_ = std::move(storage);
}

std::string ProjectManager::defaultProjectsDir() {
#ifdef _WIN32
    const char* home = getenv("USERPROFILE");
#else
    const char* home = getenv("HOME");
#endif
    if (!home) home = ".";
    fs::path dir = fs::path(home) / ".infinite_canvas" / "projects";
    fs::create_directories(dir);
    return dir.string();
}

void ProjectManager::setupAutoSave(const std::string& uuid) {
    auto it = canvases_.find(uuid);
    if (it == canvases_.end() || !storage_) return;

    auto pIt = std::find_if(projects_.begin(), projects_.end(),
        [&](const ProjectInfo& p) { return p.uuid == uuid; });
    if (pIt == projects_.end()) return;

    // Canvas onChanged callback: save node changes incrementally and config changes fully.
    it->second->onChanged([this, uuid, path = pIt->path](const std::set<std::string>& changedUuids,
                                                         bool canvasChanged) {
        if (path.empty() || !storage_) return; // Not yet saved to disk

        auto canvasIt = canvases_.find(uuid);
        if (canvasIt == canvases_.end()) return;

        if (canvasChanged) {
            if (storage_->saveCanvas(*canvasIt->second, path)) {
                if (auto info = findProject(uuid)) {
                    info->modifiedAt = std::time(nullptr);
                }
            }
            return;
        }

        for (const auto& nodeUuid : changedUuids) {
            auto node = canvasIt->second->getNode(nodeUuid);
            if (node) {
                node->modifiedAt = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                storage_->saveNode(node.get(), path);
            }
        }
    });

    // Set project path so canvas knows where to save
    it->second->setProjectPath(pIt->path);
}

std::string ProjectManager::createProject(const std::string& name) {
    std::string uuid = generateProjectUuid();
    auto canvas = std::make_unique<Canvas>();
    canvas->canvasName = name;
    canvas->createdAt = std::time(nullptr);
    canvas->modifiedAt = canvas->createdAt;

    ProjectInfo info;
    info.uuid = uuid;
    info.name = name;
    info.path.clear();
    info.createdAt = canvas->createdAt;
    info.modifiedAt = info.createdAt;

    canvases_[uuid] = std::move(canvas);
    projects_.push_back(info);

    switchTo(uuid);
    return uuid;
}

std::string ProjectManager::openProject(const std::string& path) {
    if (!storage_ || !storage_->isValid(path)) {
        std::cerr << "Invalid project path: " << path << "\n";
        return "";
    }

    auto canvas = std::make_unique<Canvas>();
    if (!storage_->loadCanvas(*canvas, path)) {
        std::cerr << "Failed to load project: " << path << "\n";
        return "";
    }

    std::string uuid = generateProjectUuid();
    canvas->modifiedAt = std::time(nullptr);

    ProjectInfo info;
    info.uuid = uuid;
    info.name = canvas->canvasName;
    info.path = path;
    info.createdAt = canvas->createdAt;
    info.modifiedAt = canvas->modifiedAt;

    canvases_[uuid] = std::move(canvas);
    projects_.push_back(info);
    addRecent(path);

    switchTo(uuid);

    // Enable auto-save after project is fully loaded and has a path
    setupAutoSave(uuid);
    return uuid;
}

bool ProjectManager::closeProject(const std::string& uuid) {
    auto info = findProject(uuid);
    if (!info) return false;

    // Flush pending changes before closing
    auto canvasIt = canvases_.find(uuid);
    if (canvasIt != canvases_.end()) {
        canvasIt->second->flushChanges();
    }

    if (onBeforeClose_) {
        if (!onBeforeClose_(*info)) return false;
    }

    projects_.erase(
        std::remove_if(projects_.begin(), projects_.end(),
            [&](const ProjectInfo& p) { return p.uuid == uuid; }),
        projects_.end());

    canvases_.erase(uuid);

    if (activeUuid_ == uuid) {
        activeUuid_ = projects_.empty() ? "" : projects_.back().uuid;
        notifyActiveChanged();
    }

    return true;
}

void ProjectManager::closeAll() {
    for (auto it = projects_.rbegin(); it != projects_.rend(); ++it) {
        closeProject(it->uuid);
    }
}

void ProjectManager::switchTo(const std::string& uuid) {
    if (canvases_.find(uuid) == canvases_.end()) return;
    activeUuid_ = uuid;
    notifyActiveChanged();
}

bool ProjectManager::saveCurrent() {
    auto info = activeProject();
    if (!info || !storage_) return false;

    if (info->path.empty()) {
        return saveAs(defaultProjectPath(*info));
    }

    auto it = canvases_.find(info->uuid);
    if (it == canvases_.end()) return false;

    // Flush pending changes first
    it->second->flushChanges();

    bool ok = storage_->saveCanvas(*(it->second), info->path);
    if (ok) {
        info->modifiedAt = std::time(nullptr);
        setupAutoSave(info->uuid); // Re-enable auto-save after full save
    }
    return ok;
}

bool ProjectManager::saveAs(const std::string& newPath) {
    auto info = activeProject();
    if (!info || !storage_) return false;

    auto it = canvases_.find(info->uuid);
    if (it == canvases_.end()) return false;

    // Flush pending changes first
    it->second->flushChanges();

    bool ok = storage_->saveCanvas(*(it->second), newPath);
    if (ok) {
        info->path = newPath;
        info->modifiedAt = std::time(nullptr);
        addRecent(newPath);
        setupAutoSave(info->uuid);
    }
    return ok;
}

void ProjectManager::processAutoSave() {
    for (auto& [_, canvas] : canvases_) {
        canvas->processPendingChanges();
    }
}

bool ProjectManager::revert() {
    auto info = activeProject();
    if (!info || !storage_ || info->path.empty()) return false;

    auto it = canvases_.find(info->uuid);
    if (it == canvases_.end()) return false;

    // Flush pending changes before reload
    it->second->flushChanges();

    if (!storage_->loadCanvas(*(it->second), info->path)) return false;

    info->modifiedAt = std::time(nullptr);
    setupAutoSave(info->uuid);
    return true;
}

Canvas* ProjectManager::activeCanvas() {
    if (activeUuid_.empty() || canvases_.find(activeUuid_) == canvases_.end())
        return nullptr;
    return canvases_[activeUuid_].get();
}

const Canvas* ProjectManager::activeCanvas() const {
    if (activeUuid_.empty() || canvases_.find(activeUuid_) == canvases_.end())
        return nullptr;
    return canvases_.at(activeUuid_).get();
}

ProjectInfo* ProjectManager::activeProject() {
    return findProject(activeUuid_);
}

const ProjectInfo* ProjectManager::activeProject() const {
    return findProject(activeUuid_);
}

ProjectInfo* ProjectManager::findProject(const std::string& uuid) const {
    for (auto& p : projects_) {
        if (p.uuid == uuid) return const_cast<ProjectInfo*>(&p);
    }
    return nullptr;
}

std::vector<std::string> ProjectManager::listSavedProjects() const {
    if (!storage_) return {};
    return storage_->listCanvases(defaultProjectsDir());
}

std::vector<std::string> ProjectManager::recentProjects(int maxCount) const {
    std::vector<std::string> result;
    for (int i = 0; i < maxCount && i < static_cast<int>(recentPaths_.size()); i++) {
        result.push_back(recentPaths_[i]);
    }
    return result;
}

 std::string ProjectManager::generateProjectUuid() {
    return Uuid::generate();
}

void ProjectManager::addRecent(const std::string& path) {
    recentPaths_.erase(
        std::remove(recentPaths_.begin(), recentPaths_.end(), path),
        recentPaths_.end());
    recentPaths_.insert(recentPaths_.begin(), path);
    if (recentPaths_.size() > 10) {
        recentPaths_.resize(10);
    }
}

void ProjectManager::notifyActiveChanged() {
    if (onActiveChanged_) {
        onActiveChanged_(findProject(activeUuid_));
    }
}

std::string ProjectInfo::relativePath() const {
    if (path.empty()) return "[Untitled]";
    fs::path p(path);
    fs::path parent = p.parent_path();
    return (parent.filename() / p.filename()).string();
}

} // namespace ic
