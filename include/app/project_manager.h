#ifndef IC_PROJECT_MANAGER_H
#define IC_PROJECT_MANAGER_H

#include "storage/storage.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ic {

class Canvas;

struct ProjectInfo {
    std::string uuid;  // session id, not stored on disk
    std::string name;
    std::string path;     // always set; the directory appears on first save
    bool onDisk = false;  // written at least once
    uint64_t createdAt = 0;
    uint64_t modifiedAt = 0;

    // "projects/Name.canvas" for display.
    std::string displayPath() const;
};

// Open projects and their persistence.
//
// Every project has a path from the moment it's created and is auto-saved
// from its first edit on, so nothing is lost if the program or the machine
// goes down. Auto-save writes only what changed (see Canvas::ChangeSet).
// A `.lock` file inside the project directory detects the same project
// being open in two windows, which would otherwise overwrite each other.
class ProjectManager {
public:
    explicit ProjectManager(std::unique_ptr<IStorage> storage);
    ~ProjectManager();

    // Where projects are kept: %USERPROFILE%/.infinite_canvas/projects.
    static std::string defaultProjectsDir();
    // A free "<dir>/<sanitized name>.canvas" path, not used on disk or by
    // any open project.
    std::string suggestPath(const std::string& name) const;

    std::string createProject(const std::string& name = "Untitled");
    std::string openProject(const std::string& path);  // "" on failure, see lastError()
    bool closeProject(const std::string& uuid);
    void closeAll();
    void switchTo(const std::string& uuid);

    bool saveCurrent();                         // full save to the project's path
    bool saveAs(const std::string& name);       // copy to a new project and continue there
    bool revert();                              // reload the active project from disk
    bool exportMarkdown(std::string& outPath);  // writes "<project>.md" next to the project

    void processAutoSave();
    void flushAll();

    Canvas* activeCanvas();
    const Canvas* activeCanvas() const;
    ProjectInfo* activeProject();
    const ProjectInfo* activeProject() const;
    const std::vector<ProjectInfo>& projects() const { return projects_; }

    // Saved projects, most recently modified first.
    std::vector<ProjectSummary> listSavedProjects() const;

    // Last failure (open/save/revert/export).
    const std::string& lastError() const { return lastError_; }
    // Messages for the status bar that haven't been shown yet (auto-save
    // failures, lock warnings). Each call returns and clears them.
    std::string takeAutoSaveError();
    std::string takeWarning();

private:
    std::unique_ptr<IStorage> storage_;
    std::vector<ProjectInfo> projects_;
    std::map<std::string, std::unique_ptr<Canvas>> canvases_;
    std::string activeUuid_;
    std::string lastError_;
    std::string autoSaveError_;
    std::string warning_;
    std::string lockToken_;  // identifies this session's locks

    ProjectInfo* find(const std::string& uuid);
    void setupAutoSave(const std::string& uuid);
    bool writeFull(ProjectInfo& info, Canvas& canvas);
    void acquireLock(const ProjectInfo& info);
    void releaseLock(const ProjectInfo& info);
};

}  // namespace ic

#endif  // IC_PROJECT_MANAGER_H
