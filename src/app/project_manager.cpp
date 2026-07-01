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

namespace ic {
namespace fs = std::filesystem;

ProjectManager::ProjectManager() {
}

ProjectManager::~ProjectManager() {
    // Clean up: close all projects (without prompting)
    for (auto& p : projects_) {
        if (p.dirty) {
            // Try to save dirty projects on destruction
            auto it = canvases_.find(p.uuid);
            if (it != canvases_.end() && storage_ && !p.path.empty()) {
                storage_->saveCanvas(*(it->second), p.path);
            }
        }
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

std::string ProjectManager::createProject(const std::string& name) {
    std::string uuid = generateProjectUuid();
    auto canvas = std::make_unique<Canvas>();
    canvas->canvasName = name;
    canvas->createdAt = std::time(nullptr);
    canvas->modifiedAt = canvas->createdAt;

    // Connect dirty callback
    canvas->onChanged([this, uuid]() { markDirty(uuid); });

    ProjectInfo info;
    info.uuid = uuid;
    info.name = name;
    info.path.clear(); // Unsaved
    info.dirty = false;
    info.createdAt = canvas->createdAt;
    info.modifiedAt = info.createdAt;

    canvases_[uuid] = std::move(canvas);
    projects_.push_back(info);

    // Auto-switch to new project
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
    canvas->onChanged([this, uuid]() { markDirty(uuid); });
    canvas->modifiedAt = std::time(nullptr);

    ProjectInfo info;
    info.uuid = uuid;
    info.name = canvas->canvasName;
    info.path = path;
    info.dirty = false;
    info.createdAt = canvas->createdAt;
    info.modifiedAt = canvas->modifiedAt;

    canvases_[uuid] = std::move(canvas);
    projects_.push_back(info);
    addRecent(path);

    switchTo(uuid);
    return uuid;
}

bool ProjectManager::closeProject(const std::string& uuid) {
    auto info = findProject(uuid);
    if (!info) return false;

    if (info->dirty && onBeforeClose_) {
        if (!onBeforeClose_(*info)) return false;
    }

    // Remove from projects list
    projects_.erase(
        std::remove_if(projects_.begin(), projects_.end(),
            [&](const ProjectInfo& p) { return p.uuid == uuid; }),
        projects_.end());

    // Remove canvas
    canvases_.erase(uuid);

    // If this was the active project, switch to another or clear
    if (activeUuid_ == uuid) {
        activeUuid_ = projects_.empty() ? "" : projects_.back().uuid;
        notifyActiveChanged();
    }

    return true;
}

void ProjectManager::closeAll() {
    // Close in reverse order to preserve active
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
    if (!info || !storage_ || info->path.empty()) return false;

    auto it = canvases_.find(info->uuid);
    if (it == canvases_.end()) return false;

    bool ok = storage_->saveCanvas(*(it->second), info->path);
    if (ok) {
        info->dirty = false;
        info->modifiedAt = std::time(nullptr);
        if (onDirtyChanged_) onDirtyChanged_(*info);
    }
    return ok;
}

bool ProjectManager::saveAs(const std::string& newPath) {
    auto info = activeProject();
    if (!info || !storage_) return false;

    auto it = canvases_.find(info->uuid);
    if (it == canvases_.end()) return false;

    bool ok = storage_->saveCanvas(*(it->second), newPath);
    if (ok) {
        info->path = newPath;
        info->dirty = false;
        info->modifiedAt = std::time(nullptr);
        addRecent(newPath);
        if (onDirtyChanged_) onDirtyChanged_(*info);
    }
    return ok;
}

bool ProjectManager::revert() {
    auto info = activeProject();
    if (!info || !storage_ || info->path.empty()) return false;

    auto it = canvases_.find(info->uuid);
    if (it == canvases_.end()) return false;

    // Reload from disk
    if (!storage_->loadCanvas(*(it->second), info->path)) return false;

    info->dirty = false;
    info->modifiedAt = std::time(nullptr);
    if (onDirtyChanged_) onDirtyChanged_(*info);
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
    // Remove if already in list
    recentPaths_.erase(
        std::remove(recentPaths_.begin(), recentPaths_.end(), path),
        recentPaths_.end());
    // Add to front
    recentPaths_.insert(recentPaths_.begin(), path);
    // Keep max 10
    if (recentPaths_.size() > 10) {
        recentPaths_.resize(10);
    }
}

void ProjectManager::markDirty(const std::string& uuid) {
    auto info = findProject(uuid);
    if (info && !info->dirty) {
        info->dirty = true;
        info->modifiedAt = std::time(nullptr);
        if (onDirtyChanged_) onDirtyChanged_(*info);
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
    // Return filename + parent for readability
    return (parent.filename() / p.filename()).string();
}

} // namespace ic
