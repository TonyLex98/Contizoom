#ifndef IC_PROJECT_MANAGER_H
#define IC_PROJECT_MANAGER_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ic {

class Canvas;
class IStorage;

struct ProjectInfo {
    std::string uuid;  // session id, not stored on disk
    std::string name;
    std::string path;  // empty until first saved
    uint64_t createdAt = 0;
    uint64_t modifiedAt = 0;

    // "projects/Name.canvas" for display.
    std::string displayPath() const;
};

// Open projects and their persistence. Each open project has its own
// Canvas; edits to a project that has a path are auto-saved incrementally
// (only the nodes that changed) once they settle.
class ProjectManager {
public:
    explicit ProjectManager(std::unique_ptr<IStorage> storage);
    ~ProjectManager();

    // Where projects are kept: %USERPROFILE%/.infinite_canvas/projects.
    static std::string defaultProjectsDir();
    // A free "<dir>/<sanitized name>.canvas" path for a new project.
    static std::string suggestPath(const std::string& name);

    std::string createProject(const std::string& name = "Untitled");
    std::string openProject(const std::string& path);  // "" on failure, see lastError()
    bool closeProject(const std::string& uuid);
    void closeAll();
    void switchTo(const std::string& uuid);

    bool saveCurrent();  // to its path, or a suggested one if never saved
    bool saveAs(const std::string& path);
    bool revert();       // reload the active project from disk

    void processAutoSave();
    void flushAll();

    Canvas* activeCanvas();
    const Canvas* activeCanvas() const;
    ProjectInfo* activeProject();
    const ProjectInfo* activeProject() const;
    const std::vector<ProjectInfo>& projects() const { return projects_; }

    std::vector<std::string> listSavedProjects() const;

    // Last failure (open/save/revert, or a background auto-save).
    const std::string& lastError() const { return lastError_; }
    // Returns and clears an auto-save failure that hasn't been shown yet.
    std::string takeAutoSaveError();

private:
    std::unique_ptr<IStorage> storage_;
    std::vector<ProjectInfo> projects_;
    std::map<std::string, std::unique_ptr<Canvas>> canvases_;
    std::string activeUuid_;
    std::string lastError_;
    std::string autoSaveError_;

    ProjectInfo* find(const std::string& uuid);
    void setupAutoSave(const std::string& uuid);
};

}  // namespace ic

#endif  // IC_PROJECT_MANAGER_H
