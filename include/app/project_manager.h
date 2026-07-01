#ifndef IC_PROJECT_MANAGER_H
#define IC_PROJECT_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <ctime>

namespace ic {

class Canvas;
class IStorage;

// Metadata for a single project
struct ProjectInfo {
    std::string uuid;           // Unique project identifier
    std::string name;           // Display name
    std::string path;           // Full path to project directory (empty = unsaved)
    bool        dirty = false;  // Has unsaved changes
    std::time_t createdAt = 0;
    std::time_t modifiedAt = 0;

    // Relative path from projects root (for display in menu)
    std::string relativePath() const;
};

// Action requested by user in project menu
enum class ProjectAction {
    None,
    CreateNew,
    OpenExisting,
    SaveCurrent,
    SaveAs,
    CloseCurrent,
    SwitchProject,
    RevertChanges,
    CloseAll,
    Quit
};

// Project manager: handles multiple open canvases (projects).
// Default projects directory is ~/.infinite_canvas/projects/ on POSIX
// or %USERPROFILE%\.infinite_canvas\projects\ on Windows.
class ProjectManager {
public:
    ProjectManager();
    ~ProjectManager();

    // Set storage backend (must be set before save/load)
    void setStorage(std::unique_ptr<IStorage> storage);
    IStorage* storage() { return storage_.get(); }
    const IStorage* storage() const { return storage_.get(); }

    // Get default projects directory
    static std::string defaultProjectsDir();

    // --- Project lifecycle ---

    // Create a new project, return its UUID
    std::string createProject(const std::string& name = "Untitled");

    // Open existing project from path, return UUID (empty on failure)
    std::string openProject(const std::string& path);

    // Close project by UUID; if dirty, calls onBeforeClose callback
    bool closeProject(const std::string& uuid);

    // Close all projects (prompts for each dirty one)
    void closeAll();

    // Switch active project
    void switchTo(const std::string& uuid);

    // --- Save operations ---

    // Save current project to its path
    bool saveCurrent();

    // Save current project to a new path
    bool saveAs(const std::string& newPath);

    // Revert current project to last saved state (load from disk)
    bool revert();

    // --- Query ---

    // Get currently active canvas (may be null)
    Canvas* activeCanvas();
    const Canvas* activeCanvas() const;

    // Get active project info (null if no active project)
    ProjectInfo* activeProject();
    const ProjectInfo* activeProject() const;

    // Get all open projects
    const std::vector<ProjectInfo>& projects() const { return projects_; }

    // Get project by UUID
    ProjectInfo* findProject(const std::string& uuid) const;

    // List all saved projects in default directory
    std::vector<std::string> listSavedProjects() const;

    // Recent projects (up to 10, ordered by last opened)
    std::vector<std::string> recentProjects(int maxCount = 10) const;

    // --- Callbacks ---

    // Called before closing a dirty project; return false to abort
    using BeforeCloseCallback = std::function<bool(const ProjectInfo&)>;
    void onBeforeClose(BeforeCloseCallback cb) { onBeforeClose_ = cb; }

    // Called when active project changes
    using ActiveChangedCallback = std::function<void(const ProjectInfo*)>;
    void onActiveChanged(ActiveChangedCallback cb) { onActiveChanged_ = cb; }

    // Called when any project's dirty flag changes
    using DirtyChangedCallback = std::function<void(const ProjectInfo&)>;
    void onDirtyChanged(DirtyChangedCallback cb) { onDirtyChanged_ = cb; }

private:
    std::unique_ptr<IStorage> storage_;
    std::vector<ProjectInfo>  projects_;
    std::string               activeUuid_; // Currently active project
    std::vector<std::string>  recentPaths_; // Recently opened paths (max 10)

    BeforeCloseCallback  onBeforeClose_;
    ActiveChangedCallback onActiveChanged_;
    DirtyChangedCallback onDirtyChanged_;

    std::map<std::string, std::unique_ptr<Canvas>> canvases_;

    // Generate a unique project UUID
    static std::string generateProjectUuid();

    // Add path to recent list
    void addRecent(const std::string& path);

    // Mark project dirty
    void markDirty(const std::string& uuid);

    // Notify active changed
    void notifyActiveChanged();
};

} // namespace ic

#endif // IC_PROJECT_MANAGER_H
